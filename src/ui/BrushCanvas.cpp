/**
 * BrushCanvas — implementation
 */
#include "BrushCanvas.h"
#include <QCursor>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <algorithm>

BrushCanvas::BrushCanvas(const QString& viewName, QWidget* parent)
    : QLabel(parent)
    , viewName_(viewName)
{
    setMouseTracking(true);
}

void BrushCanvas::setBrushActive(bool active)
{
    brushActive_ = active;
    if (active) {
        // Circle cursor
        int sz = 18;
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(2, 2, sz - 4, sz - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 200));
        int cx = sz / 2;
        p.drawEllipse(cx - 1, cx - 1, 3, 3);
        p.end();
        setCursor(QCursor(pm, sz / 2, sz / 2));
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void BrushCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && brushActive_) {
        drawing_ = true;
        Q_EMIT brushStroke(viewName_, event->pos().x(), event->pos().y());
        event->accept();
        return;
    }
    // Middle button → start panning
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

void BrushCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (brushActive_ && drawing_) {
        Q_EMIT brushStroke(viewName_, event->pos().x(), event->pos().y());
        event->accept();
        return;
    }
    if (panning_) {
        QPoint delta = event->pos() - panStart_;
        panStart_ = event->pos();
        panOffset_ += delta;
        applyZoom();
        event->accept();
        return;
    }
    QLabel::mouseMoveEvent(event);
}

void BrushCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && drawing_) {
        drawing_ = false;
        Q_EMIT brushReleased(viewName_);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        setCursor(brushActive_ ? Qt::CrossCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

void BrushCanvas::wheelEvent(QWheelEvent* event)
{
    int delta = event->angleDelta().y();
    if (delta != 0) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Ctrl + scroll = zoom
            double step = (delta > 0) ? 1.15 : (1.0 / 1.15);
            zoomFactor_ = std::clamp(zoomFactor_ * step, 0.3, 10.0);
            applyZoom();
            event->accept();
            return;
        }
        Q_EMIT scrollSlice(viewName_, delta > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QLabel::wheelEvent(event);
}

void BrushCanvas::setSourcePixmap(const QPixmap& pm)
{
    sourcePixmap_ = pm;
    applyZoom();
}

void BrushCanvas::resetZoom()
{
    zoomFactor_ = 1.0;
    panOffset_ = QPoint(0, 0);
    applyZoom();
}

void BrushCanvas::applyZoom()
{
    if (sourcePixmap_.isNull()) return;
    QSize canvasSize = size();
    if (canvasSize.width() < 10 || canvasSize.height() < 10) return;

    // Base: fit to canvas
    QSize baseSize = sourcePixmap_.size().scaled(canvasSize, Qt::KeepAspectRatio);
    QSize zoomed(static_cast<int>(baseSize.width() * zoomFactor_),
                 static_cast<int>(baseSize.height() * zoomFactor_));

    QPixmap scaled = sourcePixmap_.scaled(zoomed, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Apply pan offset: draw onto canvas-sized pixmap at offset position
    if (panOffset_.x() != 0 || panOffset_.y() != 0) {
        QPixmap canvas(canvasSize);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        int cx = (canvasSize.width() - scaled.width()) / 2 + panOffset_.x();
        int cy = (canvasSize.height() - scaled.height()) / 2 + panOffset_.y();
        p.drawPixmap(cx, cy, scaled);
        p.end();
        setPixmap(canvas);
    } else {
        setPixmap(scaled);
    }
}

void BrushCanvas::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    Q_EMIT canvasResized(viewName_);
}

void BrushCanvas::setOverlayLines(const QVector<OverlayLine>& lines)
{
    overlayLines_ = lines;
    update();  // trigger repaint
}

void BrushCanvas::clearOverlayLines()
{
    overlayLines_.clear();
    update();
}

void BrushCanvas::paintEvent(QPaintEvent* event)
{
    QLabel::paintEvent(event);  // draw the pixmap first

    if (overlayLines_.isEmpty()) return;

    // The pixmap is centered within the label (Qt::AlignCenter default)
    const QPixmap pm = pixmap();
    if (pm.isNull()) return;

    QSize pmScaled = pm.size();
    // Compute offset from centering
    int ox = (width() - pmScaled.width()) / 2;
    int oy = (height() - pmScaled.height()) / 2;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto& line : overlayLines_) {
        painter.setPen(QPen(line.color, line.width));
        QPointF a(ox + line.p1.x() * pmScaled.width(),
                  oy + line.p1.y() * pmScaled.height());
        QPointF b(ox + line.p2.x() * pmScaled.width(),
                  oy + line.p2.y() * pmScaled.height());
        painter.drawLine(a, b);
    }
}
