// SPDX-License-Identifier: GPL-3.0-or-later
// Finds every camera source the app supports: iPhones with LensLink Camera
// (USB + Bonjour) and Android phones visible to adb.
#pragma once

#include "camera/CameraSource.h"

#include <QList>
#include <QObject>

#include <atomic>

namespace otl {

class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void scan(); // async; ignored while a scan runs
    static QList<DeviceInfo> scanBlocking();

signals:
    void finished(const QList<otl::DeviceInfo> &devices);

private:
    std::atomic<bool> m_scanning{false};
};

} // namespace otl
