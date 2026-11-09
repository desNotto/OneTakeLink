// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShootPage.h"

#include "camera/DeviceDiscovery.h"
#include "camera/android/AndroidSource.h"
#include "camera/lenslink/LensLinkSource.h"
#include "ui/Icons.h"
#include "camera/StreamHandler.h"

#include <thread>
#include "recording/TakeNamer.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStorageInfo>
#include <QPointer>
#include <QProgressBar>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace otl {

namespace {

QString formatDuration(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QLatin1Char('0'))
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString formatBytes(qint64 b)
{
    if (b >= 1024LL * 1024 * 1024)
        return ShootPage::tr("%1 ГБ").arg(double(b) / (1024.0 * 1024 * 1024), 0, 'f', 1);
    return ShootPage::tr("%1 МБ").arg(double(b) / (1024.0 * 1024), 0, 'f', 0);
}

double aspectValue(const QString &key)
{
    if (key == QLatin1String("16:9"))
        return 16.0 / 9.0;
    if (key == QLatin1String("9:16"))
        return 9.0 / 16.0;
    if (key == QLatin1String("4:5"))
        return 4.0 / 5.0;
    if (key == QLatin1String("1:1"))
        return 1.0;
    return 0;
}

const char *kManualIpKey = "wifi-manual";

// File names have no spaces, so a word-wrapping label can't break them and
// would force the whole side panel wider. Allow breaks after '_' and '-'.
QString breakable(const QString &name)
{
    QString out;
    for (QChar c : name) {
        out += c;
        if (c == QLatin1Char('_') || c == QLatin1Char('-'))
            out += QChar(0x200B); // zero-width space
    }
    return out;
}

// Labels in the fixed-width side panel take the width they are given and
// never widen the panel (long text wraps or is clipped instead).
void fitPanel(QLabel *l)
{
    QSizePolicy sp = l->sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Ignored);
    l->setSizePolicy(sp);
    l->setMinimumWidth(0);
}

// Scrolling the side panel must not change a combo box or slider the
// cursor happens to pass over (e.g. the iPhone's format or zoom): the
// wheel only adjusts a control after it was clicked; otherwise it
// scrolls the panel.
class WheelGuard : public QObject {
public:
    WheelGuard(QWidget *scrollTarget, QObject *parent) : QObject(parent), m_target(scrollTarget) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel) {
            auto *w = qobject_cast<QWidget *>(obj);
            if (w && !w->hasFocus()) {
                QCoreApplication::sendEvent(m_target, event);
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QWidget *m_target;
};

} // namespace

ShootPage::ShootPage(AppSettings *settings, QWidget *parent) : QWidget(parent), m_settings(settings)
{
    m_lenslink = new lenslink::LensLinkSource(this);
    m_android = new android::AndroidSource(this);
    m_source = m_lenslink;
    m_discovery = new DeviceDiscovery(this);
    m_recorder = new Recorder(this);
    m_preview = new PreviewWidget(this);
    m_preview->setPlaceholder(tr("Выберите телефон справа и нажмите «Подключить».\n"
                                 "iPhone: откройте приложение LensLink Camera. "
                                 "Android: включите отладку по USB."));

    for (CameraSource *s : {static_cast<CameraSource *>(m_lenslink), static_cast<CameraSource *>(m_android)}) {
        s->setFrameSink(m_preview);
        s->setPacketSink(m_recorder);
    }

    // No layout of its own: MainWindow places the monitor and the
    // inspector panel in its single-screen layout (monitorWidget(),
    // inspectorWidget()); this object keeps the logic.
    buildFormatBar();
    buildPanel();
    buildMonitorButtons();

    for (QLabel *l : {m_statusLabel, m_hintLabel, m_folderLabel, m_nextNameLabel, m_durationLabel,
                      m_savedLabel, m_diskLabel, m_infoDevice, m_infoResolution, m_infoFps, m_infoCodec,
                      m_infoBitrate, m_infoMissing, m_infoLatency, m_infoDecoder, m_infoAudio})
        fitPanel(l);
    for (QComboBox *c : m_panel->widget()->findChildren<QComboBox *>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(8);
    }

    auto *guard = new WheelGuard(m_panel->viewport(), this);
    const auto guarded = m_panel->widget()->findChildren<QWidget *>();
    for (QWidget *w : guarded) {
        if (qobject_cast<QComboBox *>(w) || qobject_cast<QSlider *>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(guard);
        }
    }

    watchSource(m_lenslink);
    watchSource(m_android);
    connect(m_discovery, &DeviceDiscovery::finished, this, &ShootPage::onDevicesFound);
    connect(m_recorder, &Recorder::stateChanged, this, &ShootPage::onRecorderState);
    connect(m_recorder, &Recorder::progress, this, &ShootPage::onRecordProgress);
    connect(m_recorder, &Recorder::finished, this, &ShootPage::onTakeFinished);
    connect(m_recorder, &Recorder::failed, this, &ShootPage::onRecordFailed);
    connect(m_preview, &PreviewWidget::doubleClicked, this, &ShootPage::requestFullscreen);

    m_diskTimer.setInterval(5000);
    connect(&m_diskTimer, &QTimer::timeout, this, &ShootPage::updateDiskInfo);
    m_diskTimer.start();

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        if (--m_countdownLeft <= 0) {
            m_countdownTimer.stop();
            m_preview->setRecording(false);
            startRecordingNow();
        } else {
            m_preview->setRecording(true, tr("Старт через %1").arg(m_countdownLeft));
        }
    });

    applySourceOptions();
    updateGuides();
    updateDiskInfo();
    updateNextName();
    onStatus({});
    refreshDevices();
}

ShootPage::~ShootPage()
{
    shutdown();
}

void ShootPage::shutdown()
{
    m_audioInput.stop();
    // The source calls into the preview and the recorder from its worker
    // thread: stop it before either goes away.
    if (m_lenslink)
        m_lenslink->stop();
    if (m_android)
        m_android->stop();
}

void ShootPage::buildMonitorButtons()
{
    // Over the monitor's top-left corner: rotate 90° clockwise / reset.
    // For phones that can only be mounted sideways or upside down.
    m_monitorButtons = new QWidget(m_preview);
    m_monitorButtons->setObjectName(QStringLiteral("monitorButtons"));
    auto *h = new QHBoxLayout(m_monitorButtons);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    m_rotateButton = new QToolButton;
    m_rotateButton->setIcon(icons::rotateClockwise());
    m_rotateButton->setIconSize(QSize(22, 22));
    m_rotateButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_rotateButton->setToolTip(tr("Повернуть кадр на 90° по часовой стрелке (R).\n"
                                  "Поворот записывается в файл без перекодирования."));
    m_resetRotationButton = new QToolButton;
    m_resetRotationButton->setIcon(icons::resetRotation());
    m_resetRotationButton->setIconSize(QSize(22, 22));
    m_resetRotationButton->setToolTip(tr("Сбросить поворот — исходная ориентация камеры (Shift+R)"));
    for (QToolButton *b : {m_rotateButton, m_resetRotationButton}) {
        b->setObjectName(QStringLiteral("monitorButton"));
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        h->addWidget(b);
    }
    m_monitorButtons->move(10, 10);

    connect(m_rotateButton, &QToolButton::clicked, this, &ShootPage::rotateClockwise);
    connect(m_resetRotationButton, &QToolButton::clicked, this, &ShootPage::resetRotation);
    applyRotation(m_settings->monitorRotation);
}

