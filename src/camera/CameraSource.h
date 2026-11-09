// SPDX-License-Identifier: GPL-3.0-or-later
// Camera/Input Layer interface. The rest of the application (preview,
// recording, UI) talks only to this class, never to LensLink internals,
// so another video source can be added later without touching them.
#pragma once

#include "camera/StreamClock.h"
#include "core/VideoTypes.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace otl {

enum class Transport { Usb, Wifi };

// Which source implementation serves a device.
enum class SourceKind {
    LensLink, // iPhone running LensLink Camera (USB via usbmuxd, or Wi-Fi)
    Android,  // Android phone over adb + scrcpy server (USB)
};

// A device the user can pick. LensLink: `address` is the USB serial (UDID,
// may be empty = "first iPhone on USB") or the phone's IP/host name for
// Wi-Fi. Android: `address` is the adb serial (empty = first device).
struct DeviceInfo {
    SourceKind kind = SourceKind::LensLink;
    Transport transport = Transport::Usb;
    QString address;
    QString name;
    QString note; // e.g. adb state "unauthorized", shown in the picker

    QString displayName() const;
    QString key() const; // stable id for settings, e.g. "usb:<udid>"
    static DeviceInfo fromKey(const QString &key);
};

enum class SourceState {
    Idle,        // not started
    Searching,   // looking for the phone / dialing
    Connecting,  // transport open, waiting for the app
    Standby,     // app open but camera not running
    NotArmed,    // app open, remote start not armed
    Streaming,   // video flowing
    Error,       // a problem the user must fix (message + hint say what)
};

struct SourceStatus {
    SourceState state = SourceState::Idle;
    QString message;     // one line: what is happening
    QString hint;        // what to do about it (may be empty)
    QString deviceName;  // from the phone's HELLO
    Transport transport = Transport::Usb;
};

class CameraSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~CameraSource() override = default;

    // Sinks are set before start() and stay valid until stop() returns.
    virtual void setPacketSink(EncodedPacketSink *sink) = 0;
    virtual void setFrameSink(VideoFrameSink *sink) = 0;

    virtual void start(const DeviceInfo &device) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // Camera remote control, as a protocol command object such as
    // {"cmd":"zoom","value":2.0}. Unsupported commands are ignored by
    // the device; the UI decides what to offer from cameraState().
    virtual void sendCommand(const QJsonObject &command) = 0;

    // PC monotonic time -> this source's stream timestamps (for PC audio).
    const StreamClock &clock() const { return m_clock; }

signals:
    void statusChanged(const otl::SourceStatus &status);
    void statsChanged(const otl::StreamStats &stats);
    // Latest camera state snapshot reported by the device (zoom, lens,
    // supported formats...). Empty object when disconnected.
    void cameraStateChanged(const QJsonObject &state);

protected:
    StreamClock m_clock;
};

} // namespace otl

Q_DECLARE_METATYPE(otl::SourceStatus)
Q_DECLARE_METATYPE(QList<otl::DeviceInfo>)
