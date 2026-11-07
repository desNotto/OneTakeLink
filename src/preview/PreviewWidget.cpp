// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreviewWidget.h"

#include "CropMath.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>

#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace otl {

namespace {

const char *kVertex = R"(
attribute vec2 pos;
attribute vec2 tex;
varying vec2 v;
void main() { v = tex; gl_Position = vec4(pos, 0.0, 1.0); }
)";

// BT.709 YCbCr -> RGB, limited or full range. NV12 carries U/V in the
// luminance/alpha channels of one texture; planar formats use two.
const char *kFragment = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
uniform float nv12;
uniform float fullRange;
void main() {
    float y = texture2D(texY, v).r;
    vec2 uv;
    if (nv12 > 0.5) {
        vec4 c = texture2D(texU, v);
        uv = vec2(c.r, c.a);
    } else {
        uv = vec2(texture2D(texU, v).r, texture2D(texV, v).r);
    }
    if (fullRange < 0.5) {
        y = (y - 16.0 / 255.0) * (255.0 / 219.0);
        uv = (uv - 128.0 / 255.0) * (255.0 / 224.0);
    } else {
        uv = uv - 0.5;
    }
    float r = y + 1.5748 * uv.y;
    float g = y - 0.1873 * uv.x - 0.4681 * uv.y;
    float b = y + 1.8556 * uv.x;
    gl_FragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}
)";

} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 180);
    m_placeholder = tr("Нет сигнала");
}

PreviewWidget::~PreviewWidget()
{
    makeCurrent();
    if (m_glReady)
        glDeleteTextures(3, m_tex);
    doneCurrent();
}

void PreviewWidget::setGuides(const GuideSettings &g)
{
    m_guides = g;
    update();
}

void PreviewWidget::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    update();
}

void PreviewWidget::setRecording(bool on, const QString &badge)
{
    m_recording = on;
    m_badge = badge;
    update();
}

void PreviewWidget::onFrame(const FramePtr &frame)
{
    {
        QMutexLocker lock(&m_frameMutex);
        m_pending = frame;
    }
    // Coalesce: one queued repaint at a time; a slow UI drops frames
    // instead of building a backlog (lowest latency).
    if (!m_updateQueued.exchange(true))
        QMetaObject::invokeMethod(this, [this] { m_updateQueued = false; update(); }, Qt::QueuedConnection);
}

void PreviewWidget::onStreamLost()
{
    {
        QMutexLocker lock(&m_frameMutex);
        m_pending.reset();
    }
    QMetaObject::invokeMethod(this, [this] {
        m_shown.reset();
        m_layout = 0;
        update();
    }, Qt::QueuedConnection);
}

void PreviewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertex);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragment);
    m_program.bindAttributeLocation("pos", 0);
    m_program.bindAttributeLocation("tex", 1);
    if (!m_program.link())
        qWarning("preview shader: %s", qPrintable(m_program.log()));
    glGenTextures(3, m_tex);
    for (GLuint t : m_tex) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_glReady = true;
}

void PreviewWidget::resizeGL(int, int) {}

QRectF PreviewWidget::videoRect(int frameW, int frameH) const
{
    if (frameW <= 0 || frameH <= 0)
        return {};
    const double w = width(), h = height();
    const double scale = std::min(w / frameW, h / frameH);
    const double vw = frameW * scale, vh = frameH * scale;
    return {(w - vw) / 2, (h - vh) / 2, vw, vh};
}

