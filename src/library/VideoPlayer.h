// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in player for takes (Library Layer): FFmpeg demux + decode on a
// worker thread, frames paced by their timestamps and handed to any
// VideoFrameSink (the same OpenGL view the live monitor uses). Supports
// exact seeking and frame stepping — what you need to pick a take.
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

class VideoPlayer : public QObject {
    Q_OBJECT
public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer() override;

    void setFrameSink(VideoFrameSink *sink) { m_sink = sink; }

    // All asynchronous; results arrive as signals.
    void open(const QString &path);
    void close();
    void play();
    void pause();
    void togglePlay();
    void seek(qint64 ms, bool exact = true);
    void stepFrames(int frames); // +1 next frame, -1 previous frame

    QString path() const;
    bool isPlaying() const { return m_playing; }

signals:
    void opened(const QString &path, qint64 durationMs, double fps, int rotationCw);
    void failed(const QString &path, const QString &error);
    void closed();
    void positionChanged(qint64 ms);
    void playingChanged(bool playing);
    void endReached();

private:
    struct Cmd {
        enum Kind { Open, Close, Play, Pause, Seek, Step, Quit } kind;
        QString path;
        qint64 value = 0;
        bool exact = true;
    };
    struct State;

    void post(Cmd cmd);
    void run();

    VideoFrameSink *m_sink = nullptr;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Cmd> m_queue;
    std::atomic<bool> m_playing{false};
    mutable std::mutex m_pathMutex;
    QString m_path;
};

} // namespace otl
