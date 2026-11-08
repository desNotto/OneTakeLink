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
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
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
constexpr int kFavRole = Qt::UserRole + 4;
const QSize kGridIcon(192, 108);
const QSize kTableIcon(80, 45);
constexpr int kThumbRenderWidth = 384; // 2x the grid size, for high-DPI screens

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

// Thumbnail card: every take in the same 16:9 frame (vertical ones
// pillarboxed), with the duration badge and the favourite star on it.
QIcon thumbIcon(const QImage &img, qint64 durationMs, bool fav)
{
    QPixmap pm(kGridIcon * 2);
    pm.fill(QColor(0x1A, 0x0F, 0x42));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!img.isNull()) {
        p.fillRect(pm.rect(), QColor(0x0D, 0x09, 0x21));
        const QSize fit = img.size().scaled(pm.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((pm.width() - fit.width()) / 2, (pm.height() - fit.height()) / 2), fit), img);
    }
    QFont f = QApplication::font();
    f.setPixelSize(22);
    f.setBold(true);
    p.setFont(f);
    if (durationMs > 0) {
        const QString t = durationText(durationMs);
        const int w = QFontMetrics(f).horizontalAdvance(t) + 20;
        const QRect badge(pm.width() - w - 10, pm.height() - 40, w, 30);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x15, 0x0A, 0x35, 210));
        p.drawRoundedRect(badge, 8, 8);
        p.setPen(QColor(0xF5, 0xF7, 0xFC));
        p.drawText(badge, Qt::AlignCenter, t);
    }
    if (fav) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x15, 0x0A, 0x35, 210));
        p.drawEllipse(QRect(10, 10, 40, 40));
        p.setPen(QColor(0xA5, 0x77, 0xFF));
        f.setPixelSize(26);
        p.setFont(f);
        p.drawText(QRect(10, 9, 40, 40), Qt::AlignCenter, QStringLiteral("★"));
    }
    p.end();
    return QIcon(pm);
}

} // namespace

// Filtering (search / project / favourites) and sorting by kSortRole.
class TakeFilterModel : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString needle;
    QString project;
    bool favOnly = false;

    void refilter() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex idx = sourceModel()->index(row, ColName, parent);
        if (!needle.isEmpty() && !idx.data(Qt::DisplayRole).toString().contains(needle, Qt::CaseInsensitive))
            return false;
        if (!project.isEmpty() && idx.data(kProjectRole).toString() != project)
            return false;
        if (favOnly && !idx.data(kFavRole).toBool())
            return false;
        return true;
    }
};

