// SPDX-License-Identifier: GPL-3.0-or-later
// Media pool + take player (Library Layer UI).
//
// One QStandardItemModel of takes behind a filter/sort proxy, shown either
// as a thumbnail grid or as a detail table; both views share one selection.
// The player and the pool are separate widgets (playerWidget(),
// poolWidget()) that MainWindow places in its single-screen layout.
#pragma once

#include "core/AppSettings.h"
#include "library/MediaProbe.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QWidget>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QItemSelectionModel;
class QLabel;
class QLineEdit;
class QListView;
class QModelIndex;
class QPushButton;
class QStackedWidget;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace otl {

class PlayerPanel;
class TakeFilterModel;

class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(AppSettings *settings, QWidget *parent = nullptr);
    ~LibraryPage() override;

    PlayerPanel *playerWidget() const { return m_player; }
    QWidget *poolWidget() const { return m_pool; }

    void togglePlay();

public slots:
    void refresh();
    void selectPath(const QString &path);

private:
    struct Entry {
        qint64 size = -1;
        qint64 mtime = -1;
        bool probed = false;
        MediaInfo info;
        QImage thumb;
    };

    QWidget *buildPool();
    void onProbed(const QString &path, qint64 size, qint64 mtime, const MediaInfo &info, const QImage &thumb);
    void fillRow(QStandardItem *nameItem, const Entry &e);
    void applyFilters();
    void applySort();
    void setGridMode(bool grid);
    void rebuildProjectFilter(const QStringList &projects);
    void onSelectionChanged();
    QString detailsText(const QString &path) const;
    QStringList selectedPaths() const;
    QString pathAt(const QModelIndex &proxyIndex) const;
    void releaseFromPlayer(const QStringList &paths);
    void playSelected();
    void showContextMenu(const QPoint &globalPos);
    void revealSelected();
    void renameSelected();
    void deleteSelected();
    void toggleFavorite();
    void loadFavorites();
    void saveFavorites();
    void updateSummary();

    AppSettings *m_settings;
    PlayerPanel *m_player = nullptr;
    QWidget *m_pool = nullptr;

    QStandardItemModel *m_model = nullptr;
    TakeFilterModel *m_proxy = nullptr;
    QItemSelectionModel *m_selection = nullptr;
    QStackedWidget *m_views = nullptr;
    QListView *m_grid = nullptr;
    QTreeView *m_table = nullptr;
    QPushButton *m_gridButton = nullptr;
    QPushButton *m_listButton = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_projectFilter = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_favOnly = nullptr;
    QLabel *m_summary = nullptr;

    QSet<QString> m_favorites;
    QHash<QString, Entry> m_cache;
    QHash<QString, QStandardItem *> m_rows; // path -> name item
    std::shared_ptr<std::atomic<int>> m_generation;
};

} // namespace otl
