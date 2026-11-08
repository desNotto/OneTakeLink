// SPDX-License-Identifier: GPL-3.0-or-later
#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

extern "C" {
#include <libavutil/log.h>
}

namespace otl {

namespace {
QFile *g_file = nullptr;
QMutex g_mutex;
// One session's log never grows past this (a flood of decoder warnings
// must not fill the disk); only this and the previous session are kept.
constexpr qint64 kMaxLogBytes = 10LL * 1024 * 1024;

void handler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    default: break;
    }
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1String(level), msg);
    QMutexLocker lock(&g_mutex);
    if (g_file) {
        if (g_file->size() < kMaxLogBytes) {
            g_file->write(line.toUtf8());
        } else {
            g_file->write("... log size limit reached, further messages are not saved\n");
            g_file->close();
            delete g_file;
            g_file = nullptr;
        }
        if (g_file)
            g_file->flush();
    }
    fputs(line.toLocal8Bit().constData(), stderr);
}

void ffmpegLog(void *, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_WARNING)
        return;
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    QString s = QString::fromUtf8(buf).trimmed();
    if (!s.isEmpty())
        qWarning("[ffmpeg] %s", qPrintable(s));
}
} // namespace

QString logFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(dir).filePath(QStringLiteral("logs/onetakelink.log"));
}

void installLogging()
{
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Keep one previous session's log.
    if (QFileInfo(path).size() > 0) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
    g_file = new QFile(path);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
    }
    qInstallMessageHandler(handler);
    av_log_set_callback(ffmpegLog);
}

} // namespace otl
