// SPDX-License-Identifier: GPL-3.0-or-later
#include "LibraryPage.h"

#include "PlayerPanel.h"
#include "library/Thumbnailer.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <thread>

namespace otl {

namespace {

enum Column { ColFav, ColName, ColDate, ColDuration, ColSize, ColResolution, ColFps, ColCodec, ColSession, ColCount };
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kSortRole = Qt::UserRole + 2;
constexpr int kProjectRole = Qt::UserRole + 3;
const QSize kThumbSize(112, 63);
constexpr int kThumbRenderWidth = 224; // 2x for high-DPI screens

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

QString fpsText(double fps)
{
    if (fps <= 0)
        return QStringLiteral("—");
    const bool whole = std::abs(fps - std::round(fps)) < 0.05;
    return QString::number(fps, 'f', whole ? 0 : 2);
}

QIcon placeholderIcon()
{
    static QIcon icon = [] {
        QPixmap pm(kThumbSize * 2);
        pm.fill(QColor(0x1A, 0x0F, 0x42));
        return QIcon(pm);
    }();
    return icon;
}

// Every thumbnail gets the same 16:9 frame (vertical takes pillarboxed),
// so names line up in the list.
QIcon thumbIcon(const QImage &img)
{
    if (img.isNull())
        return placeholderIcon();
    QPixmap pm(kThumbSize * 2);
    pm.fill(QColor(0x0D, 0x09, 0x21));
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSize fit = img.size().scaled(pm.size(), Qt::KeepAspectRatio);
    p.drawImage(QRect(QPoint((pm.width() - fit.width()) / 2, (pm.height() - fit.height()) / 2), fit), img);
    p.end();
    return QIcon(pm);
}

} // namespace

LibraryPage::LibraryPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings), m_generation(std::make_shared<std::atomic<int>>(0))
{
    loadFavorites();
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);

    // Actions
    auto *bar = new QHBoxLayout;
    auto button = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        bar->addWidget(b);
        return b;
    };
    auto *play = button(tr("▶ Смотреть"), tr("Воспроизвести во встроенном плеере (Enter)"));
    play->setObjectName(QStringLiteral("primaryButton"));
    auto *reveal = button(tr("Показать в папке"), tr("Открыть папку в Проводнике"));
    auto *fav = button(tr("★ Избранное"), tr("Отметить / снять отметку (F)"));
    auto *rename = button(tr("Переименовать"), tr("F2"));
    auto *del = button(tr("Удалить"), tr("Переместить в Корзину (Delete)"));
    bar->addStretch(1);
    auto *refreshButton = button(tr("Обновить"), tr("Пересканировать папку записей (F5)"));
    v->addLayout(bar);

    // Filters
    auto *filters = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск по имени…"));
    m_search->setClearButtonEnabled(true);
    m_projectFilter = new QComboBox;
    m_projectFilter->setMinimumWidth(180);
    m_favOnly = new QCheckBox(tr("Только избранное"));
    filters->addWidget(m_search, 2);
    filters->addWidget(new QLabel(tr("Проект:")));
    filters->addWidget(m_projectFilter, 1);
    filters->addWidget(m_favOnly);
    v->addLayout(filters);

    // List | player
    m_tree = new QTreeWidget;
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({QStringLiteral("★"), tr("Дубль"), tr("Дата"), tr("Длит."), tr("Размер"),
                             tr("Разрешение"), tr("FPS"), tr("Кодек"), tr("Проект / сессия")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(ColDate, Qt::DescendingOrder);
    m_tree->setAlternatingRowColors(true);
    m_tree->setIconSize(kThumbSize);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(ColFav, 30);
    m_tree->setColumnWidth(ColName, 300);
    m_tree->setColumnWidth(ColDate, 120);
    m_tree->setColumnWidth(ColDuration, 60);
    m_tree->setColumnWidth(ColSize, 80);
    m_tree->setColumnWidth(ColResolution, 90);
    m_tree->setColumnWidth(ColFps, 45);
    m_tree->setColumnWidth(ColCodec, 55);

    m_player = new PlayerPanel;
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_player);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setChildrenCollapsible(false);
    {
        QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
        const QByteArray state = s.value("library/splitter").toByteArray();
        if (!state.isEmpty())
            m_splitter->restoreState(state);
        else
            m_splitter->setSizes({800, 560});
    }
    v->addWidget(m_splitter, 1);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_summary);

    connect(play, &QPushButton::clicked, this, &LibraryPage::playSelected);
    connect(reveal, &QPushButton::clicked, this, &LibraryPage::revealSelected);
    connect(fav, &QPushButton::clicked, this, &LibraryPage::toggleFavorite);
    connect(rename, &QPushButton::clicked, this, &LibraryPage::renameSelected);
    connect(del, &QPushButton::clicked, this, &LibraryPage::deleteSelected);
    connect(refreshButton, &QPushButton::clicked, this, &LibraryPage::refresh);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this] { playSelected(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LibraryPage::onSelectionChanged);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryPage::applyFilters);
    connect(m_projectFilter, &QComboBox::currentIndexChanged, this, &LibraryPage::applyFilters);
    connect(m_favOnly, &QCheckBox::toggled, this, &LibraryPage::applyFilters);
    connect(m_player, &PlayerPanel::openExternally, this, &LibraryPage::openExternally);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
        s.setValue("library/splitter", m_splitter->saveState());
    });

    auto shortcut = [this](const QKeySequence &k, auto fn, QWidget *scope) {
        auto *s = new QShortcut(k, scope);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Delete, [this] { deleteSelected(); }, m_tree);
    shortcut(QKeySequence(Qt::Key_F2), [this] { renameSelected(); }, m_tree);
    shortcut(QKeySequence(Qt::Key_F), [this] { toggleFavorite(); }, m_tree);
    shortcut(QKeySequence(Qt::Key_F5), [this] { refresh(); }, this);
    shortcut(QKeySequence(Qt::Key_Comma), [this] { m_player->step(-1); }, this);
    shortcut(QKeySequence(Qt::Key_Period), [this] { m_player->step(1); }, this);
    shortcut(QKeySequence(Qt::Key_Home), [this] { m_player->toStart(); }, m_player);

    refresh();
}