void ShootPage::rotateClockwise()
{
    applyRotation(m_preview->rotation() + 90);
}

void ShootPage::resetRotation()
{
    applyRotation(0);
}

void ShootPage::applyRotation(int degreesCw)
{
    // The angle is fixed per file when a take starts.
    if (m_recorder->state() != Recorder::State::Idle)
        return;
    degreesCw = ((degreesCw % 360) + 360) % 360;
    m_preview->setRotation(degreesCw);
    m_rotateButton->setText(degreesCw ? QStringLiteral("%1°").arg(degreesCw) : QString());
    m_resetRotationButton->setEnabled(degreesCw != 0);
    m_monitorButtons->adjustSize();
    m_preview->setBadgeLeft(m_monitorButtons->geometry().right() + 10);
    if (m_settings->monitorRotation != degreesCw) {
        m_settings->monitorRotation = degreesCw;
        m_settings->save();
    }
}

QWidget *ShootPage::buildFormatBar()
{
    // Lives in MainWindow's top bar (formatBar()).
    m_formatBar = new QWidget;
    m_formatBar->setObjectName(QStringLiteral("formatBar"));
    auto *h = new QHBoxLayout(m_formatBar);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    m_aspectCombo = new QComboBox;
    m_aspectCombo->addItem(tr("Полный кадр"), QStringLiteral("none"));
    m_aspectCombo->addItem(tr("16:9 — горизонтальное"), QStringLiteral("16:9"));
    m_aspectCombo->addItem(tr("9:16 — Reels / Shorts / Stories"), QStringLiteral("9:16"));
    m_aspectCombo->addItem(tr("4:5 — лента Instagram"), QStringLiteral("4:5"));
    m_aspectCombo->addItem(tr("1:1 — квадрат"), QStringLiteral("1:1"));
    m_aspectCombo->setCurrentIndex(std::max(0, m_aspectCombo->findData(m_settings->cropAspect)));
    m_aspectCombo->setToolTip(tr("Формат кадра: рамка будущего кадрирования на мониторе. "
                                 "Записывается всегда полный кадр."));

    // Platform safe zones: two toggles, at most one on. Clicking the lit
    // one turns zones off; clicking the other switches platforms.
    auto toggle = [this](const QIcon &icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("topToggle"));
        b->setIcon(icon);
        b->setIconSize(QSize(22, 22));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        return b;
    };
    const QString zonesNote = tr("Красные и оранжевые зоны — там интерфейс приложения (ник, подпись, кнопки) "
                                 "перекрывает видео. Зелёная — место для лиц, текста и логотипов.\n"
                                 "Зоны приблизительные: по рекомендациям Meta и Google для 1080×1920, "
                                 "интерфейс приложений со временем меняется.");
    m_reelsButton = toggle(icons::instagramMark(), tr("Безопасные зоны Instagram Reels (кадр 9:16)") +
                                                       QStringLiteral("\n") + zonesNote);
    m_shortsButton = toggle(icons::youtubeMark(), tr("Безопасные зоны YouTube Shorts (кадр 9:16)") +
                                                      QStringLiteral("\n") + zonesNote);
    m_guidesButton = toggle(icons::thirdsGrid(), tr("Направляющие: сетка третей (G)"));
    m_reelsButton->setChecked(m_settings->socialGuide == QLatin1String("reels"));
    m_shortsButton->setChecked(m_settings->socialGuide == QLatin1String("shorts"));
    m_guidesButton->setChecked(m_settings->guideThirds);

    auto *formatLabel = new QLabel(tr("Формат"));
    formatLabel->setObjectName(QStringLiteral("topLabel"));
    h->addWidget(formatLabel);
    h->addWidget(m_aspectCombo);
    h->addSpacing(10);
    h->addWidget(m_reelsButton);
    h->addWidget(m_shortsButton);
    h->addSpacing(4);
    h->addWidget(m_guidesButton);

    connect(m_aspectCombo, &QComboBox::currentIndexChanged, this, [this] {
        // Platform zones only make sense for 9:16.
        if (m_aspectCombo->currentData().toString() != QLatin1String("9:16")) {
            m_reelsButton->setChecked(false);
            m_shortsButton->setChecked(false);
        }
        updateGuides();
    });
    auto platformClicked = [this](QToolButton *self, QToolButton *other) {
        if (self->isChecked()) {
            other->setChecked(false);
            if (m_aspectCombo->currentData().toString() != QLatin1String("9:16")) {
                const QSignalBlocker block(m_aspectCombo);
                m_aspectCombo->setCurrentIndex(m_aspectCombo->findData(QStringLiteral("9:16")));
            }
        }
        updateGuides();
    };
    connect(m_reelsButton, &QToolButton::clicked, this, [this, platformClicked] { platformClicked(m_reelsButton, m_shortsButton); });
    connect(m_shortsButton, &QToolButton::clicked, this, [this, platformClicked] { platformClicked(m_shortsButton, m_reelsButton); });
    connect(m_guidesButton, &QToolButton::clicked, this, &ShootPage::updateGuides);
    return m_formatBar;
}

QWidget *ShootPage::formatBar() const
{
    return m_formatBar;
}

QWidget *ShootPage::buildPanel()
{
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);
    v->addWidget(buildSourceGroup());
    v->addWidget(buildRecordGroup());
    v->addWidget(buildCameraGroup());
    v->addWidget(buildAndroidGroup());
    v->addWidget(buildStreamGroup());
    v->addStretch(1);

    m_panel = new QScrollArea;
    m_panel->setObjectName(QStringLiteral("sidePanel"));
    m_panel->setWidget(content);
    m_panel->setWidgetResizable(true);
    m_panel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_panel->setFixedWidth(340);
    m_panel->setFrameShape(QFrame::NoFrame);
    return m_panel;
}

