// SPDX-License-Identifier: GPL-2.0-or-later
// Based on LensLink obs-plugin/src/h264-decoder.c (GPL-2.0-or-later).
#include "VideoDecoder.h"

#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace otl::lenslink {

// GPU decode APIs to try, best first.
static const AVHWDeviceType kHwPriority[] = {
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#else
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_VDPAU,
#endif
    AV_HWDEVICE_TYPE_NONE,
};

// get_format callback: picks the hw pixel format chosen in initHw(), or
// falls back to software when the driver does not offer it.
int getHwFormatCallback(AVCodecContext *ctx, const int *formatsInt)
{
    auto *dec = static_cast<VideoDecoder *>(ctx->opaque);
    const auto *formats = reinterpret_cast<const AVPixelFormat *>(formatsInt);
    for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == dec->m_hwPixFmt)
            return *p;
    }
    // Driver refused the hw format for this stream: take software.
    qWarning("[lenslink] hardware pixel format unavailable, decoding in software");
    dec->m_hwIndex = -1;
    return formats[0];
}

static AVPixelFormat getFormatTrampoline(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    return static_cast<AVPixelFormat>(
        getHwFormatCallback(ctx, reinterpret_cast<const int *>(formats)));
}

bool VideoDecoder::initHw(const void *codecPtr, int hwStart)
{
    const auto *codec = static_cast<const AVCodec *>(codecPtr);
    if (hwStart < 0)
        hwStart = 0;
    const int count = int(sizeof(kHwPriority) / sizeof(kHwPriority[0])) - 1;
    for (int i = hwStart; i < count; ++i) {
        AVHWDeviceType type = kHwPriority[i];
        AVPixelFormat pix = AV_PIX_FMT_NONE;
        for (int j = 0;; ++j) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (!config)
                break;
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                config->device_type == type) {
                pix = config->pix_fmt;
                break;
            }
        }
        if (pix == AV_PIX_FMT_NONE)
            continue;

        AVBufferRef *device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0)
            continue;

        m_hwDevice = device;
        m_hwPixFmt = pix;
        m_hwIndex = i;
        m_ctx->hw_device_ctx = av_buffer_ref(device);
        m_ctx->opaque = this;
        m_ctx->get_format = getFormatTrampoline;
        qInfo("[lenslink] hardware decoding via %s", av_hwdevice_get_type_name(type));
        return true;
    }
    return false;
}

std::unique_ptr<VideoDecoder> VideoDecoder::create(VideoCodec codecId, bool allowHw, int hwStart)
{
    const AVCodec *codec =
        avcodec_find_decoder(codecId == VideoCodec::HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!codec) {
        qWarning("[lenslink] no decoder for %s in this FFmpeg build",
                 qPrintable(codecName(codecId)));
        return nullptr;
    }

    std::unique_ptr<VideoDecoder> dec(new VideoDecoder);
    dec->m_ctx = avcodec_alloc_context3(codec);
    dec->m_pkt = av_packet_alloc();
    dec->m_frame = av_frame_alloc();
    if (!dec->m_ctx || !dec->m_pkt || !dec->m_frame)
        return nullptr;

    // Every packet is a complete access unit (protocol guarantee), and
    // the phone never reorders frames: decode with no added latency.
    dec->m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // Drivers sometimes refuse streams whose SPS level is outside their
    // declared support although they can decode them; let FFmpeg try.
    dec->m_ctx->hwaccel_flags |= AV_HWACCEL_FLAG_IGNORE_LEVEL |
                                 AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH;
    // Frame threading buffers one frame per thread: pure latency.
    dec->m_ctx->thread_count = 1;

    if (allowHw && !dec->initHw(codec, hwStart))
        qInfo("[lenslink] no %shardware decoder available, using software",
              hwStart > 0 ? "further " : "");

    if (avcodec_open2(dec->m_ctx, codec, nullptr) < 0) {
        qWarning("[lenslink] failed to open decoder");
        return nullptr;
    }
    return dec;
}

VideoDecoder::~VideoDecoder()
{
    av_frame_free(&m_frame);
    av_packet_free(&m_pkt);
    avcodec_free_context(&m_ctx);
    av_buffer_unref(&m_hwDevice);
}

std::string VideoDecoder::apiName() const
{
    if (m_hwIndex < 0)
        return "software";
    const char *n = av_hwdevice_get_type_name(kHwPriority[m_hwIndex]);
    return n ? n : "hardware";
}

bool VideoDecoder::decode(const uint8_t *data, size_t size, int64_t ptsNs,
                          const FrameCallback &onFrame)
{
    ++m_packetsInput;
    m_pkt->data = const_cast<uint8_t *>(data);
    m_pkt->size = int(size);
    m_pkt->pts = ptsNs;
    m_pkt->dts = ptsNs;

    int ret = avcodec_send_packet(m_ctx, m_pkt);
    m_pkt->data = nullptr;
    m_pkt->size = 0;
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char err[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(ret, err, sizeof(err));
        qWarning("[lenslink] avcodec_send_packet (%s): %s", apiName().c_str(), err);
        // Corrupt data is survivable; anything else needs a new decoder.
        return ret == AVERROR_INVALIDDATA;
    }

    for (;;) {
        ret = avcodec_receive_frame(m_ctx, m_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            char err[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(ret, err, sizeof(err));
            qWarning("[lenslink] avcodec_receive_frame (%s): %s", apiName().c_str(), err);
            return false;
        }

        AVFrame *out = av_frame_alloc();
        if (!out) {
            av_frame_unref(m_frame);
            continue;
        }
        if (m_hwDevice && m_frame->format == m_hwPixFmt) {
            // GPU frame: download to system memory (NV12).
            if (av_hwframe_transfer_data(out, m_frame, 0) < 0) {
                qWarning("[lenslink] GPU frame download failed");
                av_frame_free(&out);
                av_frame_unref(m_frame);
                return false;
            }
            av_frame_copy_props(out, m_frame);
        } else {
            av_frame_move_ref(out, m_frame);
        }
        av_frame_unref(m_frame);
        ++m_framesOutput;
        onFrame(makeFramePtr(out));
    }
    return true;
}

} // namespace otl::lenslink
