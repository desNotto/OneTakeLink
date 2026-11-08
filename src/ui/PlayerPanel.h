// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in take player for the Library: video view, timeline, play/pause,
// frame stepping. Wraps VideoPlayer + PreviewWidget.
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

namespace otl {

class PreviewWidget;
class VideoPlayer;

class PlayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlayerPanel(QWidget *parent = nullptr);
    ~PlayerPanel() override;

    void load(const QString &path, const QString &title, const QString &details);
    void unload(); // closes the file (Windows can't delete/rename it while open)
    QString currentPath() const { return m_path; }

    void togglePlay();
    void step(int frames);
    void toStart();

signals:
    void openExternally(const QString &path);

private:
    void setDuration(qint64 ms);
    void showPosition(qint64 ms);
    void setPlayingUi(bool playing);
    void setControlsEnabled(bool on);

    VideoPlayer *m_player = nullptr;
    PreviewWidget *m_view = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_time = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_details = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_prev = nullptr;
    QPushButton *m_play = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_external = nullptr;
    QString m_path;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool m_scrubbing = false;
    bool m_resumeAfterScrub = false;
    bool m_playing = false;
};

} // namespace otl
