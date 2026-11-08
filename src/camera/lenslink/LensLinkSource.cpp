// SPDX-License-Identifier: GPL-2.0-or-later
// Connection logic adapted from LensLink obs-plugin/src/ios-camera-source.c
// (GPL-2.0-or-later), rewritten without libobs for OneTakeLink.
#include "LensLinkSource.h"

#include "camera/StreamHandler.h"
#include "core/AnnexB.h"
#include "core/Version.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>

#include <cmath>
#include <cstring>

// Winsock/windows.h must not be included inside extern "C"; the proto
// headers carry their own C linkage guards.
#include "proto/net-compat.h"
#include "proto/ll-compat.h"
#include "proto/handshake.h"
#include "proto/protocol.h"
#include "proto/usbmux.h"

extern "C" {
#include <libavutil/frame.h>
}

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace otl::lenslink {

namespace {

constexpr size_t kRecvChunk = 256 * 1024;
constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kSilenceTimeoutNs = 6000 * kMs; // app pings every ~2 s
constexpr size_t kMaxPendingOut = 1024 * 1024;


// TCP connect with a timeout; returns a non-blocking socket.
socket_t tcpDial(const std::string &host, uint16_t port, int timeoutMs,
                 const std::atomic<bool> &stop)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return OBSC_INVALID_SOCKET;
    sockaddr_in addr{};
    std::memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);
    addr.sin_port = htons(port);

    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == OBSC_INVALID_SOCKET)
        return s;
    net_set_nonblocking(s);
    int r = connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (r != 0) {
        int err = net_last_error();
#ifdef _WIN32
        bool inProgress = err == WSAEWOULDBLOCK;
#else
        bool inProgress = err == EINPROGRESS;
#endif
        if (!inProgress) {
            net_close(s);
            return OBSC_INVALID_SOCKET;
        }
        int waited = 0;
        for (;;) {
            if (stop) {
                net_close(s);
                return OBSC_INVALID_SOCKET;
            }
            int w = net_wait(s, NET_WAIT_WRITE, 100);
            if (w < 0) {
                net_close(s);
                return OBSC_INVALID_SOCKET;
            }
            if (w & NET_WAIT_WRITE)
                break;
            waited += 100;
            if (waited >= timeoutMs) {
                net_close(s);
                return OBSC_INVALID_SOCKET;
            }
        }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soErr), &len);
        if (soErr != 0) {
            net_close(s);
            return OBSC_INVALID_SOCKET;
        }
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));
    return s;
}

// Whether Apple's device multiplexer is reachable at all (iTunes /
// Apple Devices installed and its service running).
bool usbmuxServiceAvailable()
{
#ifdef _WIN32
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == OBSC_INVALID_SOCKET)
        return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(27015);
    bool ok = connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    net_close(s);
    return ok;
#else
    struct stat st;
    return stat("/var/run/usbmuxd", &st) == 0;
#endif
}

QString usbHint()
{
#ifdef _WIN32
    return QCoreApplication::translate("otl::lenslink::LensLinkSource", "Подключите iPhone кабелем, разблокируйте его и подтвердите «Доверять этому "
              "компьютеру». Нужны iTunes или Apple Devices (служба Apple Mobile Device Service).");
#else
    return QCoreApplication::translate("otl::lenslink::LensLinkSource", "Подключите iPhone кабелем, разблокируйте его и подтвердите «Доверять этому "
              "компьютеру». Нужен запущенный usbmuxd.");
#endif
}

} // namespace

struct LensLinkSource::Session {
    socket_t sock = OBSC_INVALID_SOCKET;
    Transport transport = Transport::Usb;
    std::vector<uint8_t> buf;
    size_t len = 0;
    std::string out;
    bool outFailed = false;

    QString name;
    bool helloSeen = false;
    bool standby = false;
    bool unarmed = false;
    bool startSent = false;
    bool formatRequested = false;
    bool paused = false;

    std::unique_ptr<StreamHandler> stream;
    bool streaming = false;
    uint64_t lastRecvNs = 0;

    // Clock sync (TIMESYNC_REQ/RESP), NTP-style.
    uint64_t nextTimesyncNs = 0;
    uint64_t bestRtt = UINT64_MAX;
    int syncSamples = 0;
};

LensLinkSource::LensLinkSource(QObject *parent) : CameraSource(parent)
{
    net_init();
}

LensLinkSource::~LensLinkSource()
{
    stop();
}

