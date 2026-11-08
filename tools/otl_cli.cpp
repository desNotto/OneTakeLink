// SPDX-License-Identifier: GPL-3.0-or-later
// otl-cli: console tool for stage-1 checks of the LensLink link without
// the GUI — what the phone really delivers (resolution, FPS, drops,
// bitrate, latency) over a long run, and a test recording.
//
//   otl-cli devices
//   otl-cli monitor <device> [seconds]
//   otl-cli record  <device> <out.mp4> [seconds]
//   otl-cli recover <folder>
//
// <device>: usb            first iPhone on USB
//           usb:<udid>     a specific iPhone on USB
//           wifi:<ip>      LensLink Camera at this address
//           android        first Android phone on adb (USB debugging on)
//           android:<serial>
#include "camera/DeviceDiscovery.h"
#include "camera/android/AndroidSource.h"
#include "camera/lenslink/LensLinkSource.h"
#include "core/Log.h"
#include "recording/Recorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <cstdio>

extern "C" {
#include <libavutil/log.h>
}

using namespace otl;

static void usage()
{
    std::puts("usage:\n"
              "  otl-cli devices\n"
              "  otl-cli monitor <usb|usb:UDID|wifi:IP|android|android:SERIAL> [seconds]\n"
              "  otl-cli record  <device> <out.mp4> [seconds] [--audio]\n"
              "  otl-cli recover <folder>   repair takes interrupted by a crash");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OneTakeLink"));
    av_log_set_level(AV_LOG_WARNING);
    qRegisterMetaType<otl::SourceStatus>();
    qRegisterMetaType<otl::StreamStats>();
    QStringList args = app.arguments();
    // --audio: record the phone microphone too (Android: also requested
    // from the phone; iPhone: needs "Send phone mic to OBS" in the app).
    const bool withAudio = args.removeAll(QStringLiteral("--audio")) > 0;
    if (args.size() < 2) {
        usage();
        return 2;
    }
    const QString cmd = args[1];

    if (cmd == QLatin1String("devices")) {
        const QList<DeviceInfo> devices = DeviceDiscovery::scanBlocking();
        if (devices.isEmpty())
            std::puts("no devices found (USB: iPhone attached and trusted? Wi-Fi: app open, same network?)");
        for (const DeviceInfo &d : devices)
            std::printf("%-40s %s\n", qPrintable(d.key()), qPrintable(d.displayName()));
        return 0;
    }
    if (cmd == QLatin1String("recover") && args.size() >= 3) {
        const QStringList done = Recorder::recoverInterrupted(args[2]);
        for (const QString &p : done)
            std::printf("recovered %s\n", qPrintable(p));
        if (done.isEmpty())
            std::puts("nothing to recover");
        return 0;
    }
    if ((cmd != QLatin1String("monitor") && cmd != QLatin1String("record")) || args.size() < 3) {
        usage();
        return 2;
    }

    QString key = args[2];
    if (key == QLatin1String("usb"))
        key = QStringLiteral("usb:");
    if (key == QLatin1String("android"))
        key = QStringLiteral("android:");
    const DeviceInfo device = DeviceInfo::fromKey(key);
    const bool record = cmd == QLatin1String("record");
    if (record && args.size() < 4) {
        usage();
        return 2;
    }
    const QString outPath = record ? args[3] : QString();
    const int seconds = args.size() > (record ? 4 : 3) ? args[record ? 4 : 3].toInt() : (record ? 10 : 0);

    lenslink::LensLinkSource lensSource;
    android::AndroidSource androidSource;
    CameraSource &source = device.kind == SourceKind::Android ? static_cast<CameraSource &>(androidSource)
                                                               : static_cast<CameraSource &>(lensSource);
    android::AndroidOptions aopt = androidSource.options();
    aopt.audio = withAudio;
    androidSource.setOptions(aopt);
    Recorder recorder;
    source.setPacketSink(&recorder);

    int exitCode = 0;
    bool recordingRequested = false;
    QObject::connect(&source, &CameraSource::statusChanged, &app, [](const SourceStatus &s) {
        std::printf("[status] %s%s%s\n", qPrintable(s.message), s.hint.isEmpty() ? "" : "\n         ",
                    qPrintable(s.hint));
        std::fflush(stdout);
    });
    QObject::connect(&source, &CameraSource::statsChanged, &app, [&](const StreamStats &st) {
        if (!st.streaming || st.width == 0)
            return;
        static qint64 last = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last < 1000)
            return;
        last = now;
        std::printf("[stream] %dx%d %s  recv %.1f fps  decoded %.1f fps  (nominal %.0f)  %.1f Mbit/s  "
                    "missing %llu/%llu  latency %s  decoder %s  audio %s\n",
                    st.width, st.height, qPrintable(st.codec), st.fpsReceived, st.fpsDecoded, st.nominalFps,
                    st.mbps, (unsigned long long)st.framesMissing,
                    (unsigned long long)(st.framesReceived + st.framesMissing),
                    st.latencyMs >= 0 ? qPrintable(QStringLiteral("%1 ms").arg(qRound(st.latencyMs))) : "n/a",
                    qPrintable(st.decoder),
                    st.audio ? qPrintable(QStringLiteral("%1 Hz x%2").arg(st.audioSampleRate).arg(st.audioChannels)) : "none");
        std::fflush(stdout);
        if (record && !recordingRequested && recorder.hasStream()) {
            recordingRequested = true;
            QString error;
            if (!recorder.start(outPath, &error, 0, withAudio)) {
                std::printf("[record] cannot start: %s\n", qPrintable(error));
                exitCode = 1;
                app.quit();
                return;
            }
            std::printf("[record] started -> %s (%d s)\n", qPrintable(outPath), seconds);
            QTimer::singleShot(seconds * 1000, &recorder, [&] { recorder.stop(); });
        }
    });
    QObject::connect(&recorder, &Recorder::finished, &app, [&](const QString &path, qint64 ms, const QString &warn) {
        std::printf("[record] saved %s (%.2f s)%s%s\n", qPrintable(path), ms / 1000.0,
                    warn.isEmpty() ? "" : " — ", qPrintable(warn));
        app.quit();
    });
    QObject::connect(&recorder, &Recorder::failed, &app, [&](const QString &err) {
        std::printf("[record] failed: %s\n", qPrintable(err));
        exitCode = 1;
        app.quit();
    });

    source.start(device);
    if (!record && seconds > 0)
        QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    app.exec();
    source.stop();
    return exitCode;
}
