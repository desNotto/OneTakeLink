// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/CameraSource.h"
#include "camera/lenslink/LensLinkDiscovery.h"
#include "core/Log.h"
#include "core/Version.h"
#include "ui/MainWindow.h"
#include "ui/ShootPage.h"
#include "ui/Style.h"

#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QApplication::setApplicationName(QStringLiteral("OneTakeLink"));
    QApplication::setOrganizationName(QStringLiteral("OneTakeLink"));
    QApplication::setApplicationVersion(QStringLiteral(OTL_VERSION));

    QApplication app(argc, argv);
    otl::installLogging();
    qInfo("OneTakeLink %s starting", OTL_VERSION);

    qRegisterMetaType<otl::SourceStatus>();
    qRegisterMetaType<otl::StreamStats>();
    qRegisterMetaType<QList<otl::DeviceInfo>>();

    QIcon icon;
    for (int size : {16, 32, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/onetakelink-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);
    otl::applyStyle(app);

    otl::MainWindow window;
    window.show();

    // --connect=<device> (e.g. --connect=usb: or --connect=wifi:192.168.1.5)
    // connects right away; handy for shortcuts and testing.
    for (const QString &arg : app.arguments()) {
        if (arg.startsWith(QLatin1String("--connect=")))
            window.shootPage()->connectTo(arg.mid(10));
    }
    return app.exec();
}