LibraryPage::LibraryPage(AppSettings *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings), m_generation(std::make_shared<std::atomic<int>>(0))
{
    loadFavorites();
    m_player = new PlayerPanel;
    m_pool = buildPool();
    connect(m_player, &PlayerPanel::openExternally, this,
            [](const QString &path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    refresh();
}

LibraryPage::~LibraryPage()
{
    ++*m_generation; // pending background work becomes a no-op
}

QWidget *LibraryPage::buildPool()
{
    auto *pool = new QWidget;
    pool->setObjectName(QStringLiteral("mediaPoolPane"));
    auto *v = new QVBoxLayout(pool);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(8);

    // Header: title, filters, sort, view mode, actions.
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    auto *title = new QLabel(tr("Медиатека"));
    title->setObjectName(QStringLiteral("paneCaption"));
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(260);
    m_projectFilter = new QComboBox;
    m_projectFilter->setMinimumWidth(150);
    m_projectFilter->setToolTip(tr("Проект"));
    m_favOnly = new QCheckBox(tr("★ Избранное"));
    m_sortCombo = new QComboBox;
    m_sortCombo->addItem(tr("Сначала новые"));
    m_sortCombo->addItem(tr("Сначала старые"));
    m_sortCombo->addItem(tr("По имени"));
    m_sortCombo->addItem(tr("По длительности"));
    m_sortCombo->addItem(tr("По размеру"));
    m_sortCombo->setToolTip(tr("Сортировка"));

    m_gridButton = new QPushButton(tr("▦"));
    m_gridButton->setToolTip(tr("Миниатюры"));
    m_listButton = new QPushButton(tr("☰"));
    m_listButton->setToolTip(tr("Таблица"));
    for (QPushButton *b : {m_gridButton, m_listButton}) {
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("viewToggle"));
        b->setFocusPolicy(Qt::NoFocus);
    }
    auto *refreshButton = new QPushButton(tr("Обновить"));
    refreshButton->setToolTip(tr("Пересканировать папку записей (F5)"));
    refreshButton->setFocusPolicy(Qt::NoFocus);

    bar->addWidget(title);
    bar->addSpacing(8);
    bar->addWidget(m_search, 2);
    bar->addWidget(m_projectFilter, 1);
    bar->addWidget(m_favOnly);
    bar->addStretch(1);
    bar->addWidget(m_sortCombo);
    bar->addWidget(m_gridButton);
    bar->addWidget(m_listButton);
    bar->addWidget(refreshButton);
    v->addLayout(bar);

    // Model + views.
    m_model = new QStandardItemModel(0, ColCount, this);
    m_model->setHorizontalHeaderLabels({QStringLiteral("★"), tr("Дубль"), tr("Дата"), tr("Длит."), tr("Размер"),
                                        tr("Разрешение"), tr("FPS"), tr("Кодек"), tr("Проект / сессия")});
    m_proxy = new TakeFilterModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(kSortRole);
    m_proxy->setDynamicSortFilter(true);

    m_grid = new QListView;
    m_grid->setObjectName(QStringLiteral("mediaPool"));
    m_grid->setModel(m_proxy);
    m_grid->setModelColumn(ColName);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(kGridIcon);
    m_grid->setGridSize(QSize(kGridIcon.width() + 22, kGridIcon.height() + 44));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setWordWrap(false);
    m_grid->setTextElideMode(Qt::ElideMiddle);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);

    m_table = new QTreeView;
    m_table->setObjectName(QStringLiteral("mediaTable"));
    m_table->setModel(m_proxy);
    m_selection = m_grid->selectionModel();
    m_table->setSelectionModel(m_selection); // one selection for both views
    m_table->setRootIsDecorated(false);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setIconSize(kTableIcon);
    m_table->setUniformRowHeights(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->header()->setStretchLastSection(true);
    const int widths[] = {30, 320, 120, 60, 80, 90, 45, 55};
    for (int c = 0; c < ColSession; ++c)
        m_table->setColumnWidth(c, widths[c]);

    m_views = new QStackedWidget;
    m_views->addWidget(m_grid);
    m_views->addWidget(m_table);
    v->addWidget(m_views, 1);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_summary);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryPage::applyFilters);
    connect(m_projectFilter, &QComboBox::currentIndexChanged, this, &LibraryPage::applyFilters);
    connect(m_favOnly, &QCheckBox::toggled, this, &LibraryPage::applyFilters);
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &LibraryPage::applySort);
    connect(m_gridButton, &QPushButton::clicked, this, [this] { setGridMode(true); });
    connect(m_listButton, &QPushButton::clicked, this, [this] { setGridMode(false); });
    connect(refreshButton, &QPushButton::clicked, this, &LibraryPage::refresh);
    connect(m_selection, &QItemSelectionModel::selectionChanged, this, &LibraryPage::onSelectionChanged);
    for (QAbstractItemView *view : {static_cast<QAbstractItemView *>(m_grid), static_cast<QAbstractItemView *>(m_table)}) {
        connect(view, &QAbstractItemView::activated, this, [this] { playSelected(); });
        connect(view, &QWidget::customContextMenuRequested, this,
                [this, view](const QPoint &pos) { showContextMenu(view->viewport()->mapToGlobal(pos)); });
    }

    auto shortcut = [this, pool](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, pool);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Delete, [this] { deleteSelected(); });
    shortcut(QKeySequence(Qt::Key_F2), [this] { renameSelected(); });
    shortcut(QKeySequence(Qt::Key_F), [this] { toggleFavorite(); });
    shortcut(QKeySequence(Qt::Key_F5), [this] { refresh(); });

    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    m_sortCombo->setCurrentIndex(std::clamp(s.value("library/sort", 0).toInt(), 0, m_sortCombo->count() - 1));
    setGridMode(s.value("library/grid", true).toBool());
    applySort();
    return pool;
}

