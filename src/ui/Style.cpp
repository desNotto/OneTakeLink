// SPDX-License-Identifier: GPL-3.0-or-later
// Brand theme: deep Cetacean Blue surfaces, Lavender accent, Ghost White
// text (see assets/icon/onetakelink.svg). The video surround stays the
// darkest, least saturated tone so it doesn't tint colour judgement.
#include "Style.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace otl {

namespace palette {
// Brand
constexpr const char *kCetacean = "#150A35"; // Cetacean Blue
constexpr const char *kLavender = "#A577FF"; // Lavender
constexpr const char *kGhost = "#F5F7FC";    // Ghost White
} // namespace palette

void applyStyle(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x15, 0x0A, 0x35);     // Cetacean Blue
    const QColor base(0x0F, 0x08, 0x28);       // inputs, lists
    const QColor alt(0x18, 0x0E, 0x3D);
    const QColor button(0x22, 0x16, 0x52);
    const QColor text(0xF5, 0xF7, 0xFC);       // Ghost White
    const QColor muted(0xA9, 0xA3, 0xC7);
    const QColor disabled(0x5E, 0x57, 0x86);
    const QColor accent(0xA5, 0x77, 0xFF);     // Lavender

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, window);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted.darker(130));
    p.setColor(QPalette::Mid, QColor(0x2D, 0x20, 0x66));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        p.setColor(QPalette::Disabled, role, disabled);
    app.setPalette(p);

    QString css = QStringLiteral(R"(
* { outline: 0; }
QToolTip { background: #0F0828; color: @GHOST@; border: 1px solid #3A2B78; padding: 4px 6px; }

/* Header */
QTabWidget::pane { border: none; }
QTabWidget::tab-bar { left: 0; }
QTabBar { background: #0F0828; qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; padding: 11px 22px; color: #A9A3C7;
               border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:hover { color: @GHOST@; }
QTabBar::tab:selected { color: @GHOST@; border-bottom: 2px solid @LAVENDER@; }
QWidget#brand { background: #0F0828; }
QLabel#brandName { color: @GHOST@; font-weight: 800; font-size: 20px; letter-spacing: 0.3px; }
QLabel#topLabel { color: #A9A3C7; font-weight: 600; }
QWidget#topBar QComboBox { min-height: 26px; }

/* Side panel and cards */
QScrollArea#sidePanel, QScrollArea#sidePanel > QWidget > QWidget { background: @CETACEAN@; }
QGroupBox { background: #1A0F42; border: 1px solid #2D2066; border-radius: 12px;
            margin-top: 16px; padding: 12px 10px 10px 10px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #C9B2FF; }
QGroupBox QLabel, QGroupBox QCheckBox { font-weight: normal; background: transparent; }
QLabel#hintLabel { color: #A9A3C7; }
QLabel#statusLabel { font-weight: 600; }
QLabel#durationLabel { font-size: 18px; font-family: Consolas, "DejaVu Sans Mono", monospace; color: @GHOST@; }

/* Buttons */
QPushButton { background: #22165A; border: 1px solid #3A2B78; border-radius: 8px; padding: 6px 14px; color: @GHOST@; }
QPushButton:hover { background: #2C1D70; border-color: #5A43A8; }
QPushButton:pressed { background: #1A1048; }
QPushButton:disabled { color: #5E5786; background: #1A1042; border-color: #2A1E5C; }
QPushButton:flat { background: transparent; border: none; color: #A9A3C7; }
QPushButton:flat:hover { color: @GHOST@; }
QPushButton#primaryButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #B48CFF, stop:1 @LAVENDER@);
                            border: none; color: @CETACEAN@; font-weight: 700; }
QPushButton#primaryButton:hover { background: #BE9BFF; }
QPushButton#primaryButton:pressed { background: #8F5CF5; }
QPushButton#recButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #FF5C6C, stop:1 #E0364A);
                        border: none; color: white; font-size: 15px; font-weight: 700; border-radius: 10px; }
QPushButton#recButton:hover { background: #FF6E7C; }
QPushButton#recButton[recording="true"] { background: #0F0828; border: 2px solid #FF5C6C; color: #FF5C6C; }

/* Inputs */
QComboBox, QLineEdit { background: #0F0828; border: 1px solid #3A2B78; border-radius: 7px;
                       padding: 5px 8px; color: @GHOST@; selection-background-color: @LAVENDER@;
                       selection-color: @CETACEAN@; }
QComboBox:hover, QLineEdit:hover { border-color: #5A43A8; }
QComboBox:focus, QLineEdit:focus { border-color: @LAVENDER@; }
QComboBox:disabled, QLineEdit:disabled { color: #5E5786; border-color: #2A1E5C; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #120A30; border: 1px solid #3A2B78; color: @GHOST@;
                              selection-background-color: #3A2B78; selection-color: @GHOST@; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #5A43A8; background: #0F0828; }
QCheckBox::indicator:hover { border-color: @LAVENDER@; }
QCheckBox::indicator:checked { background: @LAVENDER@; border-color: @LAVENDER@;
    image: url(:/icons/check.png); }
QSlider::groove:horizontal { height: 4px; background: #2D2066; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @LAVENDER@; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 7px;
                             background: @GHOST@; border: 2px solid @LAVENDER@; }
QSlider::groove:horizontal:disabled, QSlider::sub-page:horizontal:disabled { background: #2A1E5C; }
QSlider::handle:horizontal:disabled { background: #5E5786; border-color: #2A1E5C; }

/* Lists */
QTreeWidget { background: #0F0828; alternate-background-color: #140B34; border: 1px solid #2D2066;
              border-radius: 10px; color: @GHOST@; }
QTreeWidget::item { padding: 4px 0; }
QTreeWidget::item:selected { background: #3A2B78; color: @GHOST@; }
QTreeWidget::item:hover { background: #22165A; }
QHeaderView { background: transparent; }
QHeaderView::section { background: #1A0F42; border: none; border-right: 1px solid #2D2066;
                       padding: 7px 6px; color: #C9B2FF; font-weight: 600; }

/* Scrollbars */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #3A2B78; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #5A43A8; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #3A2B78; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* Single-screen layout */
QWidget#topBar { background: #0F0828; border-bottom: 1px solid #22165A; }
QWidget#pane, QWidget#mediaPoolPane { background: #170C3C; border: 1px solid #2D2066; border-radius: 10px; }
QWidget#pane QLabel, QWidget#mediaPoolPane QLabel, QWidget#mediaPoolPane QCheckBox { background: transparent; }
QLabel#paneCaption { color: #C9B2FF; font-weight: 700; font-size: 12px; letter-spacing: 0.6px; text-transform: uppercase; }
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: #3A2B78; }
QPushButton#viewToggle { padding: 4px 10px; font-size: 15px; }
QPushButton#viewToggle:checked { background: #3A2B78; border-color: @LAVENDER@; color: @GHOST@; }
QListView#mediaPool { background: #0F0828; border: 1px solid #2D2066; border-radius: 8px; padding: 6px; color: @GHOST@; }
QListView#mediaPool::item { border-radius: 8px; padding: 4px; color: #D9D4EE; }
QListView#mediaPool::item:hover { background: #22165A; }
QListView#mediaPool::item:selected { background: #3A2B78; color: @GHOST@; }
QTreeView#mediaTable { background: #0F0828; alternate-background-color: #140B34; border: 1px solid #2D2066;
                       border-radius: 8px; color: @GHOST@; }
QTreeView#mediaTable::item:selected { background: #3A2B78; color: @GHOST@; }
QScrollArea#sidePanel { border: 1px solid #2D2066; border-radius: 10px; }
QMenu { background: #120A30; border: 1px solid #3A2B78; color: @GHOST@; padding: 4px; }
QMenu::item { padding: 6px 22px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: #3A2B78; }
QMenu::item:disabled { color: #5E5786; }
QMenu::separator { height: 1px; background: #2D2066; margin: 4px 8px; }

/* Buttons over the camera monitor */
QWidget#monitorButtons { background: transparent; }
QToolButton#monitorButton { background: rgba(21, 10, 53, 190); border: 1px solid rgba(165, 119, 255, 120);
                            border-radius: 9px; padding: 5px 7px; color: @GHOST@; font-weight: 700; }
QToolButton#monitorButton:hover { background: rgba(58, 43, 120, 230); border-color: @LAVENDER@; }
QToolButton#monitorButton:pressed { background: rgba(143, 92, 245, 230); }
QToolButton#monitorButton:disabled { background: rgba(21, 10, 53, 110); border-color: rgba(165, 119, 255, 40); }

/* Library player */
QLabel#playerTitle { font-weight: 700; font-size: 14px; color: @GHOST@; }
QLabel#playerTime { font-family: Consolas, "DejaVu Sans Mono", monospace; color: #C9B2FF; }
QPushButton#transportButton, QPushButton#playButton { padding: 6px 10px; min-width: 22px; }
QPushButton#playButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #B48CFF, stop:1 @LAVENDER@);
                         border: none; padding: 6px 16px; }
QPushButton#playButton:hover { background: #BE9BFF; }
QPushButton#playButton:disabled { background: #2A1E5C; }
QSlider#timeline::groove:horizontal { height: 6px; border-radius: 3px; }
QSlider#timeline::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; }

/* Status bar, dialogs */
QStatusBar { background: #0F0828; color: #A9A3C7; border-top: 1px solid #22165A; }
QStatusBar QLabel { padding: 0 8px; }
QMessageBox, QInputDialog, QFileDialog { background: @CETACEAN@; }
)");
    css.replace(QLatin1String("@CETACEAN@"), QLatin1String(palette::kCetacean));
    css.replace(QLatin1String("@LAVENDER@"), QLatin1String(palette::kLavender));
    css.replace(QLatin1String("@GHOST@"), QLatin1String(palette::kGhost));
    app.setStyleSheet(css);
}

} // namespace otl
