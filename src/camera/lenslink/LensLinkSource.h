// SPDX-License-Identifier: GPL-2.0-or-later
// Camera source that talks to the official LensLink Camera iOS app.
//
// A standalone rewrite of the connection logic of LensLink's OBS plugin
// (obs-plugin/src/ios-camera-source.c): it dials the app over USB
// (usbmuxd / Apple Mobile Device Service) or Wi-Fi, speaks the LensLink
// wire protocol (docs/LENSLINK_PROTOCOL.md), decodes the video and hands
// encoded packets and decoded frames to the sinks — no libobs anywhere.
#pragma once

#include "camera/CameraSource.h"

#include <QMutex>
#include <QStringList>

#include <atomic>
#include <deque>
#include <string>
#include <thread>

namespace otl::lenslink {

struct SourceOptions {
    bool autoStartCamera = true; // send start_stream when the app is idle
    bool hardwareDecode = true;
    // Format requested from the phone on connect ("" / 0 = leave as is).
    QString resolution = QStringLiteral("1080p");
    int fps = 60;
    QString codec = QStringLiteral("h264");
    QString quality = QStringLiteral("maximum");
};

class LensLinkSource final : public CameraSource {
    Q_OBJECT
public:
    explicit LensLinkSource(QObject *parent = nullptr);
    ~LensLinkSource() override;

    void setOptions(const SourceOptions &options);

    void setPacketSink(EncodedPacketSink *sink) override { m_packetSink = sink; }
    void setFrameSink(VideoFrameSink *sink) override { m_frameSink = sink; }

    void start(const DeviceInfo &device) override;
    void stop() override;
    bool isRunning() const override { return m_thread.joinable(); }

    void sendCommand(const QJsonObject &command) override;

private:
    struct Session;

    void run(DeviceInfo device);
    void runSession(Session &s);
    bool handlePacket(Session &s, int type, uint16_t flags, uint64_t pts,
                      const uint8_t *payload, uint32_t size);
    void handleVideo(Session &s, bool keyframe, uint64_t pts, const uint8_t *data, uint32_t size);
    void handleState(Session &s, const uint8_t *payload, uint32_t size);
    void sendControl(Session &s, const QByteArray &json);
    void flushOutgoing(Session &s);
    void publishStats(Session &s, bool force);
    void setStatus(SourceState state, const QString &message, const QString &hint = {});
    void sleepInterruptible(int ms);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    EncodedPacketSink *m_packetSink = nullptr;
    VideoFrameSink *m_frameSink = nullptr;

    QMutex m_mutex; // guards m_options, m_outgoing, m_status
    SourceOptions m_options;
    std::deque<QByteArray> m_outgoing;
    SourceStatus m_status;
};

} // namespace otl::lenslink
