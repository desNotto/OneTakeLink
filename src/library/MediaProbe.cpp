// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProbe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace otl {

MediaInfo probeMedia(const QString &path)
{
    MediaInfo info;
    AVFormatContext *ctx = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&ctx, utf8.constData(), nullptr, nullptr) < 0) {
        info.error = QStringLiteral("не удалось открыть файл");
        return info;
    }
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        info.error = QStringLiteral("файл повреждён");
        avformat_close_input(&ctx);
        return info;
    }
    int vs = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs >= 0) {
        AVStream *st = ctx->streams[vs];
        info.width = st->codecpar->width;
        info.height = st->codecpar->height;
        AVRational r = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
        if (r.num && r.den)
            info.fps = av_q2d(r);
        switch (st->codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            info.codec = QStringLiteral("H.264");
            break;
        case AV_CODEC_ID_HEVC:
            info.codec = QStringLiteral("H.265");
            break;
        default:
            info.codec = QString::fromUtf8(avcodec_get_name(st->codecpar->codec_id));
        }
        if (st->duration != AV_NOPTS_VALUE)
            info.durationMs = av_rescale_q(st->duration, st->time_base, AVRational{1, 1000});
    }
    if (info.durationMs <= 0 && ctx->duration != AV_NOPTS_VALUE)
        info.durationMs = ctx->duration / 1000;
    info.ok = vs >= 0;
    if (!info.ok)
        info.error = QStringLiteral("нет видеодорожки");
    avformat_close_input(&ctx);
    return info;
}

} // namespace otl
