// SPDX-License-Identifier: GPL-3.0-or-later
// "Библиотека": every take under the recordings folder, with metadata,
// favourites, rename, delete (to the Recycle Bin) and open in Explorer.
// (First version: list view. Thumbnails and the built-in player follow.)
#pragma once

#include "core/AppSettings.h"
#include "library/MediaProbe.h"

#include <QSet>
#include <QWidget>

#include <atomic>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace otl {

class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(AppSettings *settings, QWidget *parent = nullptr);
    ~LibraryPage() override;

public slots:
    void refresh();
    void selectPath(const QString &path);

private:
    void onProbed(const QString &path, const MediaInfo &info);
    QStringList selectedPaths() const;
    void openSelected();
    void revealSelected();
    void renameSelected();
    void deleteSelected();
    void toggleFavorite();
    void loadFavorites();
    void saveFavorites();

    AppSettings *m_settings;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QSet<QString> m_favorites;
    std::shared_ptr<std::atomic<int>> m_generation;
};

} // namespace otl