void PreviewWidget::uploadFrame(const AVFrame *f)
{
    const int fmt = f->format;
    int layout = 0;
    if (fmt == AV_PIX_FMT_NV12)
        layout = 1;
    else if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P)
        layout = 2;
    if (!layout) {
        m_layout = 0;
        return;
    }
    m_layout = layout;
    m_fullRange = fmt == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;

    const bool es = QOpenGLContext::currentContext()->isOpenGLES();
    auto upload = [&](int i, GLenum glFmt, int bpp, int w, int h, const uint8_t *data, int stride) {
        glBindTexture(GL_TEXTURE_2D, m_tex[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        std::vector<uint8_t> packed;
        if (stride != w * bpp) {
            if (es) {
                packed.resize(size_t(w) * bpp * h);
                for (int y = 0; y < h; ++y)
                    memcpy(packed.data() + size_t(y) * w * bpp, data + size_t(y) * stride, size_t(w) * bpp);
                data = packed.data();
            } else {
                glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bpp);
            }
        }
        if (m_texW[i] != w || m_texH[i] != h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GLint(glFmt), w, h, 0, glFmt, GL_UNSIGNED_BYTE, data);
            m_texW[i] = w;
            m_texH[i] = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, glFmt, GL_UNSIGNED_BYTE, data);
        }
        if (!es)
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    };

    const int cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
    upload(0, GL_LUMINANCE, 1, f->width, f->height, f->data[0], f->linesize[0]);
    if (layout == 1) {
        upload(1, GL_LUMINANCE_ALPHA, 2, cw, ch, f->data[1], f->linesize[1]);
    } else {
        upload(1, GL_LUMINANCE, 1, cw, ch, f->data[1], f->linesize[1]);
        upload(2, GL_LUMINANCE, 1, cw, ch, f->data[2], f->linesize[2]);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // Restore the defaults QPainter's GL engine relies on (glyph cache).
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void PreviewWidget::paintGL()
{
    FramePtr next;
    {
        QMutexLocker lock(&m_frameMutex);
        next = std::move(m_pending);
        m_pending.reset();
    }
    if (next) {
        uploadFrame(next.get());
        m_shown = std::move(next);
    }

    glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    QRectF video;
    if (m_shown && m_layout) {
        video = videoRect(m_shown->width, m_shown->height);
        const qreal dpr = devicePixelRatioF();
        // GL's origin is bottom-left.
        glViewport(GLint(video.x() * dpr), GLint((height() - video.bottom()) * dpr),
                   GLsizei(video.width() * dpr), GLsizei(video.height() * dpr));
        static const GLfloat verts[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
        m_program.bind();
        m_program.setUniformValue("texY", 0);
        m_program.setUniformValue("texU", 1);
        m_program.setUniformValue("texV", 2);
        m_program.setUniformValue("nv12", m_layout == 1 ? 1.0f : 0.0f);
        m_program.setUniformValue("fullRange", m_fullRange ? 1.0f : 0.0f);
        for (int i = 0; i < 3; ++i) {
            glActiveTexture(GL_TEXTURE0 + GLenum(i));
            glBindTexture(GL_TEXTURE_2D, m_tex[i]);
        }
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts + 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        for (int i = 2; i >= 0; --i) {
            glActiveTexture(GL_TEXTURE0 + GLenum(i));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        m_program.release();
        glViewport(0, 0, GLsizei(width() * devicePixelRatioF()), GLsizei(height() * devicePixelRatioF()));
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (video.isEmpty()) {
        p.setPen(QColor(150, 150, 160));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        p.setFont(f);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
    } else {
        drawOverlays(p, video);
    }
    if (m_recording) {
        const QRectF badge(12, 12, 150, 30);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(badge, 6, 6);
        p.setBrush(QColor(230, 40, 40));
        p.drawEllipse(QPointF(badge.left() + 16, badge.center().y()), 6, 6);
        p.setPen(Qt::white);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(badge.adjusted(30, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   m_badge.isEmpty() ? QStringLiteral("REC") : m_badge);
    }
}

void PreviewWidget::drawOverlays(QPainter &p, const QRectF &video)
{
    if (!m_guides.enabled)
        return;
    QRectF area = video;

    if (m_guides.cropAspect > 0) {
        const QRectF crop = CropMath::cropRect(video, m_guides.cropAspect, m_guides.cropCenterX,
                                               m_guides.cropCenterY);
        QPainterPath outside;
        outside.addRect(video);
        QPainterPath inside;
        inside.addRect(crop);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
        p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(crop);
        area = crop;
    }

    QPen guide(QColor(255, 255, 255, 110), 1.0);
    p.setPen(guide);
    if (m_guides.thirds) {
        for (int i = 1; i <= 2; ++i) {
            const qreal x = area.left() + area.width() * i / 3.0;
            const qreal y = area.top() + area.height() * i / 3.0;
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
    }
    if (m_guides.center) {
        p.setPen(QPen(QColor(255, 220, 80, 150), 1.0, Qt::DashLine));
        p.drawLine(QPointF(area.center().x(), area.top()), QPointF(area.center().x(), area.bottom()));
        p.drawLine(QPointF(area.left(), area.center().y()), QPointF(area.right(), area.center().y()));
    }
    if (m_guides.safeArea) {
        p.setPen(QPen(QColor(120, 200, 255, 160), 1.0, Qt::DotLine));
        const qreal mx = area.width() * 0.05, my = area.height() * 0.05;
        p.drawRect(area.adjusted(mx, my, -mx, -my));
    }
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    emit doubleClicked();
    e->accept();
}

} // namespace otl