QGroupBox *ShootPage::buildSourceGroup()
{
    auto *g = new QGroupBox(tr("Источник"));
    auto *v = new QVBoxLayout(g);

    auto *row = new QHBoxLayout;
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_deviceCombo->setMinimumContentsLength(16);
    m_refreshButton = new QPushButton(tr("Найти"));
    m_refreshButton->setToolTip(tr("Искать телефоны: iPhone по USB и Wi-Fi, Android по USB"));
    row->addWidget(m_deviceCombo, 1);
    row->addWidget(m_refreshButton);
    v->addLayout(row);

    m_hostEdit = new QLineEdit(m_settings->manualHost);
    m_hostEdit->setPlaceholderText(tr("IP iPhone, например 192.168.1.23"));
    m_hostEdit->setVisible(false);
    v->addWidget(m_hostEdit);

    m_connectButton = new QPushButton(tr("Подключить"));
    m_connectButton->setObjectName(QStringLiteral("primaryButton"));
    v->addWidget(m_connectButton);

    m_startCameraButton = new QPushButton(tr("Запустить камеру на iPhone"));
    m_startCameraButton->setVisible(false);
    v->addWidget(m_startCameraButton);

    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_hintLabel = new QLabel;
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_statusLabel);
    v->addWidget(m_hintLabel);

    // Sound: the phone's microphone or an input of the PC's audio card.
    m_audioCheck = new QCheckBox(tr("Записывать звук"));
    m_audioCheck->setChecked(m_settings->recordAudio);
    m_audioCheck->setToolTip(tr("Дорожка AAC 48 кГц стерео, синхронно с видео."));
    m_audioSourceCombo = new QComboBox;
    m_audioSourceCombo->setToolTip(tr("С камеры — микрофон телефона (iPhone: включите в LensLink Camera "
                                      "«Send phone mic to OBS»; Android: включается автоматически).\n"
                                      "Остальные пункты — входы звуковой карты компьютера."));
    m_audioChannelsCombo = new QComboBox;
    m_audioChannelsCombo->addItem(tr("Стерео (каналы 1+2)"), QStringLiteral("stereo"));
    m_audioChannelsCombo->addItem(tr("Канал 1 (моно)"), QStringLiteral("first"));
    m_audioChannelsCombo->addItem(tr("Канал 2 (моно)"), QStringLiteral("second"));
    m_audioChannelsCombo->setCurrentIndex(std::max(0, m_audioChannelsCombo->findData(m_settings->audioChannels)));
    m_audioOffsetSpin = new QSpinBox;
    m_audioOffsetSpin->setRange(-1000, 1000);
    m_audioOffsetSpin->setSingleStep(5);
    m_audioOffsetSpin->setSuffix(tr(" мс"));
    m_audioOffsetSpin->setValue(m_settings->audioOffsetMs);
    m_audioOffsetSpin->setToolTip(tr("Сдвиг звука звуковой карты относительно видео. Если звук опережает "
                                     "картинку — увеличьте значение.\niPhone: часы синхронизируются "
                                     "автоматически. Android: задержку видео приходится подбирать."));
    m_audioMeter = new QProgressBar;
    m_audioMeter->setObjectName(QStringLiteral("audioMeter"));
    m_audioMeter->setRange(0, 100);
    m_audioMeter->setTextVisible(false);
    m_audioMeter->setFixedHeight(6);
    m_audioMeter->setToolTip(tr("Уровень сигнала на входе"));
    m_audioOffsetNs = int64_t(m_settings->audioOffsetMs) * 1'000'000;

    auto *af = new QFormLayout;
    af->setContentsMargins(0, 4, 0, 0);
    af->addRow(m_audioCheck);
    m_audioSourceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_audioSourceCombo->setMinimumContentsLength(12);
    af->addRow(m_audioSourceCombo); // full width: device names are long
    m_audioInputRows = new QWidget;
    auto *rows = new QFormLayout(m_audioInputRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->addRow(tr("Каналы"), m_audioChannelsCombo);
    rows->addRow(tr("Сдвиг звука"), m_audioOffsetSpin);
    rows->addRow(tr("Уровень"), m_audioMeter);
    af->addRow(m_audioInputRows);
    v->addLayout(af);
    fillAudioSources();

    connect(m_audioCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_settings->recordAudio = on;
        m_settings->save();
        applyAudioRouting();
    });
    connect(m_audioSourceCombo, &QComboBox::activated, this, [this] {
        m_settings->audioSource = m_audioSourceCombo->currentData().toString();
        m_settings->save();
        applyAudioRouting();
    });
    connect(m_audioChannelsCombo, &QComboBox::activated, this, [this] {
        m_settings->audioChannels = m_audioChannelsCombo->currentData().toString();
        m_settings->save();
        applyAudioRouting();
    });
    connect(m_audioOffsetSpin, &QSpinBox::valueChanged, this, [this](int ms) {
        m_audioOffsetNs = int64_t(ms) * 1'000'000;
        m_settings->audioOffsetMs = ms;
        m_settings->save();
    });
    m_meterTimer.setInterval(66);
    connect(&m_meterTimer, &QTimer::timeout, this, [this] {
        const float peak = m_audioInput.takePeak();
        // dBFS -60..0 -> 0..100
        const double db = peak > 0 ? 20.0 * std::log10(double(peak)) : -60.0;
        m_audioMeter->setValue(int(std::clamp((db + 60.0) / 60.0 * 100.0, 0.0, 100.0)));
        updateAudioInfo();
    });

    connect(m_refreshButton, &QPushButton::clicked, this, &ShootPage::refreshDevices);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_hostEdit->setVisible(m_deviceCombo->currentData().toString() == QLatin1String(kManualIpKey));
        updateSourceGroups();
    });
    connect(m_connectButton, &QPushButton::clicked, this, [this] {
        if (m_source->isRunning())
            disconnectSource();
        else
            connectSelected();
    });
    connect(m_hostEdit, &QLineEdit::returnPressed, this, &ShootPage::connectSelected);
    connect(m_startCameraButton, &QPushButton::clicked, this,
            [this] { sendCommand({{"cmd", "start_stream"}}); });
    return g;
}

QWidget *ShootPage::buildStreamGroup()
{
    // Collapsible: a header with an arrow and a one-line summary; the
    // details open on click (collapsed by default, state remembered).
    auto *section = new QWidget;
    auto *sv = new QVBoxLayout(section);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(4);
    m_streamHeader = new QToolButton;
    m_streamHeader->setObjectName(QStringLiteral("sectionHeader"));
    m_streamHeader->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_streamHeader->setCheckable(true);
    m_streamHeader->setChecked(m_settings->streamInfoExpanded);
    m_streamHeader->setArrowType(m_settings->streamInfoExpanded ? Qt::DownArrow : Qt::RightArrow);
    m_streamHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_streamHeader->setCursor(Qt::PointingHandCursor);
    m_streamHeader->setFocusPolicy(Qt::NoFocus);
    auto *g = new QGroupBox;
    g->setObjectName(QStringLiteral("untitledGroup"));
    g->setVisible(m_settings->streamInfoExpanded);
    sv->addWidget(m_streamHeader);
    sv->addWidget(g);
    connect(m_streamHeader, &QToolButton::toggled, this, [this, g](bool open) {
        g->setVisible(open);
        m_streamHeader->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_settings->streamInfoExpanded = open;
        m_settings->save();
        updateStreamHeader();
    });
    auto *f = new QFormLayout(g);
    auto mk = [&](const QString &label) {
        auto *l = new QLabel(QStringLiteral("—"));
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        f->addRow(label, l);
        return l;
    };
    m_infoDevice = mk(tr("Устройство"));
    m_infoResolution = mk(tr("Разрешение"));
    m_infoFps = mk(tr("FPS"));
    m_infoCodec = mk(tr("Кодек"));
    m_infoBitrate = mk(tr("Битрейт"));
    m_infoMissing = mk(tr("Пропуски кадров"));
    m_infoLatency = mk(tr("Задержка"));
    m_infoDecoder = mk(tr("Декодер"));
    m_infoAudio = mk(tr("Звук"));
    updateStreamHeader();
    return section;
}

