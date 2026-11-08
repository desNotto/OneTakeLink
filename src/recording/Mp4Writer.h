// SPDX-License-Identifier: GPL-3.0-or-later
// Thin libavformat wrapper: writes an encoded H.264/HEVC elementary stream
// (Annex B access units) into an MP4 without re-encoding, plus an optional
// AAC audio track encoded here from 16-bit PCM.
#pragma once

#include "core/VideoTypes.h"

#include <QString>

#include <vector>

struct AVFormatContext;
struct AVStream;
struct AVPacket;
struct AVCodecContext;
struct AVFrame;

namespace otl {

class Mp4Writer {
public:
    ~Mp4Writer();

    // fragmented = crash-safe fragmented MP4 (fragment every ~0.5 s).
    // rotationCw (0/90/180/270): stored as the track's display matrix, so
    // players show the video rotated without re-encoding it.
    // audioSampleRate > 0 adds an AAC track (audioChannels channels).
    bool open(const QString &path, const StreamFormat &format, bool fragmented, QString *error,
              int rotationCw = 0, int audioSampleRate = 0, int audioChannels = 0);
    // pts relative to the first frame, in nanoseconds; must increase.
    bool write(const EncodedPacket &packet, int64_t relPtsNs, int64_t durationNs, QString *error);
    // Appends interleaved 16-bit PCM to the audio track, which starts at the
    // video's first frame (time 0) and runs sample by sample; nullptr = silence.
    bool writeAudio(const int16_t *interleaved, int frames, QString *error);
    bool hasAudio() const { return m_audioCtx != nullptr; }
    int64_t audioFramesWritten() const { return m_audioFramesIn; }
    bool close(QString *error); // flushes audio, writes the trailer
    qint64 bytesWritten() const;
    bool isOpen() const { return m_ctx != nullptr; }

private:
    bool encodeAudioFrame(bool flush, QString *error);
    bool drainAudio(QString *error);

    AVFormatContext *m_ctx = nullptr;
    AVStream *m_stream = nullptr;
    AVPacket *m_pkt = nullptr;
    // Audio
    AVCodecContext *m_audioCtx = nullptr;
    AVStream *m_audioStream = nullptr;
    AVFrame *m_audioFrame = nullptr;
    AVPacket *m_audioPkt = nullptr;
    std::vector<float> m_pending;  // interleaved samples not yet in a frame
    int m_channels = 0;
    int64_t m_audioFramesIn = 0;   // samples per channel accepted
    int64_t m_audioPts = 0;        // next encoder frame pts (1/sample rate)
    int64_t m_primingToSkip = 0;   // encoder delay compensated at the start
    int64_t m_lastDts = INT64_MIN;
    bool m_headerWritten = false;
};

QString avErrorString(int err);

} // namespace otl
