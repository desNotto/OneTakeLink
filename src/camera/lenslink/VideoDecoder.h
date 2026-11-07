// SPDX-License-Identifier: GPL-2.0-or-later
// H.264/HEVC decoder with hardware acceleration (D3D11VA, DXVA2) and a
// software fallback. Rewritten for OneTakeLink from LensLink's
// obs-plugin/src/h264-decoder.c: the libobs output path is replaced by a
// callback that receives ref-counted CPU frames.
#pragma once

#include "core/VideoTypes.h"

#include <functional>
#include <string>

struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct AVBufferRef;

namespace otl::lenslink {

class VideoDecoder {
public:
    using FrameCallback = std::function<void(FramePtr)>;

    // hwStart: index into the hardware API priority list to begin with
    // (0 = best). Past the end, or allowHw = false, decodes in software.
    static std::unique_ptr<VideoDecoder> create(VideoCodec codec, bool allowHw, int hwStart);
    ~VideoDecoder();

    // Decodes one complete Annex B access unit. Returns false on a hard
    // error: the caller should recreate the decoder (next hw slot) and
    // wait for a keyframe.
    bool decode(const uint8_t *data, size_t size, int64_t ptsNs, const FrameCallback &onFrame);

    bool isHardware() const { return m_hwIndex >= 0; }
    int hwIndex() const { return m_hwIndex; }
    std::string apiName() const;
    uint64_t framesOutput() const { return m_framesOutput; }
    uint64_t packetsInput() const { return m_packetsInput; }

private:
    VideoDecoder() = default;
    bool initHw(const void *codec, int hwStart);

    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_frame = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    int m_hwPixFmt = -1;
    int m_hwIndex = -1;
    uint64_t m_framesOutput = 0;
    uint64_t m_packetsInput = 0;

    friend int getHwFormatCallback(AVCodecContext *, const int *);
};

} // namespace otl::lenslink
