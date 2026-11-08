// SPDX-License-Identifier: GPL-2.0-or-later
// Shared handling of an encoded camera stream, used by every source that
// receives H.264/HEVC access units (LensLink, Android/scrcpy):
//   - decodes for the preview (hardware first, falls back per API),
//   - describes the stream (parameter sets) and forwards the encoded
//     packets to the recorder (passthrough),
//   - measures what really arrives (FPS, bitrate, gaps, latency).
// Adapted from LensLink's ios-camera-source.c video path.
#pragma once

#include "core/VideoTypes.h"

#include <atomic>
#include <memory>

namespace otl {

class VideoDecoder;

class StreamHandler {
public:
    StreamHandler(EncodedPacketSink *packets, VideoFrameSink *frames, bool hardwareDecode);
    ~StreamHandler();

    // Codec and nominal parameters announced by the source. A codec change
    // resets the decoder (it is re-created on the next keyframe).
    void configure(VideoCodec codec, double nominalFps, int width, int height);
    VideoCodec codec() const { return m_codec; }

    // Capture clock offset (source clock - local monotonic clock), when
    // known, to measure capture -> decode latency.
    void setClockOffset(int64_t offsetNs) { m_clockValid = true; m_clockOffset = offsetNs; }
    // While paused, timestamp gaps are expected and not counted as drops.
    void setPaused(bool paused) { m_paused = paused; }

    // One complete Annex B access unit. Returns true for the first frame
    // of the stream (the source can report "streaming").
    bool onAccessUnit(const uint8_t *data, size_t size, int64_t ptsNs, bool keyframe);

    // Interleaved little-endian 16-bit PCM from the phone microphone; ptsNs
    // on the video clock. Thread-safe (Android reads audio on its own thread).
    void onAudio(const uint8_t *pcm, size_t bytes, int sampleRate, int channels, int64_t ptsNs);

    // Bytes received from the transport (for the bitrate figure).
    void addTransportBytes(uint64_t bytes) { m_winBytes += bytes; }

    // Stats for the window since the previous call (or none yet: returns
    // false while less than `minWindowMs` passed and !force).
    bool takeStats(StreamStats *out, bool force = false, int minWindowMs = 500);

    // The stream ended: tells the sinks if anything was delivered.
    void streamLost(const QString &reason);
    bool started() const { return m_streaming || m_formatSent; }

private:
    EncodedPacketSink *m_packetSink;
    VideoFrameSink *m_frameSink;
    bool m_hwDecode;

    VideoCodec m_codec = VideoCodec::H264;
    double m_nominalFps = 0;
    int m_cfgWidth = 0, m_cfgHeight = 0;

    std::unique_ptr<VideoDecoder> m_decoder;
    int m_hwRetry = 0;
    uint64_t m_nextDecoderAttempt = 0;
    int m_frameWidth = 0, m_frameHeight = 0;

    StreamFormat m_format;
    bool m_formatSent = false;
    bool m_streaming = false;
    bool m_paused = false;

    bool m_clockValid = false;
    int64_t m_clockOffset = 0;
    double m_latencyMs = -1;

    uint64_t m_windowStartNs = 0;
    uint64_t m_winPackets = 0, m_winFrames = 0, m_winBytes = 0;
    uint64_t m_totalPackets = 0, m_missing = 0;
    std::atomic<uint64_t> m_winAudioFrames{0};
    std::atomic<int> m_audioRate{0};
    std::atomic<int> m_audioChannels{0};
    int64_t m_lastPts = -1;
};

uint64_t monotonicNs();

} // namespace otl
