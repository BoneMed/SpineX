/**
 * ============================================================================
 * VolumeExchange - numpy ↔ VTK 数据转换（实现文件）
 * ============================================================================
 */

#include "VolumeExchange.h"


#include <iostream>
#include <cstring>                         // memcpy：内存复制函数

// ============================================================================
// numpy float32 3D → vtkImageData
// ============================================================================
vtkSmartPointer<vtkImageData> VolumeExchange::numpyToVtkImage(
    py::array_t<float> array,
    double spacing[3],
    double origin[3])
{
    // ---- 获取 numpy 数组的信息 ----
    // py::buffer_info 包含数组的形状、步长、数据指针等元信息
    py::buffer_info buf = array.request();

    // 检查维度（必须是 3D）
    if (buf.ndim != 3) {
        throw std::runtime_error(
            "numpyToVtkImage: 需要 3D 数组，实际维度 = " + std::to_string(buf.ndim)
        );
    }

    // 获取各维度大小
    // numpy 的形状是 [depth, height, width]（Z, Y, X 顺序）
    // VTK 的维度是 [width, height, depth]（X, Y, Z 顺序）
    int dims[3] = {
        static_cast<int>(buf.shape[2]),    // width  (X) — static_cast 是类型转换
        static_cast<int>(buf.shape[1]),    // height (Y)
        static_cast<int>(buf.shape[0])     // depth  (Z)
    };

    // ---- 创建 vtkImageData ----
    // vtkSmartPointer::New() 创建一个 VTK 智能指针管理的对象
    auto imageData = vtkSmartPointer<vtkImageData>::New();
    imageData->SetDimensions(dims);         // 设置维度
    imageData->SetSpacing(spacing);         // 设置体素间距（mm）
    imageData->SetOrigin(origin);           // 设置原点
    imageData->AllocateScalars(VTK_FLOAT, 1);  // 分配内存（float 类型，1 个分量）

    // ---- 复制数据 ----
    // 获取 VTK 内部数据指针
    float* vtkPtr = static_cast<float*>(imageData->GetScalarPointer());
    // 获取 numpy 数据指针
    const float* npPtr = static_cast<const float*>(buf.ptr);

    // numpy 是行优先（C-order），VTK 也是行优先但维度顺序不同
    // 需要逐体素复制并调整维度顺序
    int nx = dims[0], ny = dims[1], nz = dims[2];
    for (int z = 0; z < nz; z++) {
        for (int y = 0; y < ny; y++) {
            for (int x = 0; x < nx; x++) {
                // numpy 索引: [z][y][x]，偏移 = z * ny * nx + y * nx + x
                // VTK  索引: (x, y, z)，偏移 = z * ny * nx + y * nx + x
                // 两者的内存布局恰好一致！（都是 z-y-x 顺序）
                vtkPtr[z * ny * nx + y * nx + x] = npPtr[z * ny * nx + y * nx + x];
            }
        }
    }

    return imageData;
}

// ============================================================================
// numpy uint8 3D → vtkImageData (掩膜)
// ============================================================================
vtkSmartPointer<vtkImageData> VolumeExchange::numpyMaskToVtkImage(
    py::array_t<uint8_t> array,
    double spacing[3],
    double origin[3])
{
    py::buffer_info buf = array.request();

    if (buf.ndim != 3) {
        throw std::runtime_error(
            "numpyMaskToVtkImage: 需要 3D 数组，实际维度 = " + std::to_string(buf.ndim)
        );
    }

    int dims[3] = {
        static_cast<int>(buf.shape[2]),
        static_cast<int>(buf.shape[1]),
        static_cast<int>(buf.shape[0])
    };

    auto imageData = vtkSmartPointer<vtkImageData>::New();
    imageData->SetDimensions(dims);
    imageData->SetSpacing(spacing);
    imageData->SetOrigin(origin);
    imageData->AllocateScalars(VTK_UNSIGNED_CHAR, 1);  // uint8 类型

    // 直接 memcpy（内存布局一致时可以批量复制）
    uint8_t* vtkPtr = static_cast<uint8_t*>(imageData->GetScalarPointer());
    const uint8_t* npPtr = static_cast<const uint8_t*>(buf.ptr);
    size_t totalBytes = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    std::memcpy(vtkPtr, npPtr, totalBytes);

    return imageData;
}

