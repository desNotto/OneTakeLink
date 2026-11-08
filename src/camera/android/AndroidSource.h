// SPDX-License-Identifier: GPL-3.0-or-later
// Camera source for Android phones (Android 12+), over USB with adb.
//
// Uses the scrcpy server (https://github.com/Genymobile/scrcpy, Apache-2.0)
// shipped with the app: it is pushed to the phone and started with
// app_process, captures the camera with Camera2 and encodes it with the
// phone's hardware encoder (MediaCodec). The H.264/HEVC stream arrives over
// an adb tunnel and goes through the same StreamHandler as LensLink:
// decoded for the preview, passed through untouched to the recorder.
// Nothing is installed on the phone; USB debugging must be enabled.
#pragma once

#include "camera/CameraSource.h"

#include <QMutex>

#include <atomic>

class QThread;

namespace otl::android {

struct AndroidOptions {
    QString facing = QStringLiteral("back"); // back | front | external
    int width = 1920;
    int height = 1080;
    int fps = 60;
    int bitrateMbps = 20;
    QString codec = QStringLiteral("h264"); // h264 | h265
    bool hardwareDecode = true;
    bool audio = false; // phone microphone (camcorder profile), raw PCM 48 kHz stereo
};

class AndroidSource final : public CameraSource {
    Q_OBJECT
public:
    explicit AndroidSource(QObject *parent = nullptr);
    ~AndroidSource() override;

    void setOptions(const AndroidOptions &options);
    AndroidOptions options() const;

    void setPacketSink(EncodedPacketSink *sink) override { m_packetSink = sink; }
    void setFrameSink(VideoFrameSink *sink) override { m_frameSink = sink; }

    void start(const DeviceInfo &device) override;
    void stop() override;
    bool isRunning() const override { return m_thread != nullptr; }

    // scrcpy is started with fixed options; camera commands are not supported.
    void sendCommand(const QJsonObject &) override {}

    static const char *serverVersion();

private:
    void run(DeviceInfo device);
    void setStatus(SourceState state, const QString &message, const QString &hint = {});
    void sleepInterruptible(int ms);

    QThread *m_thread = nullptr;
    std::atomic<bool> m_stop{false};
    EncodedPacketSink *m_packetSink = nullptr;
    VideoFrameSink *m_frameSink = nullptr;

    mutable QMutex m_mutex; // guards m_options, m_status
    AndroidOptions m_options;
    SourceStatus m_status;
};

} // namespace otl::android
