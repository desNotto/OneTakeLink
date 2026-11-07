// SPDX-License-Identifier: GPL-3.0-or-later
#include "CameraSource.h"

#include <QCoreApplication>

namespace otl {

QString DeviceInfo::displayName() const
{
    if (transport == Transport::Usb) {
        if (address.isEmpty())
            return QCoreApplication::translate("Device", "USB — первый найденный iPhone");
        QString label = name.isEmpty() ? QStringLiteral("iPhone") : name;
        return QStringLiteral("USB — %1 (…%2)").arg(label, address.right(6));
    }
    if (name.isEmpty() || name == address)
        return QStringLiteral("Wi-Fi — %1").arg(address);
    return QStringLiteral("Wi-Fi — %1 (%2)").arg(name, address);
}

QString DeviceInfo::key() const
{
    return (transport == Transport::Usb ? QStringLiteral("usb:") : QStringLiteral("wifi:")) + address;
}

DeviceInfo DeviceInfo::fromKey(const QString &key)
{
    DeviceInfo d;
    if (key.startsWith(QLatin1String("wifi:"))) {
        d.transport = Transport::Wifi;
        d.address = key.mid(5);
    } else if (key.startsWith(QLatin1String("usb:"))) {
        d.transport = Transport::Usb;
        d.address = key.mid(4);
    }
    return d;
}

} // namespace otl
