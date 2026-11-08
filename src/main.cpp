// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/CameraSource.h"
#include "camera/lenslink/LensLinkDiscovery.h"
#include "core/Log.h"
#include "core/Version.h"
#include "ui/MainWindow.h"
#include "ui/ShootPage.h"
#include "ui/Style.h"

#include "core/AppSettings.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QApplication::setApplicationName(QStringLiteral("OneTakeLink"));
    QApplication::setOrganizationName(QStringLiteral("OneTakeLink"));
    QApplication::setApplicationVersion(QStringLiteral(OTL_VERSION));

    QApplication app(argc, argv);
    otl::installLogging();

    // UI language: the source strings are Russian; English comes from
    // the embedded translation. "auto" follows the system language.
    QString lang = otl::AppSettings::load().language;
    if (lang != QLatin1String("ru") && lang != QLatin1String("en")) {
        const QLocale::Language sys = QLocale::system().language();
        lang = (sys == QLocale::Russian || sys == QLocale::Ukrainian || sys == QLocale::Belarusian ||
                sys == QLocale::Kazakh)
                   ? QStringLiteral("ru")
                   : QStringLiteral("en");
    }
    QLocale::setDefault(QLocale(lang == QLatin1String("ru") ? QLocale::Russian : QLocale::English));
    QTranslator appTranslator;
    if (lang == QLatin1String("en") && appTranslator.load(QStringLiteral(":/i18n/onetakelink_en.qm")))
        app.installTranslator(&appTranslator);
    // Qt's own strings (dialog buttons) come in English; Russian needs qtbase_ru.
    QTranslator qtTranslator;
    if (lang == QLatin1String("ru")) {
        const QStringList dirs = {QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")),
                                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
        for (const QString &dir : dirs) {
            if (qtTranslator.load(QStringLiteral("qtbase_ru"), dir)) {
                app.installTranslator(&qtTranslator);
                break;
            }
        }
    }
    qInfo("UI language: %s", qPrintable(lang));
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