void LibraryPage::setGridMode(bool grid)
{
    m_gridButton->setChecked(grid);
    m_listButton->setChecked(!grid);
    m_views->setCurrentWidget(grid ? static_cast<QWidget *>(m_grid) : static_cast<QWidget *>(m_table));
    if (!grid) {
        // The grid selects single cells; show whole rows in the table.
        const QStringList paths = selectedPaths();
        for (int r = 0; r < m_proxy->rowCount(); ++r) {
            const QModelIndex idx = m_proxy->index(r, ColName);
            if (paths.contains(pathAt(idx)))
                m_selection->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    s.setValue("library/grid", grid);
}

void LibraryPage::applySort()
{
    struct S {
        int col;
        Qt::SortOrder order;
    };
    static const S sorts[] = {{ColDate, Qt::DescendingOrder}, {ColDate, Qt::AscendingOrder},
                              {ColName, Qt::AscendingOrder},  {ColDuration, Qt::DescendingOrder},
                              {ColSize, Qt::DescendingOrder}};
    const S s = sorts[std::clamp(m_sortCombo->currentIndex(), 0, 4)];
    m_table->sortByColumn(s.col, s.order); // sorts the shared proxy, keeps the header indicator right
    QSettings settings(AppSettings::iniPath(), QSettings::IniFormat);
    settings.setValue("library/sort", m_sortCombo->currentIndex());
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

QString LibraryPage::pathAt(const QModelIndex &proxyIndex) const
{
    return m_proxy->index(proxyIndex.row(), ColName).data(kPathRole).toString();
}

void LibraryPage::refresh()
{
    const int gen = ++*m_generation;
    const QString root = m_settings->rootFolder;
    const QStringList selected = selectedPaths();

    struct Job {
        QString path;
        qint64 size;
        qint64 mtime;
    };
    std::vector<Job> jobs;
    QStringList projects;
    QSet<QString> present;

    {
        const QSignalBlocker block(m_selection);
        m_model->removeRows(0, m_model->rowCount());
        m_rows.clear();

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

                QList<QStandardItem *> row;
                for (int c = 0; c < ColCount; ++c)
                    row << new QStandardItem;
                QStandardItem *name = row[ColName];
                const bool fav = m_favorites.contains(path);
                name->setText(fi.completeBaseName());
                name->setData(path, kPathRole);
                name->setData(project, kProjectRole);
                name->setData(fav, kFavRole);
                name->setData(fi.completeBaseName().toLower(), kSortRole);
                name->setToolTip(QDir::toNativeSeparators(path));
                row[ColFav]->setText(fav ? QStringLiteral("★") : QString());
                row[ColFav]->setData(fav ? 1 : 0, kSortRole);
                const QDateTime created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
                row[ColDate]->setText(created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                row[ColDate]->setData(created.toMSecsSinceEpoch(), kSortRole);
                row[ColSize]->setText(sizeText(fi.size()));
                row[ColSize]->setData(fi.size(), kSortRole);
                const QString session = relDir == QLatin1String(".") ? QString() : relDir;
                row[ColSession]->setText(session);
                row[ColSession]->setData(session.toLower(), kSortRole);
                m_model->appendRow(row);
                m_rows.insert(path, name);

                Entry &e = m_cache[path];
                if (!(e.probed && e.size == fi.size() && e.mtime == mtime)) {
                    e = Entry{};
                    e.size = fi.size();
                    e.mtime = mtime;
                    jobs.push_back({path, fi.size(), mtime});
                }
                fillRow(name, e);
            }
        }
        for (auto it = m_cache.begin(); it != m_cache.end();)
            it = present.contains(it.key()) ? std::next(it) : m_cache.erase(it);

        // Forget favourites of takes deleted outside the app. Only when the
        // folder is still there: a disconnected drive must not wipe them.
        bool favChanged = false;
        for (auto it = m_favorites.begin(); it != m_favorites.end();) {
            const QFileInfo fi(*it);
            if (!fi.exists() && QDir(fi.absolutePath()).exists()) {
                it = m_favorites.erase(it);
                favChanged = true;
            } else {
                ++it;
            }
        }
        if (favChanged)
            saveFavorites();

        projects.sort(Qt::CaseInsensitive);
        rebuildProjectFilter(projects);
        applyFilters();

        // Keep the user's place across rescans.
        for (const QString &p : selected) {
            QStandardItem *item = m_rows.value(p);
            if (!item)
                continue;
            const QModelIndex idx = m_proxy->mapFromSource(item->index());
            if (idx.isValid())
                m_selection->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    if (!m_player->currentPath().isEmpty() && !present.contains(m_player->currentPath()))
        m_player->unload();
    updateSummary();

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

void LibraryPage::fillRow(QStandardItem *name, const Entry &e)
{
    const int r = name->row();
    auto cell = [&](int c) { return m_model->item(r, c); };
    const bool fav = name->data(kFavRole).toBool();
    name->setIcon(thumbIcon(e.thumb, e.probed && e.info.ok ? e.info.durationMs : 0, fav));
    if (!e.probed) {
        cell(ColDuration)->setText(QStringLiteral("…"));
        return;
    }
    if (!e.info.ok) {
        cell(ColDuration)->setText(tr("ошибка"));
        cell(ColDuration)->setToolTip(e.info.error);
        return;
    }
    cell(ColDuration)->setText(durationText(e.info.durationMs));
    cell(ColDuration)->setData(e.info.durationMs, kSortRole);
    cell(ColResolution)->setText(QStringLiteral("%1×%2").arg(e.info.width).arg(e.info.height));
    cell(ColResolution)->setData(qint64(e.info.width) * e.info.height, kSortRole);
    cell(ColFps)->setText(fpsText(e.info.fps));
    cell(ColFps)->setData(e.info.fps, kSortRole);
    cell(ColCodec)->setText(e.info.codec);
    cell(ColCodec)->setData(e.info.codec, kSortRole);
}

void LibraryPage::onProbed(const QString &path, qint64 size, qint64 mtime, const MediaInfo &info, const QImage &thumb)
{
    Entry &e = m_cache[path];
    e.size = size;
    e.mtime = mtime;
    e.probed = true;
    e.info = info;
    e.thumb = thumb;
    if (QStandardItem *item = m_rows.value(path))
        fillRow(item, e);
    if (m_player->currentPath() == path)
        m_player->load(path, QFileInfo(path).completeBaseName(), detailsText(path));
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
    m_proxy->needle = m_search->text().trimmed();
    m_proxy->project = m_projectFilter->currentData().toString();
    m_proxy->favOnly = m_favOnly->isChecked();
    m_proxy->refilter();
    updateSummary();
}

void LibraryPage::updateSummary()
{
    qint64 bytes = 0;
    for (int r = 0; r < m_proxy->rowCount(); ++r)
        bytes += m_proxy->index(r, ColSize).data(kSortRole).toLongLong();
    const QString root = QDir::toNativeSeparators(m_settings->rootFolder);
    if (m_model->rowCount() == 0)
        m_summary->setText(tr("В папке %1 пока нет записей.").arg(root));
    else
        m_summary->setText(tr("Показано %1 из %2 · %3 · %4")
                               .arg(m_proxy->rowCount())
                               .arg(m_model->rowCount())
                               .arg(sizeText(bytes), root));
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
    if (paths.size() == 1)
        m_player->load(paths.first(), QFileInfo(paths.first()).completeBaseName(), detailsText(paths.first()));
}

void LibraryPage::selectPath(const QString &path)
{
    QStandardItem *item = m_rows.value(path);
    if (!item)
        return;
    const QModelIndex idx = m_proxy->mapFromSource(item->index());
    if (!idx.isValid())
        return;
    m_selection->clearSelection();
    m_selection->setCurrentIndex(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    m_grid->scrollTo(idx);
    m_table->scrollTo(idx);
}

QStringList LibraryPage::selectedPaths() const
{
    QStringList out;
    if (!m_selection)
        return out;
    for (const QModelIndex &idx : m_selection->selectedIndexes()) {
        if (idx.column() != ColName)
            continue;
        out << idx.data(kPathRole).toString();
    }
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
    m_player->load(paths.first(), QFileInfo(paths.first()).completeBaseName(), detailsText(paths.first()));
    m_player->togglePlay();
}

void LibraryPage::showContextMenu(const QPoint &globalPos)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    QMenu menu;
    menu.addAction(tr("Смотреть"), this, &LibraryPage::playSelected)->setEnabled(paths.size() == 1);
    menu.addAction(tr("Открыть во внешнем плеере"), this, [paths] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
    });
    menu.addAction(tr("Показать в папке"), this, &LibraryPage::revealSelected);
    menu.addSeparator();
    const bool allFav = std::all_of(paths.begin(), paths.end(), [this](const QString &p) { return m_favorites.contains(p); });
    menu.addAction(allFav ? tr("Убрать из избранного\tF") : tr("В избранное\tF"), this, &LibraryPage::toggleFavorite);
    menu.addAction(tr("Переименовать…\tF2"), this, &LibraryPage::renameSelected)->setEnabled(paths.size() == 1);
    menu.addSeparator();
    menu.addAction(paths.size() == 1 ? tr("Удалить\tDel") : tr("Удалить (%1)\tDel").arg(paths.size()), this,
                   &LibraryPage::deleteSelected);
    menu.exec(globalPos);
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
    const QString name = QInputDialog::getText(window(), tr("Переименовать дубль"), tr("Новое имя:"),
                                               QLineEdit::Normal, fi.completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == fi.completeBaseName())
        return;
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*])"));
    if (name.contains(bad)) {
        QMessageBox::warning(window(), tr("Недопустимое имя"), tr("Имя не может содержать символы < > : \" / \\ | ? *"));
        return;
    }
    const QString target = fi.dir().filePath(name + QLatin1Char('.') + fi.suffix());
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(window(), tr("Имя занято"), tr("Файл с таким именем уже есть."));
        return;
    }
    releaseFromPlayer(paths);
    if (!QFile::rename(fi.absoluteFilePath(), target)) {
        QMessageBox::warning(window(), tr("Не удалось переименовать"),
                             tr("Файл открыт в другой программе или нет прав на запись."));
        return;
    }
    removeThumbnails(fi.absoluteFilePath());
    m_cache.remove(fi.absoluteFilePath());
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
    if (QMessageBox::question(window(), tr("Удаление"), question + QStringLiteral("\n\n") +
                                  tr("Файлы будут перемещены в Корзину.")) != QMessageBox::Yes)
        return;
    releaseFromPlayer(paths);
    QStringList failed;
    for (const QString &p : paths) {
        if (!QFile::moveToTrash(p)) {
            // No Recycle Bin on this drive (e.g. some USB/network disks).
            const auto answer = QMessageBox::question(
                window(), tr("Корзина недоступна"),
                tr("Не удалось переместить «%1» в Корзину. Удалить безвозвратно?").arg(QFileInfo(p).fileName()));
            if (answer != QMessageBox::Yes || !QFile::remove(p)) {
                failed << QFileInfo(p).fileName();
                continue;
            }
        }
        m_favorites.remove(p);
        removeThumbnails(p);
        m_cache.remove(p);
    }
    saveFavorites();
    if (!failed.isEmpty())
        QMessageBox::warning(window(), tr("Не всё удалено"),
                             tr("Не удалены (файл занят другой программой?): %1").arg(failed.join(QStringLiteral(", "))));
    refresh();
}

void LibraryPage::toggleFavorite()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    const bool allFav = std::all_of(paths.begin(), paths.end(), [this](const QString &p) { return m_favorites.contains(p); });
    for (const QString &p : paths) {
        if (allFav)
            m_favorites.remove(p);
        else
            m_favorites.insert(p);
        if (QStandardItem *name = m_rows.value(p)) {
            const bool on = !allFav;
            name->setData(on, kFavRole);
            m_model->item(name->row(), ColFav)->setText(on ? QStringLiteral("★") : QString());
            m_model->item(name->row(), ColFav)->setData(on ? 1 : 0, kSortRole);
            fillRow(name, m_cache.value(p));
        }
    }
    saveFavorites();
    applyFilters();
}

} // namespace otl
