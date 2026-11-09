// SPDX-License-Identifier: GPL-3.0-or-later
#include "Recorder.h"

#include "Mp4Writer.h"
#include "camera/StreamHandler.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace otl {

namespace {
constexpr size_t kMaxGopPackets = 60 * 10; // 10 s at 60 fps
const QString kTempSuffix = QStringLiteral(".recording.mp4");
} // namespace

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<otl::Recorder::State>();
    m_thread = std::thread([this] { run(); });
}

Recorder::~Recorder()
{
    post({Item::Stop, {}, {}, {}});
    post({Item::Quit, {}, {}, {}});
    if (m_thread.joinable())
        m_thread.join();
}

QString Recorder::tempPathFor(const QString &finalPath)
{
    QString base = finalPath;
    if (base.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive))
        base.chop(4);
    return base + kTempSuffix;
}

bool Recorder::hasStream() const
{
    return m_hasStream;
}

bool Recorder::start(const QString &finalPath, QString *error, int rotationCw, bool recordAudio)
{
    if (m_state != State::Idle) {
        *error = tr("Запись уже идёт");
        return false;
    }
    if (!m_hasStream) {
        *error = tr("Нет видеопотока с камеры");
        return false;
    }
    QFileInfo fi(finalPath);
    if (!QDir().mkpath(fi.absolutePath())) {
        *error = tr("Не удалось создать папку %1. Проверьте права доступа или выберите другую папку.")
                     .arg(QDir::toNativeSeparators(fi.absolutePath()));
        return false;
    }
    if (fi.exists() || QFileInfo::exists(tempPathFor(finalPath))) {
        *error = tr("Файл %1 уже существует").arg(fi.fileName());
        return false;
    }
    QStorageInfo storage(fi.absolutePath());
    if (storage.isValid() && storage.bytesAvailable() < kMinFreeToStart) {
        *error = tr("Мало места на диске (%1 МБ свободно). Освободите место или выберите другой диск.")
                     .arg(storage.bytesAvailable() / (1024 * 1024));
        return false;
    }
    setState(State::WaitingKeyframe);
    Item item{Item::Start, {}, {}, finalPath, rotationCw};
    item.flag = recordAudio;
    post(std::move(item));
    return true;
}

void Recorder::stop()
{
    post({Item::Stop, {}, {}, {}});
}

void Recorder::onStreamFormat(const StreamFormat &format)
{
    m_hasStream = true;
    post({Item::Format, format, {}, {}});
}

void Recorder::onPacket(const EncodedPacket &packet)
{
    post({Item::Packet, {}, packet, {}});
}

void Recorder::onAudio(const AudioChunk &chunk)
{
    if (chunk.origin != m_audioOrigin.load())
        return;
    Item item{Item::Audio, {}, {}, {}};
    item.audio = chunk;
    post(std::move(item));
}

void Recorder::onStreamLost(const QString &reason)
{
    m_hasStream = false;
    post({Item::Lost, {}, {}, reason});
}

void Recorder::post(Item item)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(item));
    }
    m_cv.notify_one();
}

void Recorder::setState(State s)
{
    if (m_state.exchange(s) != s)
        emit stateChanged(s);
}

void Recorder::run()
{
    for (;;) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_queue.empty(); });
            item = std::move(m_queue.front());
            m_queue.pop_front();
        }

        switch (item.kind) {
        case Item::Quit:
            return;
        case Item::Format:
            if (m_writer && m_formatValid &&
                (item.format.codec != m_format.codec || item.format.width != m_format.width ||
                 item.format.height != m_format.height)) {
                finish(tr("Формат видео с телефона изменился во время записи — дубль сохранён до "
                          "этого момента."));
            }
            m_format = item.format;
            m_formatValid = true;
            m_gop.clear();
            break;
        case Item::Packet: {
            const EncodedPacket &p = item.packet;
            if (p.keyframe)
                m_gop.clear();
            if (!m_gop.empty() || p.keyframe) {
                if (m_gop.size() < kMaxGopPackets)
                    m_gop.push_back(p);
            }
            if (m_writer) {
                writePacket(p);
            } else if (m_state == State::WaitingKeyframe && !m_finalPath.isEmpty() && p.keyframe) {
                beginFile();
            }
            break;
        }
        case Item::Audio: {
            const AudioChunk &a = item.audio;
            if (a.frames() <= 0)
                break;
            m_lastAudioLocalNs = monotonicNs();
            // Keep ~12 s for the pre-roll (a keyframe interval is <= 10 s).
            m_audioBuffer.push_back(a);
            while (!m_audioBuffer.empty() && a.ptsNs - m_audioBuffer.front().ptsNs > 12'000'000'000LL)
                m_audioBuffer.pop_front();
            if (m_writer && m_writer->hasAudio())
                writeAudio(a);
            break;
        }
        case Item::Lost:
            m_gop.clear();
            m_audioBuffer.clear();
            m_formatValid = false;
            if (m_writer) {
                finish(tr("%1 — сохранён записанный фрагмент.").arg(item.text));
            } else if (m_state == State::WaitingKeyframe) {
                m_finalPath.clear();
                setState(State::Idle);
                emit failed(tr("%1 — запись не началась.").arg(item.text));
            }
            break;
        case Item::Start:
            m_finalPath = item.text;
            m_rotation = item.value;
            m_recordAudio = item.flag;
            // Start from the last keyframe we hold, if any.
            if (m_formatValid && !m_gop.empty() && m_gop.front().keyframe)
                beginFile();
            break;
        case Item::Stop:
            if (m_writer) {
                finish({});
            } else if (m_state == State::WaitingKeyframe) {
                m_finalPath.clear();
                setState(State::Idle);
            }
            break;
        }
    }
}

