// SPDX-License-Identifier: GPL-3.0-or-later
#include "Adb.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpSocket>

#include <atomic>
#include <mutex>

namespace otl::android {

namespace {

std::once_flag g_probeOnce;
std::atomic<bool> g_weStartedServer{false};

bool adbServerRunning()
{
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, 5037);
    const bool up = s.waitForConnected(300);
    s.abort();
    return up;
}

QString bundled(const QString &name)
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {app.filePath(QStringLiteral("android/") + name), app.filePath(name)}) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}
} // namespace

QString Adb::executable()
{
    const QString env = qEnvironmentVariable("OTL_ADB");
    if (!env.isEmpty())
        return env;
#ifdef Q_OS_WIN
    QString path = bundled(QStringLiteral("adb.exe"));
#else
    QString path = bundled(QStringLiteral("adb"));
#endif
    if (path.isEmpty())
        path = QStandardPaths::findExecutable(QStringLiteral("adb"));
    return path;
}

QString Adb::serverJar()
{
    const QString env = qEnvironmentVariable("OTL_SCRCPY_SERVER");
    if (!env.isEmpty())
        return env;
    return bundled(QStringLiteral("scrcpy-server"));
}

AdbResult Adb::run(const QStringList &args, int timeoutMs)
{
    AdbResult r;
    const QString exe = executable();
    if (exe.isEmpty())
        return r;
    std::call_once(g_probeOnce, [] { g_weStartedServer = !adbServerRunning(); });
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, args);
    if (!p.waitForStarted(5000))
        return r;
    r.started = true;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        r.out = QString::fromUtf8(p.readAll());
        return r;
    }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAll());
    return r;
}

void Adb::shutdownIfWeStartedIt()
{
    if (!g_weStartedServer.exchange(false))
        return;
    const QString exe = executable();
    if (exe.isEmpty())
        return;
    QProcess::execute(exe, {QStringLiteral("kill-server")});
}

QList<AdbDevice> Adb::devices()
{
    QList<AdbDevice> list;
    const AdbResult r = run({QStringLiteral("devices"), QStringLiteral("-l")}, 8000);
    if (!r.started)
        return list;
    // "SERIAL  device product:x model:Pixel_8 device:y transport_id:1"
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList lines = r.out.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("List of devices")) || line.startsWith(QLatin1Char('*')))
            continue;
        const QStringList parts = line.split(ws, Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        AdbDevice d;
        d.serial = parts[0];
        // "no permissions (...)" has spaces in the state.
        int i = 1;
        QStringList state;
        for (; i < parts.size() && !parts[i].contains(QLatin1Char(':')); ++i)
            state << parts[i];
        d.state = state.join(QLatin1Char(' '));
        for (; i < parts.size(); ++i) {
            if (parts[i].startsWith(QLatin1String("model:")))
                d.model = parts[i].mid(6).replace(QLatin1Char('_'), QLatin1Char(' '));
        }
        list << d;
    }
    return list;
}

} // namespace otl::android