void ShootPage::updateStreamHeader()
{
    QString text = tr("Фактический поток");
    if (!m_streamHeader->isChecked() && m_stats.streaming && m_stats.width > 0)
        text += QStringLiteral("  ·  %1×%2 · %3 fps").arg(m_stats.width).arg(m_stats.height)
                    .arg(m_stats.fpsReceived, 0, 'f', 0);
    m_streamHeader->setText(text);
}

QGroupBox *ShootPage::buildCameraGroup()
{
    m_cameraGroup = new QGroupBox(tr("Камера"));
    auto *f = new QFormLayout(m_cameraGroup);

    m_formatCombo = new QComboBox;
    f->addRow(tr("Режим"), m_formatCombo);
    m_lensCombo = new QComboBox;
    f->addRow(tr("Объектив"), m_lensCombo);

    auto *zoomRow = new QHBoxLayout;
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(100, 1000);
    m_zoomLabel = new QLabel(QStringLiteral("1.0×"));
    m_zoomLabel->setMinimumWidth(40);
    zoomRow->addWidget(m_zoomSlider, 1);
    zoomRow->addWidget(m_zoomLabel);
    f->addRow(tr("Зум"), zoomRow);

    auto *biasRow = new QHBoxLayout;
    m_biasSlider = new QSlider(Qt::Horizontal);
    m_biasSlider->setRange(-30, 30);
    m_biasLabel = new QLabel(QStringLiteral("0.0 EV"));
    m_biasLabel->setMinimumWidth(52);
    biasRow->addWidget(m_biasSlider, 1);
    biasRow->addWidget(m_biasLabel);
    f->addRow(tr("Экспокоррекция"), biasRow);

    m_lockFocus = new QCheckBox(tr("Фокус"));
    m_lockExposure = new QCheckBox(tr("Экспозиция"));
    m_lockWb = new QCheckBox(tr("Баланс белого"));
    auto *locks = new QVBoxLayout;
    locks->addWidget(m_lockFocus);
    locks->addWidget(m_lockExposure);
    locks->addWidget(m_lockWb);
    f->addRow(tr("Зафиксировать"), locks);

    connect(m_formatCombo, &QComboBox::activated, this, [this](int) {
        const QStringList parts = m_formatCombo->currentData().toString().split(QLatin1Char('@'));
        if (parts.size() != 2)
            return;
        m_settings->streamResolution = parts[0];
        m_settings->streamFps = parts[1].toInt();
        m_settings->save();
        applySourceOptions();
        sendCommand({{"cmd", "set_format"}, {"resolution", parts[0]}, {"fps", parts[1].toInt()}});
    });
    connect(m_lensCombo, &QComboBox::activated, this, [this](int) {
        sendCommand({{"cmd", "selectLens"}, {"label", m_lensCombo->currentText()}});
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_zoomLabel->setText(QStringLiteral("%1×").arg(v / 100.0, 0, 'f', 1));
        if (!m_updatingCamera)
            sendCommand({{"cmd", "zoom"}, {"value", v / 100.0}});
    });
    connect(m_biasSlider, &QSlider::valueChanged, this, [this](int v) {
        m_biasLabel->setText(QStringLiteral("%1 EV").arg(v / 10.0, 0, 'f', 1));
        if (!m_updatingCamera)
            sendCommand({{"cmd", "exposure_bias"}, {"value", v / 10.0}});
    });
    auto lock = [this](QCheckBox *box, const char *target) {
        connect(box, &QCheckBox::toggled, this, [this, target](bool on) {
            if (!m_updatingCamera)
                sendCommand({{"cmd", "lock"}, {"target", QLatin1String(target)}, {"on", on}});
        });
    };
    lock(m_lockFocus, "focus");
    lock(m_lockExposure, "exposure");
    lock(m_lockWb, "whiteBalance");

    m_cameraGroup->setEnabled(false);
    m_cameraGroup->setToolTip(tr("Настройки появятся, когда iPhone начнёт передавать видео"));
    return m_cameraGroup;
}

QGroupBox *ShootPage::buildAndroidGroup()
{
    m_androidGroup = new QGroupBox(tr("Камера"));
    auto *f = new QFormLayout(m_androidGroup);

    m_androidFacing = new QComboBox;
    m_androidFacing->addItem(tr("Основная"), QStringLiteral("back"));
    m_androidFacing->addItem(tr("Фронтальная"), QStringLiteral("front"));
    m_androidFacing->addItem(tr("Внешняя"), QStringLiteral("external"));
    f->addRow(tr("Камера"), m_androidFacing);

    m_androidMode = new QComboBox;
    for (const auto &m : {std::pair{"1920x1080@60", "1080p · 60 fps"}, std::pair{"1920x1080@30", "1080p · 30 fps"},
                          std::pair{"1280x720@60", "720p · 60 fps"}, std::pair{"1280x720@30", "720p · 30 fps"},
                          std::pair{"3840x2160@30", "4K · 30 fps"}})
        m_androidMode->addItem(QString::fromUtf8(m.second), QString::fromLatin1(m.first));
    f->addRow(tr("Режим"), m_androidMode);

    m_androidBitrate = new QComboBox;
    for (int mbps : {10, 20, 40, 60})
        m_androidBitrate->addItem(tr("%1 Мбит/с").arg(mbps), mbps);
    f->addRow(tr("Битрейт"), m_androidBitrate);

    m_androidCodec = new QComboBox;
    m_androidCodec->addItem(QStringLiteral("H.264"), QStringLiteral("h264"));
    m_androidCodec->addItem(QStringLiteral("H.265 (HEVC)"), QStringLiteral("h265"));
    f->addRow(tr("Кодек"), m_androidCodec);

    auto *note = new QLabel(tr("Изменение перезапускает камеру телефона. Если телефон не умеет 60 fps "
                               "в выбранном режиме, используется 30 fps."));
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("hintLabel"));
    f->addRow(note);

    auto select = [](QComboBox *c, const QVariant &v) { c->setCurrentIndex(std::max(0, c->findData(v))); };
    select(m_androidFacing, m_settings->androidFacing);
    select(m_androidMode, m_settings->androidMode);
    select(m_androidBitrate, m_settings->androidBitrate);
    select(m_androidCodec, m_settings->androidCodec);

    for (QComboBox *c : {m_androidFacing, m_androidMode, m_androidBitrate, m_androidCodec}) {
        connect(c, &QComboBox::activated, this, [this] {
            m_settings->androidFacing = m_androidFacing->currentData().toString();
            m_settings->androidMode = m_androidMode->currentData().toString();
            m_settings->androidBitrate = m_androidBitrate->currentData().toInt();
            m_settings->androidCodec = m_androidCodec->currentData().toString();
            m_settings->save();
            applyAndroidOptions(true);
        });
    }
    m_androidGroup->setVisible(false);
    return m_androidGroup;
}

