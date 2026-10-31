/**
 * ============================================================================
 * SliceRenderer - VTK 2D 切片渲染管线
 * ============================================================================
 * 替代 Python 中的 QLabel + QPixmap 方案。
 *
 * VTK 渲染管线（Pipeline）概念：
 *   数据源 → 过滤器 → 映射器 → Actor → 渲染器 → 窗口
 *
 * 对于 CT 切片：
 *   vtkImageData(CT体) → vtkImageReslice(切片提取)
 *     → vtkImageMapToWindowLevelColors(窗宽窗位)
 *     → vtkImageActor → vtkRenderer → QVTKOpenGLNativeWidget
 *
 * 性能优势：
 *   - 切片提取在 GPU 上完成，比 numpy 切片快 10-50 倍
 *   - 缩放/平移通过移动相机实现，不需要重新计算像素
 *   - 窗宽窗位调整也在 GPU 上完成
 * ============================================================================
 */

#ifndef SPINEX_SLICE_RENDERER_H
#define SPINEX_SLICE_RENDERER_H

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkImageReslice.h>               // 切片提取（任意平面）
#include <vtkImageMapToWindowLevelColors.h> // 窗宽窗位
#include <vtkImageActor.h>                 // 2D 图像显示
#include <vtkRenderer.h>                   // 渲染器
#include <vtkMatrix4x4.h>                  // 4x4 变换矩阵
#include <vtkLookupTable.h>                // 颜色查找表

#include <string>

/**
 * 切片方向枚举
 */
enum class SliceOrientation {
    Axial,                                 // 轴位面（从上往下看）
    Sagittal,                              // 矢状面（从侧面看）
    Coronal                                // 冠状面（从前往后看）
};

class SliceRenderer
{
public:
    SliceRenderer();
    ~SliceRenderer();

    // ---- 设置数据 ----
    // 设置 CT 体数据（输入源）
    void setImageData(vtkImageData* imageData);

    // ---- 切片控制 ----
    // 设置切片方向（轴位/矢状/冠状）
    void setOrientation(SliceOrientation orientation);

    // 设置当前切片索引
    void setSliceIndex(int index);

    // 获取当前切片索引
    int getSliceIndex() const;

    // 获取切片范围 [min, max]
    void getSliceRange(int& minSlice, int& maxSlice) const;

    // ---- 窗宽窗位 ----
    // 设置窗宽（Window）和窗位（Level）
    // 窗宽控制对比度，窗位控制亮度
    // CT 常用值：骨窗 W=2000 L=500，软组织窗 W=400 L=40
    void setWindowLevel(double window, double level);
    double getWindow() const;
    double getLevel() const;

    // ---- 获取 VTK 对象 ----
    // 返回渲染器（用于添加到 QVTKOpenGLNativeWidget）
    vtkRenderer* getRenderer();

    // 返回图像 Actor（用于叠加掩膜等）
    vtkImageActor* getImageActor();

    // ---- 相机控制 ----
    // 重置相机（适应整个图像）
    void resetCamera();

private:
    // 更新切片管线
    void updateSlice();

    // ---- VTK 管线对象 ----
    vtkSmartPointer<vtkImageData> imageData_;
    vtkSmartPointer<vtkImageReslice> reslice_;
    vtkSmartPointer<vtkImageMapToWindowLevelColors> windowLevel_;
    vtkSmartPointer<vtkImageActor> imageActor_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkMatrix4x4> resliceAxes_;

    SliceOrientation orientation_;
    int sliceIndex_;
    double window_;
    double level_;
};

#endif // SPINEX_SLICE_RENDERER_H
