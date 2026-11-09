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

QIcon instagramMark(const QColor &color)
{
    return render([color](QPainter &p) {
        p.setPen(QPen(color, 1.7));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 2.5, 15.0, 15.0), 4.6, 4.6);
        p.drawEllipse(QPointF(10.0, 10.0), 3.6, 3.6);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(14.4, 5.6), 1.15, 1.15);
    });
}

QIcon youtubeMark(const QColor &color)
{
    return render([color](QPainter &p) {
        p.setPen(QPen(color, 1.7));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(1.8, 4.2, 16.4, 11.6), 3.6, 3.6);
        QPainterPath play;
        play.moveTo(8.2, 7.0);
        play.lineTo(13.4, 10.0);
        play.lineTo(8.2, 13.0);
        play.closeSubpath();
        p.setPen(QPen(color, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(color);
        p.drawPath(play);
    });
}

QIcon thirdsGrid(const QColor &color)
{
    return render([color](QPainter &p) {
        p.setPen(QPen(color, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 3.5, 15.0, 13.0), 2.4, 2.4);
        p.setPen(QPen(color, 1.1));
        for (int i = 1; i <= 2; ++i) {
            const qreal x = 2.5 + 15.0 * i / 3.0;
            const qreal y = 3.5 + 13.0 * i / 3.0;
            p.drawLine(QPointF(x, 3.5), QPointF(x, 16.5));
            p.drawLine(QPointF(2.5, y), QPointF(17.5, y));
        }
    });
}

namespace {

// 20x14 flag area centred in the 20x20 icon, with rounded corners.
const QRectF kFlag(0.5, 3.5, 19.0, 13.0);

template <typename Fill>
QIcon flag(Fill fill)
{
    return render([fill](QPainter &p) {
        QPainterPath clip;
        clip.addRoundedRect(kFlag, 2.2, 2.2);
        p.save();
        p.setClipPath(clip);
        fill(p);
        p.restore();
        p.setPen(QPen(QColor(0xF5, 0xF7, 0xFC, 90), 0.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(clip);
    });
}

} // namespace

QIcon flagRu()
{
    return flag([](QPainter &p) {
        const qreal h = kFlag.height() / 3;
        p.fillRect(QRectF(kFlag.left(), kFlag.top(), kFlag.width(), h), QColor(0xFF, 0xFF, 0xFF));
        p.fillRect(QRectF(kFlag.left(), kFlag.top() + h, kFlag.width(), h), QColor(0x00, 0x39, 0xA6));
        p.fillRect(QRectF(kFlag.left(), kFlag.top() + 2 * h, kFlag.width(), h), QColor(0xD5, 0x2B, 0x1E));
    });
}

QIcon flagUs()
{
    return flag([](QPainter &p) {
        const QColor red(0xB2, 0x22, 0x34), white(0xFF, 0xFF, 0xFF), blue(0x3C, 0x3B, 0x6E);
        const qreal stripe = kFlag.height() / 13;
        for (int i = 0; i < 13; ++i)
            p.fillRect(QRectF(kFlag.left(), kFlag.top() + i * stripe, kFlag.width(), stripe + 0.01),
                       i % 2 ? white : red);
        const QRectF canton(kFlag.left(), kFlag.top(), kFlag.width() * 0.42, stripe * 7);
        p.fillRect(canton, blue);
        // Stars reduced to dots: 3 rows x 4 columns read well at icon size.
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                p.drawEllipse(QPointF(canton.left() + canton.width() * (c + 0.5) / 4,
                                      canton.top() + canton.height() * (r + 0.5) / 3),
                              0.45, 0.45);
    });
}

} // namespace otl::icons
