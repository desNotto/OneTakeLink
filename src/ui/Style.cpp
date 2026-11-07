// SPDX-License-Identifier: GPL-3.0-or-later
// Dark, compact theme — the monitor is the star, the chrome stays quiet.
#include "Style.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace otl {

void applyStyle(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    const QColor bg(0x1b, 0x1d, 0x22), base(0x14, 0x15, 0x19), text(0xe6, 0xe8, 0xeb),
        mid(0x2a, 0x2d, 0x34), accent(0x4f, 0x8c, 0xff);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x1f, 0x21, 0x27));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, mid);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x86, 0x8f));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6b, 0x70, 0x78));
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
QScrollArea#sidePanel, QScrollArea#sidePanel > QWidget > QWidget { background: #1b1d22; }
QGroupBox { border: 1px solid #2c2f36; border-radius: 8px; margin-top: 14px; padding: 10px 8px 8px 8px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #aeb4bd; }
QGroupBox QLabel, QGroupBox QCheckBox { font-weight: normal; }
QPushButton { background: #2a2d34; border: 1px solid #383c45; border-radius: 6px; padding: 6px 12px; }
QPushButton:hover { background: #333740; }
QPushButton:disabled { color: #6b7078; }
QPushButton#primaryButton { background: #2f5fd0; border-color: #3b6fe6; color: white; font-weight: 600; }
QPushButton#primaryButton:hover { background: #3b6fe6; }
QPushButton#recButton { background: #c62828; border-color: #e53935; color: white; font-size: 15px; font-weight: 700; }
QPushButton#recButton:hover { background: #e53935; }
QPushButton#recButton[recording="true"] { background: #3a3d45; border-color: #e53935; color: #ff5252; }
QLabel#hintLabel { color: #8b919a; }
QLabel#statusLabel { font-weight: 600; }
QLabel#durationLabel { font-size: 18px; font-family: Consolas, monospace; color: #e6e8eb; }
QComboBox, QLineEdit { background: #14151a; border: 1px solid #383c45; border-radius: 5px; padding: 4px 6px; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; padding: 10px 22px; color: #9aa0a6; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #4f8cff; }
QStatusBar { background: #15161a; color: #aeb4bd; }
QStatusBar QLabel { padding: 0 8px; }
QTreeWidget { border: 1px solid #2c2f36; border-radius: 6px; }
QHeaderView::section { background: #1f2127; border: none; border-right: 1px solid #2c2f36; padding: 6px; color: #aeb4bd; }
)"));
}

} // namespace otl
