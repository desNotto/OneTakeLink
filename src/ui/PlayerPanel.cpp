// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerPanel.h"

#include "library/VideoPlayer.h"
#include "preview/PreviewWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>

namespace otl {

namespace {

enum class Glyph { Play, Pause, Next, Prev, Start };

// Transport icons drawn in code: identical on every Windows install,
// no dependency on symbol fonts.
QIcon glyphIcon(Glyph g, const QColor &color = QColor(0xF5, 0xF7, 0xFC))
{
    QIcon icon;
    for (int size : {18, 36}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const qreal s = size / 18.0;
        auto tri = [&](qreal x0, qreal x1, bool right) {
            QPainterPath path;
            if (right) {
                path.moveTo(x0 * s, 3 * s);
                path.lineTo(x1 * s, 9 * s);
                path.lineTo(x0 * s, 15 * s);
            } else {
                path.moveTo(x1 * s, 3 * s);
                path.lineTo(x0 * s, 9 * s);
                path.lineTo(x1 * s, 15 * s);
            }
            path.closeSubpath();
            p.drawPath(path);
        };
        switch (g) {
        case Glyph::Play:
            tri(5, 15, true);
            break;
        case Glyph::Pause:
            p.drawRoundedRect(QRectF(4 * s, 3 * s, 3.5 * s, 12 * s), 1 * s, 1 * s);
            p.drawRoundedRect(QRectF(10.5 * s, 3 * s, 3.5 * s, 12 * s), 1 * s, 1 * s);
            break;
        case Glyph::Next:
            tri(4, 12, true);
            p.drawRect(QRectF(12.5 * s, 3 * s, 2.5 * s, 12 * s));
            break;
        case Glyph::Prev:
            p.drawRect(QRectF(3 * s, 3 * s, 2.5 * s, 12 * s));
            tri(6, 14, false);
            break;
        case Glyph::Start:
            p.drawRect(QRectF(3 * s, 3 * s, 2.5 * s, 12 * s));
            tri(6, 11, false);
            tri(10, 15, false);
            break;
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QString clock(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 s = ms / 1000;
    const QString frac = QStringLiteral("%1").arg((ms % 1000) / 10, 2, 10, QLatin1Char('0'));
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3.%4").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')).arg(frac);
    return QStringLiteral("%1:%2.%3").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')).arg(frac);
}

// A click anywhere on the groove jumps there (instead of paging) and
// keeps dragging from that point.
class TimelineSlider : public QSlider {
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            QStyleOptionSlider opt;
            initStyleOption(&opt);
            const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            if (!handle.contains(e->position().toPoint())) {
                const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
                const int x = int(e->position().x()) - groove.x() - handle.width() / 2;
                const int span = groove.width() - handle.width();
                setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), x, std::max(1, span)));
            }
        }
        QSlider::mousePressEvent(e);
    }
};

} // namespace

