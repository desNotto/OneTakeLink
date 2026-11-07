// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSettings.h"

#include "recording/TakeNamer.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace otl {

QString AppSettings::iniPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("settings.ini"));
}

QString AppSettings::defaultRootFolder()
{
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::homePath();
    return QDir(videos).filePath(QStringLiteral("OneTakeLink"));
}

AppSettings AppSettings::load()
{
    QSettings s(iniPath(), QSettings::IniFormat);
    AppSettings a;
    a.deviceKey = s.value("source/device", a.deviceKey).toString();
    a.manualHost = s.value("source/manualHost").toString();
    a.autoStartCamera = s.value("source/autoStart", a.autoStartCamera).toBool();
    a.hardwareDecode = s.value("source/hwDecode", a.hardwareDecode).toBool();
    a.streamResolution = s.value("source/resolution", a.streamResolution).toString();
    a.streamFps = s.value("source/fps", a.streamFps).toInt();
    a.streamCodec = s.value("source/codec", a.streamCodec).toString();
    a.streamQuality = s.value("source/quality", a.streamQuality).toString();
    a.rootFolder = s.value("storage/root", defaultRootFolder()).toString();
    a.project = s.value("storage/project", a.project).toString();
    a.namePattern = s.value("storage/pattern", TakeNamer::defaultPattern()).toString();
    a.guideThirds = s.value("guides/thirds", a.guideThirds).toBool();
    a.guideCenter = s.value("guides/center", a.guideCenter).toBool();
    a.guideSafe = s.value("guides/safe", a.guideSafe).toBool();
    a.cropAspect = s.value("guides/aspect", a.cropAspect).toString();
    a.guidesEnabled = s.value("guides/enabled", a.guidesEnabled).toBool();
    a.countdownSeconds = s.value("record/countdown", a.countdownSeconds).toInt();
    return a;
}

void AppSettings::save() const
{
    QSettings s(iniPath(), QSettings::IniFormat);
    s.setValue("source/device", deviceKey);
    s.setValue("source/manualHost", manualHost);
    s.setValue("source/autoStart", autoStartCamera);
    s.setValue("source/hwDecode", hardwareDecode);
    s.setValue("source/resolution", streamResolution);
    s.setValue("source/fps", streamFps);
    s.setValue("source/codec", streamCodec);
    s.setValue("source/quality", streamQuality);
    s.setValue("storage/root", rootFolder);
    s.setValue("storage/project", project);
    s.setValue("storage/pattern", namePattern);
    s.setValue("guides/thirds", guideThirds);
    s.setValue("guides/center", guideCenter);
    s.setValue("guides/safe", guideSafe);
    s.setValue("guides/aspect", cropAspect);
    s.setValue("guides/enabled", guidesEnabled);
    s.setValue("record/countdown", countdownSeconds);
}

} // namespace otl
