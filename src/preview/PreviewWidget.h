// SPDX-License-Identifier: GPL-3.0-or-later
// Preview Layer: shows decoded frames with OpenGL (YUV -> RGB on the GPU,
// aspect ratio kept, letterboxed) and draws composition guides on top.
// Guides are a UI overlay only — they never reach recorded/exported files.
#pragma once

#include "core/VideoTypes.h"

#include <QMutex>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <atomic>

namespace otl {

struct GuideSettings {
    bool thirds = true;
    bool center = false;
    bool safeArea = false;     // 90 % action-safe rectangle
    double cropAspect = 0;     // width / height of the target format, 0 = none
    double cropCenterX = 0.5;  // horizontal position of the crop window, 0..1
    double cropCenterY = 0.5;
    bool enabled = true;       // master switch for all overlays
};

class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions, public VideoFrameSink {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;

    void setGuides(const GuideSettings &g);
    GuideSettings guides() const { return m_guides; }
    void setPlaceholder(const QString &text);
    void setRecording(bool on, const QString &badge = {});

    // VideoFrameSink — any thread.
    void onFrame(const FramePtr &frame) override;
    void onStreamLost() override;

signals:
    void doubleClicked();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    QRectF videoRect(int frameW, int frameH) const;
    void uploadFrame(const AVFrame *f);
    void drawOverlays(QPainter &p, const QRectF &video);

    QMutex m_frameMutex;
    FramePtr m_pending;
    std::atomic<bool> m_updateQueued{false};
    FramePtr m_shown;

    QOpenGLShaderProgram m_program;
    GLuint m_tex[3] = {0, 0, 0};
    int m_texW[3] = {0, 0, 0};
    int m_texH[3] = {0, 0, 0};
    int m_layout = 0; // 0 = none, 1 = NV12, 2 = planar YUV420
    bool m_fullRange = false;
    bool m_glReady = false;

    GuideSettings m_guides;
    QString m_placeholder;
    bool m_recording = false;
    QString m_badge;
};

} // namespace otl
