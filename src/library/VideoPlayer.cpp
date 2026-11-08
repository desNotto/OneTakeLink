// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoPlayer.h"

#include "MediaProbe.h"

#include <algorithm>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace otl {

using Clock = std::chrono::steady_clock;

namespace {

QString errText(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

bool previewFormat(int fmt)
{
    return fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P;
}

} // namespace

// Everything below is owned by the worker thread.
struct VideoPlayer::State {
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    SwsContext *sws = nullptr;
    int stream = -1;
    AVRational tb{1, 1000};
    int64_t startPts = 0;
    qint64 durationMs = 0;
    double fps = 0;
    qint64 frameMs = 17;
    bool demuxEof = false;
    bool decodeEof = false;

    // Playback clock.
    FramePtr pending;          // next frame to show while playing
    qint64 pendingMs = 0;
    bool anchored = false;
    Clock::time_point anchorWall;
    qint64 anchorMs = 0;
    qint64 positionMs = 0;     // timestamp of the frame on screen
    Clock::time_point lastPositionEmit;

    bool isOpen() const { return dec != nullptr; }

    void release()
    {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        stream = -1;
        pending.reset();
        demuxEof = decodeEof = false;
    }

    qint64 msOf(const AVFrame *f) const
    {
        int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (pts == AV_NOPTS_VALUE)
            return positionMs;
        return av_rescale_q(pts - startPts, tb, AVRational{1, 1000});
    }

    // Next decoded frame (converted to a format the preview shows), or
    // null at the end of the file / on error.
    FramePtr decodeNext()
    {
        while (!decodeEof) {
            int ret = avcodec_receive_frame(dec, frame);
            if (ret == 0)
                return convert();
            if (ret == AVERROR_EOF) {
                decodeEof = true;
                break;
            }
            if (ret != AVERROR(EAGAIN))
                return nullptr;
            // Decoder wants input.
            if (demuxEof) {
                avcodec_send_packet(dec, nullptr); // drain
                continue;
            }
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) {
                demuxEof = true;
                continue;
            }
            if (pkt->stream_index == stream)
                avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
        }
        return nullptr;
    }

    FramePtr convert()
    {
        AVFrame *out = av_frame_alloc();
        if (!out) {
            av_frame_unref(frame);
            return nullptr;
        }
        if (previewFormat(frame->format)) {
            av_frame_move_ref(out, frame);
        } else {
            // e.g. 10-bit HEVC: down-convert for display only.
            out->format = AV_PIX_FMT_YUV420P;
            out->width = frame->width;
            out->height = frame->height;
            if (av_frame_get_buffer(out, 0) < 0) {
                av_frame_free(&out);
                av_frame_unref(frame);
                return nullptr;
            }
            sws = sws_getCachedContext(sws, frame->width, frame->height, AVPixelFormat(frame->format),
                                       out->width, out->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                       nullptr, nullptr, nullptr);
            if (sws)
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, out->data, out->linesize);
            av_frame_copy_props(out, frame);
            av_frame_unref(frame);
        }
        return makeFramePtr(out);
    }

    // Seeks to `ms`. Exact: decodes forward to the frame shown at `ms`;
    // otherwise returns the keyframe at or before it (fast scrubbing).
    FramePtr seekTo(qint64 ms, bool exact)
    {
        ms = std::clamp<qint64>(ms, 0, std::max<qint64>(0, durationMs));
        const int64_t target = av_rescale_q(ms, AVRational{1, 1000}, tb) + startPts;
        av_seek_frame(fmt, stream, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
        demuxEof = decodeEof = false;
        pending.reset();
        anchored = false;

        FramePtr best;
        for (;;) {
            FramePtr f = decodeNext();
            if (!f)
                break;
            best = f;
            if (!exact || msOf(f.get()) + frameMs / 2 >= ms)
                break;
        }
        return best;
    }
};

VideoPlayer::VideoPlayer(QObject *parent) : QObject(parent)
{
    m_thread = std::thread([this] { run(); });
}

VideoPlayer::~VideoPlayer()
{
    post({Cmd::Quit, {}, 0, true});
    if (m_thread.joinable())
        m_thread.join();
}

QString VideoPlayer::path() const
{
    std::lock_guard<std::mutex> lock(m_pathMutex);
    return m_path;
}

void VideoPlayer::open(const QString &path)
{
    {
        std::lock_guard<std::mutex> lock(m_pathMutex);
        m_path = path;
    }
    post({Cmd::Open, path, 0, true});
}

void VideoPlayer::close()
{
    {
        std::lock_guard<std::mutex> lock(m_pathMutex);
        m_path.clear();
    }
    post({Cmd::Close, {}, 0, true});
}

void VideoPlayer::play() { post({Cmd::Play, {}, 0, true}); }
void VideoPlayer::pause() { post({Cmd::Pause, {}, 0, true}); }
void VideoPlayer::togglePlay() { m_playing ? pause() : play(); }
void VideoPlayer::seek(qint64 ms, bool exact) { post({Cmd::Seek, {}, ms, exact}); }
void VideoPlayer::stepFrames(int frames) { post({Cmd::Step, {}, frames, true}); }

void VideoPlayer::post(Cmd cmd)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Scrubbing produces a burst of seeks: only the latest matters.
        if (cmd.kind == Cmd::Seek) {
            for (auto it = m_queue.begin(); it != m_queue.end();) {
                if (it->kind == Cmd::Seek)
                    it = m_queue.erase(it);
                else
                    ++it;
            }
        }
        m_queue.push_back(std::move(cmd));
    }
    m_cv.notify_one();
}

