/**
 * ============================================================================
 * ProjectIO - SpineX 项目文件读写 (.si 格式)
 * ============================================================================
 * .si 文件是 ZIP 压缩包，内部结构：
 *   manifest.json    — 项目元数据、参数、版本
 *   ct.nii.gz        — CT 数据
 *   mask.nii.gz      — 分割 mask（如果有）
 *   e_volume.nii.gz  — BMD 弹性模量体积（如果有）
 *   thumbnail.png    — 3D 预览缩略图
 * ============================================================================
 */

#ifndef SPINEX_PROJECT_IO_H
#define SPINEX_PROJECT_IO_H

#include <string>
#include <map>
#include <vector>
#include <QVariantMap>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ProjectData {
    std::string name;
    std::string path;                      // .si 文件路径
    std::string createdAt;
    std::string lastModified;
    std::map<std::string, std::string> files;  // 内部文件 {类型: 原始路径}
    json metadata;                         // 校准参数、选中椎体等

    // 从 QVariantMap 构建
    static ProjectData fromVariantMap(const QVariantMap& map);
    QVariantMap toVariantMap() const;
};

class ProjectIO
{
public:
    // ---- .si 文件操作 ----

    /**
     * 保存项目为 .si 文件（ZIP 打包）
     * 将 CT、mask、E_volume 等数据文件打包进单个 .si 文件
     */
    static bool saveAsSI(const QVariantMap& projectData,
                         const std::string& siFilePath);

    /**
     * 从 .si 文件加载项目
     * 解压到临时目录，返回文件路径映射
     */
    static QVariantMap loadFromSI(const std::string& siFilePath);

    /**
     * 获取 .si 文件的预览信息（不解压数据文件）
     */
    static ProjectData peekSI(const std::string& siFilePath);

    // ---- 最近项目 ----
    static std::vector<ProjectData> getRecentProjects(const std::string& configDir);
    static void addToRecentProjects(const ProjectData& project,
                                    const std::string& configDir);

    // ---- 旧接口兼容 ----
    static bool saveProject(const ProjectData& project, const std::string& filePath);
    static ProjectData loadProject(const std::string& filePath);
};

#endif // SPINEX_PROJECT_IO_H
