// SPDX-License-Identifier: GPL-3.0-or-later
// Where takes go and how they are named:
//   <root>/<Project>/Session_<yyyy-MM-dd>/<template>.mp4
// Template placeholders: {date} {time} {project} {take} (3-digit number).
#pragma once

#include <QDate>
#include <QString>

namespace otl {

struct TakeNamer {
    QString root;
    QString project = QStringLiteral("MyProject");
    QString pattern = QStringLiteral("{date}_{project}_Take_{take}");

    static QString defaultPattern() { return QStringLiteral("{date}_{project}_Take_{take}"); }
    static QString sanitize(QString name);

    QString projectDir() const;
    QString sessionDir(const QDate &date = QDate::currentDate()) const;
    QString exportsDir() const;
    // Next free take path in today's session.
    QString nextTakePath() const;
};

} // namespace otl
