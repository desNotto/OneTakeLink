// SPDX-License-Identifier: GPL-3.0-or-later
// Client side of the scrcpy protocol (video socket only), see
// docs/SCRCPY_PROTOCOL.md for the wire format used here.
#include "AndroidSource.h"

#include "Adb.h"
#include "camera/StreamHandler.h"
#include "core/Version.h"

#include <QProcess>
#include <QRandomGenerator>
#include <QThread>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "net-compat.h"

namespace otl::android {

namespace {

constexpr const char *kDevicePath = "/data/local/tmp/scrcpy-server.jar";
constexpr uint64_t kPacketSession = 1ULL << 63;
constexpr uint64_t kPacketConfig = 1ULL << 62;
constexpr uint64_t kPacketKey = 1ULL << 61;
constexpr uint64_t kPtsMask = kPacketKey - 1;
constexpr uint32_t kCodecH264 = 0x68323634; // "h264"
constexpr uint32_t kCodecH265 = 0x68323635; // "h265"
constexpr uint32_t kCodecRaw = 0x00726177;  // "raw": 48 kHz stereo S16LE
constexpr int kSilenceTimeoutMs = 6000;

uint32_t be32(const uint8_t *p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint64_t be64(const uint8_t *p)
{
    return uint64_t(be32(p)) << 32 | be32(p + 4);
}

// Reads exactly `len` bytes. 1 = ok, 0 = timeout/stopped, -1 = closed/error.
int readExact(socket_t s, uint8_t *buf, size_t len, int timeoutMs, const std::atomic<bool> &stop)
{
    size_t got = 0;
    int waited = 0;
    while (got < len) {
        if (stop)
            return 0;
        const int w = net_wait(s, NET_WAIT_READ, 100);
        if (w < 0)
            return -1;
        if (w == 0) {
            waited += 100;
            if (waited >= timeoutMs)
                return 0;
            continue;
        }
        const int n = int(recv(s, reinterpret_cast<char *>(buf + got), int(len - got), 0));
        if (n == 0)
            return -1;
        if (n < 0) {
            if (net_would_block())
                continue;
            return -1;
        }
        got += size_t(n);
        waited = 0;
    }
    return 1;
}

socket_t connectLocal(uint16_t port)
{
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == OBSC_INVALID_SOCKET)
        return s;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        net_close(s);
        return OBSC_INVALID_SOCKET;
    }
    net_set_nonblocking(s);
    return s;
}

// The last "[server] ERROR: ..." line, else the last non-empty line.
QString serverError(const QString &output)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const int at = it->indexOf(QLatin1String("ERROR: "));
        if (at >= 0)
            return it->mid(at + 7).trimmed();
    }
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

} // namespace

const char *AndroidSource::serverVersion()
{
    return OTL_SCRCPY_VERSION;
}

AndroidSource::AndroidSource(QObject *parent) : CameraSource(parent)
{
    net_init();
}

AndroidSource::~AndroidSource()
{
    stop();
}

void AndroidSource::setOptions(const AndroidOptions &options)
{
    QMutexLocker lock(&m_mutex);
    m_options = options;
}

AndroidOptions AndroidSource::options() const
{
    QMutexLocker lock(&m_mutex);
    return m_options;
}

void AndroidSource::start(const DeviceInfo &device)
{
    stop();
    m_stop = false;
    // A real QThread (not std::thread): QProcess needs a Qt thread.
    m_thread = QThread::create([this, device] { run(device); });
    m_thread->start();
}

void AndroidSource::stop()
{
    if (!m_thread)
        return;
    m_stop = true;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    setStatus(SourceState::Idle, tr("Источник отключён"));
    emit statsChanged({});
}

void AndroidSource::setStatus(SourceState state, const QString &message, const QString &hint)
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

void AndroidSource::sleepInterruptible(int ms)
{
    for (int t = 0; t < ms && !m_stop; t += 50)
        QThread::msleep(50);
}

