/**
 * ============================================================================
 * CTSliceViewer - CT 切片查看器（替代 BrushCanvas）
 * ============================================================================
 * 这是对 Python BrushCanvas 的完全重写。
 *
 * Python 中的问题：
 *   BrushCanvas 继承 QLabel，用 QPixmap 显示图像。
 *   每次缩放/平移/滚动都要 CPU 重新生成 QPixmap → 非常慢。
 *
 * C++ 的方案：
 *   使用 QVTKOpenGLNativeWidget，VTK 在 GPU 上渲染。
 *   缩放/平移通过移动 VTK 相机实现，零开销。
 *   滚动通过改变 vtkImageReslice 的切片位置实现。
 *
 * QVTKOpenGLNativeWidget：
 *   VTK 提供的 Qt 小部件，内嵌 OpenGL 渲染窗口。
 *   VTK 的 3D/2D 渲染直接在这个小部件中进行。
 * ============================================================================
 */

#ifndef SPINEX_CT_SLICE_VIEWER_H
#define SPINEX_CT_SLICE_VIEWER_H

#include <QWidget>                         // Qt 基础小部件
#include <QVBoxLayout>                     // 垂直布局
#include <QLabel>                          // 标签（显示文字）
#include <QSlider>                         // 滑块
#include <QVTKOpenGLNativeWidget.h>        // VTK Qt 渲染小部件

#include "rendering/SliceRenderer.h"       // 切片渲染管线
#include "rendering/MaskOverlay.h"         // 掩膜叠加

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkGenericOpenGLRenderWindow.h>

/**
 * CTSliceViewer - 单个切面的 CT 查看器
 *
 * 每个 SharedViewport 包含 3 个 CTSliceViewer（轴位/矢状/冠状）
 */
class CTSliceViewer : public QWidget
{
    Q_OBJECT

public:
    explicit CTSliceViewer(
        SliceOrientation orientation,
        QWidget* parent = nullptr
    );
    ~CTSliceViewer();

    // 设置 CT 体数据
    void setImageData(vtkImageData* imageData);

    // 设置分割掩膜
    void setMaskData(vtkImageData* maskData);

    // 获取/设置当前切片
    int getSliceIndex() const;
    void setSliceIndex(int index);

    // 获取方向
    SliceOrientation getOrientation() const;

    // 窗宽窗位
    void setWindowLevel(double window, double level);

    // 获取渲染器（用于添加额外的 Actor）
    vtkRenderer* getRenderer();

    // 刷新显示
    void render();

// Qt 信号
Q_SIGNALS:
    // 切片改变信号（连接到其他视图同步）
    void sliceChanged(SliceOrientation orientation, int sliceIndex);

    // 鼠标位置信号（显示坐标和像素值）
    void mousePositionChanged(double x, double y, double z, double value);

private Q_SLOTS:
    // 滑块值改变
    void onSliderChanged(int value);

private:
    void setupUI();
    void setupVTK();

    // UI 组件
    QVTKOpenGLNativeWidget* vtkWidget_;    // VTK 渲染窗口
    QSlider* sliceSlider_;                 // 切片滑块
    QLabel* infoLabel_;                    // 信息标签（方向 + 切片号）

    // 渲染组件
    SliceRenderer sliceRenderer_;
    MaskOverlay maskOverlay_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;

    SliceOrientation orientation_;
};

#endif // SPINEX_CT_SLICE_VIEWER_H
