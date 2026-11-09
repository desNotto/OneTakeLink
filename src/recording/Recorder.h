// SPDX-License-Identifier: GPL-3.0-or-later
// Recording Layer.
//
// Records the phone's own H.264/HEVC stream into MP4 without re-encoding
// (passthrough): no quality loss, almost no CPU/GPU load, and nothing in
// the encode path that can fail during a take.
//
// Crash safety: the take is first written as a fragmented MP4
// ("<name>.recording.mp4", a fragment every ~0.5 s), which stays playable
// up to the last complete fragment if the program or the PC dies. On STOP
// it is remuxed into a regular MP4 (moov at the front) under the final
// name. Leftover .recording.mp4 files are repaired on the next start
// (see recoverInterrupted()).
//
// Recording must begin on a keyframe. The recorder keeps the packets since
// the last keyframe, so a take starts at most one keyframe interval
// (2–4 s with LensLink) *before* REC was pressed — nothing is cut off.
#pragma once

#include "core/VideoTypes.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace otl {

class Mp4Writer;

class Recorder : public QObject, public EncodedPacketSink {
    Q_OBJECT
public:
    enum class State { Idle, WaitingKeyframe, Recording, Finalizing };
    Q_ENUM(State)

    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // Main thread. `finalPath` is where the finished .mp4 goes.
    // rotationCw: display rotation stored in the file (lossless).
    // recordAudio: add an AAC track if the source is sending microphone
    // audio when the take starts (otherwise the take is video only).
    bool start(const QString &finalPath, QString *error, int rotationCw = 0, bool recordAudio = false);
    void stop();
    State state() const { return m_state; }
    bool hasStream() const;

    // Minimum free space to start / keep recording.
    static constexpr qint64 kMinFreeToStart = 1024LL * 1024 * 1024;
    static constexpr qint64 kMinFreeWhileRecording = 300LL * 1024 * 1024;

    // EncodedPacketSink — called on the camera worker thread.
    void onStreamFormat(const StreamFormat &format) override;
    void onPacket(const EncodedPacket &packet) override;
    void onAudio(const AudioChunk &chunk) override;
    // Which audio to record; chunks from the other origin are ignored.
    void setAudioOrigin(AudioChunk::Origin origin) { m_audioOrigin = origin; }
    void onStreamLost(const QString &reason) override;

    // Repairs takes interrupted by a crash or power loss in `folder`
    // (recursively). Returns the recovered final paths. Blocking.
    static QStringList recoverInterrupted(const QString &folder);
    // Fragmented/any MP4 -> regular MP4 with the index up front.
    static bool remux(const QString &in, const QString &out, QString *error);
    static QString tempPathFor(const QString &finalPath);

signals:
    void stateChanged(otl::Recorder::State state);
    void progress(qint64 durationMs, qint64 bytes);
    // `warning` is non-empty when the take ended abnormally but was saved.
    void finished(const QString &path, qint64 durationMs, const QString &warning);
    void failed(const QString &error);

private:
    struct Item {
        enum Kind { Format, Packet, Audio, Lost, Start, Stop, Quit } kind;
        StreamFormat format;
        EncodedPacket packet;
        QString text;
        int value = 0;
        bool flag = false;
        AudioChunk audio;
    };

    void post(Item item);
    void run();
    void setState(State s);
    void beginFile();
    void writePacket(const EncodedPacket &p);
    void writeAudio(const AudioChunk &chunk);
    void finish(const QString &warning);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Item> m_queue;
    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_hasStream{false};
    std::atomic<AudioChunk::Origin> m_audioOrigin{AudioChunk::Origin::Phone};

    // Worker-thread state.
    StreamFormat m_format;
    bool m_formatValid = false;
    std::vector<EncodedPacket> m_gop; // packets since the last keyframe
    QString m_finalPath;
    int m_rotation = 0;
    bool m_recordAudio = false;
    std::deque<AudioChunk> m_audioBuffer;  // recent audio, for the pre-roll
    int64_t m_lastAudioLocalNs = 0;        // when the last chunk arrived
    bool m_audioAligned = false;
    int64_t m_audioClockOffset = 0;        // audio pts - video pts clock skew
    int64_t m_beginLocalNs = 0;
    int64_t m_beginRelNs = 0;
    std::unique_ptr<Mp4Writer> m_writer;
    int64_t m_firstPts = 0;
    int64_t m_lastPts = 0;
    qint64 m_lastProgressMs = -1;
    qint64 m_lastSpaceCheckMs = -1;
};

} // namespace otl