void Recorder::beginFile()
{
    QString error;
    auto writer = std::make_unique<Mp4Writer>();
    const QString temp = tempPathFor(m_finalPath);
    // An audio track only if microphone audio is actually arriving.
    const bool audioLive = m_recordAudio && !m_audioBuffer.empty() &&
                           monotonicNs() - uint64_t(m_lastAudioLocalNs) < 1'500'000'000ULL;
    const int sampleRate = audioLive ? m_audioBuffer.back().sampleRate : 0;
    const int channels = audioLive ? m_audioBuffer.back().channels : 0;
    if (!writer->open(temp, m_format, true, &error, m_rotation, sampleRate, channels)) {
        QFile::remove(temp);
        m_finalPath.clear();
        setState(State::Idle);
        emit failed(tr("Не удалось начать запись: %1").arg(error));
        return;
    }
    m_writer = std::move(writer);
    m_firstPts = m_gop.front().ptsNs;
    m_lastPts = m_firstPts;
    m_lastProgressMs = -1;
    m_lastSpaceCheckMs = -1;
    setState(State::Recording);
    // Pre-roll: everything since the last keyframe (the packet that
    // triggered beginFile() is already the last element of m_gop).
    const std::vector<EncodedPacket> gop = m_gop;
    for (const EncodedPacket &p : gop) {
        if (!m_writer)
            break;
        writePacket(p);
    }
    // Audio from the same moment on.
    m_audioAligned = false;
    m_audioClockOffset = 0;
    m_beginLocalNs = int64_t(monotonicNs());
    m_beginRelNs = m_lastPts - m_firstPts;
    if (m_writer && m_writer->hasAudio()) {
        const std::deque<AudioChunk> buffered = m_audioBuffer;
        for (const AudioChunk &a : buffered) {
            if (!m_writer)
                break;
            writeAudio(a);
        }
    }
}

void Recorder::writeAudio(const AudioChunk &a)
{
    const int sr = std::max(1, a.sampleRate);
    int64_t rel = a.ptsNs - m_firstPts;
    if (!m_audioAligned) {
        m_audioAligned = true;
        // Phone audio and video share a clock (LensLink, scrcpy). If they
        // obviously don't, align by arrival time instead of drifting apart.
        if (rel > 10'000'000'000LL || rel < -20'000'000'000LL) {
            const int64_t expected = m_beginRelNs + (int64_t(monotonicNs()) - m_beginLocalNs);
            m_audioClockOffset = rel - expected;
            qWarning("[recorder] audio clock differs from video by %lld ms, aligning by arrival",
                     static_cast<long long>(m_audioClockOffset / 1000000));
        }
    }
    rel -= m_audioClockOffset;

    const int64_t startFrame = rel * sr / 1'000'000'000LL; // position of this chunk
    const int64_t written = m_writer->audioFramesWritten();
    const int frames = a.frames();
    int skip = 0;
    if (startFrame < written) {
        // Overlap or before the take's start: drop what's already covered.
        skip = int(std::min<int64_t>(frames, written - startFrame));
    } else if (startFrame - written > sr / 50) {
        // A gap (> 20 ms, e.g. a lost chunk): fill with silence.
        QString error;
        if (!m_writer->writeAudio(nullptr, int(startFrame - written), &error)) {
            finish(error);
            return;
        }
    }
    if (skip >= frames)
        return;
    QString error;
    if (!m_writer->writeAudio(a.samples->data() + size_t(skip) * size_t(a.channels), frames - skip, &error))
        finish(error);
}

void Recorder::writePacket(const EncodedPacket &p)
{
    const int64_t rel = p.ptsNs - m_firstPts;
    const int64_t frameNs = m_format.fps > 0 ? int64_t(1e9 / m_format.fps) : 16666667;
    QString error;
    if (!m_writer->write(p, rel, frameNs, &error)) {
        finish(error);
        return;
    }
    m_lastPts = p.ptsNs;

    const qint64 ms = (m_lastPts - m_firstPts) / 1000000;
    if (m_lastProgressMs < 0 || ms - m_lastProgressMs >= 250) {
        m_lastProgressMs = ms;
        emit progress(ms, m_writer->bytesWritten());
    }
    if (m_lastSpaceCheckMs < 0 || ms - m_lastSpaceCheckMs >= 2000) {
        m_lastSpaceCheckMs = ms;
        QStorageInfo storage(QFileInfo(m_finalPath).absolutePath());
        if (storage.isValid() && storage.bytesAvailable() < kMinFreeWhileRecording)
            finish(tr("Заканчивается место на диске — запись остановлена, дубль сохранён."));
    }
}

