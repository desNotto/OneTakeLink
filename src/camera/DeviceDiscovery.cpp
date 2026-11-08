// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceDiscovery.h"

#include "camera/android/Adb.h"
#include "camera/lenslink/LensLinkDiscovery.h"

#include <QCoreApplication>
#include <QPointer>

#include <future>
#include <thread>

namespace otl {

QList<DeviceInfo> DeviceDiscovery::scanBlocking()
{
    // iPhone (USB + ~1.5 s Bonjour) and adb in parallel.
    auto lens = std::async(std::launch::async, [] { return lenslink::LensLinkDiscovery::scanBlocking(); });
    QList<DeviceInfo> out;
    for (const android::AdbDevice &a : android::Adb::devices()) {
        DeviceInfo d;
        d.kind = SourceKind::Android;
        d.address = a.serial;
        d.name = a.model;
        if (a.state == QLatin1String("unauthorized"))
            d.note = QCoreApplication::translate("Device", "подтвердите отладку на телефоне");
        else if (a.state != QLatin1String("device"))
            d.note = a.state;
        out << d;
    }
    return lens.get() + out;
}

void DeviceDiscovery::scan()
{
    if (m_scanning.exchange(true))
        return;
    QPointer<DeviceDiscovery> self(this);
    std::thread([self] {
        QList<DeviceInfo> devices = scanBlocking();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, devices] {
            if (!self)
                return;
            self->m_scanning = false;
            emit self->finished(devices);
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace otl