QGroupBox *ShootPage::buildRecordGroup()
{
    auto *g = new QGroupBox(tr("Запись"));
    auto *v = new QVBoxLayout(g);

    auto *f = new QFormLayout;
    auto *folderRow = new QHBoxLayout;
    m_folderLabel = new QLabel;
    m_folderLabel->setMinimumWidth(60);
    m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *folderButton = new QPushButton(tr("Изменить…"));
    folderRow->addWidget(m_folderLabel, 1);
    folderRow->addWidget(folderButton);
    f->addRow(tr("Папка"), folderRow);

    m_projectEdit = new QLineEdit(m_settings->project);
    f->addRow(tr("Проект"), m_projectEdit);

    m_countdownCombo = new QComboBox;
    m_countdownCombo->addItem(tr("Без таймера"), 0);
    m_countdownCombo->addItem(tr("3 секунды"), 3);
    m_countdownCombo->addItem(tr("5 секунд"), 5);
    m_countdownCombo->addItem(tr("10 секунд"), 10);
    m_countdownCombo->setCurrentIndex(std::max(0, m_countdownCombo->findData(m_settings->countdownSeconds)));
    f->addRow(tr("Таймер"), m_countdownCombo);
    v->addLayout(f);

    m_nextNameLabel = new QLabel;
    m_nextNameLabel->setWordWrap(true);
    m_nextNameLabel->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_nextNameLabel);

    m_recButton = new QPushButton(tr("● REC  (Space)"));
    m_recButton->setObjectName(QStringLiteral("recButton"));
    m_recButton->setMinimumHeight(44);
    v->addWidget(m_recButton);

    m_durationLabel = new QLabel(formatDuration(0));
    m_durationLabel->setAlignment(Qt::AlignCenter);
    m_durationLabel->setObjectName(QStringLiteral("durationLabel"));
    v->addWidget(m_durationLabel);

    m_savedLabel = new QLabel;
    m_savedLabel->setWordWrap(true);
    m_savedLabel->setObjectName(QStringLiteral("hintLabel"));
    m_savedLabel->setVisible(false);
    v->addWidget(m_savedLabel);

    m_diskLabel = new QLabel;
    m_diskLabel->setObjectName(QStringLiteral("hintLabel"));
    v->addWidget(m_diskLabel);

    connect(folderButton, &QPushButton::clicked, this, &ShootPage::chooseFolder);
    connect(m_projectEdit, &QLineEdit::editingFinished, this, [this] {
        m_settings->project = TakeNamer::sanitize(m_projectEdit->text());
        m_projectEdit->setText(m_settings->project);
        m_settings->save();
        updateNextName();
    });
    connect(m_countdownCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_settings->countdownSeconds = m_countdownCombo->currentData().toInt();
        m_settings->save();
    });
    connect(m_recButton, &QPushButton::clicked, this, &ShootPage::toggleRecording);
    return g;
}

// ---------------------------------------------------------------- source

void ShootPage::refreshDevices()
{
    m_refreshButton->setEnabled(false);
    m_refreshButton->setText(tr("Поиск…"));
    m_discovery->scan();
    // Sound-card inputs, off the UI thread.
    QPointer<ShootPage> self(this);
    std::thread([self] {
        const QList<AudioInputDevice> inputs = listAudioInputs();
        QMetaObject::invokeMethod(qApp, [self, inputs] {
            if (!self)
                return;
            self->m_inputDevices = inputs;
            self->fillAudioSources();
            self->applyAudioRouting();
        }, Qt::QueuedConnection);
    }).detach();
}

void ShootPage::fillAudioSources()
{
    const QSignalBlocker block(m_audioSourceCombo);
    m_audioSourceCombo->clear();
    m_audioSourceCombo->addItem(tr("С камеры (микрофон телефона)"), QStringLiteral("camera"));
    for (const AudioInputDevice &d : m_inputDevices)
        m_audioSourceCombo->addItem(tr("Звуковая карта: %1").arg(d.name), QStringLiteral("input:") + d.id);
    int idx = m_audioSourceCombo->findData(m_settings->audioSource);
    if (idx < 0 && m_settings->audioSource.startsWith(QLatin1String("input:"))) {
        // Remembered device not present (yet): keep it selectable.
        m_audioSourceCombo->addItem(tr("Звуковая карта (не найдена)"), m_settings->audioSource);
        idx = m_audioSourceCombo->count() - 1;
    }
    m_audioSourceCombo->setCurrentIndex(std::max(0, idx));
}

void ShootPage::applyAudioRouting()
{
    const bool on = m_settings->recordAudio;
    const QString src = m_settings->audioSource;
    const bool input = on && src.startsWith(QLatin1String("input:"));
    m_audioSourceCombo->setEnabled(on && m_recorder->state() == Recorder::State::Idle);
    m_audioInputRows->setVisible(input);
    m_recorder->setAudioOrigin(input ? AudioChunk::Origin::PcInput : AudioChunk::Origin::Phone);

    // Sound-card capture runs while selected, so the level meter works and
    // the take's pre-roll has audio too.
    const QString id = src.mid(6);
    const InputChannels channels = m_settings->audioChannels == QLatin1String("first")    ? InputChannels::First
                                   : m_settings->audioChannels == QLatin1String("second") ? InputChannels::Second
                                                                                         : InputChannels::Stereo;
    if (!input) {
        m_audioInput.stop();
        m_meterTimer.stop();
        m_audioMeter->setValue(0);
    } else if (!m_audioInput.isRunning() || m_audioInputId != id || m_audioInputChannels != channels) {
        AudioInputDevice device;
        for (const AudioInputDevice &d : m_inputDevices)
            if (d.id == id)
                device = d;
        if (device.id.isEmpty()) {
            m_audioInput.stop();
            device = {id, id}; // not enumerated (yet): try to open it anyway
        }
        m_audioInputId = id;
        m_audioInputChannels = channels;
        m_audioInput.start(device, channels, [this](const int16_t *samples, int frames, int64_t local) {
            // Capture thread: place the sound on the active camera's timeline.
            m_lastInputChunkNs = local;
            CameraSource *source = m_audioClockSource.load();
            int64_t pts = 0;
            if (!source || !source->clock().map(local, &pts))
                return; // no video yet: nothing to sync to
            AudioChunk chunk;
            chunk.origin = AudioChunk::Origin::PcInput;
            chunk.samples = std::make_shared<std::vector<int16_t>>(samples, samples + size_t(frames) * 2);
            chunk.sampleRate = 48000;
            chunk.channels = 2;
            chunk.ptsNs = pts + m_audioOffsetNs.load();
            m_recorder->onAudio(chunk);
        });
        m_meterTimer.start();
    }
    applyAndroidOptions(true); // Android sends its mic only for "camera"
    updateAudioInfo();
}

void ShootPage::onDevicesFound(const QList<DeviceInfo> &devices)
{
    m_refreshButton->setEnabled(true);
    m_refreshButton->setText(tr("Найти"));

    const QString previous = m_deviceCombo->count() ? m_deviceCombo->currentData().toString()
                                                    : m_settings->deviceKey;
    m_deviceCombo->clear();
    DeviceInfo autoIphone;
    m_deviceCombo->addItem(autoIphone.displayName(), autoIphone.key());
    DeviceInfo autoAndroid;
    autoAndroid.kind = SourceKind::Android;
    m_deviceCombo->addItem(autoAndroid.displayName(), autoAndroid.key());
    for (const DeviceInfo &d : devices)
        m_deviceCombo->addItem(d.displayName(), d.key());
    m_deviceCombo->addItem(tr("Wi-Fi — ввести IP вручную…"), QLatin1String(kManualIpKey));

    int idx = m_deviceCombo->findData(previous);
    if (idx < 0 && previous.startsWith(QLatin1String("wifi:"))) {
        // A remembered Wi-Fi phone that didn't answer the browse: offer it anyway.
        DeviceInfo d = DeviceInfo::fromKey(previous);
        m_deviceCombo->insertItem(m_deviceCombo->count() - 1, d.displayName(), d.key());
        idx = m_deviceCombo->findData(previous);
    }
    m_deviceCombo->setCurrentIndex(std::max(0, idx));
    m_hostEdit->setVisible(m_deviceCombo->currentData().toString() == QLatin1String(kManualIpKey));
    updateSourceGroups();
}

