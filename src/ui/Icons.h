// SPDX-License-Identifier: GPL-3.0-or-later
// Small UI icons drawn in code: crisp at any DPI, no symbol-font or SVG
// plugin dependencies.
#pragma once

#include <QColor>
#include <QIcon>

namespace otl::icons {

// Portrait frame with a clockwise arrow: rotate the picture by 90°.
QIcon rotateClockwise(const QColor &color = QColor(0xF5, 0xF7, 0xFC));
// Counter-clockwise arrow back to the start: reset to the original orientation.
QIcon resetRotation(const QColor &color = QColor(0xF5, 0xF7, 0xFC));

// Platform marks drawn in the app's own line style (not the brand artwork):
// a rounded camera with lens and dot (Instagram), a rounded screen with a
// play triangle (YouTube).
QIcon instagramMark(const QColor &color = QColor(0xF5, 0xF7, 0xFC));
QIcon youtubeMark(const QColor &color = QColor(0xF5, 0xF7, 0xFC));
// Rule-of-thirds grid in a rounded frame: composition guides on/off.
QIcon thirdsGrid(const QColor &color = QColor(0xF5, 0xF7, 0xFC));

// Language picker: national flags in a rounded frame (Russia, USA).
QIcon flagRu();
QIcon flagUs();

} // namespace otl::icons
