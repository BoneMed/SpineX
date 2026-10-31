/**
 * ============================================================================
 * MaskOverlay - 分割掩膜叠加渲染
 * ============================================================================
 * 在 CT 切片上叠加彩色分割掩膜。
 *
 * Python 中的做法：QPainter 逐像素 RGBA 合成（CPU）
 * C++ 中的做法：vtkImageBlend + vtkLookupTable（GPU）
 *
 * vtkLookupTable 将标签值（0-26）映射到 RGBA 颜色，
 * 然后 vtkImageBlend 将掩膜颜色和 CT 灰度图混合。
 * ============================================================================
 */

#ifndef SPINEX_MASK_OVERLAY_H
#define SPINEX_MASK_OVERLAY_H

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkImageMapToColors.h>
#include <vtkImageBlend.h>
#include <vtkImageActor.h>

#include <map>
#include <array>

class MaskOverlay
{
public:
    MaskOverlay();
    ~MaskOverlay();

    // 设置掩膜数据
    void setMaskData(vtkImageData* maskData);

    // 设置/更新单个标签的颜色 [r, g, b, a]，值范围 0-255
    void setLabelColor(int label, int r, int g, int b, int a = 128);

    // 使用默认的脊柱椎体颜色表（与 Python 版本一致）
    void setDefaultSpineColors();

    // 设置掩膜透明度（0.0 = 完全透明，1.0 = 完全不透明）
    void setOpacity(double opacity);

    // 获取颜色映射后的掩膜 Actor
    vtkImageActor* getMaskActor();

    // 获取颜色查找表
    vtkLookupTable* getLookupTable();

    // 设置某个标签是否可见
    void setLabelVisible(int label, bool visible);

private:
    void buildLookupTable();

    vtkSmartPointer<vtkImageData> maskData_;
    vtkSmartPointer<vtkLookupTable> lookupTable_;
    vtkSmartPointer<vtkImageMapToColors> colorMapper_;
    vtkSmartPointer<vtkImageActor> maskActor_;

    // 标签颜色表：标签值 → [R, G, B, A]
    // std::array<int, 4> 是固定大小的数组（类似 Python 的 tuple(int, int, int, int)）
    std::map<int, std::array<int, 4>> labelColors_;
    double opacity_;
};

#endif // SPINEX_MASK_OVERLAY_H