void AndroidSource::run(DeviceInfo device)
{
    {
        QMutexLocker lock(&m_mutex);
        m_status = SourceStatus{};
        m_status.transport = Transport::Usb;
    }
    int fpsOverride = 0; // set after the phone refused the requested FPS
    bool firstSearch = true;

    while (!m_stop) {
        // 1. adb and the device.
        if (Adb::executable().isEmpty() || Adb::serverJar().isEmpty()) {
            setStatus(SourceState::Error, tr("Не найдены компоненты для Android (adb, scrcpy-server)"),
                      tr("Они должны лежать в папке android рядом с OneTakeLink.exe. Распакуйте архив "
                         "программы заново целиком."));
            sleepInterruptible(3000);
            continue;
        }
        // Only the first time: later passes keep the specific problem on
        // screen instead of flickering back to "searching".
        if (firstSearch)
            setStatus(SourceState::Searching, tr("Ищем Android-телефон по USB…"));
        firstSearch = false;
        const QList<AdbDevice> devices = Adb::devices();
        const AdbDevice *chosen = nullptr;
        const AdbDevice *pending = nullptr;
        for (const AdbDevice &d : devices) {
            if (!device.address.isEmpty() && d.serial != device.address)
                continue;
            if (d.state == QLatin1String("device")) {
                chosen = &d;
                break;
            }
            pending = &d;
        }
        if (!chosen) {
            if (pending && pending->state == QLatin1String("unauthorized")) {
                setStatus(SourceState::Searching, tr("Телефон не разрешил отладку по USB"),
                          tr("Разблокируйте телефон и подтвердите запрос «Разрешить отладку по USB?» "
                             "(можно отметить «Всегда разрешать с этого компьютера»)."));
            } else if (pending) {
                setStatus(SourceState::Searching, tr("Телефон подключён, но недоступен (%1)").arg(pending->state),
                          tr("Отключите и снова подключите кабель. Если не помогло — выключите и "
                             "включите «Отладку по USB» на телефоне."));
            } else {
                setStatus(SourceState::Searching,
                          device.address.isEmpty() ? tr("Android-телефон не найден по USB")
                                                   : tr("Выбранный Android-телефон не подключён"),
                          tr("Подключите телефон кабелем и включите «Отладку по USB»: Настройки → "
                             "О телефоне → 7 раз нажать «Номер сборки», затем Настройки → Для "
                             "разработчиков → Отладка по USB. Нужен Android 12 или новее. Если "
                             "телефон не виден, установите USB-драйвер производителя."));
            }
            sleepInterruptible(1500);
            continue;
        }
        const QString serial = chosen->serial;
        const QString model = chosen->model.isEmpty() ? serial : chosen->model;
        {
            QMutexLocker lock(&m_mutex);
            m_status.deviceName = model;
        }

        // 2. Push the server and open the tunnel (forward: we connect).
        setStatus(SourceState::Connecting, tr("Запускаем камеру на %1…").arg(model));
        AdbResult r = Adb::run({QStringLiteral("-s"), serial, QStringLiteral("push"), Adb::serverJar(),
                                QString::fromLatin1(kDevicePath)},
                               20000);
        if (!r.ok()) {
            setStatus(SourceState::Error, tr("Не удалось передать компонент на телефон"), r.out.trimmed().left(300));
            sleepInterruptible(3000);
            continue;
        }
        const uint32_t scid = QRandomGenerator::global()->generate() & 0x7FFFFFFF;
        const QString socketName = QStringLiteral("localabstract:scrcpy_%1").arg(scid, 8, 16, QLatin1Char('0'));
        r = Adb::run({QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("tcp:0"), socketName});
        const uint16_t port = uint16_t(r.out.trimmed().toUInt());
        if (!r.ok() || port == 0) {
            setStatus(SourceState::Error, tr("Не удалось открыть канал adb"), r.out.trimmed().left(300));
            sleepInterruptible(3000);
            continue;
        }

        // 3. Start the server.
        AndroidOptions opt = options();
        const int fps = fpsOverride > 0 ? fpsOverride : opt.fps;
        QStringList args = {QStringLiteral("-s"), serial, QStringLiteral("shell"),
                            QStringLiteral("CLASSPATH=%1").arg(QLatin1String(kDevicePath)), QStringLiteral("app_process"),
                            QStringLiteral("/"), QStringLiteral("com.genymobile.scrcpy.Server"),
                            QString::fromLatin1(serverVersion()),
                            QStringLiteral("scid=%1").arg(scid, 8, 16, QLatin1Char('0')),
                            QStringLiteral("log_level=info"),
                            QStringLiteral("video_source=camera"),
                            opt.audio ? QStringLiteral("audio=true") : QStringLiteral("audio=false"),
                            QStringLiteral("control=false"),
                            QStringLiteral("tunnel_forward=true"),
                            QStringLiteral("send_dummy_byte=true"),
                            QStringLiteral("send_device_meta=true"),
                            QStringLiteral("send_stream_meta=true"),
                            QStringLiteral("send_frame_meta=true"),
                            QStringLiteral("video_codec=%1").arg(opt.codec),
                            QStringLiteral("video_bit_rate=%1").arg(qint64(opt.bitrateMbps) * 1000000),
                            QStringLiteral("camera_facing=%1").arg(opt.facing),
                            QStringLiteral("camera_size=%1x%2").arg(opt.width).arg(opt.height),
                            QStringLiteral("camera_fps=%1").arg(fps),
                            // Keyframe every second: takes start close to REC.
                            QStringLiteral("video_codec_options=i-frame-interval:int=1"),
                            QStringLiteral("cleanup=true")};
        if (opt.audio) {
            // Uncompressed: encoded to AAC by the recorder like iPhone audio.
            // The camcorder profile is tuned for video recording.
            args << QStringLiteral("audio_codec=raw") << QStringLiteral("audio_source=mic-camcorder");
        }
        QProcess server;
        server.setProcessChannelMode(QProcess::MergedChannels);
        server.start(Adb::executable(), args);
        QString serverOut;
        auto pumpServerOutput = [&] {
            server.waitForReadyRead(0);
            const QByteArray chunk = server.readAll();
            if (chunk.isEmpty())
                return;
            const QString text = QString::fromUtf8(chunk);
            serverOut += text;
            for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                qInfo("[android] %s", qPrintable(line.trimmed()));
        };

        // 4. Connect; the dummy byte proves the server is listening.
        socket_t sock = OBSC_INVALID_SOCKET;
        for (int attempt = 0; attempt < 100 && !m_stop; ++attempt) {
            pumpServerOutput();
            if (server.state() == QProcess::NotRunning)
                break;
            sock = connectLocal(port);
            if (sock != OBSC_INVALID_SOCKET) {
                uint8_t dummy = 0xFF;
                if (readExact(sock, &dummy, 1, 300, m_stop) == 1 && dummy == 0)
                    break;
                net_close(sock);
                sock = OBSC_INVALID_SOCKET;
            }
            QThread::msleep(100);
        }

        // The audio socket must be accepted before the server sends
        // anything else, so connect it right after the video socket.
        socket_t audioSock = OBSC_INVALID_SOCKET;
        if (sock != OBSC_INVALID_SOCKET && opt.audio)
            audioSock = connectLocal(port);

        bool gotVideo = false;
        QString endReason;
        if (sock != OBSC_INVALID_SOCKET) {
            StreamHandler stream(m_packetSink, m_frameSink, opt.hardwareDecode);
            std::atomic<bool> sessionDone{false};
            std::thread audioThread;
            if (audioSock != OBSC_INVALID_SOCKET) {
                audioThread = std::thread([&] {
                    uint8_t h[12];
                    if (readExact(audioSock, h, 4, 10000, sessionDone) != 1)
                        return;
                    const uint32_t id = be32(h);
                    if (id != kCodecRaw) {
                        // 0: the phone could not capture audio; video goes on.
                        qWarning("[android] audio not available (codec id %u)", id);
                        return;
                    }
                    std::vector<uint8_t> pcm;
                    while (!sessionDone) {
                        const int r = readExact(audioSock, h, sizeof(h), 2000, sessionDone);
                        if (r < 0)
                            return; // closed
                        if (r == 0)
                            continue; // quiet (or stopping: the loop checks)
                        const uint64_t flags = be64(h);
                        const uint32_t size = be32(h + 8);
                        if (size > 4u * 1024 * 1024)
                            return;
                        pcm.resize(size);
                        if (size && readExact(audioSock, pcm.data(), size, 2000, sessionDone) != 1)
                            return;
                        if (flags & kPacketConfig)
                            continue;
                        stream.onAudio(pcm.data(), pcm.size(), 48000, 2, int64_t(flags & kPtsMask) * 1000);
                    }
                });
            }
            uint8_t meta[64];
            uint8_t head[12];
            if (readExact(sock, meta, sizeof(meta), 5000, m_stop) == 1 &&
                readExact(sock, head, 4, 10000, m_stop) == 1) {
                meta[63] = 0;
                const QString deviceName = QString::fromUtf8(reinterpret_cast<const char *>(meta));
                const uint32_t codecId = be32(head);
                if (codecId == kCodecH264 || codecId == kCodecH265) {
                    const VideoCodec codec = codecId == kCodecH265 ? VideoCodec::HEVC : VideoCodec::H264;
                    stream.configure(codec, fps, opt.width, opt.height);
                    {
                        QMutexLocker lock(&m_mutex);
                        if (!deviceName.isEmpty())
                            m_status.deviceName = deviceName;
                    }
                    std::vector<uint8_t> config;
                    std::vector<uint8_t> data;
                    while (!m_stop) {
                        const int rh = readExact(sock, head, sizeof(head), kSilenceTimeoutMs, m_stop);
                        if (rh != 1) {
                            endReason = tr("Связь с телефоном потеряна");
                            break;
                        }
                        const uint64_t ptsFlags = be64(head);
                        if (ptsFlags & kPacketSession) {
                            // New capture session (e.g. rotation): new size.
                            const int w = int(be32(head + 4)), h = int(be32(head + 8));
                            stream.configure(codec, fps, w, h);
                            qInfo("[android] session %dx%d", w, h);
                            continue;
                        }
                        const uint32_t size = be32(head + 8);
                        if (size == 0 || size > 32u * 1024 * 1024) {
                            endReason = tr("Ошибка в данных от телефона");
                            break;
                        }
                        // A config packet (SPS/PPS) is prepended to the next
                        // frame, so keyframes are self-contained.
                        const size_t prefix = (ptsFlags & kPacketConfig) ? 0 : config.size();
                        data.resize(prefix + size);
                        if (prefix)
                            std::memcpy(data.data(), config.data(), prefix);
                        if (readExact(sock, data.data() + prefix, size, kSilenceTimeoutMs, m_stop) != 1) {
                            endReason = tr("Связь с телефоном потеряна");
                            break;
                        }
                        stream.addTransportBytes(sizeof(head) + size);
                        if (ptsFlags & kPacketConfig) {
                            config.assign(data.begin(), data.end());
                            continue;
                        }
                        config.clear();
                        const bool key = (ptsFlags & kPacketKey) != 0;
                        const int64_t ptsNs = int64_t(ptsFlags & kPtsMask) * 1000;
                        if (stream.onAccessUnit(data.data(), data.size(), ptsNs, key)) {
                            gotVideo = true;
                            setStatus(SourceState::Streaming, tr("Видео с %1").arg(model));
                        }
                        StreamStats st;
                        if (stream.takeStats(&st))
                            emit statsChanged(st);
                        pumpServerOutput();
                    }
                } else {
                    endReason = tr("Телефон не смог запустить камеру");
                }
            }
            sessionDone = true;
            if (audioThread.joinable())
                audioThread.join();
            net_close(sock);
            stream.streamLost(m_stop ? tr("Источник остановлен") : endReason);
        }
        if (audioSock != OBSC_INVALID_SOCKET)
            net_close(audioSock);

        // 5. Tear down: the server exits when its socket closes.
        pumpServerOutput();
        if (server.state() != QProcess::NotRunning) {
            server.kill();
            server.waitForFinished(2000);
        }
        pumpServerOutput();
        Adb::run({QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("--remove"),
                  QStringLiteral("tcp:%1").arg(port)},
                 3000);
        emit statsChanged({});
        if (m_stop)
            break;

        if (!gotVideo) {
            const QString why = serverError(serverOut);
            if (why.contains(QLatin1String("Android 12"))) {
                setStatus(SourceState::Error, tr("Камера доступна только на Android 12 и новее"),
                          tr("На этом телефоне трансляция камеры через USB не поддерживается."));
                sleepInterruptible(5000);
                continue;
            }
            if (fps > 30 && fpsOverride == 0) {
                // Many phones can't stream the camera at 60 fps in every size.
                qWarning("[android] start failed at %d fps (%s), retrying at 30 fps", fps, qPrintable(why));
                fpsOverride = 30;
                continue;
            }
            setStatus(SourceState::Error, tr("Не удалось запустить камеру телефона"),
                      why.isEmpty() ? tr("Разблокируйте телефон и закройте приложения, которые используют камеру.")
                                    : why);
            sleepInterruptible(3000);
            continue;
        }
        setStatus(SourceState::Searching, endReason.isEmpty() ? tr("Связь с телефоном потеряна") : endReason,
                  tr("Проверьте кабель. Переподключаемся…"));
        sleepInterruptible(1000);
    }
}

} // namespace otl::android
