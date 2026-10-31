/**
 * ============================================================================
 * SliceRenderer - VTK 2D 切片渲染管线（实现）
 * ============================================================================
 */

#include "SliceRenderer.h"

#include <vtkImageProperty.h>              // 图像显示属性
#include <vtkImageMapper3D.h>              // 图像映射器基类
#include <vtkRenderWindow.h>               // 渲染窗口
#include <vtkCamera.h>                     // 相机
#include <vtkInteractorStyleImage.h>       // 2D 图像交互样式
#include <iostream>

// ---- 构造函数 ----
SliceRenderer::SliceRenderer()
    : orientation_(SliceOrientation::Axial)
    , sliceIndex_(0)
    , window_(2000.0)                      // 默认骨窗
    , level_(500.0)
{
    // 创建 VTK 管线对象
    reslice_ = vtkSmartPointer<vtkImageReslice>::New();
    windowLevel_ = vtkSmartPointer<vtkImageMapToWindowLevelColors>::New();
    imageActor_ = vtkSmartPointer<vtkImageActor>::New();
    renderer_ = vtkSmartPointer<vtkRenderer>::New();
    resliceAxes_ = vtkSmartPointer<vtkMatrix4x4>::New();

    // 配置 reslice（切片提取器）
    reslice_->SetOutputDimensionality(2);  // 输出 2D 图像
    reslice_->SetInterpolationModeToLinear(); // 线性插值

    // 配置窗宽窗位
    windowLevel_->SetWindow(window_);
    windowLevel_->SetLevel(level_);
    windowLevel_->SetInputConnection(reslice_->GetOutputPort());

    // 配置图像 Actor
    imageActor_->GetMapper()->SetInputConnection(windowLevel_->GetOutputPort());

    // 配置渲染器
    renderer_->AddActor(imageActor_);
    renderer_->SetBackground(0.05, 0.05, 0.05);  // 深灰色背景
    renderer_->GetActiveCamera()->ParallelProjectionOn();  // 正交投影（2D 查看）
}

// ---- 析构函数 ----
SliceRenderer::~SliceRenderer() = default;  // 使用默认析构（智能指针自动清理）

// ---- 设置体数据 ----
void SliceRenderer::setImageData(vtkImageData* imageData)
{
    if (!imageData) return;

    imageData_ = imageData;
    reslice_->SetInputData(imageData_);

    // 设置默认切片到中间位置
    int* dims = imageData_->GetDimensions();
    switch (orientation_) {
        case SliceOrientation::Axial:
            sliceIndex_ = dims[2] / 2;     // Z 轴中间
            break;
        case SliceOrientation::Sagittal:
            sliceIndex_ = dims[0] / 2;     // X 轴中间
            break;
        case SliceOrientation::Coronal:
            sliceIndex_ = dims[1] / 2;     // Y 轴中间
            break;
    }

    updateSlice();
    resetCamera();
}

// ---- 设置切片方向 ----
void SliceRenderer::setOrientation(SliceOrientation orientation)
{
    orientation_ = orientation;
    if (imageData_) {
        updateSlice();
        resetCamera();
    }
}

// ---- 设置切片索引 ----
void SliceRenderer::setSliceIndex(int index)
{
    sliceIndex_ = index;
    if (imageData_) {
        updateSlice();
    }
}

int SliceRenderer::getSliceIndex() const { return sliceIndex_; }

// ---- 获取切片范围 ----
void SliceRenderer::getSliceRange(int& minSlice, int& maxSlice) const
{
    if (!imageData_) {
        minSlice = 0;
        maxSlice = 0;
        return;
    }

    int* dims = imageData_->GetDimensions();
    minSlice = 0;

    switch (orientation_) {
        case SliceOrientation::Axial:    maxSlice = dims[2] - 1; break;
        case SliceOrientation::Sagittal: maxSlice = dims[0] - 1; break;
        case SliceOrientation::Coronal:  maxSlice = dims[1] - 1; break;
    }
}

// ---- 窗宽窗位 ----
void SliceRenderer::setWindowLevel(double window, double level)
{
    window_ = window;
    level_ = level;
    windowLevel_->SetWindow(window_);
    windowLevel_->SetLevel(level_);
}

double SliceRenderer::getWindow() const { return window_; }
double SliceRenderer::getLevel() const { return level_; }

// ---- 获取 VTK 对象 ----
vtkRenderer* SliceRenderer::getRenderer() { return renderer_; }
vtkImageActor* SliceRenderer::getImageActor() { return imageActor_; }

// ---- 重置相机 ----
void SliceRenderer::resetCamera()
{
    renderer_->ResetCamera();
    renderer_->GetRenderWindow()->Render();
}

// ---- 更新切片（核心方法）----
void SliceRenderer::updateSlice()
{
    if (!imageData_) return;

    double* spacing = imageData_->GetSpacing();
    double* origin = imageData_->GetOrigin();

    // 重置变换矩阵为单位矩阵
    resliceAxes_->Identity();

    // 根据方向设置切片平面
    // reslice 矩阵定义了切片平面在 3D 空间中的位置和方向
    switch (orientation_) {
        case SliceOrientation::Axial:
            // 轴位面：XY 平面，沿 Z 轴滑动
            // 矩阵的第4列是平面的位置
            resliceAxes_->SetElement(0, 0, 1);  // X 轴 → 图像 X
            resliceAxes_->SetElement(1, 1, 1);  // Y 轴 → 图像 Y
            resliceAxes_->SetElement(2, 2, 1);  // Z 轴 → 法线方向
            resliceAxes_->SetElement(2, 3, origin[2] + sliceIndex_ * spacing[2]);
            break;

        case SliceOrientation::Sagittal:
            // 矢状面：YZ 平面，沿 X 轴滑动
            resliceAxes_->SetElement(0, 0, 0);
            resliceAxes_->SetElement(0, 1, 0);
            resliceAxes_->SetElement(0, 2, 1);  // 法线沿 X
            resliceAxes_->SetElement(1, 0, 0);
            resliceAxes_->SetElement(1, 1, 1);  // 图像 Y → Y
            resliceAxes_->SetElement(1, 2, 0);
            resliceAxes_->SetElement(2, 0, 1);  // 图像 X → Z
            resliceAxes_->SetElement(2, 1, 0);
            resliceAxes_->SetElement(2, 2, 0);
            resliceAxes_->SetElement(0, 3, origin[0] + sliceIndex_ * spacing[0]);
            break;

        case SliceOrientation::Coronal:
            // 冠状面：XZ 平面，沿 Y 轴滑动
            resliceAxes_->SetElement(0, 0, 1);  // 图像 X → X
            resliceAxes_->SetElement(1, 0, 0);
            resliceAxes_->SetElement(1, 1, 0);
            resliceAxes_->SetElement(1, 2, 1);  // 图像 Y → Z
            resliceAxes_->SetElement(2, 1, 1);  // 法线沿 Y
            resliceAxes_->SetElement(2, 2, 0);
            resliceAxes_->SetElement(1, 3, origin[1] + sliceIndex_ * spacing[1]);
            break;
    }

    reslice_->SetResliceAxes(resliceAxes_);
    reslice_->Update();
}