void ShootPage::applySourceOptions()
{
    lenslink::SourceOptions o;
    o.autoStartCamera = m_settings->autoStartCamera;
    o.hardwareDecode = m_settings->hardwareDecode;
    o.resolution = m_settings->streamResolution;
    o.fps = m_settings->streamFps;
    o.codec = m_settings->streamCodec;
    o.quality = m_settings->streamQuality;
    m_lenslink->setOptions(o);
    applyAndroidOptions(false);
}

void ShootPage::applyAndroidOptions(bool restartIfRunning)
{
    android::AndroidOptions o;
    o.facing = m_settings->androidFacing;
    const QStringList mode = m_settings->androidMode.split(QLatin1Char('@'));
    const QStringList size = mode.value(0).split(QLatin1Char('x'));
    o.width = size.value(0).toInt() > 0 ? size.value(0).toInt() : 1920;
    o.height = size.value(1).toInt() > 0 ? size.value(1).toInt() : 1080;
    o.fps = mode.value(1).toInt() > 0 ? mode.value(1).toInt() : 60;
    o.bitrateMbps = m_settings->androidBitrate;
    o.codec = m_settings->androidCodec;
    o.hardwareDecode = m_settings->hardwareDecode;
    o.audio = m_settings->recordAudio && !m_settings->audioSource.startsWith(QLatin1String("input:"));
    m_android->setOptions(o);
    if (restartIfRunning && m_source == m_android && m_android->isRunning()) {
        if (isRecording())
            return; // applied on the next connection
        m_android->start(m_activeDevice);
    }
}

void ShootPage::watchSource(CameraSource *source)
{
    // Only the active source drives the UI; the idle one may still report
    // its own "disconnected" state.
    connect(source, &CameraSource::statusChanged, this, [this, source](const SourceStatus &st) {
        if (source == m_source)
            onStatus(st);
    });
    connect(source, &CameraSource::statsChanged, this, [this, source](const StreamStats &st) {
        if (source == m_source)
            onStats(st);
    });
    connect(source, &CameraSource::cameraStateChanged, this, [this, source](const QJsonObject &state) {
        if (source == m_source)
            onCameraState(state);
    });
}

void ShootPage::updateSourceGroups()
{
    QString key = m_deviceCombo ? m_deviceCombo->currentData().toString() : QString();
    if (m_source && m_source->isRunning())
        key = m_activeDevice.key();
    const bool android = key.startsWith(QLatin1String("android:"));
    m_cameraGroup->setVisible(!android);
    m_androidGroup->setVisible(android);
}

void ShootPage::connectSelected()
{
    QString key = m_deviceCombo->currentData().toString();
    if (key == QLatin1String(kManualIpKey)) {
        const QString host = m_hostEdit->text().trimmed();
        if (host.isEmpty()) {
            m_hostEdit->setFocus();
            return;
        }
        m_settings->manualHost = host;
        key = QStringLiteral("wifi:") + host;
    }
    m_settings->deviceKey = key;
    m_settings->save();
    applySourceOptions();
    DeviceInfo device = DeviceInfo::fromKey(key);
    m_lenslink->stop();
    m_android->stop();
    m_source = device.kind == SourceKind::Android ? static_cast<CameraSource *>(m_android)
                                                  : static_cast<CameraSource *>(m_lenslink);
    m_activeDevice = device;
    m_audioClockSource = m_source;
    m_source->start(device);
    updateSourceGroups();
    m_connectButton->setText(tr("Отключить"));
    m_deviceCombo->setEnabled(false);
    m_hostEdit->setEnabled(false);
}

void ShootPage::connectTo(const QString &deviceKey)
{
    DeviceInfo d = DeviceInfo::fromKey(deviceKey);
    int idx = m_deviceCombo->findData(d.key());
    if (idx < 0) {
        m_deviceCombo->insertItem(std::max(0, m_deviceCombo->count() - 1), d.displayName(), d.key());
        idx = m_deviceCombo->findData(d.key());
    }
    m_deviceCombo->setCurrentIndex(idx);
    if (!m_source->isRunning())
        connectSelected();
}

