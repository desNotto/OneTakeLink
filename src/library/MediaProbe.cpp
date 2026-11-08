// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProbe.h"

#include <QCoreApplication>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

#include <cmath>

namespace otl {

int displayRotationCw(const AVCodecParameters *par)
{
    if (!par)
        return 0;
    const AVPacketSideData *sd =
        av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < sizeof(int32_t) * 9)
        return 0;
    const double ccw = av_display_rotation_get(reinterpret_cast<const int32_t *>(sd->data));
    if (std::isnan(ccw))
        return 0;
    const int cw = int(std::lround(-ccw / 90.0)) * 90;
    return ((cw % 360) + 360) % 360;
}

MediaInfo probeMedia(const QString &path)
{
    MediaInfo info;
    AVFormatContext *ctx = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (avformat_open_input(&ctx, utf8.constData(), nullptr, nullptr) < 0) {
        info.error = QCoreApplication::translate("otl::MediaProbe", "не удалось открыть файл");
        return info;
    }
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        info.error = QCoreApplication::translate("otl::MediaProbe", "файл повреждён");
        avformat_close_input(&ctx);
        return info;
    }
    int vs = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs >= 0) {
        AVStream *st = ctx->streams[vs];
        info.rotation = displayRotationCw(st->codecpar);
        const bool sideways = info.rotation == 90 || info.rotation == 270;
        info.width = sideways ? st->codecpar->height : st->codecpar->width;
        info.height = sideways ? st->codecpar->width : st->codecpar->height;
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
        info.error = QCoreApplication::translate("otl::MediaProbe", "нет видеодорожки");
    avformat_close_input(&ctx);
    return info;
}

} // namespace otl
