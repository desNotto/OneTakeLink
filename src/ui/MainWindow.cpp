// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "LibraryPage.h"
#include "ShootPage.h"
#include "core/Version.h"
#include "recording/Recorder.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>

#include <thread>

namespace otl {

MainWindow::MainWindow() : m_settings(AppSettings::load())
{
    setWindowTitle(QStringLiteral("OneTakeLink"));
    resize(1400, 860);

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_shoot = new ShootPage(&m_settings);
    m_library = new LibraryPage(&m_settings);
    m_tabs->addTab(m_shoot, tr("Съёмка"));
    m_tabs->addTab(m_library, tr("Библиотека"));
    auto *about = new QPushButton(tr("О программе"));
    about->setFlat(true);
    m_tabs->setCornerWidget(about, Qt::TopRightCorner);

    auto *brand = new QWidget;
    brand->setObjectName(QStringLiteral("brand"));
    auto *brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(14, 4, 18, 4);
    brandLayout->setSpacing(8);
    auto *logo = new QLabel;
    logo->setPixmap(QApplication::windowIcon().pixmap(QSize(24, 24), devicePixelRatio()));
    auto *name = new QLabel(QStringLiteral("OneTakeLink"));
    name->setObjectName(QStringLiteral("brandName"));
    brandLayout->addWidget(logo);
    brandLayout->addWidget(name);
    m_tabs->setCornerWidget(brand, Qt::TopLeftCorner);
    setCentralWidget(m_tabs);

    m_sbSource = new QLabel;
    m_sbStream = new QLabel;
    m_sbRec = new QLabel;
    m_sbDisk = new QLabel;
    statusBar()->addWidget(m_sbSource, 1);
    statusBar()->addPermanentWidget(m_sbStream);
    statusBar()->addPermanentWidget(m_sbRec);
    statusBar()->addPermanentWidget(m_sbDisk);

    connect(about, &QPushButton::clicked, this, &MainWindow::showAbout);
    connect(m_shoot, &ShootPage::statusSummary, this,
            [this](const QString &src, const QString &stream, const QString &rec, const QString &disk) {
                m_sbSource->setText(src);
                m_sbStream->setText(stream);
                m_sbRec->setText(rec);
                m_sbRec->setStyleSheet(rec.isEmpty() ? QString() : QStringLiteral("color:#ff5c6c;font-weight:600"));
                m_sbDisk->setText(disk);
            });
    connect(m_shoot, &ShootPage::takeSaved, this, [this](const QString &path) {
        m_library->refresh();
        m_library->selectPath(path);
    });
    connect(m_shoot, &ShootPage::requestFullscreen, this, &MainWindow::toggleFullscreen);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (m_tabs->widget(i) == m_library)
            m_library->refresh();
    });

    // Window hotkeys: active only while the main window itself is active,
    // so Space in a dialog never toggles recording. Text fields consume
    // Space first, so typing a project name doesn't either.
    auto hotkey = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    hotkey(QKeySequence(Qt::Key_Space), [this] {
        if (m_tabs->currentWidget() == m_shoot || m_fullscreen)
            m_shoot->toggleRecording();
    });
    hotkey(QKeySequence(Qt::Key_F11), [this] { toggleFullscreen(); });
    hotkey(QKeySequence(Qt::Key_Escape), [this] { exitFullscreen(); });

    recoverInterruptedTakes();
}

MainWindow::~MainWindow()
{
    m_shoot->shutdown();
}

void MainWindow::toggleFullscreen()
{
    if (m_fullscreen) {
        exitFullscreen();
        return;
    }
    m_tabs->setCurrentWidget(m_shoot);
    m_wasMaximized = isMaximized();
    m_fullscreen = true;
    m_tabs->tabBar()->hide();
    m_tabs->cornerWidget(Qt::TopRightCorner)->hide();
    m_tabs->cornerWidget(Qt::TopLeftCorner)->hide();
    statusBar()->hide();
    m_shoot->setControlsVisible(false);
    showFullScreen();
}

void MainWindow::exitFullscreen()
{
    if (!m_fullscreen)
        return;
    m_fullscreen = false;
    m_tabs->tabBar()->show();
    m_tabs->cornerWidget(Qt::TopRightCorner)->show();
    m_tabs->cornerWidget(Qt::TopLeftCorner)->show();
    statusBar()->show();
    m_shoot->setControlsVisible(true);
    if (m_wasMaximized)
        showMaximized();
    else
        showNormal();
}

void MainWindow::recoverInterruptedTakes()
{
    const QString root = m_settings.rootFolder;
    QPointer<MainWindow> self(this);
    std::thread([self, root] {
        const QStringList recovered = Recorder::recoverInterrupted(root);
        if (recovered.isEmpty())
            return;
        QMetaObject::invokeMethod(qApp, [self, recovered] {
            if (!self)
                return;
            QStringList names;
            for (const QString &p : recovered)
                names << QDir::toNativeSeparators(p);
            QMessageBox::information(self, tr("Восстановлены записи"),
                                     tr("Найдены дубли, прерванные аварийным завершением программы или "
                                        "отключением питания. Они восстановлены:\n\n%1").arg(names.join('\n')));
            self->m_library->refresh();
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_shoot->isRecording()) {
        const auto answer = QMessageBox::question(
            this, tr("Идёт запись"), tr("Остановить запись, сохранить дубль и выйти?"));
        if (answer != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_shoot->stopRecordingAndWait();
    }
    m_settings.save();
    e->accept();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("О программе OneTakeLink"),
        tr("<h3>OneTakeLink %1</h3>"
           "<p>Бесплатная программа для записи дублей с камеры iPhone на Windows.<br>"
           "Без рекламы, подписок, водяных знаков и регистрации. Работает без интернета.</p>"
           "<p>Автор: desNotto</p>"
           "<p>Видео с iPhone принимается через официальное приложение "
           "<b>LensLink Camera</b> (App Store). Модуль подключения основан на исходном коде "
           "<a href='https://github.com/MyNamesEMurray/LensLink'>LensLink</a> (GPL-2.0-or-later).</p>"
           "<p>Лицензия: GNU GPL v3 или новее. Использует Qt (LGPLv3) и FFmpeg (GPL).</p>")
            .arg(QLatin1String(OTL_VERSION)));
}

} // namespace otl
