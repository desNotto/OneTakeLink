// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp4Writer.h"

#include <QCoreApplication>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
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
    av_frame_free(&m_audioFrame);
    av_packet_free(&m_audioPkt);
    avcodec_free_context(&m_audioCtx);
}

bool Mp4Writer::open(const QString &path, const StreamFormat &format, bool fragmented, QString *error,
                     int rotationCw, int audioSampleRate, int audioChannels)
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

    if (audioSampleRate > 0 && audioChannels > 0) {
        // AAC, FFmpeg's native encoder: 48 kHz stereo at 192 kbit/s.
        const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
        m_audioCtx = aac ? avcodec_alloc_context3(aac) : nullptr;
        if (!m_audioCtx) {
            *error = QCoreApplication::translate("otl::Mp4Writer", "Нет кодировщика AAC для звука");
            return false;
        }
        m_audioCtx->sample_rate = audioSampleRate;
        m_audioCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&m_audioCtx->ch_layout, audioChannels);
        m_audioCtx->bit_rate = audioChannels > 1 ? 192000 : 128000;
        m_audioCtx->time_base = AVRational{1, audioSampleRate};
        if (m_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            m_audioCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(m_audioCtx, aac, nullptr);
        if (ret < 0) {
            *error = QStringLiteral("AAC: %1").arg(avErrorString(ret));
            return false;
        }
        m_audioStream = avformat_new_stream(m_ctx, nullptr);
        if (!m_audioStream) {
            *error = QCoreApplication::translate("otl::Mp4Writer", "MP4: не удалось создать звуковую дорожку");
            return false;
        }
        avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCtx);
        m_audioStream->time_base = m_audioCtx->time_base;
        m_audioFrame = av_frame_alloc();
        m_audioPkt = av_packet_alloc();
        m_audioFrame->nb_samples = m_audioCtx->frame_size > 0 ? m_audioCtx->frame_size : 1024;
        m_audioFrame->format = m_audioCtx->sample_fmt;
        av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCtx->ch_layout);
        m_audioFrame->sample_rate = audioSampleRate;
        if (av_frame_get_buffer(m_audioFrame, 0) < 0) {
            *error = QCoreApplication::translate("otl::Mp4Writer", "Недостаточно памяти");
            return false;
        }
        m_channels = audioChannels;
        // AAC encoders delay their output by `initial_padding` samples
        // (1024 here). Edit lists that would hide it don't survive the
        // fragmented -> regular remux, so drop that many leading samples
        // instead: sound then lines up with the picture exactly.
        m_primingToSkip = std::max(0, m_audioCtx->initial_padding);
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
    // Interleaved: with an audio track the muxer orders both by time.
    int ret = av_interleaved_write_frame(m_ctx, m_pkt);
    av_packet_unref(m_pkt);
    if (ret < 0) {
        *error = QCoreApplication::translate("otl::Mp4Writer", "Ошибка записи файла: %1").arg(avErrorString(ret));
        return false;
    }
    return true;
}

bool Mp4Writer::writeAudio(const int16_t *interleaved, int frames, QString *error)
{
    if (!m_audioCtx || frames <= 0)
        return true;
    m_audioFramesIn += frames; // the timeline counts every sample
    if (m_primingToSkip > 0) {
        const int skip = int(std::min<int64_t>(m_primingToSkip, frames));
        m_primingToSkip -= skip;
        frames -= skip;
        if (interleaved)
            interleaved += size_t(skip) * size_t(m_channels);
        if (frames <= 0)
            return true;
    }
    const size_t n = size_t(frames) * size_t(m_channels);
    const size_t old = m_pending.size();
    m_pending.resize(old + n);
    for (size_t i = 0; i < n; ++i)
        m_pending[old + i] = interleaved ? float(interleaved[i]) / 32768.0f : 0.0f;
    const size_t frameSamples = size_t(m_audioFrame->nb_samples) * size_t(m_channels);
    while (m_pending.size() >= frameSamples) {
        if (!encodeAudioFrame(false, error))
            return false;
    }
    return true;
}

bool Mp4Writer::encodeAudioFrame(bool flush, QString *error)
{
    const int nb = m_audioFrame->nb_samples;
    const size_t avail = m_pending.size() / size_t(m_channels);
    const int take = int(std::min<size_t>(avail, size_t(nb)));
    if (av_frame_make_writable(m_audioFrame) < 0) {
        *error = QCoreApplication::translate("otl::Mp4Writer", "Недостаточно памяти");
        return false;
    }
    // Interleaved s16 -> planar float; the last frame is padded with silence.
    for (int c = 0; c < m_channels; ++c) {
        float *plane = reinterpret_cast<float *>(m_audioFrame->extended_data[c]);
        for (int i = 0; i < nb; ++i)
            plane[i] = i < take ? m_pending[size_t(i) * size_t(m_channels) + size_t(c)] : 0.0f;
    }
    m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(size_t(take) * size_t(m_channels)));
    m_audioFrame->pts = m_audioPts;
    m_audioPts += nb;
    int ret = avcodec_send_frame(m_audioCtx, m_audioFrame);
    if (ret < 0) {
        *error = QStringLiteral("AAC: %1").arg(avErrorString(ret));
        return false;
    }
    (void)flush;
    return drainAudio(error);
}

bool Mp4Writer::drainAudio(QString *error)
{
    for (;;) {
        int ret = avcodec_receive_packet(m_audioCtx, m_audioPkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0) {
            *error = QStringLiteral("AAC: %1").arg(avErrorString(ret));
            return false;
        }
        av_packet_rescale_ts(m_audioPkt, m_audioCtx->time_base, m_audioStream->time_base);
        m_audioPkt->stream_index = m_audioStream->index;
        ret = av_interleaved_write_frame(m_ctx, m_audioPkt);
        av_packet_unref(m_audioPkt);
        if (ret < 0) {
            *error = QCoreApplication::translate("otl::Mp4Writer", "Ошибка записи файла: %1").arg(avErrorString(ret));
            return false;
        }
    }
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
    if (m_headerWritten && m_audioCtx) {
        // Remaining samples (padded) and the encoder's delayed packets.
        QString audioError;
        if (!m_pending.empty() && !encodeAudioFrame(true, &audioError))
            ok = false;
        if (avcodec_send_frame(m_audioCtx, nullptr) >= 0 && !drainAudio(&audioError))
            ok = false;
        if (!ok)
            *error = audioError;
    }
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
    m_audioStream = nullptr;
    av_frame_free(&m_audioFrame);
    av_packet_free(&m_audioPkt);
    avcodec_free_context(&m_audioCtx);
    m_pending.clear();
    return ok;
}

} // namespace otl