PlayerPanel::PlayerPanel(QWidget *parent) : QWidget(parent)
{
    m_player = new VideoPlayer(this);
    m_view = new PreviewWidget(this);
    GuideSettings noGuides;
    noGuides.enabled = false;
    m_view->setGuides(noGuides);
    m_view->setPlaceholder(tr("Выберите дубль в списке"));
    m_player->setFrameSink(m_view);

    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("playerTitle"));
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_details = new QLabel;
    m_details->setObjectName(QStringLiteral("hintLabel"));
    m_details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_slider = new TimelineSlider(Qt::Horizontal);
    m_slider->setObjectName(QStringLiteral("timeline"));
    m_slider->setRange(0, 0);
    m_time = new QLabel(QStringLiteral("0:00.00 / 0:00.00"));
    m_time->setObjectName(QStringLiteral("playerTime"));

    auto button = [this](Glyph g, const QString &tip) {
        auto *b = new QPushButton;
        b->setIcon(glyphIcon(g));
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setObjectName(QStringLiteral("transportButton"));
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_start = button(Glyph::Start, tr("В начало (Home)"));
    m_prev = button(Glyph::Prev, tr("Предыдущий кадр ( , )"));
    m_play = button(Glyph::Play, tr("Воспроизвести / пауза (K)"));
    m_play->setObjectName(QStringLiteral("playButton"));
    m_next = button(Glyph::Next, tr("Следующий кадр ( . )"));
    m_external = new QPushButton(tr("Внешний плеер"));
    m_external->setToolTip(tr("Открыть в программе Windows по умолчанию"));
    m_external->setFocusPolicy(Qt::NoFocus);

    auto *transport = new QHBoxLayout;
    transport->setSpacing(6);
    transport->addWidget(m_start);
    transport->addWidget(m_prev);
    transport->addWidget(m_play);
    transport->addWidget(m_next);
    transport->addSpacing(10);
    transport->addWidget(m_time);
    transport->addStretch(1);
    transport->addWidget(m_external);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    v->addWidget(m_view, 1);
    v->addWidget(m_slider);
    v->addLayout(transport);
    v->addWidget(m_title);
    v->addWidget(m_details);

    connect(m_start, &QPushButton::clicked, this, &PlayerPanel::toStart);
    connect(m_prev, &QPushButton::clicked, this, [this] { step(-1); });
    connect(m_next, &QPushButton::clicked, this, [this] { step(1); });
    connect(m_play, &QPushButton::clicked, this, &PlayerPanel::togglePlay);
    connect(m_external, &QPushButton::clicked, this, [this] {
        if (!m_path.isEmpty())
            emit openExternally(m_path);
    });
    connect(m_view, &PreviewWidget::doubleClicked, this, &PlayerPanel::togglePlay);

    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        m_scrubbing = true;
        m_resumeAfterScrub = m_playing;
        if (m_playing)
            m_player->pause();
        m_player->seek(m_slider->value(), false);
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        m_player->seek(v, false); // keyframes while dragging: fast
        m_time->setText(clock(v) + QStringLiteral(" / ") + clock(m_duration));
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_scrubbing = false;
        m_player->seek(m_slider->value(), true); // exact frame on release
        if (m_resumeAfterScrub)
            m_player->play();
    });

    connect(m_player, &VideoPlayer::opened, this, [this](const QString &path, qint64 durationMs, double) {
        if (path != m_path)
            return;
        setDuration(durationMs);
        setControlsEnabled(true);
    });
    connect(m_player, &VideoPlayer::failed, this, [this](const QString &path, const QString &error) {
        if (path != m_path)
            return;
        setControlsEnabled(false);
        m_view->setPlaceholder(tr("Не удалось открыть файл: %1").arg(error));
        m_view->onStreamLost();
    });
    connect(m_player, &VideoPlayer::positionChanged, this, &PlayerPanel::showPosition);
    connect(m_player, &VideoPlayer::playingChanged, this, &PlayerPanel::setPlayingUi);

    setControlsEnabled(false);
}

PlayerPanel::~PlayerPanel()
{
    // The player's thread feeds m_view: stop it first.
    delete m_player;
    m_player = nullptr;
}

void PlayerPanel::load(const QString &path, const QString &title, const QString &details)
{
    m_title->setText(title);
    m_details->setText(details);
    if (path == m_path)
        return;
    m_path = path;
    m_position = 0;
    setDuration(0);
    setControlsEnabled(false);
    m_view->setPlaceholder(tr("Загрузка…"));
    m_player->open(path);
}

void PlayerPanel::unload()
{
    if (m_path.isEmpty())
        return;
    m_path.clear();
    m_player->close();
    m_title->clear();
    m_details->clear();
    setDuration(0);
    setControlsEnabled(false);
    m_view->setPlaceholder(tr("Выберите дубль в списке"));
}

void PlayerPanel::togglePlay()
{
    if (!m_path.isEmpty())
        m_player->togglePlay();
}

void PlayerPanel::step(int frames)
{
    if (!m_path.isEmpty())
        m_player->stepFrames(frames);
}

void PlayerPanel::toStart()
{
    if (!m_path.isEmpty())
        m_player->seek(0, true);
}

void PlayerPanel::setDuration(qint64 ms)
{
    m_duration = ms;
    m_slider->setRange(0, int(std::min<qint64>(ms, INT_MAX)));
    showPosition(0);
}

void PlayerPanel::showPosition(qint64 ms)
{
    m_position = ms;
    if (!m_scrubbing) {
        const QSignalBlocker block(m_slider);
        m_slider->setValue(int(ms));
    }
    m_time->setText(clock(ms) + QStringLiteral(" / ") + clock(m_duration));
}

void PlayerPanel::setPlayingUi(bool playing)
{
    m_playing = playing;
    m_play->setIcon(glyphIcon(playing ? Glyph::Pause : Glyph::Play));
}

void PlayerPanel::setControlsEnabled(bool on)
{
    for (QWidget *w : {static_cast<QWidget *>(m_slider), static_cast<QWidget *>(m_start),
                       static_cast<QWidget *>(m_prev), static_cast<QWidget *>(m_play),
                       static_cast<QWidget *>(m_next), static_cast<QWidget *>(m_external)})
        w->setEnabled(on);
}

} // namespace otl
