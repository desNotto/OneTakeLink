// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioInput.h"

#include "camera/StreamHandler.h" // monotonicNs()

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace otl {

namespace {

constexpr int kOutRate = 48000;

#ifdef _WIN32
constexpr const char *kDeviceFormat = "dshow";
#else
constexpr const char *kDeviceFormat = "pulse";
#endif

void registerDevices()
{
    static std::once_flag once;
    std::call_once(once, [] { avdevice_register_all(); });
}

QString errText(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

int interruptCallback(void *opaque)
{
    return static_cast<std::atomic<bool> *>(opaque)->load() ? 1 : 0;
}

} // namespace

QList<AudioInputDevice> listAudioInputs()
{
    registerDevices();
    QList<AudioInputDevice> out;
    if (const AVInputFormat *fmt = av_find_input_format(kDeviceFormat)) {
        AVDeviceInfoList *list = nullptr;
        if (avdevice_list_input_sources(fmt, nullptr, nullptr, &list) >= 0 && list) {
            for (int i = 0; i < list->nb_devices; ++i) {
                const AVDeviceInfo *d = list->devices[i];
                bool audio = d->nb_media_types == 0; // unknown: keep
                for (int t = 0; t < d->nb_media_types; ++t)
                    audio = audio || d->media_types[t] == AVMEDIA_TYPE_AUDIO;
                if (!audio || !d->device_name)
                    continue;
                QString name = QString::fromUtf8(d->device_description ? d->device_description : d->device_name);
                QString open = QString::fromUtf8(d->device_name);
                // DirectShow lists the unique "@device_..." moniker in one
                // field and the friendly name in the other: show the friendly one.
                if (name.startsWith(QLatin1String("@device")))
                    std::swap(name, open);
#ifdef _WIN32
                open = QStringLiteral("audio=") + open;
#endif
                out.push_back({QString::fromLatin1(kDeviceFormat) + QLatin1Char('|') + open, name});
            }
            avdevice_free_list_devices(&list);
        }
    }
    const QString test = qEnvironmentVariable("OTL_AUDIO_TEST_INPUT");
    if (!test.isEmpty())
        out.push_back({QStringLiteral("lavfi|") + test, QStringLiteral("Test tone (lavfi)")});
    return out;
}

AudioInputCapture::~AudioInputCapture()
{
    stop();
}

void AudioInputCapture::start(const AudioInputDevice &device, InputChannels channels, Callback callback)
{
    stop();
    m_device = device;
    m_channels = channels;
    m_callback = std::move(callback);
    m_stop = false;
    setError({});
    m_thread = std::thread([this] { run(); });
}

void AudioInputCapture::stop()
{
    if (!m_thread.joinable())
        return;
    m_stop = true;
    m_thread.join();
}

QString AudioInputCapture::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void AudioInputCapture::setError(const QString &e)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_error = e;
    if (!e.isEmpty())
        qWarning("[audio-in] %s", qPrintable(e));
}

void AudioInputCapture::run()
{
    registerDevices();
    const int bar = m_device.id.indexOf(QLatin1Char('|'));
    const QByteArray formatName = m_device.id.left(bar).toUtf8();
    const QByteArray url = m_device.id.mid(bar + 1).toUtf8();
    const AVInputFormat *fmt = av_find_input_format(formatName.constData());
    if (!fmt) {
        setError(QCoreApplication::translate("otl::AudioInput", "Захват звука (%1) недоступен")
                     .arg(QString::fromUtf8(formatName)));
        return;
    }

    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback.callback = interruptCallback;
    ctx->interrupt_callback.opaque = &m_stop;
    AVDictionary *opts = nullptr;
    if (formatName == "dshow")
        av_dict_set(&opts, "audio_buffer_size", "20", 0); // ms: low latency
    int ret = avformat_open_input(&ctx, url.constData(), fmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        setError(QCoreApplication::translate("otl::AudioInput", "Не удалось открыть «%1»: %2")
                     .arg(m_device.name, errText(ret)));
        return; // ctx freed by avformat_open_input on failure
    }
    avformat_find_stream_info(ctx, nullptr);
    const AVCodec *codec = nullptr;
    const int stream = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    AVCodecContext *dec = nullptr;
    if (stream >= 0) {
        dec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec, ctx->streams[stream]->codecpar);
        ret = avcodec_open2(dec, codec, nullptr);
    }
    if (stream < 0 || ret < 0) {
        setError(QCoreApplication::translate("otl::AudioInput", "В «%1» нет звука").arg(m_device.name));
        avcodec_free_context(&dec);
        avformat_close_input(&ctx);
        return;
    }
    const int extraLatencyNs = formatName == "dshow" ? 20'000'000 : 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    SwrContext *swr = nullptr;
    int inChannels = 0;
    std::vector<int16_t> converted;
    std::vector<int16_t> stereo;

    while (!m_stop) {
        ret = av_read_frame(ctx, pkt);
        if (ret == AVERROR(EAGAIN))
            continue;
        if (ret < 0) {
            if (!m_stop)
                setError(QCoreApplication::translate("otl::AudioInput", "Устройство «%1» перестало отвечать: %2")
                             .arg(m_device.name, errText(ret)));
            break;
        }
        if (pkt->stream_index != stream || avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);
        while (avcodec_receive_frame(dec, frame) == 0) {
            const int64_t now = int64_t(monotonicNs());
            if (!swr) {
                inChannels = std::max(1, frame->ch_layout.nb_channels);
                AVChannelLayout outLayout;
                av_channel_layout_default(&outLayout, inChannels);
                if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, kOutRate, &frame->ch_layout,
                                        AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr) < 0 ||
                    swr_init(swr) < 0) {
                    setError(QCoreApplication::translate("otl::AudioInput", "Неподдерживаемый формат звука"));
                    m_stop = true;
                    break;
                }
            }
            const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            converted.resize(size_t(std::max(0, maxOut)) * size_t(inChannels));
            uint8_t *outPlanes[1] = {reinterpret_cast<uint8_t *>(converted.data())};
            const int got = swr_convert(swr, outPlanes, maxOut, const_cast<const uint8_t **>(frame->extended_data),
                                        frame->nb_samples);
            av_frame_unref(frame);
            if (got <= 0)
                continue;
            // Selected channel(s) -> stereo.
            stereo.resize(size_t(got) * 2);
            float peak = 0.0f;
            for (int i = 0; i < got; ++i) {
                const int16_t *in = converted.data() + size_t(i) * size_t(inChannels);
                int16_t l, r;
                switch (m_channels) {
                case InputChannels::First:
                    l = r = in[0];
                    break;
                case InputChannels::Second:
                    l = r = in[std::min(1, inChannels - 1)];
                    break;
                default:
                    l = in[0];
                    r = in[std::min(1, inChannels - 1)];
                    break;
                }
                stereo[size_t(i) * 2] = l;
                stereo[size_t(i) * 2 + 1] = r;
                peak = std::max({peak, std::abs(float(l)), std::abs(float(r))});
            }
            float prev = m_peak.load();
            const float p = peak / 32768.0f;
            while (p > prev && !m_peak.compare_exchange_weak(prev, p)) {
            }
            const int64_t capture = now - int64_t(got) * 1'000'000'000LL / kOutRate - extraLatencyNs;
            if (m_callback)
                m_callback(stereo.data(), got, capture);
        }
    }
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&ctx);
}

} // namespace otl