void LensLinkSource::setOptions(const SourceOptions &options)
{
    QMutexLocker lock(&m_mutex);
    m_options = options;
}

void LensLinkSource::start(const DeviceInfo &device)
{
    stop();
    m_stop = false;
    {
        QMutexLocker lock(&m_mutex);
        m_outgoing.clear();
    }
    m_thread = std::thread([this, device] { run(device); });
}

void LensLinkSource::stop()
{
    if (!m_thread.joinable())
        return;
    m_stop = true;
    m_thread.join();
    setStatus(SourceState::Idle, tr("Источник отключён"));
    emit cameraStateChanged({});
    emit statsChanged({});
}

void LensLinkSource::sendCommand(const QJsonObject &command)
{
    QMutexLocker lock(&m_mutex);
    if (m_outgoing.size() < 64)
        m_outgoing.push_back(QJsonDocument(command).toJson(QJsonDocument::Compact));
}

void LensLinkSource::setStatus(SourceState state, const QString &message, const QString &hint)
{
    SourceStatus st;
    {
        QMutexLocker lock(&m_mutex);
        if (m_status.state == state && m_status.message == message && m_status.hint == hint)
            return;
        m_status.state = state;
        m_status.message = message;
        m_status.hint = hint;
        st = m_status;
    }
    emit statusChanged(st);
}

