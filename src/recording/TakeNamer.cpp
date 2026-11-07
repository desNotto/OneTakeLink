// SPDX-License-Identifier: GPL-3.0-or-later
#include "TakeNamer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace otl {

QString TakeNamer::sanitize(QString name)
{
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    name.replace(bad, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')))
        name.chop(1);
    return name.isEmpty() ? QStringLiteral("Untitled") : name;
}

QString TakeNamer::projectDir() const
{
    return QDir(root).filePath(sanitize(project));
}

QString TakeNamer::sessionDir(const QDate &date) const
{
    return QDir(projectDir()).filePath(QStringLiteral("Session_") + date.toString(Qt::ISODate));
}

QString TakeNamer::exportsDir() const
{
    return QDir(projectDir()).filePath(QStringLiteral("Exports"));
}

QString TakeNamer::nextTakePath() const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDir dir(sessionDir(now.date()));
    const QString pat = pattern.isEmpty() ? defaultPattern() : pattern;
    for (int take = 1; take < 100000; ++take) {
        QString name = pat;
        name.replace(QLatin1String("{date}"), now.date().toString(Qt::ISODate));
        name.replace(QLatin1String("{time}"), now.time().toString(QStringLiteral("HH-mm-ss")));
        name.replace(QLatin1String("{project}"), project);
        name.replace(QLatin1String("{take}"), QStringLiteral("%1").arg(take, 3, 10, QLatin1Char('0')));
        const QString path = dir.filePath(sanitize(name) + QStringLiteral(".mp4"));
        const QString temp = dir.filePath(sanitize(name) + QStringLiteral(".recording.mp4"));
        if (!QFileInfo::exists(path) && !QFileInfo::exists(temp))
            return path;
        if (!pat.contains(QLatin1String("{take}"))) {
            // No take number in the pattern: append one.
            return dir.filePath(sanitize(name) + QStringLiteral("_%1.mp4").arg(take + 1, 3, 10, QLatin1Char('0')));
        }
    }
    return dir.filePath(QStringLiteral("Take.mp4"));
}

} // namespace otl
