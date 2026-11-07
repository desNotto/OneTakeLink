// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/AppSettings.h"

#include <QMainWindow>

class QLabel;
class QTabWidget;

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

    AppSettings m_settings;
    QTabWidget *m_tabs = nullptr;
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
