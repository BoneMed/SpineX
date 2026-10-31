/**
 * ============================================================================
 * MeshGenerator - 3D 网格生成器
 * ============================================================================
 * 从分割掩膜生成 3D 网格（表面重建）。
 *
 * Python 中的做法：
 *   for each label:
 *       mesh = grid.contour([0.5])  # PyVista，逐标签循环
 *
 * C++ 的做法：
 *   vtkDiscreteMarchingCubes  — 一次性从多标签掩膜提取所有表面
 *   比逐标签循环快 5-10 倍！
 *
 * Marching Cubes 算法：
 *   遍历体数据中的每个小立方体（体素），根据 8 个顶点的值
 *   判断等值面通过该立方体的方式，生成三角面片。
 *   "Discrete" 版本专门针对整数标签（分割掩膜），一次处理所有标签。
 * ============================================================================
 */

#ifndef SPINEX_MESH_GENERATOR_H
#define SPINEX_MESH_GENERATOR_H

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkActor.h>

#include <map>
#include <vector>
#include <array>

class MeshGenerator
{
public:
    MeshGenerator();
    ~MeshGenerator();

    /**
     * 从分割掩膜生成所有标签的 3D 网格
     *
     * @param maskData   分割掩膜（uint8 vtkImageData）
     * @param labels     要提取的标签列表（为空则提取所有非零标签）
     * @return           标签 → 网格 的映射
     *
     * 返回类型：std::map<int, vtkSmartPointer<vtkPolyData>>
     *   类似 Python 的 dict[int, vtkPolyData]
     */
    std::map<int, vtkSmartPointer<vtkPolyData>> generateMeshes(
        vtkImageData* maskData,
        const std::vector<int>& labels = {}
    );

    /**
     * 生成单个标签的网格
     */
    vtkSmartPointer<vtkPolyData> generateSingleMesh(
        vtkImageData* maskData,
        int label
    );

    // ---- 参数设置 ----
    // 平滑迭代次数（越大越光滑，但细节越少）
    void setSmoothingIterations(int iterations);

    // 网格简化比例（0.0-1.0，越小三角面越少）
    void setDecimationRatio(double ratio);

private:
    int smoothingIterations_;
    double decimationRatio_;
};

#endif // SPINEX_MESH_GENERATOR_H
