// SPDX-License-Identifier: GPL-3.0-or-later
// Source-independent video types shared by every layer. Nothing here knows
// about LensLink: a future camera source only has to produce these.
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

struct AVFrame;

namespace otl {

enum class VideoCodec { H264, HEVC };

inline QString codecName(VideoCodec c)
{
    return c == VideoCodec::HEVC ? QStringLiteral("H.265/HEVC") : QStringLiteral("H.264/AVC");
}

// Format of an encoded elementary stream as it arrives from the source.
struct StreamFormat {
    VideoCodec codec = VideoCodec::H264;
    int width = 0;
    int height = 0;
    double fps = 0;          // nominal, as announced by the source
    QByteArray parameterSets; // Annex B SPS/PPS (+VPS for HEVC)

    bool sameStream(const StreamFormat &o) const
    {
        return codec == o.codec && width == o.width && height == o.height &&
               parameterSets == o.parameterSets;
    }
};

// One encoded access unit (Annex B, start-code delimited NAL units).
struct EncodedPacket {
    std::shared_ptr<const std::vector<uint8_t>> data;
    int64_t ptsNs = 0;  // capture time on the source clock, monotonic
    bool keyframe = false;
};

// Decoded picture on the CPU (NV12, YUV420P or YUVJ420P). Ref-counted
// AVFrame; never mutated after it is published.
using FramePtr = std::shared_ptr<const AVFrame>;
FramePtr makeFramePtr(AVFrame *frameTakeOwnership);

// Receives the encoded stream (recording passthrough). Called on the
// source's worker thread: implementations must return quickly.
class EncodedPacketSink {
public:
    virtual ~EncodedPacketSink() = default;
    virtual void onStreamFormat(const StreamFormat &format) = 0;
    virtual void onPacket(const EncodedPacket &packet) = 0;
    virtual void onStreamLost(const QString &reason) = 0;
};

// Receives decoded frames (preview). Called on the source's worker thread.
class VideoFrameSink {
public:
    virtual ~VideoFrameSink() = default;
    virtual void onFrame(const FramePtr &frame) = 0;
    virtual void onStreamLost() {}
};

// Live measurements of what really arrives — never the requested values.
struct StreamStats {
    bool streaming = false;
    int width = 0;
    int height = 0;
    QString codec;
    double fpsReceived = 0;   // packets per second from the device
    double fpsDecoded = 0;    // frames per second out of the decoder
    double nominalFps = 0;    // what the device announced
    double mbps = 0;
    uint64_t framesReceived = 0;
    uint64_t framesMissing = 0; // gaps in the capture timestamps
    double latencyMs = -1;      // capture -> decoded, if clocks are synced
    QString decoder;            // "d3d11va", "software", ...
};

} // namespace otl

Q_DECLARE_METATYPE(otl::StreamStats)
