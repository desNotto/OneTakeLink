// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp4Writer.h"

#include <QCoreApplication>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
}

#include <cstring>

namespace otl {

QString avErrorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

Mp4Writer::~Mp4Writer()
{
    if (m_ctx) {
        QString ignored;
        close(&ignored);
    }
    av_packet_free(&m_pkt);
}

bool Mp4Writer::open(const QString &path, const StreamFormat &format, bool fragmented, QString *error,
                     int rotationCw)
{
    const QByteArray utf8 = path.toUtf8();
    int ret = avformat_alloc_output_context2(&m_ctx, nullptr, "mp4", utf8.constData());
    if (ret < 0 || !m_ctx) {
        *error = QStringLiteral("MP4: %1").arg(avErrorString(ret));
        return false;
    }
    m_stream = avformat_new_stream(m_ctx, nullptr);
    if (!m_stream) {
        *error = QCoreApplication::translate("otl::Mp4Writer", "MP4: не удалось создать видеодорожку");
        return false;
    }
    AVCodecParameters *par = m_stream->codecpar;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = format.codec == VideoCodec::HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    // hvc1 is what Apple players and most editors expect for HEVC.
    if (format.codec == VideoCodec::HEVC)
        par->codec_tag = MKTAG('h', 'v', 'c', '1');
    par->width = format.width;
    par->height = format.height;
    par->format = AV_PIX_FMT_YUV420P;
    par->color_primaries = AVCOL_PRI_BT709;
    par->color_trc = AVCOL_TRC_BT709;
    par->color_space = AVCOL_SPC_BT709;
    par->color_range = AVCOL_RANGE_MPEG;
    // Annex B parameter sets: the MP4 muxer converts them to avcC/hvcC
    // and the access units to length-prefixed NAL units.
    if (!format.parameterSets.isEmpty()) {
        par->extradata = static_cast<uint8_t *>(
            av_mallocz(size_t(format.parameterSets.size()) + AV_INPUT_BUFFER_PADDING_SIZE));
        std::memcpy(par->extradata, format.parameterSets.constData(), size_t(format.parameterSets.size()));
        par->extradata_size = format.parameterSets.size();
    }
    if (rotationCw % 360 != 0) {
        AVPacketSideData *sd = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                       AV_PKT_DATA_DISPLAYMATRIX, sizeof(int32_t) * 9, 0);
        // av_display_rotation_set() takes a clockwise angle (while
        // av_display_rotation_get() returns a counter-clockwise one): a
        // 90° clockwise display shows up in ffprobe as rotation=-90, like a
        // portrait iPhone video.
        if (sd)
            av_display_rotation_set(reinterpret_cast<int32_t *>(sd->data), double(rotationCw));
    }
    m_stream->time_base = AVRational{1, 90000};
    if (format.fps > 0) {
        m_stream->avg_frame_rate = av_d2q(format.fps, 1000);
        m_stream->r_frame_rate = m_stream->avg_frame_rate;
    }

    if (!(m_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_ctx->pb, utf8.constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            *error = QCoreApplication::translate("otl::Mp4Writer", "Не удалось создать файл: %1").arg(avErrorString(ret));
            avformat_free_context(m_ctx);
            m_ctx = nullptr;
            return false;
        }
    }

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "video_track_timescale", "90000", 0);
    if (fragmented) {
        av_dict_set(&opts, "movflags", "+empty_moov+default_base_moof+frag_keyframe", 0);
        av_dict_set(&opts, "frag_duration", "500000", 0); // 0.5 s
        av_dict_set(&opts, "flush_packets", "1", 0);
    }
    ret = avformat_write_header(m_ctx, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        *error = QStringLiteral("MP4: %1").arg(avErrorString(ret));
        avio_closep(&m_ctx->pb);
        avformat_free_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    m_headerWritten = true;
    m_pkt = av_packet_alloc();
    return m_pkt != nullptr;
}

bool Mp4Writer::write(const EncodedPacket &packet, int64_t relPtsNs, int64_t durationNs, QString *error)
{
    if (!m_ctx)
        return false;
    const AVRational ns{1, 1000000000};
    int64_t ts = av_rescale_q(relPtsNs, ns, m_stream->time_base);
    if (ts <= m_lastDts)
        ts = m_lastDts + 1; // the phone never reorders; keep DTS strictly increasing
    m_lastDts = ts;

    // The muxer may rewrite the data in place (Annex B -> length
    // prefixed), so hand it its own copy.
    if (av_new_packet(m_pkt, int(packet.data->size())) < 0) {
        *error = QCoreApplication::translate("otl::Mp4Writer", "Недостаточно памяти");
        return false;
    }
    std::memcpy(m_pkt->data, packet.data->data(), packet.data->size());
    m_pkt->pts = ts;
    m_pkt->dts = ts;
    m_pkt->duration = durationNs > 0 ? av_rescale_q(durationNs, ns, m_stream->time_base) : 0;
    m_pkt->stream_index = m_stream->index;
    if (packet.keyframe)
        m_pkt->flags |= AV_PKT_FLAG_KEY;
    int ret = av_write_frame(m_ctx, m_pkt);
    av_packet_unref(m_pkt);
    if (ret < 0) {
        *error = QCoreApplication::translate("otl::Mp4Writer", "Ошибка записи файла: %1").arg(avErrorString(ret));
        return false;
    }
    return true;
}

qint64 Mp4Writer::bytesWritten() const
{
    return (m_ctx && m_ctx->pb) ? qint64(avio_tell(m_ctx->pb)) : 0;
}

bool Mp4Writer::close(QString *error)
{
    if (!m_ctx)
        return true;
    bool ok = true;
    if (m_headerWritten) {
        int ret = av_write_trailer(m_ctx);
        if (ret < 0) {
            *error = QStringLiteral("MP4: %1").arg(avErrorString(ret));
            ok = false;
        }
    }
    if (!(m_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&m_ctx->pb);
    avformat_free_context(m_ctx);
    m_ctx = nullptr;
    m_stream = nullptr;
    return ok;
}

} // namespace otl
