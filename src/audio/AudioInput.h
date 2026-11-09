// SPDX-License-Identifier: GPL-3.0-or-later
// Sound from the PC's own audio inputs (sound card / audio interface),
// captured with FFmpeg's device layer: DirectShow on Windows, PulseAudio
// on Linux. Output is always 48 kHz stereo S16, from the selected channels.
//
// OTL_AUDIO_TEST_INPUT=<lavfi graph> adds a virtual test device (e.g.
// "sine=frequency=440:sample_rate=48000") for development and tests.
#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace otl {

struct AudioInputDevice {
    QString id;   // what FFmpeg opens (format-specific)
    QString name; // shown to the user
};

enum class InputChannels {
    Stereo, // channels 1+2 as left/right (mono devices: duplicated)
    First,  // channel 1 on both sides
    Second, // channel 2 on both sides
};

QList<AudioInputDevice> listAudioInputs();

class AudioInputCapture {
public:
    // 48 kHz stereo interleaved S16; localCaptureNs = monotonic time of the
    // first sample. Called on the capture thread.
    using Callback = std::function<void(const int16_t *samples, int frames, int64_t localCaptureNs)>;

    ~AudioInputCapture();

    // Starts the capture thread; errors are reported through lastError().
    void start(const AudioInputDevice &device, InputChannels channels, Callback callback);
    void stop();
    bool isRunning() const { return m_thread.joinable(); }
    QString lastError() const;
    QString deviceName() const { return m_device.name; }
    // Peak level since the last call, 0..1 (for a meter).
    float takePeak() { return m_peak.exchange(0.0f); }

private:
    void run();
    void setError(const QString &e);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<float> m_peak{0.0f};
    AudioInputDevice m_device;
    InputChannels m_channels = InputChannels::Stereo;
    Callback m_callback;
    mutable std::mutex m_mutex;
    QString m_error;
};

} // namespace otl
