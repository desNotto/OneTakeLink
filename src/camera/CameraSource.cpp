// SPDX-License-Identifier: GPL-3.0-or-later
#include "CameraSource.h"

#include <QCoreApplication>

namespace otl {

QString DeviceInfo::displayName() const
{
    if (kind == SourceKind::Android) {
        if (address.isEmpty())
            return QCoreApplication::translate("Device", "Android — первый найденный (USB)");
        QString label = name.isEmpty() ? QStringLiteral("Android") : name;
        QString text = QStringLiteral("Android — %1 (…%2)").arg(label, address.right(6));
        if (!note.isEmpty())
            text += QStringLiteral(" — ") + note;
        return text;
    }
    if (transport == Transport::Usb) {
        if (address.isEmpty())
            return QCoreApplication::translate("Device", "iPhone — первый найденный (USB)");
        QString label = name.isEmpty() ? QStringLiteral("iPhone") : name;
        return QStringLiteral("iPhone USB — %1 (…%2)").arg(label, address.right(6));
    }
    if (name.isEmpty() || name == address)
        return QStringLiteral("iPhone Wi-Fi — %1").arg(address);
    return QStringLiteral("iPhone Wi-Fi — %1 (%2)").arg(name, address);
}

QString DeviceInfo::key() const
{
    if (kind == SourceKind::Android)
        return QStringLiteral("android:") + address;
    return (transport == Transport::Usb ? QStringLiteral("usb:") : QStringLiteral("wifi:")) + address;
}

DeviceInfo DeviceInfo::fromKey(const QString &key)
{
    DeviceInfo d;
    if (key.startsWith(QLatin1String("android:"))) {
        d.kind = SourceKind::Android;
        d.address = key.mid(8);
    } else if (key.startsWith(QLatin1String("wifi:"))) {
        d.transport = Transport::Wifi;
        d.address = key.mid(5);
    } else if (key.startsWith(QLatin1String("usb:"))) {
        d.transport = Transport::Usb;
        d.address = key.mid(4);
    }
    return d;
}

} // namespace otl
