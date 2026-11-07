// SPDX-License-Identifier: GPL-3.0-or-later
// Crop geometry shared by the preview overlay and the exporter, so what
// the user sees framed is exactly what gets exported. Crops never stretch:
// the largest rectangle of the target aspect that fits the source.
#pragma once

#include <QRectF>
#include <QSize>

#include <algorithm>

namespace otl::CropMath {

// Largest rect of `aspect` (w/h) inside `source`, centred on (cx, cy)
// given as 0..1 fractions and clamped to stay inside.
inline QRectF cropRect(const QRectF &source, double aspect, double cx = 0.5, double cy = 0.5)
{
    if (aspect <= 0 || source.isEmpty())
        return source;
    double w = source.width(), h = source.height();
    if (w / h > aspect)
        w = h * aspect;
    else
        h = w / aspect;
    double x = source.left() + cx * source.width() - w / 2;
    double y = source.top() + cy * source.height() - h / 2;
    x = std::clamp(x, source.left(), source.right() - w);
    y = std::clamp(y, source.top(), source.bottom() - h);
    return {x, y, w, h};
}

// Pixel-exact crop of a frame (even width/height for 4:2:0 video).
inline QRect cropPixels(const QSize &frame, double aspect, double cx = 0.5, double cy = 0.5)
{
    QRectF r = cropRect(QRectF(0, 0, frame.width(), frame.height()), aspect, cx, cy);
    int w = int(r.width()) & ~1, h = int(r.height()) & ~1;
    int x = int(r.x()) & ~1, y = int(r.y()) & ~1;
    x = std::min(x, frame.width() - w);
    y = std::min(y, frame.height() - h);
    return {x, y, w, h};
}

} // namespace otl::CropMath
