// SPDX-License-Identifier: GPL-3.0-or-later
#include "LibraryPage.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <thread>

namespace otl {

namespace {

enum Column { ColFav, ColName, ColDate, ColDuration, ColSize, ColResolution, ColFps, ColCodec, ColFolder, ColCount };
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kSortRole = Qt::UserRole + 2;

class SortItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(col, kSortRole), b = other.data(col, kSortRole);
        if (a.isValid() && b.isValid())
            return QVariant::compare(a, b) == QPartialOrdering::Less;
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};

QString durationText(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString sizeText(qint64 b)
{
    if (b >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 ГБ").arg(double(b) / (1024.0 * 1024 * 1024), 0, 'f', 2);
    return QStringLiteral("%1 МБ").arg(double(b) / (1024.0 * 1024), 0, 'f', 1);
}

} // namespace

LibraryPage::LibraryPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings), m_generation(std::make_shared<std::atomic<int>>(0))
{
    loadFavorites();
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);

    auto *bar = new QHBoxLayout;
    auto button = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        bar->addWidget(b);
        return b;
    };
    auto *open = button(tr("Воспроизвести"), tr("Открыть во внешнем проигрывателе (Enter)"));
    auto *reveal = button(tr("Показать в папке"), tr("Открыть папку в Проводнике"));
    auto *fav = button(tr("★ Избранное"), tr("Отметить / снять отметку (F)"));
    auto *rename = button(tr("Переименовать"), tr("F2"));
    auto *del = button(tr("Удалить"), tr("Переместить в Корзину (Delete)"));
    bar->addStretch(1);
    auto *refreshButton = button(tr("Обновить"), tr("Пересканировать папку записей (F5)"));
    v->addLayout(bar);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({QStringLiteral("★"), tr("Имя"), tr("Дата"), tr("Длительность"), tr("Размер"),
                             tr("Разрешение"), tr("FPS"), tr("Кодек"), tr("Папка")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColDate, Qt::DescendingOrder);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setColumnWidth(ColFav, 32);
    v->addWidget(m_tree, 1);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_summary);

    connect(open, &QPushButton::clicked, this, &LibraryPage::openSelected);
    connect(reveal, &QPushButton::clicked, this, &LibraryPage::revealSelected);
    connect(fav, &QPushButton::clicked, this, &LibraryPage::toggleFavorite);
    connect(rename, &QPushButton::clicked, this, &LibraryPage::renameSelected);
    connect(del, &QPushButton::clicked, this, &LibraryPage::deleteSelected);
    connect(refreshButton, &QPushButton::clicked, this, &LibraryPage::refresh);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this] { openSelected(); });

    auto shortcut = [this](const QKeySequence &k, void (LibraryPage::*fn)()) {
        auto *s = new QShortcut(k, m_tree);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Delete, &LibraryPage::deleteSelected);
    shortcut(QKeySequence(Qt::Key_F2), &LibraryPage::renameSelected);
    shortcut(QKeySequence(Qt::Key_F), &LibraryPage::toggleFavorite);
    shortcut(QKeySequence(Qt::Key_F5), &LibraryPage::refresh);

    refresh();
}

LibraryPage::~LibraryPage()
{
    ++*m_generation; // pending probes become no-ops
}

void LibraryPage::loadFavorites()
{
    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    const QStringList list = s.value("library/favorites").toStringList();
    m_favorites = QSet<QString>(list.begin(), list.end());
}

void LibraryPage::saveFavorites()
{
    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    s.setValue("library/favorites", QStringList(m_favorites.begin(), m_favorites.end()));
}