LibraryPage::~LibraryPage()
{
    ++*m_generation; // pending background work becomes a no-op
}

void LibraryPage::togglePlay()
{
    if (m_player->currentPath().isEmpty())
        playSelected();
    else
        m_player->togglePlay();
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

    // Keep the user's place across rescans (tab switches, new takes).
    const QStringList selected = selectedPaths();
    const QString current = m_tree->currentItem() ? m_tree->currentItem()->data(ColName, kPathRole).toString() : QString();

    const QSignalBlocker blockSelection(m_tree);
    m_tree->setSortingEnabled(false);
    m_tree->clear();

    struct Job {
        QString path;
        qint64 size;
        qint64 mtime;
    };
    std::vector<Job> jobs;
    QStringList projects;
    QSet<QString> present;

    if (QDir(root).exists()) {
        QDirIterator it(root, {QStringLiteral("*.mp4"), QStringLiteral("*.mov")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String(".recording.mp4")))
                continue; // a take in progress (or awaiting recovery)
            const QFileInfo fi(path);
            const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
            present.insert(path);

            const QString relDir = QDir(root).relativeFilePath(fi.absolutePath());
            const QString project = relDir == QLatin1String(".") ? QString() : relDir.section(QLatin1Char('/'), 0, 0);
            if (!project.isEmpty() && !projects.contains(project))
                projects << project;

            auto *item = new SortItem(m_tree);
            item->setData(ColName, kPathRole, path);
            item->setData(ColName, kProjectRole, project);
            const bool fav = m_favorites.contains(path);
            item->setText(ColFav, fav ? QStringLiteral("★") : QString());
            item->setData(ColFav, kSortRole, fav ? 1 : 0);
            item->setText(ColName, fi.completeBaseName());
            item->setToolTip(ColName, QDir::toNativeSeparators(path));
            const QDateTime created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
            item->setText(ColDate, created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            item->setData(ColDate, kSortRole, created.toMSecsSinceEpoch());
            item->setText(ColSize, sizeText(fi.size()));
            item->setData(ColSize, kSortRole, fi.size());
            item->setText(ColSession, relDir == QLatin1String(".") ? QString() : relDir);

            Entry &e = m_cache[path];
            if (e.probed && e.size == fi.size() && e.mtime == mtime) {
                fillItem(item, e);
            } else {
                e = Entry{};
                e.size = fi.size();
                e.mtime = mtime;
                item->setIcon(ColName, placeholderIcon());
                item->setText(ColDuration, QStringLiteral("…"));
                jobs.push_back({path, fi.size(), mtime});
            }
        }
    }
    // Forget deleted files.
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = present.contains(it.key()) ? std::next(it) : m_cache.erase(it);

    m_tree->setSortingEnabled(true);
    projects.sort(Qt::CaseInsensitive);
    rebuildProjectFilter(projects);
    applyFilters();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const QString p = item->data(ColName, kPathRole).toString();
        if (selected.contains(p))
            item->setSelected(true);
        if (p == current)
            m_tree->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
    if (!m_player->currentPath().isEmpty() && !present.contains(m_player->currentPath()))
        m_player->unload();
    updateSummary();

    // Metadata and thumbnails off the UI thread.
    if (jobs.empty())
        return;
    QPointer<LibraryPage> self(this);
    auto generation = m_generation;
    std::thread([self, generation, gen, jobs] {
        for (const Job &j : jobs) {
            if (*generation != gen)
                return;
            MediaInfo info = probeMedia(j.path);
            QImage thumb = info.ok ? takeThumbnail(j.path, kThumbRenderWidth) : QImage();
            QMetaObject::invokeMethod(qApp, [self, generation, gen, j, info, thumb] {
                if (self && *generation == gen)
                    self->onProbed(j.path, j.size, j.mtime, info, thumb);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void LibraryPage::rebuildProjectFilter(const QStringList &projects)
{
    const QString previous = m_projectFilter->currentData().toString();
    const QSignalBlocker block(m_projectFilter);
    m_projectFilter->clear();
    m_projectFilter->addItem(tr("Все проекты"), QString());
    for (const QString &p : projects)
        m_projectFilter->addItem(p, p);
    m_projectFilter->setCurrentIndex(std::max(0, m_projectFilter->findData(previous)));
}

void LibraryPage::applyFilters()
{
    const QString needle = m_search->text().trimmed();
    const QString project = m_projectFilter->currentData().toString();
    const bool favOnly = m_favOnly->isChecked();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        bool show = needle.isEmpty() || item->text(ColName).contains(needle, Qt::CaseInsensitive);
        show = show && (project.isEmpty() || item->data(ColName, kProjectRole).toString() == project);
        show = show && (!favOnly || m_favorites.contains(item->data(ColName, kPathRole).toString()));
        item->setHidden(!show);
    }
    updateSummary();
}

void LibraryPage::updateSummary()
{
    int shown = 0;
    qint64 bytes = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->isHidden())
            continue;
        ++shown;
        bytes += item->data(ColSize, kSortRole).toLongLong();
    }
    const QString root = QDir::toNativeSeparators(m_settings->rootFolder);
    if (m_tree->topLevelItemCount() == 0)
        m_summary->setText(tr("В папке %1 пока нет записей.").arg(root));
    else
        m_summary->setText(tr("Показано %1 из %2 · %3 · %4")
                               .arg(shown)
                               .arg(m_tree->topLevelItemCount())
                               .arg(sizeText(bytes), root));
}

void LibraryPage::fillItem(QTreeWidgetItem *item, const Entry &e)
{
    item->setIcon(ColName, thumbIcon(e.thumb));
    if (!e.info.ok) {
        item->setText(ColDuration, tr("ошибка"));
        item->setToolTip(ColDuration, e.info.error);
        return;
    }
    item->setText(ColDuration, durationText(e.info.durationMs));
    item->setData(ColDuration, kSortRole, e.info.durationMs);
    item->setText(ColResolution, QStringLiteral("%1×%2").arg(e.info.width).arg(e.info.height));
    item->setData(ColResolution, kSortRole, qint64(e.info.width) * e.info.height);
    item->setText(ColFps, fpsText(e.info.fps));
    item->setData(ColFps, kSortRole, e.info.fps);
    item->setText(ColCodec, e.info.codec);
}

void LibraryPage::onProbed(const QString &path, qint64 size, qint64 mtime, const MediaInfo &info, const QImage &thumb)
{
    Entry &e = m_cache[path];
    e.size = size;
    e.mtime = mtime;
    e.probed = true;
    e.info = info;
    e.thumb = thumb;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(ColName, kPathRole).toString() == path) {
            fillItem(item, e);
            break;
        }
    }
    if (m_player->currentPath() == path) {
        QTreeWidgetItem *cur = m_tree->currentItem();
        m_player->load(path, cur ? cur->text(ColName) : QFileInfo(path).completeBaseName(), detailsText(path));
    }
}

QString LibraryPage::detailsText(const QString &path) const
{
    const auto it = m_cache.constFind(path);
    if (it == m_cache.constEnd() || !it->probed || !it->info.ok)
        return QDir::toNativeSeparators(path);
    const MediaInfo &i = it->info;
    return QStringLiteral("%1×%2 · %3 fps · %4 · %5 · %6")
        .arg(i.width)
        .arg(i.height)
        .arg(fpsText(i.fps), i.codec, durationText(i.durationMs), sizeText(it->size));
}

void LibraryPage::onSelectionChanged()
{
    const QStringList paths = selectedPaths();
    if (paths.size() != 1)
        return;
    QTreeWidgetItem *item = m_tree->selectedItems().constFirst();
    m_player->load(paths.first(), item->text(ColName), detailsText(paths.first()));
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

void LibraryPage::releaseFromPlayer(const QStringList &paths)
{
    if (paths.contains(m_player->currentPath()))
        m_player->unload();
}

void LibraryPage::playSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    QTreeWidgetItem *item = m_tree->selectedItems().constFirst();
    m_player->load(paths.first(), item->text(ColName), detailsText(paths.first()));
    m_player->togglePlay();
}

void LibraryPage::openExternally(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void LibraryPage::revealSelected()
{
    const QStringList paths = selectedPaths();
#ifdef Q_OS_WIN
    if (!paths.isEmpty()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(paths.first())});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(paths.isEmpty() ? m_settings->rootFolder
                                                                  : QFileInfo(paths.first()).absolutePath()));
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
    releaseFromPlayer(paths);
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
    selectPath(target); // reopens it in the player
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
    releaseFromPlayer(paths);
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
        QMessageBox::warning(this, tr("Не всё удалено"),
                             tr("Не удалены (файл занят другой программой?): %1").arg(failed.join(QStringLiteral(", "))));
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
    applyFilters();
}

} // namespace otl
