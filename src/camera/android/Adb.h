// SPDX-License-Identifier: GPL-3.0-or-later
// Thin wrapper around the adb command-line tool (Android Debug Bridge).
// OneTakeLink ships adb next to the executable (android/adb.exe); PATH is
// the fallback. OTL_ADB overrides it (tests use a fake adb).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace otl::android {

struct AdbDevice {
    QString serial;
    QString state; // "device", "unauthorized", "offline", "no permissions", ...
    QString model; // from `adb devices -l`, underscores replaced by spaces
};

struct AdbResult {
    bool started = false;
    int exitCode = -1;
    QString out; // stdout + stderr
    bool ok() const { return started && exitCode == 0; }
};

class Adb {
public:
    static QString executable();      // empty if not found
    static QString serverJar();       // scrcpy-server shipped with the app
    static AdbResult run(const QStringList &args, int timeoutMs = 10000);
    static QList<AdbDevice> devices();

    // adb keeps a background server (port 5037) running after the first
    // command. If OneTakeLink started it, stop it on exit; never touch one
    // that was already running (Android Studio, scrcpy, ...).
    static void shutdownIfWeStartedIt();
};

} // namespace otl::android