void LensLinkSource::sleepInterruptible(int ms)
{
    for (int t = 0; t < ms && !m_stop; t += 50)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void LensLinkSource::run(DeviceInfo device)
{
    {
        QMutexLocker lock(&m_mutex);
        m_status = SourceStatus{};
        m_status.transport = device.transport;
    }

    while (!m_stop) {
        socket_t sock = OBSC_INVALID_SOCKET;

        if (device.transport == Transport::Usb) {
            setStatus(SourceState::Searching, tr("Ищем iPhone по USB…"));
            usbmux_device devs[16];
            int n = usbmux_list_devices(devs, 16);
            int chosen = -1;
            for (int i = 0; i < n; ++i) {
                if (device.address.isEmpty() || device.address == QString::fromUtf8(devs[i].udid)) {
                    chosen = i;
                    break;
                }
            }
            if (chosen < 0) {
                if (!usbmuxServiceAvailable()) {
                    setStatus(SourceState::Error,
                              tr("Служба Apple для USB-подключения не найдена"),
#ifdef _WIN32
                              tr("Установите iTunes или Apple Devices из Microsoft Store и "
                                 "перезапустите OneTakeLink. Либо подключитесь по Wi-Fi.")
#else
                              tr("Установите и запустите usbmuxd, либо подключитесь по Wi-Fi.")
#endif
                    );
                } else {
                    setStatus(SourceState::Searching,
                              device.address.isEmpty() ? tr("iPhone не найден по USB")
                                                       : tr("Выбранный iPhone не подключён по USB"),
                              usbHint());
                }
                sleepInterruptible(1000);
                continue;
            }
            sock = usbmux_connect_device(devs[chosen].id, OBSC_USB_PORT);
            if (sock == OBSC_INVALID_SOCKET) {
                setStatus(SourceState::Searching,
                          tr("iPhone подключён, но LensLink Camera не отвечает"),
                          tr("Откройте приложение LensLink Camera на iPhone и держите его на "
                             "экране. Если оно открыто — закройте его и запустите снова."));
                sleepInterruptible(1000);
                continue;
            }
        } else {
            if (device.address.isEmpty()) {
                setStatus(SourceState::Error, tr("Не указан IP-адрес iPhone"),
                          tr("Выберите телефон из списка или введите IP, который показывает "
                             "приложение LensLink Camera."));
                sleepInterruptible(1000);
                continue;
            }
            setStatus(SourceState::Searching, tr("Подключаемся к %1 по Wi-Fi…").arg(device.address));
            sock = tcpDial(device.address.toStdString(), OBSC_USB_PORT, 3000, m_stop);
            if (sock == OBSC_INVALID_SOCKET) {
                if (m_stop)
                    break;
                setStatus(SourceState::Searching,
                          tr("Не удаётся подключиться к %1").arg(device.address),
                          tr("Проверьте, что LensLink Camera открыт на iPhone, телефон и компьютер "
                             "в одной сети Wi-Fi, а IP совпадает с показанным в приложении. "
                             "Брандмауэр Windows может блокировать подключение — разрешите "
                             "OneTakeLink в частных сетях."));
                sleepInterruptible(2000);
                continue;
            }
        }

        setStatus(SourceState::Connecting, tr("Подключено, ждём ответ LensLink Camera…"));
        {
            QMutexLocker lock(&m_mutex);
            m_outgoing.clear(); // commands for a previous connection are stale
        }

        Session session;
        session.sock = sock;
        session.transport = device.transport;
        {
            QMutexLocker lock(&m_mutex);
            session.stream = std::make_unique<StreamHandler>(m_packetSink, m_frameSink, m_options.hardwareDecode);
        }
        runSession(session);
        net_close(sock);

        session.stream->streamLost(m_stop ? tr("Источник остановлен") : tr("Связь с iPhone потеряна"));
        emit cameraStateChanged({});
        emit statsChanged({});
        if (!m_stop) {
            setStatus(SourceState::Searching, tr("Связь с iPhone потеряна, переподключаемся…"),
                      device.transport == Transport::Usb
                          ? tr("Проверьте кабель и что LensLink Camera открыт на iPhone.")
                          : tr("Проверьте Wi-Fi и что LensLink Camera открыт на iPhone. "
                               "Для долгих записей надёжнее USB."));
            sleepInterruptible(1000);
        }
    }
}

void LensLinkSource::runSession(Session &s)
{
    s.buf.resize(kRecvChunk * 2);
    const uint64_t now0 = ll_gettime_ns();
    s.lastRecvNs = now0;
    s.nextTimesyncNs = now0 + 500 * kMs;

    while (!m_stop) {
        // Outgoing commands from the UI.
        std::deque<QByteArray> pending;
        {
            QMutexLocker lock(&m_mutex);
            pending.swap(m_outgoing);
        }
        for (const QByteArray &json : pending)
            sendControl(s, json);

        const uint64_t now = ll_gettime_ns();
        if (now >= s.nextTimesyncNs) {
            uint8_t hdr[OBSC_HEADER_SIZE];
            obsc_build_header(hdr, OBSC_PKT_TIMESYNC_REQ, 0, now, 0);
            s.out.append(reinterpret_cast<const char *>(hdr), sizeof(hdr));
            s.nextTimesyncNs = now + 1000 * kMs;
        }
        flushOutgoing(s);
        if (s.outFailed)
            break;

        int events = NET_WAIT_READ | (s.out.empty() ? 0 : NET_WAIT_WRITE);
        int ret = net_wait(s.sock, events, 100);
        if (ret < 0)
            break;

        if (ret & NET_WAIT_READ) {
            if (s.buf.size() - s.len < kRecvChunk)
                s.buf.resize(s.len + kRecvChunk);
            int n = int(recv(s.sock, reinterpret_cast<char *>(s.buf.data() + s.len), int(kRecvChunk), 0));
            if (n == 0)
                break; // closed by the phone
            if (n < 0) {
                if (net_would_block())
                    continue;
                break;
            }
            s.len += size_t(n);
            s.stream->addTransportBytes(uint64_t(n));
            s.lastRecvNs = ll_gettime_ns();

            size_t off = 0;
            bool ok = true;
            while (s.len - off >= OBSC_HEADER_SIZE) {
                obsc_header hdr{};
                if (!obsc_parse_header(s.buf.data() + off, &hdr)) {
                    qWarning("[lenslink] bad packet header, dropping connection");
                    ok = false;
                    break;
                }
                const size_t total = OBSC_HEADER_SIZE + hdr.payload_size;
                if (s.len - off < total) {
                    if (s.buf.size() < off + total)
                        s.buf.resize(off + total + kRecvChunk);
                    break;
                }
                if (!handlePacket(s, hdr.type, hdr.flags, hdr.pts_ns,
                                  s.buf.data() + off + OBSC_HEADER_SIZE, hdr.payload_size)) {
                    ok = false;
                    break;
                }
                off += total;
            }
            if (off > 0) {
                std::memmove(s.buf.data(), s.buf.data() + off, s.len - off);
                s.len -= off;
            }
            if (!ok)
                break;
        }

        if (ll_gettime_ns() - s.lastRecvNs > kSilenceTimeoutNs) {
            qWarning("[lenslink] no data from the phone for 6 s, reconnecting");
            break;
        }
        publishStats(s, false);
    }
}

void LensLinkSource::sendControl(Session &s, const QByteArray &json)
{
    uint8_t hdr[OBSC_HEADER_SIZE];
    obsc_build_header(hdr, OBSC_PKT_CONTROL, 0, 0, uint32_t(json.size()));
    s.out.append(reinterpret_cast<const char *>(hdr), sizeof(hdr));
    s.out.append(json.constData(), size_t(json.size()));
}

void LensLinkSource::flushOutgoing(Session &s)
{
    while (!s.out.empty()) {
        int n = int(send(s.sock, s.out.data(), int(s.out.size()), 0));
        if (n > 0) {
            s.out.erase(0, size_t(n));
            continue;
        }
        if (n < 0 && net_would_block())
            break;
        s.outFailed = true;
        return;
    }
    if (s.out.size() > kMaxPendingOut)
        s.outFailed = true; // the phone stopped reading
}

bool LensLinkSource::handlePacket(Session &s, int type, uint16_t flags, uint64_t pts,
                                  const uint8_t *payload, uint32_t size)
{
    switch (type) {
    case OBSC_PKT_HELLO: {
        lenslink_hello hello{};
        if (!lenslink_hello_parse(reinterpret_cast<const char *>(payload), size, &hello))
            qWarning("[lenslink] malformed HELLO; treating it as a camera");
        if (hello.has_name)
            s.name = QString::fromUtf8(hello.name);
        s.standby = !hello.is_screen && hello.standby;
        s.unarmed = s.standby && hello.unarmed;
        {
            QMutexLocker lock(&m_mutex);
            m_status.deviceName = s.name;
        }

        if (!s.helloSeen) {
            s.helloSeen = true;
            // Introduce ourselves (shown on the phone's Home screen) and
            // turn off the lip-sync reference audio: we never use sound.
            QString host = QSysInfo::machineHostName().section(QLatin1Char('.'), 0, 0);
            QJsonObject identify{{"cmd", "identify"},
                                 {"host", host},
                                 {"obs", QStringLiteral("OneTakeLink %1").arg(QLatin1String(OTL_VERSION))},
                                 {"transport", s.transport == Transport::Usb ? "usb" : "lan"}};
            sendControl(s, QJsonDocument(identify).toJson(QJsonDocument::Compact));
            sendControl(s, R"({"cmd":"reference","on":false})");
        }

        if (hello.is_screen) {
            setStatus(SourceState::Error, tr("На iPhone запущена трансляция экрана, а не камера"),
                      tr("Остановите трансляцию экрана и откройте камеру в LensLink Camera."));
            return false;
        }
        if (s.unarmed) {
            setStatus(SourceState::NotArmed,
                      tr("LensLink Camera открыт, но камера не запущена"),
                      tr("Нажмите старт трансляции в приложении на iPhone или включите там "
                         "удалённый запуск (Arm Remote Start)."));
        } else if (s.standby) {
            bool autoStart;
            {
                QMutexLocker lock(&m_mutex);
                autoStart = m_options.autoStartCamera;
            }
            if (autoStart && !s.startSent) {
                s.startSent = true;
                sendControl(s, R"({"cmd":"start_stream"})");
                setStatus(SourceState::Standby, tr("Запускаем камеру на iPhone…"));
            } else {
                setStatus(SourceState::Standby, tr("LensLink Camera открыт, камера не запущена"),
                          tr("Нажмите «Запустить камеру» или старт в приложении на iPhone."));
            }
        } else {
            setStatus(SourceState::Connecting,
                      tr("Подключено к %1, ждём видео…").arg(s.name.isEmpty() ? tr("iPhone") : s.name));
        }
        break;
    }
    case OBSC_PKT_VIDEO_CONFIG: {
        lenslink_video_config cfg{};
        lenslink_video_config_parse(reinterpret_cast<const char *>(payload), size, &cfg);
        const VideoCodec codec = cfg.codec == LENSLINK_CODEC_HEVC ? VideoCodec::HEVC : VideoCodec::H264;
        if (cfg.has_kind && cfg.is_screen) {
            setStatus(SourceState::Error, tr("На iPhone запущена трансляция экрана, а не камера"),
                      tr("Остановите трансляцию экрана и откройте камеру в LensLink Camera."));
            return false;
        }
        // Width/height/fps are informational (the bitstream is
        // authoritative) but give us the nominal frame interval.
        QJsonObject o = QJsonDocument::fromJson(
                            QByteArray::fromRawData(reinterpret_cast<const char *>(payload), int(size)))
                            .object();
        const int width = o.value("width").toInt();
        const int height = o.value("height").toInt();
        const double fps = o.value("fps").toDouble();
        s.stream->configure(codec, fps, width, height);
        s.standby = false;
        qInfo("[lenslink] video config: %s %dx%d @ %.2f", qPrintable(codecName(codec)), width, height, fps);
        break;
    }
    case OBSC_PKT_VIDEO:
        handleVideo(s, (flags & OBSC_FLAG_KEYFRAME) != 0, pts, payload, size);
        break;
    case OBSC_PKT_STATE:
        handleState(s, payload, size);
        break;
    case OBSC_PKT_TIMESYNC_RESP:
        if (size >= 8) {
            const uint64_t t1 = obsc_read_u64(payload);
            const uint64_t t3 = ll_gettime_ns();
            if (t3 > t1) {
                const uint64_t rtt = t3 - t1;
                // Keep the offset from the lowest-RTT sample; refresh the
                // reference now and then so clock drift can't accumulate.
                if (rtt <= s.bestRtt || ++s.syncSamples >= 30) {
                    s.bestRtt = rtt;
                    s.syncSamples = 0;
                    s.stream->setClockOffset(int64_t(pts) - int64_t((t1 + t3) / 2));
                }
            }
        }
        break;
    case OBSC_PKT_SCREEN_AUDIO:
        // Phone microphone as playable audio (LensLink Camera's "Send phone
        // mic to OBS"): 48 kHz stereo S16LE, pts on the video clock.
        s.stream->onAudio(payload, size, OBSC_SCREEN_AUDIO_SAMPLE_RATE, OBSC_SCREEN_AUDIO_CHANNELS, int64_t(pts));
        break;
    default:
        break; // PING, AUDIO (lip-sync reference), DIAG, REQUEST... not used
    }
    return true;
}

void LensLinkSource::handleVideo(Session &s, bool keyframe, uint64_t pts, const uint8_t *data, uint32_t size)
{
    if (s.stream->onAccessUnit(data, size, int64_t(pts), keyframe)) {
        s.streaming = true;
        setStatus(SourceState::Streaming,
                  tr("Видео с %1").arg(s.name.isEmpty() ? tr("iPhone") : s.name));
    }
}

void LensLinkSource::handleState(Session &s, const uint8_t *payload, uint32_t size)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(payload), int(size)), &err);
    if (!doc.isObject())
        return;
    const QJsonObject state = doc.object();
    s.paused = state.value("paused").toBool();
    s.stream->setPaused(s.paused);

    // Ask once per connection for the preferred format, if the phone
    // offers it and isn't already using it.
    if (!s.formatRequested) {
        s.formatRequested = true;
        SourceOptions opt;
        {
            QMutexLocker lock(&m_mutex);
            opt = m_options;
        }
        QJsonObject cmd{{"cmd", "set_format"}};
        auto offers = [&](const char *listKey, const QJsonValue &v) {
            const QJsonArray list = state.value(QLatin1String(listKey)).toArray();
            return list.isEmpty() || list.contains(v);
        };
        if (!opt.resolution.isEmpty() && state.value("resolution").toString() != opt.resolution &&
            offers("resolutions", opt.resolution))
            cmd.insert("resolution", opt.resolution);
        if (opt.fps > 0 && state.value("fps").toInt() != opt.fps && offers("frameRates", opt.fps))
            cmd.insert("fps", opt.fps);
        if (!opt.codec.isEmpty() && state.value("codec").toString() != opt.codec &&
            offers("codecs", opt.codec))
            cmd.insert("codec", opt.codec);
        if (cmd.size() > 1) {
            qInfo("[lenslink] requesting format %s",
                  QJsonDocument(cmd).toJson(QJsonDocument::Compact).constData());
            sendControl(s, QJsonDocument(cmd).toJson(QJsonDocument::Compact));
        }
        if (!opt.quality.isEmpty() && state.value("quality").toString() != opt.quality) {
            QJsonObject q{{"cmd", "set_quality"}, {"quality", opt.quality}};
            sendControl(s, QJsonDocument(q).toJson(QJsonDocument::Compact));
        }
    }
    emit cameraStateChanged(state);
}

void LensLinkSource::publishStats(Session &s, bool force)
{
    StreamStats st;
    if (s.stream->takeStats(&st, force))
        emit statsChanged(st);
}

} // namespace otl::lenslink