void ShootPage::disconnectSource()
{
    if (isRecording()) {
        const auto answer = QMessageBox::question(
            this, tr("Идёт запись"), tr("Отключение источника остановит запись. Продолжить?"));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_source->stop();
    onCameraState({});
    m_connectButton->setText(tr("Подключить"));
    m_deviceCombo->setEnabled(true);
    m_hostEdit->setEnabled(true);
}

void ShootPage::onStatus(const SourceStatus &status)
{
    m_status = status;
    QString color;
    switch (status.state) {
    case SourceState::Streaming:
        color = QStringLiteral("#4ade80");
        break;
    case SourceState::Error:
        color = QStringLiteral("#ff5c6c");
        break;
    case SourceState::Idle:
        color = QStringLiteral("#a9a3c7");
        break;
    default:
        color = QStringLiteral("#fbbf24");
        break;
    }
    const QString msg = status.message.isEmpty() ? tr("Не подключено") : status.message;
    m_statusLabel->setText(QStringLiteral("<span style='color:%1'>●</span> %2").arg(color, msg.toHtmlEscaped()));
    m_hintLabel->setText(status.hint);
    m_hintLabel->setVisible(!status.hint.isEmpty());
    m_startCameraButton->setVisible(status.state == SourceState::Standby);
    if (status.state != SourceState::Streaming) {
        QString text = msg;
        if (!status.hint.isEmpty())
            text += QStringLiteral("\n\n") + status.hint;
        m_preview->setPlaceholder(text);
    }
    m_infoDevice->setText(status.deviceName.isEmpty() ? QStringLiteral("—") : status.deviceName);
    emitSummary();
}

void ShootPage::onStats(const StreamStats &st)
{
    m_stats = st;
    updateStreamHeader();
    if (!st.streaming || st.width == 0) {
        for (QLabel *l : {m_infoResolution, m_infoFps, m_infoCodec, m_infoBitrate, m_infoMissing,
                          m_infoLatency, m_infoDecoder})
            l->setText(QStringLiteral("—"));
        updateAudioInfo();
        emitSummary();
        return;
    }
    updateAudioInfo();
    m_infoResolution->setText(QStringLiteral("%1×%2").arg(st.width).arg(st.height));
    QString fps = QStringLiteral("%1").arg(st.fpsReceived, 0, 'f', 1);
    if (st.nominalFps > 0)
        fps += tr(" (заявлено %1)").arg(st.nominalFps, 0, 'g', 4);
    const bool lowFps = st.nominalFps > 0 && st.fpsReceived < st.nominalFps * 0.9;
    m_infoFps->setText(lowFps ? QStringLiteral("<span style='color:#fbbf24'>%1</span>").arg(fps) : fps);
    m_infoCodec->setText(st.codec);
    m_infoBitrate->setText(tr("%1 Мбит/с").arg(st.mbps, 0, 'f', 1));
    m_infoMissing->setText(st.framesMissing == 0
                               ? QStringLiteral("0")
                               : tr("%1 из %2").arg(st.framesMissing).arg(st.framesReceived + st.framesMissing));
    m_infoLatency->setText(st.latencyMs >= 0 ? tr("~%1 мс").arg(qRound(st.latencyMs)) : QStringLiteral("—"));
    m_infoDecoder->setText(st.decoder.isEmpty() ? QStringLiteral("—") : st.decoder);
    emitSummary();
}

void ShootPage::updateAudioInfo()
{
    QString text;
    const bool input = m_settings->recordAudio && m_settings->audioSource.startsWith(QLatin1String("input:"));
    if (!m_settings->recordAudio) {
        text = tr("не записывается");
    } else if (input) {
        const QString error = m_audioInput.lastError();
        const bool flowing = int64_t(monotonicNs()) - m_lastInputChunkNs.load() < 1'000'000'000LL;
        if (!error.isEmpty())
            text = QStringLiteral("<span style='color:#ff5c6c'>%1</span>").arg(error.toHtmlEscaped());
        else if (!flowing)
            text = QStringLiteral("<span style='color:#fbbf24'>%1</span>").arg(tr("звуковая карта молчит"));
        else if (!m_stats.streaming)
            text = tr("звуковая карта: ждём видео для синхронизации");
        else
            text = tr("звуковая карта, 48 кГц, стерео");
    } else if (m_stats.audio) {
        text = tr("%1 кГц, %2").arg(m_stats.audioSampleRate / 1000.0, 0, 'g', 3)
                   .arg(m_stats.audioChannels >= 2 ? tr("стерео") : tr("моно"));
    } else if (m_stats.streaming) {
        text = m_activeDevice.kind == SourceKind::Android
                   ? QStringLiteral("<span style='color:#fbbf24'>%1</span>").arg(tr("не поступает"))
                   : QStringLiteral("<span style='color:#fbbf24'>%1</span>")
                         .arg(tr("не поступает — включите «Send phone mic to OBS» в LensLink Camera"));
    } else {
        text = QStringLiteral("—");
    }
    m_infoAudio->setText(text);
}

void ShootPage::onCameraState(const QJsonObject &state)
{
    m_cameraState = state;
    m_cameraGroup->setEnabled(!state.isEmpty());
    if (state.isEmpty())
        return;
    m_updatingCamera = true;

    // Format: every resolution x frame rate the active lens advertises.
    m_formatCombo->clear();
    const QJsonArray resolutions = state.value("resolutions").toArray();
    const QJsonArray rates = state.value("frameRates").toArray();
    const QString curRes = state.value("resolution").toString();
    const int curFps = state.value("fps").toInt();
    for (const QJsonValue &r : resolutions)
        for (const QJsonValue &f : rates)
            m_formatCombo->addItem(QStringLiteral("%1 · %2 fps").arg(r.toString()).arg(f.toInt()),
                                   QStringLiteral("%1@%2").arg(r.toString()).arg(f.toInt()));
    m_formatCombo->setCurrentIndex(m_formatCombo->findData(QStringLiteral("%1@%2").arg(curRes).arg(curFps)));
    m_formatCombo->setEnabled(m_formatCombo->count() > 0);

    m_lensCombo->clear();
    for (const QJsonValue &l : state.value("lenses").toArray())
        m_lensCombo->addItem(l.toString());
    m_lensCombo->setCurrentText(state.value("lens").toString());
    m_lensCombo->setEnabled(m_lensCombo->count() > 1);

    const double maxZoom = std::min(10.0, state.value("maxZoom").toDouble(1.0));
    m_zoomSlider->setRange(100, std::max(100, int(maxZoom * 100)));
    m_zoomSlider->setValue(int(std::lround(state.value("zoom").toDouble(1.0) * 100)));
    m_zoomSlider->setEnabled(maxZoom > 1.0);

    m_biasSlider->setValue(int(std::lround(state.value("exposureBias").toDouble() * 10)));

    // The tray "lock" command exists only on apps that report lensFactors
    // (docs/LENSLINK_PROTOCOL.md); hide what the phone can't do.
    const bool hasLock = state.contains("lensFactors");
    m_lockFocus->setVisible(hasLock);
    m_lockExposure->setVisible(hasLock);
    m_lockWb->setVisible(hasLock && state.value("supportsWhiteBalanceLock").toBool(true));
    m_lockFocus->setChecked(state.value("focusMode").toString() == QLatin1String("locked"));
    m_lockExposure->setChecked(state.value("exposureMode").toString() == QLatin1String("manual"));
    m_lockWb->setChecked(state.value("whiteBalanceMode").toString() == QLatin1String("locked"));

    m_updatingCamera = false;
}

void ShootPage::sendCommand(const QJsonObject &cmd)
{
    m_source->sendCommand(cmd);
}

// -------------------------------------------------------------- guides

void ShootPage::updateGuides()
{
    GuideSettings g;
    g.enabled = true;
    g.thirds = m_guidesButton->isChecked();
    const QString aspect = m_aspectCombo->currentData().toString();
    g.cropAspect = aspectValue(aspect);
    const QString social = m_reelsButton->isChecked()    ? QStringLiteral("reels")
                           : m_shortsButton->isChecked() ? QStringLiteral("shorts")
                                                         : QStringLiteral("none");
    g.social = social == QLatin1String("reels")    ? SocialGuide::InstagramReels
               : social == QLatin1String("shorts") ? SocialGuide::YouTubeShorts
                                                   : SocialGuide::None;
    m_preview->setGuides(g);

    m_settings->socialGuide = social;
    m_settings->guideThirds = g.thirds;
    m_settings->cropAspect = aspect;
    m_settings->save();
}

void ShootPage::toggleGuides()
{
    m_guidesButton->toggle();
    updateGuides();
}

// ----------------------------------------------------------- recording

bool ShootPage::isRecording() const
{
    return m_recorder->state() != Recorder::State::Idle || m_countdownTimer.isActive();
}

void ShootPage::toggleRecording()
{
    if (m_countdownTimer.isActive()) {
        m_countdownTimer.stop();
        m_preview->setRecording(false);
        onRecorderState(m_recorder->state());
        return;
    }
    if (m_recorder->state() != Recorder::State::Idle) {
        m_recorder->stop();
        return;
    }
    if (!m_recorder->hasStream()) {
        QMessageBox::information(this, tr("Нет видео"),
                                 tr("Сначала подключите телефон: запись начинается, когда идёт видеопоток."));
        return;
    }
    const int countdown = m_countdownCombo->currentData().toInt();
    if (countdown > 0) {
        m_countdownLeft = countdown;
        m_preview->setRecording(true, tr("Старт через %1").arg(countdown));
        m_recButton->setText(tr("Отменить таймер"));
        m_countdownTimer.start();
        return;
    }
    startRecordingNow();
}

void ShootPage::startRecordingNow()
{
    TakeNamer namer;
    namer.root = m_settings->rootFolder;
    namer.project = m_settings->project;
    namer.pattern = m_settings->namePattern;
    const QString path = namer.nextTakePath();
    QString error;
    if (!m_recorder->start(path, &error, m_preview->rotation(), m_settings->recordAudio)) {
        onRecorderState(m_recorder->state());
        QMessageBox::warning(this, tr("Запись не началась"), error);
        return;
    }
    m_currentTake = path;
    m_savedLabel->setVisible(false);
    m_nextNameLabel->setText(tr("Записывается: %1").arg(breakable(QFileInfo(path).fileName())));
}

void ShootPage::stopRecordingAndWait()
{
    m_countdownTimer.stop();
    if (m_recorder->state() == Recorder::State::Idle)
        return;
    m_recorder->stop();
    // Finalizing (remux) takes well under a second per minute of video.
    QElapsedTimer t;
    t.start();
    while (m_recorder->state() != Recorder::State::Idle && t.elapsed() < 60000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void ShootPage::onRecorderState(Recorder::State state)
{
    switch (state) {
    case Recorder::State::Idle:
        m_recButton->setText(tr("● REC  (Space)"));
        m_recButton->setProperty("recording", false);
        m_preview->setRecording(false);
        m_recSummary.clear();
        updateNextName();
        break;
    case Recorder::State::WaitingKeyframe:
        m_recButton->setText(tr("■ STOP  (Space)"));
        m_recButton->setProperty("recording", true);
        m_preview->setRecording(true, QStringLiteral("REC …"));
        m_recSummary = tr("Начинаем запись…");
        m_nextNameLabel->setText(tr("Записывается: %1").arg(breakable(QFileInfo(m_currentTake).fileName())));
        break;
    case Recorder::State::Recording:
        m_recButton->setText(tr("■ STOP  (Space)"));
        m_recButton->setProperty("recording", true);
        m_recSummary = tr("● Запись");
        break;
    case Recorder::State::Finalizing:
        m_recButton->setText(tr("Сохранение…"));
        m_recSummary = tr("Сохранение дубля…");
        break;
    }
    m_recButton->setEnabled(state != Recorder::State::Finalizing);
    const bool idle = state == Recorder::State::Idle;
    m_audioCheck->setEnabled(idle);
    m_audioSourceCombo->setEnabled(idle && m_settings->recordAudio);
    m_audioChannelsCombo->setEnabled(idle);
    m_rotateButton->setEnabled(idle);
    m_resetRotationButton->setEnabled(idle && m_preview->rotation() != 0);
    // Re-polish so the stylesheet picks up the "recording" property.
    m_recButton->style()->unpolish(m_recButton);
    m_recButton->style()->polish(m_recButton);
    m_deviceCombo->setEnabled(!m_source->isRunning());
    emitSummary();
}

void ShootPage::onRecordProgress(qint64 durationMs, qint64 bytes)
{
    const QString d = formatDuration(durationMs);
    m_durationLabel->setText(QStringLiteral("%1 · %2").arg(d, formatBytes(bytes)));
    m_preview->setRecording(true, QStringLiteral("REC  %1").arg(d));
    m_recSummary = tr("● Запись %1").arg(d);
    emitSummary();
}

void ShootPage::onTakeFinished(const QString &path, qint64 durationMs, const QString &warning)
{
    m_durationLabel->setText(formatDuration(durationMs));
    m_savedLabel->setText(tr("✓ Сохранено: %1").arg(breakable(QFileInfo(path).fileName())));
    m_savedLabel->setToolTip(QDir::toNativeSeparators(path));
    m_savedLabel->setVisible(true);
    emit takeSaved(path);
    if (!warning.isEmpty())
        QMessageBox::warning(this, tr("Запись прервана"), warning + QStringLiteral("\n\n") + QDir::toNativeSeparators(path));
    updateDiskInfo();
}

void ShootPage::onRecordFailed(const QString &error)
{
    QMessageBox::critical(this, tr("Ошибка записи"), error);
}

void ShootPage::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка для записей"), m_settings->rootFolder);
    if (dir.isEmpty())
        return;
    QFileInfo fi(dir);
    if (!fi.isWritable()) {
        QMessageBox::warning(this, tr("Нет доступа"),
                             tr("В папку %1 нельзя записывать. Выберите другую.").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    m_settings->rootFolder = dir;
    m_settings->save();
    emit rootFolderChanged();
    updateNextName();
    updateDiskInfo();
}

void ShootPage::updateNextName()
{
    const QString folder = QDir::toNativeSeparators(m_settings->rootFolder);
    m_folderLabel->setText(m_folderLabel->fontMetrics().elidedText(folder, Qt::ElideMiddle, 170));
    m_folderLabel->setToolTip(folder);
    TakeNamer namer;
    namer.root = m_settings->rootFolder;
    namer.project = m_settings->project;
    namer.pattern = m_settings->namePattern;
    m_nextNameLabel->setText(tr("Следующий дубль: %1").arg(breakable(QFileInfo(namer.nextTakePath()).fileName())));
}

void ShootPage::updateDiskInfo()
{
    QString dir = m_settings->rootFolder;
    while (!dir.isEmpty() && !QDir(dir).exists())
        dir = QFileInfo(dir).absolutePath() == dir ? QString() : QFileInfo(dir).absolutePath();
    QStorageInfo storage(dir.isEmpty() ? QDir::homePath() : dir);
    if (!storage.isValid()) {
        m_diskLabel->setText(tr("Свободно: неизвестно"));
        return;
    }
    const qint64 free = storage.bytesAvailable();
    // H.264 1080p60 from LensLink is ~8-30 Mbit/s; estimate with 25.
    const qint64 minutes = free / (25LL * 1000 * 1000 / 8 * 60);
    QString text = tr("Свободно %1 (≈ %2 мин записи)").arg(formatBytes(free)).arg(minutes);
    if (free < Recorder::kMinFreeToStart)
        text = QStringLiteral("<span style='color:#ff6b6b'>%1</span>").arg(text);
    m_diskLabel->setText(text);
    m_diskSummary = tr("Диск: %1").arg(formatBytes(free));
    emitSummary();
}

void ShootPage::setControlsVisible(bool visible)
{
    m_panel->setVisible(visible);
}

QWidget *ShootPage::monitorWidget() const
{
    return m_preview;
}

QWidget *ShootPage::inspectorWidget() const
{
    return m_panel;
}

QString ShootPage::sessionFolderHint() const
{
    return m_settings->rootFolder;
}

void ShootPage::emitSummary()
{
    QString source = m_status.message.isEmpty() ? tr("Не подключено") : m_status.message;
    QString stream;
    if (m_stats.streaming && m_stats.width > 0)
        stream = QStringLiteral("%1×%2 · %3 fps").arg(m_stats.width).arg(m_stats.height).arg(m_stats.fpsReceived, 0, 'f', 1);
    emit statusSummary(source, stream, m_recSummary, m_diskSummary);
}

} // namespace otl
