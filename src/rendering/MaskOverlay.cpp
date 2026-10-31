/**
 * ============================================================================
 * MaskOverlay - 分割掩膜叠加渲染（实现）
 * ============================================================================
 */

#include "MaskOverlay.h"
#include <vtkImageProperty.h>
#include <vtkImageMapper3D.h>              // 图像映射器基类
#include <vtkRenderWindow.h>               // 渲染窗口
#include <iostream>

MaskOverlay::MaskOverlay()
    : opacity_(0.5)
{
    lookupTable_ = vtkSmartPointer<vtkLookupTable>::New();
    colorMapper_ = vtkSmartPointer<vtkImageMapToColors>::New();
    maskActor_ = vtkSmartPointer<vtkImageActor>::New();

    // 设置默认颜色表
    setDefaultSpineColors();
}

MaskOverlay::~MaskOverlay() = default;

// ---- 设置掩膜数据 ----
void MaskOverlay::setMaskData(vtkImageData* maskData)
{
    if (!maskData) return;
    maskData_ = maskData;

    buildLookupTable();

    colorMapper_->SetInputData(maskData_);
    colorMapper_->SetLookupTable(lookupTable_);
    colorMapper_->Update();

    maskActor_->GetMapper()->SetInputConnection(colorMapper_->GetOutputPort());
    maskActor_->GetProperty()->SetOpacity(opacity_);
}

// ---- 设置单个标签颜色 ----
void MaskOverlay::setLabelColor(int label, int r, int g, int b, int a)
{
    labelColors_[label] = {r, g, b, a};
}

// ---- 默认脊柱颜色表 ----
// 与 Python 版本中的 color_table 保持一致
void MaskOverlay::setDefaultSpineColors()
{
    // 标签 0 = 背景（透明）
    setLabelColor(0, 0, 0, 0, 0);

    // 椎体标签颜色（从 data_utilities.py 中的 color_table 移植）
    // C1-C7 (颈椎) - 蓝色系
    setLabelColor(1,  70, 130, 180, 160);  // C1
    setLabelColor(2,  65, 105, 225, 160);  // C2
    setLabelColor(3, 100, 149, 237, 160);  // C3
    setLabelColor(4,  30, 144, 255, 160);  // C4
    setLabelColor(5,   0, 191, 255, 160);  // C5
    setLabelColor(6, 135, 206, 250, 160);  // C6
    setLabelColor(7, 176, 196, 222, 160);  // C7

    // T1-T12 (胸椎) - 绿色系
    setLabelColor(8,   0, 128,   0, 160);  // T1
    setLabelColor(9,  34, 139,  34, 160);  // T2
    setLabelColor(10, 50, 205,  50, 160);  // T3
    setLabelColor(11,  0, 255,   0, 160);  // T4
    setLabelColor(12,124, 252,   0, 160);  // T5
    setLabelColor(13,173, 255,  47, 160);  // T6
    setLabelColor(14,  0, 250, 154, 160);  // T7
    setLabelColor(15,  0, 255, 127, 160);  // T8
    setLabelColor(16, 60, 179, 113, 160);  // T9
    setLabelColor(17, 46, 139,  87, 160);  // T10
    setLabelColor(18, 32, 178, 170, 160);  // T11
    setLabelColor(19, 64, 224, 208, 160);  // T12

    // L1-L6 (腰椎) - 红/橙色系
    setLabelColor(20, 255,  69,   0, 160); // L1
    setLabelColor(21, 255, 140,   0, 160); // L2
    setLabelColor(22, 255, 165,   0, 160); // L3
    setLabelColor(23, 255, 215,   0, 160); // L4
    setLabelColor(24, 255, 255,   0, 160); // L5
    setLabelColor(25, 218, 165,  32, 160); // L6

    // 骶骨
    setLabelColor(26, 199,  21, 133, 160); // Sacrum
}

// ---- 设置透明度 ----
void MaskOverlay::setOpacity(double opacity)
{
    opacity_ = opacity;
    if (maskActor_) {
        maskActor_->GetProperty()->SetOpacity(opacity_);
    }
}

// ---- 获取 Actor ----
vtkImageActor* MaskOverlay::getMaskActor() { return maskActor_; }

// ---- 获取查找表 ----
vtkLookupTable* MaskOverlay::getLookupTable() { return lookupTable_; }

// ---- 设置标签可见性 ----
void MaskOverlay::setLabelVisible(int label, bool visible)
{
    if (visible) {
        // 恢复原始颜色
        if (labelColors_.count(label)) {
            auto& c = labelColors_[label];
            lookupTable_->SetTableValue(label,
                c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, c[3] / 255.0);
        }
    } else {
        // 设置为完全透明
        lookupTable_->SetTableValue(label, 0, 0, 0, 0);
    }
    lookupTable_->Modified();
}

// ---- 构建查找表 ----
void MaskOverlay::buildLookupTable()
{
    // 查找最大标签值
    int maxLabel = 0;
    for (auto& [label, color] : labelColors_) {
        if (label > maxLabel) maxLabel = label;
    }

    lookupTable_->SetNumberOfTableValues(maxLabel + 1);
    lookupTable_->SetRange(0, maxLabel);

    // 先将所有标签设为透明
    for (int i = 0; i <= maxLabel; i++) {
        lookupTable_->SetTableValue(i, 0, 0, 0, 0);
    }

    // 设置有颜色的标签
    for (auto& [label, color] : labelColors_) {
        lookupTable_->SetTableValue(label,
            color[0] / 255.0,              // R (0-1 范围)
            color[1] / 255.0,              // G
            color[2] / 255.0,              // B
            color[3] / 255.0               // A
        );
    }

    lookupTable_->Build();
}
