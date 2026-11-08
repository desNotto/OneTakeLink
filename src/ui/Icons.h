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

} // namespace otl::icons
