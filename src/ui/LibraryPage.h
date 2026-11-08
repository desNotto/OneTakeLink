// SPDX-License-Identifier: GPL-3.0-or-later
// "Библиотека": every take under the recordings folder — thumbnails,
// metadata, filters (search / project / favourites), the built-in player,
// rename, delete to the Recycle Bin, open in Explorer.
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
class QLabel;
class QLineEdit;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;

namespace otl {

class PlayerPanel;

class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(AppSettings *settings, QWidget *parent = nullptr);
    ~LibraryPage() override;

    void togglePlay(); // Space on this tab

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

    void onProbed(const QString &path, qint64 size, qint64 mtime, const MediaInfo &info, const QImage &thumb);
    void fillItem(QTreeWidgetItem *item, const Entry &e);
    void applyFilters();
    void rebuildProjectFilter(const QStringList &projects);
    void onSelectionChanged();
    QString detailsText(const QString &path) const;
    QStringList selectedPaths() const;
    void releaseFromPlayer(const QStringList &paths);
    void playSelected();
    void openExternally(const QString &path);
    void revealSelected();
    void renameSelected();
    void deleteSelected();
    void toggleFavorite();
    void loadFavorites();
    void saveFavorites();
    void updateSummary();

    AppSettings *m_settings;
    QTreeWidget *m_tree = nullptr;
    PlayerPanel *m_player = nullptr;
    QSplitter *m_splitter = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_projectFilter = nullptr;
    QCheckBox *m_favOnly = nullptr;
    QLabel *m_summary = nullptr;
    QSet<QString> m_favorites;
    QHash<QString, Entry> m_cache;
    std::shared_ptr<std::atomic<int>> m_generation;
};

} // namespace otl
