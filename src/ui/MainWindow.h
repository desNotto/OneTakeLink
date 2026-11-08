// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/AppSettings.h"

#include <QMainWindow>

class QLabel;
class QSplitter;

namespace otl {

class LibraryPage;
class ShootPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ShootPage *shootPage() const { return m_shoot; }
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void toggleFullscreen();
    void exitFullscreen();
    void recoverInterruptedTakes();
    void showAbout();
    QWidget *pane(const QString &caption, QWidget *content, QLabel **captionOut = nullptr);
    void saveLayout();
    void restoreLayout();

    AppSettings m_settings;
    QWidget *m_topBar = nullptr;
    QWidget *m_playerPane = nullptr;
    QWidget *m_monitorPane = nullptr;
    QLabel *m_monitorCaption = nullptr;
    QWidget *m_inspector = nullptr;
    QWidget *m_poolPane = nullptr;
    QSplitter *m_rootSplit = nullptr;   // [work area | inspector]
    QSplitter *m_workSplit = nullptr;   // [viewers / media pool]
    QSplitter *m_viewerSplit = nullptr; // [player | monitor]
    ShootPage *m_shoot = nullptr;
    LibraryPage *m_library = nullptr;
    QLabel *m_sbSource = nullptr;
    QLabel *m_sbStream = nullptr;
    QLabel *m_sbRec = nullptr;
    QLabel *m_sbDisk = nullptr;
    bool m_fullscreen = false;
    bool m_wasMaximized = false;
};

} // namespace otl
