// SPDX-License-Identifier: GPL-2.0-or-later
#include "LensLinkDiscovery.h"

#include <QCoreApplication>
#include <QPointer>

#include <thread>

#include "proto/net-compat.h"
#include "proto/mdns.h"
#include "proto/usbmux.h"

namespace otl::lenslink {

QList<DeviceInfo> LensLinkDiscovery::scanBlocking()
{
    QList<DeviceInfo> out;

    usbmux_device usb[16];
    const int n = usbmux_list_devices(usb, 16);
    for (int i = 0; i < n; ++i) {
        DeviceInfo d;
        d.transport = Transport::Usb;
        d.address = QString::fromUtf8(usb[i].udid);
        d.name = QStringLiteral("iPhone");
        out.push_back(d);
    }

    mdns_result found[16];
    const int m = mdns_browse("_lenslink._tcp.local", 1500, found, 16);
    for (int i = 0; i < m; ++i) {
        DeviceInfo d;
        d.transport = Transport::Wifi;
        d.address = QString::fromUtf8(found[i].host);
        d.name = QString::fromUtf8(found[i].name);
        out.push_back(d);
    }
    return out;
}

void LensLinkDiscovery::scan()
{
    if (m_scanning.exchange(true))
        return;
    net_init();
    QPointer<LensLinkDiscovery> self(this);
    std::thread([self] {
        QList<DeviceInfo> devices = scanBlocking();
        // QPointer is only checked on the main thread.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, devices] {
            if (!self)
                return;
            self->m_scanning = false;
            emit self->finished(devices);
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace otl::lenslink
