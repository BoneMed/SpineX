/**
 * ============================================================================
 * NiftiIO - NIfTI 医学图像文件读写
 * ============================================================================
 * NIfTI (.nii / .nii.gz) 是脑影像和脊柱CT的标准格式。
 * 在 Python 中用 nibabel 读取，这里用 VTK 自带的 NIfTI 读写器。
 *
 * VTK 自带 NIfTI 支持（vtkNIFTIImageReader），不需要额外安装 ITK。
 * 这简化了依赖关系。
 * ============================================================================
 */

#ifndef SPINEX_NIFTI_IO_H
#define SPINEX_NIFTI_IO_H

#include <string>
#include <vtkSmartPointer.h>
#include <vtkImageData.h>

/**
 * NIfTI 文件的元数据信息
 *
 * struct 是 C++ 的结构体，和 class 几乎一样，
 * 唯一区别是 struct 默认 public，class 默认 private。
 * 通常用 struct 来存储纯数据，用 class 来封装行为。
 */
struct NiftiInfo {
    int dimensions[3];         // 体数据维度 [width, height, depth]
    double spacing[3];         // 体素间距 [sx, sy, sz]，单位 mm
    double origin[3];          // 原点坐标 [ox, oy, oz]
    double direction[9];       // 方向余弦矩阵（3x3，行优先）
    std::string description;   // 文件描述信息
};

/**
 * NiftiIO - 静态工具类
 */
class NiftiIO
{
public:
    /**
     * 读取 NIfTI 文件，返回 vtkImageData
     *
     * @param filePath  NIfTI 文件路径 (.nii 或 .nii.gz)
     * @return          VTK 3D 体数据
     *
     * 支持压缩格式 (.nii.gz)，VTK 会自动解压
     */
    static vtkSmartPointer<vtkImageData> readNifti(const std::string& filePath);

    /**
     * 读取 NIfTI 文件并同时返回元数据
     */
    static vtkSmartPointer<vtkImageData> readNifti(
        const std::string& filePath,
        NiftiInfo& info                    // 引用参数：函数内部修改，调用者能看到
    );

    /**
     * 将 vtkImageData 写入 NIfTI 文件
     */
    static bool writeNifti(
        vtkImageData* imageData,
        const std::string& filePath
    );

    /**
     * 检查文件是否为 NIfTI 格式
     */
    static bool isNiftiFile(const std::string& filePath);

    /**
     * 将 DICOM 文件夹转换为临时 NIfTI 文件，返回临时文件路径
     * 临时文件保存在系统临时目录，以文件夹路径哈希命名（避免重复转换）
     */
    static std::string dicomToTempNifti(const std::string& dicomFolder);

    /**
     * 检查路径是否为包含 DICOM 文件的文件夹
     */
    static bool isDicomFolder(const std::string& folderPath);
};

#endif // SPINEX_NIFTI_IO_H
