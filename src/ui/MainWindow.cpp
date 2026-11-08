// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "LibraryPage.h"
#include "PlayerPanel.h"
#include "ShootPage.h"
#include "core/Version.h"
#include "library/Thumbnailer.h"
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
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

#include <thread>

namespace otl {

MainWindow::MainWindow() : m_settings(AppSettings::load())
{
    setWindowTitle(QStringLiteral("OneTakeLink"));
    resize(1600, 960);

    // Logic objects; their widgets are laid out below on one screen.
    m_shoot = new ShootPage(&m_settings, this);
    m_shoot->hide();
    m_library = new LibraryPage(&m_settings, this);
    m_library->hide();

    // Top bar: brand + about.
    m_topBar = new QWidget;
    m_topBar->setObjectName(QStringLiteral("topBar"));
    auto *top = new QHBoxLayout(m_topBar);
    top->setContentsMargins(14, 6, 10, 6);
    top->setSpacing(8);
    auto *logo = new QLabel;
    logo->setPixmap(QApplication::windowIcon().pixmap(QSize(24, 24), devicePixelRatio()));
    auto *name = new QLabel(QStringLiteral("OneTakeLink"));
    name->setObjectName(QStringLiteral("brandName"));
    auto *about = new QPushButton(tr("О программе"));
    about->setFlat(true);
    about->setFocusPolicy(Qt::NoFocus);
    top->addWidget(logo);
    top->addWidget(name);
    top->addStretch(1);
    top->addWidget(about);

    //  ┌ player ┬ monitor ┐┌ inspector ┐
    //  ├────── media pool ─┤│           │
    m_playerPane = pane(tr("Плеер"), m_library->playerWidget());
    m_monitorPane = pane(tr("Камера"), m_shoot->monitorWidget(), &m_monitorCaption);
    m_poolPane = m_library->poolWidget();
    m_poolPane->setObjectName(QStringLiteral("pane"));
    m_inspector = m_shoot->inspectorWidget();

    m_viewerSplit = new QSplitter(Qt::Horizontal);
    m_viewerSplit->addWidget(m_playerPane);
    m_viewerSplit->addWidget(m_monitorPane);
    m_viewerSplit->setStretchFactor(0, 2);
    m_viewerSplit->setStretchFactor(1, 3);

    m_workSplit = new QSplitter(Qt::Vertical);
    m_workSplit->addWidget(m_viewerSplit);
    m_workSplit->addWidget(m_poolPane);
    m_workSplit->setStretchFactor(0, 3);
    m_workSplit->setStretchFactor(1, 2);

    m_rootSplit = new QSplitter(Qt::Horizontal);
    m_rootSplit->addWidget(m_workSplit);
    m_rootSplit->addWidget(m_inspector);
    m_rootSplit->setStretchFactor(0, 1);
    m_rootSplit->setStretchFactor(1, 0);
    for (QSplitter *sp : {m_viewerSplit, m_workSplit, m_rootSplit}) {
        sp->setChildrenCollapsible(false);
        sp->setHandleWidth(6);
        connect(sp, &QSplitter::splitterMoved, this, &MainWindow::saveLayout);
    }

    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(m_topBar);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(6, 6, 6, 6);
    bodyLayout->addWidget(m_rootSplit);
    v->addWidget(body, 1);
    setCentralWidget(central);
    restoreLayout();

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
                m_monitorCaption->setText(stream.isEmpty() ? tr("Камера") : tr("Камера · %1").arg(stream));
            });
    connect(m_shoot, &ShootPage::takeSaved, this, [this](const QString &path) {
        m_library->refresh();
        m_library->selectPath(path);
    });
    connect(m_shoot, &ShootPage::rootFolderChanged, m_library, &LibraryPage::refresh);
    connect(m_shoot, &ShootPage::requestFullscreen, this, &MainWindow::toggleFullscreen);

    // Window hotkeys: active only while the main window itself is active,
    // so keys in a dialog never act here. Text fields consume printable
    // keys first, so typing a project name doesn't trigger them either.
    auto hotkey = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    hotkey(QKeySequence(Qt::Key_Space), [this] { m_shoot->toggleRecording(); }); // spec: Space = REC/STOP
    hotkey(QKeySequence(Qt::Key_K), [this] { m_library->togglePlay(); });
    hotkey(QKeySequence(Qt::Key_Comma), [this] { m_library->playerWidget()->step(-1); });
    hotkey(QKeySequence(Qt::Key_Period), [this] { m_library->playerWidget()->step(1); });
    hotkey(QKeySequence(Qt::Key_F11), [this] { toggleFullscreen(); });
    hotkey(QKeySequence(Qt::Key_Escape), [this] { exitFullscreen(); });

    recoverInterruptedTakes();
}

QWidget *MainWindow::pane(const QString &caption, QWidget *content, QLabel **captionOut)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("pane"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(8, 6, 8, 8);
    v->setSpacing(6);
    auto *label = new QLabel(caption);
    label->setObjectName(QStringLiteral("paneCaption"));
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    v->addWidget(label);
    v->addWidget(content, 1);
    if (captionOut)
        *captionOut = label;
    return w;
}

void MainWindow::saveLayout()
{
    if (m_fullscreen)
        return;
    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    s.setValue("layout/root", m_rootSplit->saveState());
    s.setValue("layout/work", m_workSplit->saveState());
    s.setValue("layout/viewers", m_viewerSplit->saveState());
}

void MainWindow::restoreLayout()
{
    QSettings s(AppSettings::iniPath(), QSettings::IniFormat);
    if (!m_rootSplit->restoreState(s.value("layout/root").toByteArray()))
        m_rootSplit->setSizes({1240, 350});
    if (!m_workSplit->restoreState(s.value("layout/work").toByteArray()))
        m_workSplit->setSizes({560, 360});
    if (!m_viewerSplit->restoreState(s.value("layout/viewers").toByteArray()))
        m_viewerSplit->setSizes({480, 740});
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
    m_wasMaximized = isMaximized();
    m_fullscreen = true;
    for (QWidget *w : {m_topBar, m_playerPane, m_poolPane, m_inspector, static_cast<QWidget *>(m_monitorCaption)})
        w->hide();
    statusBar()->hide();
    showFullScreen();
}

void MainWindow::exitFullscreen()
{
    if (!m_fullscreen)
        return;
    m_fullscreen = false;
    for (QWidget *w : {m_topBar, m_playerPane, m_poolPane, m_inspector, static_cast<QWidget *>(m_monitorCaption)})
        w->show();
    statusBar()->show();
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
        // Housekeeping: keep the thumbnail cache bounded (age + size).
        const qint64 freed = pruneThumbnailCache();
        if (freed > 0)
            qInfo("thumbnail cache: freed %lld KB", static_cast<long long>(freed / 1024));
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
