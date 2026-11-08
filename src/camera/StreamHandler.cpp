// SPDX-License-Identifier: GPL-2.0-or-later
#include "StreamHandler.h"

#include "VideoDecoder.h"
#include "core/AnnexB.h"

#include <QDebug>

#include <chrono>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
}

namespace otl {

namespace {
constexpr uint64_t kMs = 1000000ULL;
}

uint64_t monotonicNs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

StreamHandler::StreamHandler(EncodedPacketSink *packets, VideoFrameSink *frames, bool hardwareDecode)
    : m_packetSink(packets), m_frameSink(frames), m_hwDecode(hardwareDecode)
{
    m_windowStartNs = monotonicNs();
}

StreamHandler::~StreamHandler() = default;

void StreamHandler::configure(VideoCodec codec, double nominalFps, int width, int height)
{
    m_cfgWidth = width;
    m_cfgHeight = height;
    m_nominalFps = nominalFps;
    if (codec != m_codec || !m_decoder) {
        m_codec = codec;
        m_decoder.reset();
        m_hwRetry = 0;
    }
    m_lastPts = -1;
}

bool StreamHandler::onAccessUnit(const uint8_t *data, size_t size, int64_t ptsNs, bool keyframe)
{
    ++m_totalPackets;
    ++m_winPackets;

    // Gaps in capture timestamps = frames the phone dropped or the link lost.
    if (m_lastPts >= 0 && m_nominalFps > 1 && !m_paused) {
        const double interval = 1e9 / m_nominalFps;
        const double delta = double(ptsNs - m_lastPts);
        if (delta > 1.5 * interval)
            m_missing += uint64_t(std::llround(delta / interval) - 1);
    }
    m_lastPts = ptsNs;

    // Decode first: it yields the true frame size for the recorder.
    if (!m_decoder) {
        const uint64_t now = monotonicNs();
        if (keyframe && now >= m_nextDecoderAttempt) {
            m_decoder = VideoDecoder::create(m_codec, m_hwDecode, m_hwRetry);
            if (!m_decoder)
                m_nextDecoderAttempt = now + 2000 * kMs;
        }
    }
    if (m_decoder) {
        const bool ok = m_decoder->decode(data, size, ptsNs, [&](FramePtr frame) {
            ++m_winFrames;
            m_frameWidth = frame->width;
            m_frameHeight = frame->height;
            if (m_clockValid) {
                const int64_t local = ptsNs - m_clockOffset;
                m_latencyMs = double(int64_t(monotonicNs()) - local) / 1e6;
            }
            if (m_frameSink)
                m_frameSink->onFrame(frame);
        });
        // A hardware decoder that accepts packets but emits nothing (or
        // errors) is replaced by the next API in the priority list.
        const bool silent = m_decoder->isHardware() && m_decoder->packetsInput() > 90 &&
                            m_decoder->framesOutput() == 0;
        if (!ok || silent) {
            const int idx = m_decoder->hwIndex();
            qWarning("[stream] decoder %s failed, falling back", m_decoder->apiName().c_str());
            m_hwRetry = idx >= 0 ? idx + 1 : 0;
            m_decoder.reset();
        }
    }

    const bool first = !m_streaming;
    m_streaming = true;

    // Encoded stream for the recorder.
    if (keyframe) {
        QByteArray ps = extractParameterSets(m_codec, data, size);
        if (!ps.isEmpty()) {
            StreamFormat f;
            f.codec = m_codec;
            f.width = m_frameWidth > 0 ? m_frameWidth : m_cfgWidth;
            f.height = m_frameHeight > 0 ? m_frameHeight : m_cfgHeight;
            f.fps = m_nominalFps;
            f.parameterSets = ps;
            if (!m_formatSent || !f.sameStream(m_format)) {
                m_format = f;
                m_formatSent = true;
                if (m_packetSink)
                    m_packetSink->onStreamFormat(f);
            }
        }
    }
    if (m_formatSent && m_packetSink) {
        EncodedPacket p;
        p.data = std::make_shared<const std::vector<uint8_t>>(data, data + size);
        p.ptsNs = ptsNs;
        p.keyframe = keyframe;
        m_packetSink->onPacket(p);
    }
    return first;
}

void StreamHandler::onAudio(const uint8_t *pcm, size_t bytes, int sampleRate, int channels, int64_t ptsNs)
{
    if (channels <= 0 || bytes < size_t(2 * channels))
        return;
    const size_t samples = bytes / 2 / size_t(channels) * size_t(channels);
    auto data = std::make_shared<std::vector<int16_t>>(samples);
    // Wire format is little-endian, like every PC this runs on.
    std::memcpy(data->data(), pcm, samples * 2);
    m_audioRate = sampleRate;
    m_audioChannels = channels;
    m_winAudioFrames += samples / size_t(channels);
    if (m_packetSink) {
        AudioChunk c;
        c.samples = std::move(data);
        c.sampleRate = sampleRate;
        c.channels = channels;
        c.ptsNs = ptsNs;
        m_packetSink->onAudio(c);
    }
}

bool StreamHandler::takeStats(StreamStats *out, bool force, int minWindowMs)
{
    const uint64_t now = monotonicNs();
    const uint64_t dt = now - m_windowStartNs;
    if (!force && dt < uint64_t(minWindowMs) * kMs)
        return false;
    const double sec = double(dt) / 1e9;
    StreamStats st;
    st.streaming = m_streaming;
    st.width = m_frameWidth;
    st.height = m_frameHeight;
    st.codec = codecName(m_codec);
    st.fpsReceived = sec > 0 ? double(m_winPackets) / sec : 0;
    st.fpsDecoded = sec > 0 ? double(m_winFrames) / sec : 0;
    st.nominalFps = m_nominalFps;
    st.mbps = sec > 0 ? double(m_winBytes) * 8.0 / sec / 1e6 : 0;
    st.framesReceived = m_totalPackets;
    st.framesMissing = m_missing;
    st.latencyMs = m_latencyMs;
    st.decoder = m_decoder ? QString::fromStdString(m_decoder->apiName()) : QString();
    st.audio = m_winAudioFrames.exchange(0) > 0;
    st.audioSampleRate = st.audio ? m_audioRate.load() : 0;
    st.audioChannels = st.audio ? m_audioChannels.load() : 0;
    m_windowStartNs = now;
    m_winPackets = m_winFrames = m_winBytes = 0;
    *out = st;
    return true;
}

void StreamHandler::streamLost(const QString &reason)
{
    if (!started())
        return;
    if (m_packetSink)
        m_packetSink->onStreamLost(reason);
    if (m_frameSink)
        m_frameSink->onStreamLost();
}

} // namespace otl
