// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace otl::icons {

namespace {

// Arrow head at the end of an arc, pointing along the direction of travel.
void arrowHead(QPainter &p, QPointF tip, double angleDeg, qreal size)
{
    const double a = qDegreesToRadians(angleDeg);
    const QPointF dir(std::cos(a), -std::sin(a)); // Qt's y axis points down
    const QPointF normal(-dir.y(), dir.x());
    QPainterPath head;
    head.moveTo(tip + dir * size * 0.55);
    head.lineTo(tip - dir * size * 0.45 + normal * size * 0.6);
    head.lineTo(tip - dir * size * 0.45 - normal * size * 0.6);
    head.closeSubpath();
    p.fillPath(head, p.pen().color());
}

template <typename Draw>
QIcon render(Draw draw)
{
    QIcon icon;
    for (int size : {20, 40, 60}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 20.0, size / 20.0);
        draw(p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

QIcon rotateClockwise(const QColor &color)
{
    return render([color](QPainter &p) {
        // Phone/frame in portrait, lower-left.
        p.setPen(QPen(color, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3.0, 7.0, 7.5, 11.0), 1.6, 1.6);
        // Clockwise arc around the top-right corner with an arrow head.
        const QRectF arc(6.5, 2.0, 11.5, 11.5);
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arc, 160 * 16, -130 * 16); // from 160° clockwise to 30°
        const double end = 30.0;
        const QPointF c = arc.center();
        const QPointF tip(c.x() + arc.width() / 2 * std::cos(qDegreesToRadians(end)),
                          c.y() - arc.height() / 2 * std::sin(qDegreesToRadians(end)));
        arrowHead(p, tip, end - 90.0, 4.2); // tangent of a clockwise arc
    });
}

QIcon resetRotation(const QColor &color)
{
    return render([color](QPainter &p) {
        // Counter-clockwise circular arrow (undo-style) around a dot.
        const QRectF arc(3.0, 3.5, 14.0, 14.0);
        p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(arc, 300 * 16, 270 * 16); // from 300° counter-clockwise to 210°
        const double end = 210.0;
        const QPointF c = arc.center();
        const QPointF tip(c.x() + arc.width() / 2 * std::cos(qDegreesToRadians(end)),
                          c.y() - arc.height() / 2 * std::sin(qDegreesToRadians(end)));
        arrowHead(p, tip, end + 90.0, 4.2); // tangent of a counter-clockwise arc
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(c, 1.7, 1.7);
    });
}

} // namespace otl::icons