// ============================================================================
// vtkImageData → numpy float32
// ============================================================================
py::array_t<float> VolumeExchange::vtkImageToNumpy(vtkImageData* imageData)
{
    if (!imageData) {
        throw std::runtime_error("vtkImageToNumpy: imageData 为空");
    }

    // 获取 VTK 数据的维度
    int* dims = imageData->GetDimensions();  // [width, height, depth]

    // numpy 的形状是 [depth, height, width]
    std::vector<ssize_t> shape = {dims[2], dims[1], dims[0]};
    // 步长（strides）告诉 numpy 如何在内存中跳转到下一个元素
    std::vector<ssize_t> strides = {
        static_cast<ssize_t>(dims[1] * dims[0] * sizeof(float)),  // z 步长
        static_cast<ssize_t>(dims[0] * sizeof(float)),             // y 步长
        static_cast<ssize_t>(sizeof(float))                         // x 步长
    };

    // 创建 numpy 数组并复制数据
    auto result = py::array_t<float>(shape, strides);
    py::buffer_info buf = result.request();
    float* resultPtr = static_cast<float*>(buf.ptr);
    float* vtkPtr = static_cast<float*>(imageData->GetScalarPointer());

    size_t totalElements = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    std::memcpy(resultPtr, vtkPtr, totalElements * sizeof(float));

    return result;
}

// ============================================================================
// vtkImageData (uint8) → numpy
// ============================================================================
py::array_t<uint8_t> VolumeExchange::vtkMaskToNumpy(vtkImageData* imageData)
{
    if (!imageData) {
        throw std::runtime_error("vtkMaskToNumpy: imageData 为空");
    }

    int* dims = imageData->GetDimensions();

    std::vector<ssize_t> shape = {dims[2], dims[1], dims[0]};
    std::vector<ssize_t> strides = {
        static_cast<ssize_t>(dims[1] * dims[0]),
        static_cast<ssize_t>(dims[0]),
        static_cast<ssize_t>(1)
    };

    auto result = py::array_t<uint8_t>(shape, strides);
    py::buffer_info buf = result.request();
    uint8_t* resultPtr = static_cast<uint8_t*>(buf.ptr);
    uint8_t* vtkPtr = static_cast<uint8_t*>(imageData->GetScalarPointer());

    size_t totalElements = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    std::memcpy(resultPtr, vtkPtr, totalElements);

    return result;
}

// ============================================================================
// numpy (N, 4) → vtkPolyData 点云
// ============================================================================
vtkSmartPointer<vtkPolyData> VolumeExchange::numpyToPointCloud(
    py::array_t<float> xyzv)
{
    py::buffer_info buf = xyzv.request();

    if (buf.ndim != 2 || buf.shape[1] != 4) {
        throw std::runtime_error(
            "numpyToPointCloud: 需要 (N, 4) 数组，实际形状: ("
            + std::to_string(buf.shape[0]) + ", " + std::to_string(buf.shape[1]) + ")"
        );
    }

    int numPoints = static_cast<int>(buf.shape[0]);
    const float* data = static_cast<const float*>(buf.ptr);

    // 创建 VTK 点集
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(numPoints);

    // 创建标量值数组（第4列：E值或应力值）
    auto scalars = vtkSmartPointer<vtkFloatArray>::New();
    scalars->SetName("Values");
    scalars->SetNumberOfTuples(numPoints);

    // 填充数据
    for (int i = 0; i < numPoints; i++) {
        int offset = i * 4;
        points->SetPoint(i, data[offset], data[offset + 1], data[offset + 2]);
        scalars->SetValue(i, data[offset + 3]);
    }

    // 组装 vtkPolyData
    auto polyData = vtkSmartPointer<vtkPolyData>::New();
    polyData->SetPoints(points);
    polyData->GetPointData()->SetScalars(scalars);

    return polyData;
}