void LibraryPage::refresh()
{
    const int gen = ++*m_generation;
    const QString root = m_settings->rootFolder;
    m_tree->setSortingEnabled(false);
    m_tree->clear();

    QStringList paths;
    qint64 totalBytes = 0;
    if (QDir(root).exists()) {
        QDirIterator it(root, {QStringLiteral("*.mp4"), QStringLiteral("*.mov")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String(".recording.mp4")))
                continue; // a take in progress (or awaiting recovery)
            const QFileInfo fi(path);
            auto *item = new SortItem(m_tree);
            item->setData(ColName, kPathRole, path);
            item->setText(ColFav, m_favorites.contains(path) ? QStringLiteral("★") : QString());
            item->setData(ColFav, kSortRole, m_favorites.contains(path) ? 1 : 0);
            item->setText(ColName, fi.completeBaseName());
            item->setToolTip(ColName, QDir::toNativeSeparators(path));
            const QDateTime created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
            item->setText(ColDate, created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            item->setData(ColDate, kSortRole, created.toMSecsSinceEpoch());
            item->setText(ColSize, sizeText(fi.size()));
            item->setData(ColSize, kSortRole, fi.size());
            item->setText(ColFolder, QDir(root).relativeFilePath(fi.absolutePath()));
            item->setText(ColDuration, QStringLiteral("…"));
            totalBytes += fi.size();
            paths << path;
        }
    }
    m_tree->setSortingEnabled(true);
    m_summary->setText(paths.isEmpty()
                           ? tr("В папке %1 пока нет записей.").arg(QDir::toNativeSeparators(root))
                           : tr("%1 файлов · %2 · %3").arg(paths.size()).arg(sizeText(totalBytes), QDir::toNativeSeparators(root)));

    // Metadata off the UI thread.
    QPointer<LibraryPage> self(this);
    auto generation = m_generation;
    std::thread([self, generation, gen, paths] {
        for (const QString &p : paths) {
            if (*generation != gen)
                return;
            MediaInfo info = probeMedia(p);
            QMetaObject::invokeMethod(qApp, [self, generation, gen, p, info] {
                if (self && *generation == gen)
                    self->onProbed(p, info);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void LibraryPage::onProbed(const QString &path, const MediaInfo &info)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(ColName, kPathRole).toString() != path)
            continue;
        if (!info.ok) {
            item->setText(ColDuration, tr("ошибка"));
            item->setToolTip(ColDuration, info.error);
            return;
        }
        item->setText(ColDuration, durationText(info.durationMs));
        item->setData(ColDuration, kSortRole, info.durationMs);
        item->setText(ColResolution, QStringLiteral("%1×%2").arg(info.width).arg(info.height));
        item->setData(ColResolution, kSortRole, qint64(info.width) * info.height);
        const bool whole = std::abs(info.fps - std::round(info.fps)) < 0.05;
        item->setText(ColFps, info.fps > 0 ? QString::number(info.fps, 'f', whole ? 0 : 2) : QStringLiteral("—"));
        item->setData(ColFps, kSortRole, info.fps);
        item->setText(ColCodec, info.codec);
        return;
    }
}

void LibraryPage::selectPath(const QString &path)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(ColName, kPathRole).toString() == path) {
            m_tree->clearSelection();
            item->setSelected(true);
            m_tree->scrollToItem(item);
            m_tree->setCurrentItem(item);
            return;
        }
    }
}

QStringList LibraryPage::selectedPaths() const
{
    QStringList out;
    for (QTreeWidgetItem *item : m_tree->selectedItems())
        out << item->data(ColName, kPathRole).toString();
    return out;
}

void LibraryPage::openSelected()
{
    const QStringList paths = selectedPaths();
    if (!paths.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
}

void LibraryPage::revealSelected()
{
    const QStringList paths = selectedPaths();
    const QString path = paths.isEmpty() ? m_settings->rootFolder : paths.first();
#ifdef Q_OS_WIN
    if (!paths.isEmpty()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(paths.isEmpty() ? path : QFileInfo(path).absolutePath()));
}

void LibraryPage::renameSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.size() != 1)
        return;
    const QFileInfo fi(paths.first());
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Переименовать дубль"), tr("Новое имя:"),
                                               QLineEdit::Normal, fi.completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == fi.completeBaseName())
        return;
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*])"));
    if (name.contains(bad)) {
        QMessageBox::warning(this, tr("Недопустимое имя"), tr("Имя не может содержать символы < > : \" / \\ | ? *"));
        return;
    }
    const QString target = fi.dir().filePath(name + QLatin1Char('.') + fi.suffix());
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Имя занято"), tr("Файл с таким именем уже есть."));
        return;
    }
    if (!QFile::rename(fi.absoluteFilePath(), target)) {
        QMessageBox::warning(this, tr("Не удалось переименовать"),
                             tr("Файл открыт в другой программе или нет прав на запись."));
        return;
    }
    if (m_favorites.remove(fi.absoluteFilePath())) {
        m_favorites.insert(target);
        saveFavorites();
    }
    refresh();
    selectPath(target);
}

void LibraryPage::deleteSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const QString question = paths.size() == 1
        ? tr("Удалить «%1»?").arg(QFileInfo(paths.first()).fileName())
        : tr("Удалить выбранные дубли (%1)?").arg(paths.size());
    if (QMessageBox::question(this, tr("Удаление"), question + QStringLiteral("\n\n") +
                                  tr("Файлы будут перемещены в Корзину.")) != QMessageBox::Yes)
        return;
    QStringList failed;
    for (const QString &p : paths) {
        if (!QFile::moveToTrash(p)) {
            // No Recycle Bin on this drive (e.g. some USB/network disks).
            const auto answer = QMessageBox::question(
                this, tr("Корзина недоступна"),
                tr("Не удалось переместить «%1» в Корзину. Удалить безвозвратно?").arg(QFileInfo(p).fileName()));
            if (answer != QMessageBox::Yes || !QFile::remove(p)) {
                failed << QFileInfo(p).fileName();
                continue;
            }
        }
        m_favorites.remove(p);
    }
    saveFavorites();
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Не всё удалено"), tr("Не удалены: %1").arg(failed.join(QStringLiteral(", "))));
    refresh();
}

void LibraryPage::toggleFavorite()
{
    for (QTreeWidgetItem *item : m_tree->selectedItems()) {
        const QString p = item->data(ColName, kPathRole).toString();
        const bool on = !m_favorites.contains(p);
        if (on)
            m_favorites.insert(p);
        else
            m_favorites.remove(p);
        item->setText(ColFav, on ? QStringLiteral("★") : QString());
        item->setData(ColFav, kSortRole, on ? 1 : 0);
    }
    saveFavorites();
}

} // namespace otl
