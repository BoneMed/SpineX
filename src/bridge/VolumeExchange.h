/**
 * ============================================================================
 * VolumeExchange - numpy 和 VTK 数据之间的零拷贝转换
 * ============================================================================
 * 这是 C++/Python 桥接的核心：让 3D 医学图像数据在 C++ (VTK) 和 Python (numpy)
 * 之间高效传递，不需要复制数据。
 *
 * 关键概念：
 *   - vtkImageData：VTK 中的 3D 体数据（类似 numpy 的 3D 数组）
 *   - py::array_t<T>：pybind11 对 numpy ndarray 的封装
 *   - 零拷贝：两边共享同一块内存，修改一边另一边也能看到
 *
 * vtkSmartPointer：
 *   VTK 的智能指针，自动管理 VTK 对象的生命周期。
 *   当没有人引用该对象时自动销毁，类似 Python 的引用计数。
 * ============================================================================
 */

#ifndef SPINEX_VOLUME_EXCHANGE_H
#define SPINEX_VOLUME_EXCHANGE_H

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>                // numpy 数组支持

// Python's object.h defines a "slots" macro that conflicts with Qt's "slots" keyword.
#ifdef slots
#undef slots
#endif

#include <vtkSmartPointer.h>               // VTK 智能指针
#include <vtkImageData.h>                  // VTK 3D 体数据
#include <vtkPointData.h>                  // 点数据属性
#include <vtkFloatArray.h>                 // float 类型数组
#include <vtkUnsignedCharArray.h>          // uint8 类型数组（用于掩膜）
#include <vtkPolyData.h>                   // 多边形数据（网格/点云）
#include <vtkPoints.h>                     // 点集合

namespace py = pybind11;

/**
 * VolumeExchange 类 - 静态工具类
 *
 * 所有函数都是 static 的，不需要创建实例就能调用。
 * 类似 Python 中用 @staticmethod 装饰的方法。
 *
 * 调用方式：VolumeExchange::numpyToVtkImage(...)
 */
class VolumeExchange
{
public:
    // ====================================================================
    // numpy → VTK 转换
    // ====================================================================

    /**
     * 将 numpy float32 3D 数组转换为 vtkImageData
     *
     * @param array     numpy 数组 (shape: [depth, height, width])
     * @param spacing   体素间距 [sx, sy, sz]，单位 mm
     * @param origin    原点坐标 [ox, oy, oz]
     * @return          VTK 3D 体数据
     *
     * 注意：这里会复制数据（因为 VTK 和 numpy 的内存布局可能不同）
     * 对于 512³ 的 CT 数据，复制大约需要 0.5 秒。
     */
    static vtkSmartPointer<vtkImageData> numpyToVtkImage(
        py::array_t<float> array,
        double spacing[3],
        double origin[3]
    );

    /**
     * 将 numpy uint8 3D 数组转换为 vtkImageData（用于分割掩膜）
     */
    static vtkSmartPointer<vtkImageData> numpyMaskToVtkImage(
        py::array_t<uint8_t> array,
        double spacing[3],
        double origin[3]
    );

    // ====================================================================
    // VTK → numpy 转换
    // ====================================================================

    /**
     * 将 vtkImageData 转换为 numpy float32 3D 数组
     * 用于将 C++ 读取的 CT 数据传给 Python AI 模型
     */
    static py::array_t<float> vtkImageToNumpy(vtkImageData* imageData);

    /**
     * 将 vtkImageData (uint8) 转换为 numpy 数组（用于掩膜）
     */
    static py::array_t<uint8_t> vtkMaskToNumpy(vtkImageData* imageData);

    // ====================================================================
    // 点云数据转换
    // ====================================================================

    /**
     * 将 numpy (N, 4) 数组 [x, y, z, value] 转换为 vtkPolyData 点云
     * 用于材料属性和应力场的可视化
     */
    static vtkSmartPointer<vtkPolyData> numpyToPointCloud(
        py::array_t<float> xyzv
    );
};

#endif // SPINEX_VOLUME_EXCHANGE_H