void Recorder::finish(const QString &warning)
{
    if (!m_writer)
        return;
    setState(State::Finalizing);
    QString error;
    const bool closed = m_writer->close(&error);
    m_writer.reset();
    const qint64 durationMs = (m_lastPts - m_firstPts) / 1000000;
    const QString temp = tempPathFor(m_finalPath);
    const QString finalPath = m_finalPath;
    m_finalPath.clear();

    QString note = warning;
    if (!closed && note.isEmpty())
        note = error;

    // Fragmented -> regular MP4. If that fails, the fragmented file is a
    // valid MP4 too: keep it under the final name.
    QString remuxError;
    if (remux(temp, finalPath, &remuxError)) {
        QFile::remove(temp);
    } else {
        QFile::remove(finalPath);
        if (!QFile::rename(temp, finalPath)) {
            setState(State::Idle);
            emit failed(tr("Не удалось сохранить дубль (%1). Временный файл: %2")
                            .arg(remuxError, QDir::toNativeSeparators(temp)));
            return;
        }
    }
    setState(State::Idle);
    emit finished(finalPath, durationMs, note);
}

bool Recorder::remux(const QString &in, const QString &out, QString *error)
{
    const QByteArray inUtf8 = in.toUtf8();
    const QByteArray outUtf8 = out.toUtf8();
    AVFormatContext *ictx = nullptr;
    int ret = avformat_open_input(&ictx, inUtf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        *error = avErrorString(ret);
        return false;
    }
    avformat_find_stream_info(ictx, nullptr);

    AVFormatContext *octx = nullptr;
    ret = avformat_alloc_output_context2(&octx, nullptr, "mp4", outUtf8.constData());
    if (ret < 0) {
        *error = avErrorString(ret);
        avformat_close_input(&ictx);
        return false;
    }

    std::vector<int> map(ictx->nb_streams, -1);
    for (unsigned i = 0; i < ictx->nb_streams; ++i) {
        AVStream *is = ictx->streams[i];
        if (is->codecpar->codec_type != AVMEDIA_TYPE_VIDEO && is->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        AVStream *os = avformat_new_stream(octx, nullptr);
        avcodec_parameters_copy(os->codecpar, is->codecpar);
        os->codecpar->codec_tag = is->codecpar->codec_tag;
        os->time_base = is->time_base;
        os->avg_frame_rate = is->avg_frame_rate;
        os->r_frame_rate = is->r_frame_rate;
        map[i] = os->index;
    }

    bool ok = false;
    AVPacket *pkt = av_packet_alloc();
    if (avio_open(&octx->pb, outUtf8.constData(), AVIO_FLAG_WRITE) >= 0) {
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "movflags", "+faststart", 0);
        ret = avformat_write_header(octx, &opts);
        av_dict_free(&opts);
        if (ret >= 0) {
            ok = true;
            int64_t packets = 0;
            while (av_read_frame(ictx, pkt) >= 0) {
                if (map[size_t(pkt->stream_index)] >= 0) {
                    AVStream *is = ictx->streams[pkt->stream_index];
                    AVStream *os = octx->streams[map[size_t(pkt->stream_index)]];
                    av_packet_rescale_ts(pkt, is->time_base, os->time_base);
                    pkt->stream_index = os->index;
                    pkt->pos = -1;
                    if (av_interleaved_write_frame(octx, pkt) < 0) {
                        ok = false;
                        av_packet_unref(pkt);
                        break;
                    }
                    ++packets;
                }
                av_packet_unref(pkt);
            }
            if (ok && packets == 0) {
                *error = tr("в файле нет кадров");
                ok = false;
            }
            if (av_write_trailer(octx) < 0)
                ok = false;
        } else {
            *error = avErrorString(ret);
        }
        avio_closep(&octx->pb);
    } else {
        *error = tr("не удалось создать %1").arg(out);
    }
    av_packet_free(&pkt);
    avformat_free_context(octx);
    avformat_close_input(&ictx);
    if (!ok)
        QFile::remove(out);
    return ok;
}

QStringList Recorder::recoverInterrupted(const QString &folder)
{
    QStringList recovered;
    if (folder.isEmpty() || !QDir(folder).exists())
        return recovered;
    QDirIterator it(folder, {QStringLiteral("*") + kTempSuffix}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString temp = it.next();
        QString base = temp;
        base.chop(kTempSuffix.size());
        QString target = base + QStringLiteral(".mp4");
        for (int n = 2; QFileInfo::exists(target); ++n)
            target = QStringLiteral("%1_recovered%2.mp4").arg(base).arg(n == 2 ? QString() : QString::number(n));
        QString error;
        if (remux(temp, target, &error)) {
            QFile::remove(temp);
            recovered << target;
        } else {
            qWarning("recovery of %s failed: %s", qPrintable(temp), qPrintable(error));
        }
    }
    return recovered;
}

} // namespace otl
