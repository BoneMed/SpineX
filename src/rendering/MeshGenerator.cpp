/**
 * ============================================================================
 * MeshGenerator - 3D 网格生成器（实现）
 * ============================================================================
 */

#include "MeshGenerator.h"

#include <vtkDiscreteMarchingCubes.h>      // 多标签 Marching Cubes
#include <vtkWindowedSincPolyDataFilter.h> // 平滑滤波器
#include <vtkDecimatePro.h>                // 网格简化
#include <vtkThreshold.h>                  // 阈值过滤
#include <vtkGeometryFilter.h>             // 非结构化网格 → 多边形
#include <vtkPolyDataNormals.h>            // 法线计算（影响光照效果）
#include <vtkConnectivityFilter.h>         // 连通性过滤

#include <iostream>
#include <set>

MeshGenerator::MeshGenerator()
    : smoothingIterations_(20)
    , decimationRatio_(0.3)                // 保留 70% 的三角面
{
}

MeshGenerator::~MeshGenerator() = default;

// ---- 生成所有标签的网格 ----
std::map<int, vtkSmartPointer<vtkPolyData>> MeshGenerator::generateMeshes(
    vtkImageData* maskData,
    const std::vector<int>& labels)
{
    std::map<int, vtkSmartPointer<vtkPolyData>> meshes;

    if (!maskData) return meshes;

    // 如果没有指定标签，扫描掩膜找出所有非零标签
    std::vector<int> targetLabels = labels;
    if (targetLabels.empty()) {
        // 获取数据范围
        double range[2];
        maskData->GetScalarRange(range);

        // 收集所有唯一标签
        // set 是有序集合，自动去重（类似 Python 的 set）
        std::set<int> uniqueLabels;
        int numVoxels = maskData->GetNumberOfPoints();
        uint8_t* data = static_cast<uint8_t*>(maskData->GetScalarPointer());

        for (int i = 0; i < numVoxels; i++) {
            if (data[i] > 0) {
                uniqueLabels.insert(data[i]);
            }
        }

        for (int label : uniqueLabels) {
            targetLabels.push_back(label);
        }
    }

    // 使用 vtkDiscreteMarchingCubes 一次性提取所有标签
    auto marchingCubes = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    marchingCubes->SetInputData(maskData);

    // 设置要提取的等值
    marchingCubes->SetNumberOfContours(static_cast<int>(targetLabels.size()));
    for (size_t i = 0; i < targetLabels.size(); i++) {
        marchingCubes->SetValue(static_cast<int>(i), targetLabels[i]);
    }

    marchingCubes->Update();

    // 获取完整的多标签网格
    vtkPolyData* fullMesh = marchingCubes->GetOutput();

    // 按标签分离网格
    for (int label : targetLabels) {
        auto singleMesh = generateSingleMesh(maskData, label);
        if (singleMesh && singleMesh->GetNumberOfPoints() > 0) {
            meshes[label] = singleMesh;
        }
    }

    return meshes;
}

// ---- 生成单个标签的网格 ----
vtkSmartPointer<vtkPolyData> MeshGenerator::generateSingleMesh(
    vtkImageData* maskData,
    int label)
{
    if (!maskData) return nullptr;

    // 第1步：Marching Cubes 提取等值面
    auto marchingCubes = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    marchingCubes->SetInputData(maskData);
    marchingCubes->SetNumberOfContours(1);
    marchingCubes->SetValue(0, label);
    marchingCubes->Update();

    vtkPolyData* rawMesh = marchingCubes->GetOutput();
    if (rawMesh->GetNumberOfPoints() == 0) {
        return nullptr;
    }

    // 第2步：平滑（去除阶梯状锯齿）
    // WindowedSinc 比 Laplacian 平滑效果更好，不容易收缩
    auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
    smoother->SetInputData(rawMesh);
    smoother->SetNumberOfIterations(smoothingIterations_);
    smoother->SetPassBand(0.1);            // 通带：值越小越光滑
    smoother->BoundarySmoothingOn();
    smoother->NonManifoldSmoothingOn();
    smoother->NormalizeCoordinatesOn();
    smoother->Update();

    // 第3步：网格简化（减少三角面数量以提高渲染性能）
    auto decimator = vtkSmartPointer<vtkDecimatePro>::New();
    decimator->SetInputData(smoother->GetOutput());
    decimator->SetTargetReduction(decimationRatio_);  // 减少 30% 的三角面
    decimator->PreserveTopologyOn();       // 保持拓扑结构
    decimator->Update();

    // 第4步：重新计算法线（光照效果）
    auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
    normals->SetInputData(decimator->GetOutput());
    normals->ComputePointNormalsOn();
    normals->ComputeCellNormalsOn();
    normals->SplittingOff();               // 不分裂尖锐边缘
    normals->Update();

    // 返回最终网格
    auto result = vtkSmartPointer<vtkPolyData>::New();
    result->DeepCopy(normals->GetOutput());

    return result;
}

// ---- 参数设置 ----
void MeshGenerator::setSmoothingIterations(int iterations)
{
    smoothingIterations_ = iterations;
}

void MeshGenerator::setDecimationRatio(double ratio)
{
    decimationRatio_ = ratio;
}
