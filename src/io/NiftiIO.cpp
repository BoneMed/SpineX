/**
 * ============================================================================
 * NiftiIO - NIfTI 文件读写（实现）
 * ============================================================================
 */

#include "NiftiIO.h"

#include <vtkNIFTIImageReader.h>
#include <vtkNIFTIImageWriter.h>
#include <vtkNIFTIImageHeader.h>
#include <vtkMatrix4x4.h>
#include <vtkMatrix3x3.h>
#include <vtkStringArray.h>
#include <vtkDICOMImageReader.h>
#include <vtkImageCast.h>
#include <vtkXMLImageDataReader.h>
#include <vtkXMLImageDataWriter.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>

// ---- 读取 NIfTI（简单版）----
vtkSmartPointer<vtkImageData> NiftiIO::readNifti(const std::string& filePath)
{
    NiftiInfo info;
    return readNifti(filePath, info);
}

// ---- 读取 NIfTI（带元数据）----
vtkSmartPointer<vtkImageData> NiftiIO::readNifti(
    const std::string& filePath,
    NiftiInfo& info)
{
    // 检查文件格式
    if (!isNiftiFile(filePath)) {
        throw std::runtime_error("Not a NIfTI file: " + filePath);
    }

    // 创建 VTK NIfTI 读取器
    auto reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
    reader->SetFileName(filePath.c_str());  // .c_str() 将 std::string 转为 C 字符串

    // 执行读取（VTK 的管线模式：设置参数 → Update() → 获取输出）
    reader->Update();

    // 获取输出数据
    vtkImageData* output = reader->GetOutput();
    if (!output) {
        throw std::runtime_error("NIfTI read failed: " + filePath);
    }

    // 填充元数据
    int* dims = output->GetDimensions();
    double* spacing = output->GetSpacing();
    double* origin = output->GetOrigin();

    for (int i = 0; i < 3; i++) {
        info.dimensions[i] = dims[i];
        info.spacing[i] = spacing[i];
        info.origin[i] = origin[i];
    }

    // 读取方向矩阵
    vtkMatrix4x4* qFormMatrix = reader->GetQFormMatrix();
    if (qFormMatrix) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                info.direction[i * 3 + j] = qFormMatrix->GetElement(i, j);
            }
        }
    }

    // 读取描述信息
    vtkNIFTIImageHeader* header = reader->GetNIFTIHeader();
    if (header) {
        info.description = header->GetDescrip();
    }

    // 深拷贝输出数据（reader 销毁后数据仍然有效）
    auto result = vtkSmartPointer<vtkImageData>::New();
    result->DeepCopy(output);

    // 将 sform/qform 的旋转部分写入 vtkImageData::DirectionMatrix
    // 供 SharedViewport 使用 affine 矩阵判断轴方向
    // 优先使用 sform（更精确），退而使用 qform
    // 注意：sform = R * diag(spacing)，需要归一化每列以提取纯旋转矩阵
    vtkMatrix4x4* sFormMatrix = reader->GetSFormMatrix();
    vtkMatrix4x4* orientMat   = sFormMatrix ? sFormMatrix : qFormMatrix;
    if (orientMat) {
        auto dir3 = vtkSmartPointer<vtkMatrix3x3>::New();
        for (int j = 0; j < 3; j++) {
            // 计算列向量的长度（= spacing[j]）
            double len = 0;
            for (int i = 0; i < 3; i++) {
                double v = orientMat->GetElement(i, j);
                len += v * v;
            }
            len = std::sqrt(len);
            if (len < 1e-10) len = 1.0;  // 避免除以零
            // 归一化列向量，去掉 spacing，只保留旋转方向
            for (int i = 0; i < 3; i++) {
                dir3->SetElement(i, j, orientMat->GetElement(i, j) / len);
            }
        }
        result->SetDirectionMatrix(dir3);
    }

    return result;
}

// ---- 写入 NIfTI ----
bool NiftiIO::writeNifti(vtkImageData* imageData, const std::string& filePath)
{
    if (!imageData) {
        std::cerr << "[NiftiIO] Write failed: imageData is null" << std::endl;
        return false;
    }

    auto writer = vtkSmartPointer<vtkNIFTIImageWriter>::New();
    writer->SetFileName(filePath.c_str());
    writer->SetInputData(imageData);       // 设置输入数据
    writer->Write();                       // 执行写入

    return true;
}

// ---- 检查文件格式 ----
bool NiftiIO::isNiftiFile(const std::string& filePath)
{
    // 转换为小写进行比较
    std::string lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 检查文件扩展名
    // string::npos 表示"未找到"，类似 Python 中 str.find() 返回 -1
    return (lower.find(".nii.gz") != std::string::npos ||
            lower.find(".nii") != std::string::npos);
}

