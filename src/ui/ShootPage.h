// SPDX-License-Identifier: GPL-3.0-or-later
// "Съёмка": monitor, source selection, camera controls, guides, REC/STOP.
#pragma once

#include "camera/CameraSource.h"
#include "core/AppSettings.h"
#include "preview/PreviewWidget.h"
#include "recording/Recorder.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QScrollArea;
class QToolButton;

namespace otl {

class DeviceDiscovery;
namespace lenslink {
class LensLinkSource;
} // namespace lenslink
namespace android {
class AndroidSource;
} // namespace android

class ShootPage : public QWidget {
    Q_OBJECT
public:
    explicit ShootPage(AppSettings *settings, QWidget *parent = nullptr);
    ~ShootPage() override;

    bool isRecording() const;
    void toggleRecording();
    void stopRecordingAndWait();
    void setControlsVisible(bool visible); // compact fullscreen mode
    void shutdown();                       // stop source before teardown
    void connectTo(const QString &deviceKey); // e.g. "usb:" or "wifi:192.168.1.5"
    void rotateClockwise();   // +90°, also written into new takes
    void resetRotation();     // back to the camera's own orientation
    QString sessionFolderHint() const;
    QWidget *monitorWidget() const;
    QWidget *inspectorWidget() const;
    QWidget *formatBar() const; // frame format + social guides, for the top bar

signals:
    void takeSaved(const QString &path);
    void requestFullscreen();
    void rootFolderChanged();
    void statusSummary(const QString &source, const QString &stream, const QString &rec,
                       const QString &disk);

private:
    QWidget *buildPanel();
    QWidget *buildFormatBar();
    QGroupBox *buildSourceGroup();
    QGroupBox *buildStreamGroup();
    QGroupBox *buildCameraGroup();
    QGroupBox *buildAndroidGroup();
    void updateSourceGroups();
    void applyAndroidOptions(bool restartIfRunning);
    void watchSource(CameraSource *source);
    QGroupBox *buildFrameGroup();
    QGroupBox *buildRecordGroup();

    void refreshDevices();
    void onDevicesFound(const QList<DeviceInfo> &devices);
    void connectSelected();
    void disconnectSource();
    void onStatus(const SourceStatus &status);
    void onStats(const StreamStats &stats);
    void onCameraState(const QJsonObject &state);
    void onRecorderState(Recorder::State state);
    void onRecordProgress(qint64 durationMs, qint64 bytes);
    void onTakeFinished(const QString &path, qint64 durationMs, const QString &warning);
    void onRecordFailed(const QString &error);
    void startRecordingNow();
    void updateGuides();
    void updateDiskInfo();
    void updateNextName();
    void chooseFolder();
    void applySourceOptions();
    void sendCommand(const QJsonObject &cmd);
    void emitSummary();
    void buildMonitorButtons();
    void applyRotation(int degreesCw);

    AppSettings *m_settings;
    CameraSource *m_source = nullptr; // the active one of the two below
    lenslink::LensLinkSource *m_lenslink = nullptr;
    android::AndroidSource *m_android = nullptr;
    DeviceDiscovery *m_discovery = nullptr;
    DeviceInfo m_activeDevice;
    Recorder *m_recorder = nullptr;
    PreviewWidget *m_preview = nullptr;
    QScrollArea *m_panel = nullptr;

    // Source group
    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_startCameraButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;

    // Stream group
    QLabel *m_infoDevice = nullptr;
    QLabel *m_infoResolution = nullptr;
    QLabel *m_infoFps = nullptr;
    QLabel *m_infoCodec = nullptr;
    QLabel *m_infoBitrate = nullptr;
    QLabel *m_infoMissing = nullptr;
    QLabel *m_infoLatency = nullptr;
    QLabel *m_infoDecoder = nullptr;

    // Camera group
    QGroupBox *m_cameraGroup = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_lensCombo = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QSlider *m_biasSlider = nullptr;
    QLabel *m_biasLabel = nullptr;
    QCheckBox *m_lockFocus = nullptr;
    QCheckBox *m_lockExposure = nullptr;
    QCheckBox *m_lockWb = nullptr;
    QJsonObject m_cameraState;
    bool m_updatingCamera = false;

    // Android group
    QGroupBox *m_androidGroup = nullptr;
    QComboBox *m_androidFacing = nullptr;
    QComboBox *m_androidMode = nullptr;
    QComboBox *m_androidBitrate = nullptr;
    QComboBox *m_androidCodec = nullptr;

    // Frame group
    QWidget *m_formatBar = nullptr;
    QComboBox *m_aspectCombo = nullptr;
    QComboBox *m_socialCombo = nullptr;
    QSlider *m_cropPos = nullptr;
    QCheckBox *m_guidesEnabled = nullptr;
    QCheckBox *m_thirds = nullptr;
    QCheckBox *m_center = nullptr;
    QCheckBox *m_safe = nullptr;

    // Record group
    QLabel *m_folderLabel = nullptr;
    QLineEdit *m_projectEdit = nullptr;
    QLabel *m_nextNameLabel = nullptr;
    QComboBox *m_countdownCombo = nullptr;
    QPushButton *m_recButton = nullptr;
    QLabel *m_durationLabel = nullptr;
    QLabel *m_savedLabel = nullptr;
    QLabel *m_diskLabel = nullptr;

    QWidget *m_monitorButtons = nullptr;
    QToolButton *m_rotateButton = nullptr;
    QToolButton *m_resetRotationButton = nullptr;

    QTimer m_diskTimer;
    QTimer m_countdownTimer;
    int m_countdownLeft = 0;
    QString m_currentTake;
    SourceStatus m_status;
    StreamStats m_stats;
    QString m_recSummary;
    QString m_diskSummary;
};

} // namespace otl