void VideoPlayer::run()
{
    State s;
    auto show = [&](const FramePtr &f, bool forcePosition) {
        if (!f)
            return;
        s.positionMs = s.msOf(f.get());
        if (m_sink)
            m_sink->onFrame(f);
        const auto now = Clock::now();
        if (forcePosition || now - s.lastPositionEmit > std::chrono::milliseconds(33)) {
            s.lastPositionEmit = now;
            emit positionChanged(s.positionMs);
        }
    };
    auto setPlaying = [&](bool on) {
        if (m_playing.exchange(on) != on)
            emit playingChanged(on);
        s.anchored = false;
    };

    for (;;) {
        Cmd cmd{Cmd::Quit, {}, 0, true};
        bool haveCmd = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const bool waitingForFrame = m_playing && s.isOpen() && s.pending && s.anchored;
            if (waitingForFrame) {
                const auto due = s.anchorWall + std::chrono::milliseconds(s.pendingMs - s.anchorMs);
                m_cv.wait_until(lock, due, [this] { return !m_queue.empty(); });
            } else if (!(m_playing && s.isOpen())) {
                m_cv.wait(lock, [this] { return !m_queue.empty(); });
            }
            if (!m_queue.empty()) {
                cmd = std::move(m_queue.front());
                m_queue.pop_front();
                haveCmd = true;
            }
        }

        if (haveCmd) {
            switch (cmd.kind) {
            case Cmd::Quit:
                s.release();
                return;
            case Cmd::Close:
                setPlaying(false);
                s.release();
                if (m_sink)
                    m_sink->onStreamLost();
                emit closed();
                break;
            case Cmd::Open: {
                setPlaying(false);
                s.release();
                const QByteArray utf8 = cmd.path.toUtf8();
                int ret = avformat_open_input(&s.fmt, utf8.constData(), nullptr, nullptr);
                if (ret >= 0)
                    ret = avformat_find_stream_info(s.fmt, nullptr);
                const AVCodec *codec = nullptr;
                if (ret >= 0)
                    ret = s.stream = av_find_best_stream(s.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
                if (ret >= 0) {
                    AVStream *st = s.fmt->streams[s.stream];
                    s.dec = avcodec_alloc_context3(codec);
                    avcodec_parameters_to_context(s.dec, st->codecpar);
                    s.dec->thread_count = 0; // auto: playback can afford frame threads
                    ret = avcodec_open2(s.dec, codec, nullptr);
                    s.tb = st->time_base;
                    s.startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
                    if (st->duration != AV_NOPTS_VALUE)
                        s.durationMs = av_rescale_q(st->duration, s.tb, AVRational{1, 1000});
                    else if (s.fmt->duration != AV_NOPTS_VALUE)
                        s.durationMs = s.fmt->duration / 1000;
                    const AVRational r = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
                    s.fps = (r.num && r.den) ? av_q2d(r) : 30.0;
                    s.frameMs = qint64(1000.0 / std::max(1.0, s.fps) + 0.5);
                    s.pkt = av_packet_alloc();
                    s.frame = av_frame_alloc();
                }
                if (ret < 0 || !s.pkt || !s.frame) {
                    const QString why = ret < 0 ? errText(ret) : QStringLiteral("out of memory");
                    s.release();
                    emit failed(cmd.path, why);
                    break;
                }
                emit opened(cmd.path, s.durationMs, s.fps, displayRotationCw(s.fmt->streams[s.stream]->codecpar));
                show(s.decodeNext(), true); // poster: the first frame
                break;
            }
            case Cmd::Play:
                if (!s.isOpen())
                    break;
                if (s.positionMs + s.frameMs >= s.durationMs) // at the end: start over
                    show(s.seekTo(0, true), true);
                setPlaying(true);
                break;
            case Cmd::Pause:
                setPlaying(false);
                s.pending.reset();
                emit positionChanged(s.positionMs);
                break;
            case Cmd::Seek:
                if (s.isOpen())
                    show(s.seekTo(cmd.value, cmd.exact), true);
                break;
            case Cmd::Step:
                if (!s.isOpen())
                    break;
                setPlaying(false);
                if (cmd.value > 0) {
                    FramePtr f = s.pending ? s.pending : s.decodeNext();
                    s.pending.reset();
                    for (qint64 i = 1; f && i < cmd.value; ++i)
                        f = s.decodeNext();
                    show(f, true);
                } else {
                    show(s.seekTo(s.positionMs + cmd.value * s.frameMs, true), true);
                }
                break;
            }
            continue;
        }

        // Playback.
        if (!m_playing || !s.isOpen())
            continue;
        if (!s.pending) {
            s.pending = s.decodeNext();
            if (!s.pending) {
                setPlaying(false);
                emit positionChanged(s.positionMs);
                emit endReached();
                continue;
            }
            s.pendingMs = s.msOf(s.pending.get());
        }
        const auto now = Clock::now();
        if (!s.anchored) {
            s.anchored = true;
            s.anchorWall = now;
            s.anchorMs = s.pendingMs;
        }
        const auto due = s.anchorWall + std::chrono::milliseconds(s.pendingMs - s.anchorMs);
        if (now + std::chrono::milliseconds(1) >= due) {
            // Falling far behind (slow PC, huge file): re-anchor instead
            // of racing to catch up.
            if (now - due > std::chrono::milliseconds(250))
                s.anchored = false;
            FramePtr f = std::move(s.pending);
            s.pending.reset();
            show(f, false);
        }
    }
}

} // namespace otl