// ---- 检查是否为 DICOM 文件夹 ----
bool NiftiIO::isDicomFolder(const std::string& folderPath)
{
    namespace fs = std::filesystem;
    if (!fs::is_directory(folderPath)) return false;

    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".dcm" || ext == ".dicom") return true;
    }
    return false;
}

// ---- DICOM 文件夹 → 临时 NIfTI 文件 ----
std::string NiftiIO::dicomToTempNifti(const std::string& dicomFolder)
{
    namespace fs = std::filesystem;

    // 读取 DICOM 序列
    auto reader = vtkSmartPointer<vtkDICOMImageReader>::New();
    reader->SetDirectoryName(dicomFolder.c_str());
    reader->Update();

    vtkImageData* output = reader->GetOutput();
    if (!output || output->GetNumberOfPoints() == 0) {
        throw std::runtime_error("DICOM read failed (empty output): " + dicomFolder);
    }

    // 从 DICOM 读取 Image Orientation Patient (IOP)，设置方向矩阵
    // IOP = [rowDir[3], colDir[3]]，描述图像行/列在患者坐标系中的方向
    // 注意：VTK 的 vtkDICOMImageReader 将 dim 1（rows）的存储顺序翻转
    //       因此 IOP 列方向（dim 1）需要取反以匹配实际数据方向
    float* iop = reader->GetImageOrientationPatient();
    if (iop) {
        auto dir3 = vtkSmartPointer<vtkMatrix3x3>::New();
        // 列 0 = 行方向（dim 0 = columns，不翻转）
        dir3->SetElement(0, 0, iop[0]);
        dir3->SetElement(1, 0, iop[1]);
        dir3->SetElement(2, 0, iop[2]);
        // 列 1 = 列方向取反（dim 1 = rows，VTK 翻转了行序）
        dir3->SetElement(0, 1, -iop[3]);
        dir3->SetElement(1, 1, -iop[4]);
        dir3->SetElement(2, 1, -iop[5]);
        // 列 2 = 切片法向量（用原始 IOP 计算，不受行序翻转影响）
        double n0 = iop[1]*iop[5] - iop[2]*iop[4];
        double n1 = iop[2]*iop[3] - iop[0]*iop[5];
        double n2 = iop[0]*iop[4] - iop[1]*iop[3];
        dir3->SetElement(0, 2, n0);
        dir3->SetElement(1, 2, n1);
        dir3->SetElement(2, 2, n2);
        output->SetDirectionMatrix(dir3);
    }

    // Cast 为 short（NIfTI 常用，节省空间）
    auto caster = vtkSmartPointer<vtkImageCast>::New();
    caster->SetInputData(output);
    caster->SetOutputScalarTypeToShort();
    caster->Update();

    // vtkImageCast 可能不传递 direction matrix，需要手动复制
    vtkImageData* castOutput = caster->GetOutput();
    if (iop) {
        castOutput->SetDirectionMatrix(output->GetDirectionMatrix());
    }

    // 生成唯一临时文件路径（用文件夹路径的哈希值命名，避免重复转换）
    std::size_t h = std::hash<std::string>{}(dicomFolder);
    fs::path tempPath = fs::temp_directory_path()
        / ("spinex_dicom_" + std::to_string(h) + ".nii.gz");
    std::string tempStr = tempPath.string();

    // 写出 NIfTI — 必须通过 SetQFormMatrix/SetSFormMatrix 显式设置方向
    // vtkNIFTIImageWriter 不会自动从 vtkImageData::GetDirectionMatrix() 读取方向
    auto writer = vtkSmartPointer<vtkNIFTIImageWriter>::New();
    writer->SetFileName(tempStr.c_str());
    writer->SetInputData(castOutput);

    if (iop) {
        // 构建 4x4 仿射矩阵: sform = dir * diag(spacing) + origin
        double* spacing = castOutput->GetSpacing();
        double* origin  = castOutput->GetOrigin();
        vtkMatrix3x3* dir3 = castOutput->GetDirectionMatrix();

        auto sform = vtkSmartPointer<vtkMatrix4x4>::New();
        sform->Identity();
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                sform->SetElement(i, j, dir3->GetElement(i, j) * spacing[j]);
            }
            sform->SetElement(i, 3, origin[i]);
        }
        writer->SetQFormMatrix(sform);
        writer->SetSFormMatrix(sform);
    }

    writer->Write();

    return tempStr;
}
