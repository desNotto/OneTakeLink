// SPDX-License-Identifier: GPL-3.0-or-later
// All user settings, stored locally (QSettings: registry-free INI in the
// user's AppData folder) and restored on the next start.
#pragma once

#include <QString>

namespace otl {

struct AppSettings {
    // Source
    QString deviceKey = QStringLiteral("usb:"); // "usb:" = first iPhone on USB
    QString manualHost;
    bool autoStartCamera = true;
    bool hardwareDecode = true;
    QString streamResolution = QStringLiteral("1080p");
    int streamFps = 60;
    QString streamCodec = QStringLiteral("h264");
    QString streamQuality = QStringLiteral("maximum");
    // Android (scrcpy)
    QString androidFacing = QStringLiteral("back");
    QString androidMode = QStringLiteral("1920x1080@60"); // WxH@fps
    int androidBitrate = 20;                             // Mbit/s
    QString androidCodec = QStringLiteral("h264");

    // Storage
    QString rootFolder;
    QString project = QStringLiteral("MyProject");
    QString namePattern;

    // Guides
    bool guideThirds = true;
    bool guideCenter = false;
    bool guideSafe = false;
    QString cropAspect = QStringLiteral("none"); // none, 16:9, 9:16, 4:5, 1:1
    QString socialGuide = QStringLiteral("none"); // none, reels, shorts
    QString language = QStringLiteral("auto");    // auto, ru, en
    int monitorRotation = 0;                      // clockwise degrees
    bool guidesEnabled = true;

    int countdownSeconds = 0; // 0, 3, 5, 10
    bool recordAudio = false; // phone microphone -> AAC track

    static AppSettings load();
    void save() const;
    static QString defaultRootFolder();
    static QString iniPath();
};

} // namespace otl
