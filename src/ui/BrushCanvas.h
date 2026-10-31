/**
 * BrushCanvas — QLabel subclass with mouse-driven brush strokes + scroll slice.
 * Port of Python ui/brush_canvas.py
 */
#ifndef SPINEX_BRUSH_CANVAS_H
#define SPINEX_BRUSH_CANVAS_H

#include <QLabel>
#include <QString>
#include <QMouseEvent>
#include <QWheelEvent>

class BrushCanvas : public QLabel
{
    Q_OBJECT

public:
    explicit BrushCanvas(const QString& viewName, QWidget* parent = nullptr);

    void setBrushActive(bool active);
    bool isBrushActive() const { return brushActive_; }

    // Zoom (Ctrl + scroll)
    double zoomFactor() const { return zoomFactor_; }
    QPoint panOffset() const { return panOffset_; }
    void resetZoom();
    void setSourcePixmap(const QPixmap& pm);  // store original, apply zoom

    // Overlay lines (for Cobb angle etc.)
    struct OverlayLine {
        QPointF p1, p2;   // normalized 0..1 coordinates
        QColor color;
        float width = 2.0f;
    };
    void setOverlayLines(const QVector<OverlayLine>& lines);
    void clearOverlayLines();

Q_SIGNALS:
    void brushStroke(const QString& viewName, int cx, int cy);
    void brushReleased(const QString& viewName);
    void scrollSlice(const QString& viewName, int direction);
    void canvasResized(const QString& viewName);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString viewName_;
    bool brushActive_ = false;
    bool drawing_ = false;
    QVector<OverlayLine> overlayLines_;
    QPixmap sourcePixmap_;
    double zoomFactor_ = 1.0;
    QPoint panOffset_;       // pan offset in pixels
    bool panning_ = false;
    QPoint panStart_;
    void applyZoom();
};

#endif // SPINEX_BRUSH_CANVAS_H
