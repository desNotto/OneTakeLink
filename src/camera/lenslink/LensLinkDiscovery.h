// SPDX-License-Identifier: GPL-2.0-or-later
// Finds phones running LensLink Camera: iPhones attached over USB (via
// usbmuxd) and apps advertising _lenslink._tcp over Bonjour on the LAN.
#pragma once

#include "camera/CameraSource.h"

#include <QList>
#include <QObject>

#include <atomic>

namespace otl::lenslink {

class LensLinkDiscovery : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Runs in the background (USB listing + ~1.5 s Bonjour browse) and
    // emits finished(). Ignored while a scan is already running.
    void scan();
    bool isScanning() const { return m_scanning; }

    // Blocking variant, for the worker thread.
    static QList<DeviceInfo> scanBlocking();

signals:
    void finished(const QList<otl::DeviceInfo> &devices);

private:
    std::atomic<bool> m_scanning{false};
};

} // namespace otl::lenslink

Q_DECLARE_METATYPE(QList<otl::DeviceInfo>)
