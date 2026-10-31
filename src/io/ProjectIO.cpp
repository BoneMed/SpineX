/**
 * ============================================================================
 * ProjectIO - .si 项目文件读写（实现）
 * ============================================================================
 * .si 文件本质是 ZIP 压缩包，使用 Qt 的 QProcess 调用系统 tar/zip
 * 或使用 minizip/zlib。这里用 Qt 内置的方式简化实现。
 * ============================================================================
 */

#include "ProjectIO.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QCoreApplication>

namespace fs = std::filesystem;

// ---- SI 文件魔数 ----
static const char SI_MAGIC[] = "SPINEX_SI";
static const int SI_VERSION = 1;

// ---- 辅助：创建临时目录 ----
static QString createTempDir(const QString& prefix)
{
    QString base = QDir::tempPath() + "/spinex_" + prefix + "_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(base);
    return base;
}

// ---- 辅助：复制文件 ----
static bool copyFileIfExists(const QString& src, const QString& dst)
{
    if (!QFile::exists(src)) return false;
    QFileInfo dstInfo(dst);
    QDir().mkpath(dstInfo.absolutePath());
    if (QFile::exists(dst)) QFile::remove(dst);
    return QFile::copy(src, dst);
}

// ============================================================================
// 保存为 .si 文件
// ============================================================================
bool ProjectIO::saveAsSI(const QVariantMap& projectData,
                          const std::string& siFilePath)
{
    QString siPath = QString::fromStdString(siFilePath);
    QString tempDir = createTempDir("save");

    // ---- 1. 构建 manifest.json ----
    QJsonObject manifest;
    manifest["format"] = "SpineX SI";
    manifest["version"] = SI_VERSION;
    manifest["app_version"] = "2.0";
    manifest["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 项目名称
    manifest["name"] = projectData.value("project_name", "Untitled").toString();

    // 文件清单
    QJsonObject files;
    QStringList dataKeys = {"ct_file_path", "mask_file_path", "e_volume_path",
                            "stress_volume_path", "element_npz_path"};
    QStringList internalNames = {"ct.nii.gz", "mask.nii.gz", "e_volume.nii.gz",
                                 "stress.nii.gz", "element_data.npz"};

    for (int i = 0; i < dataKeys.size(); ++i) {
        QString srcPath = projectData.value(dataKeys[i]).toString();
        if (!srcPath.isEmpty() && QFile::exists(srcPath)) {
            QString dstPath = tempDir + "/" + internalNames[i];
            // 如果源文件不是 .gz，需要保持原扩展名
            if (!srcPath.endsWith(".gz") && srcPath.endsWith(".nii")) {
                // 复制为 .nii（不压缩，ZIP 会压缩）
                QString altName = internalNames[i];
                altName.replace(".nii.gz", ".nii");
                dstPath = tempDir + "/" + altName;
                internalNames[i] = altName;
            }
            if (copyFileIfExists(srcPath, dstPath)) {
                files[dataKeys[i]] = internalNames[i];
            }
        }
    }
    manifest["files"] = files;

    // 参数
    QJsonObject params;
    // 校准参数
    if (projectData.contains("calib_slope"))
        params["calib_slope"] = projectData.value("calib_slope").toDouble();
    if (projectData.contains("calib_intercept"))
        params["calib_intercept"] = projectData.value("calib_intercept").toDouble();
    if (projectData.contains("calib_coefficient"))
        params["calib_coefficient"] = projectData.value("calib_coefficient").toDouble();
    if (projectData.contains("calib_exponent"))
        params["calib_exponent"] = projectData.value("calib_exponent").toDouble();
    if (projectData.contains("has_bmd"))
        params["has_bmd"] = projectData.value("has_bmd").toBool();

    // 选中的椎体
    if (projectData.contains("selected_labels")) {
        QJsonArray labels;
        for (const auto& v : projectData.value("selected_labels").toList())
            labels.append(v.toInt());
        params["selected_labels"] = labels;
    }

    // Stress prediction params
    if (projectData.contains("has_stress"))
        params["has_stress"] = projectData.value("has_stress").toBool();
    if (projectData.contains("stress_min"))
        params["stress_min"] = projectData.value("stress_min").toDouble();
    if (projectData.contains("stress_max"))
        params["stress_max"] = projectData.value("stress_max").toDouble();
    if (projectData.contains("bc_count"))
        params["bc_count"] = projectData.value("bc_count").toInt();
    if (projectData.contains("load_count"))
        params["load_count"] = projectData.value("load_count").toInt();
    if (projectData.contains("u_disp"))
        params["u_disp"] = projectData.value("u_disp").toDouble();

    // Mesh output directory (copy .inp files into .si)
    QString meshDir = projectData.value("mesh_output_dir").toString();
    if (!meshDir.isEmpty() && QDir(meshDir).exists()) {
        QDir md(meshDir);
        QStringList inpFiles = md.entryList({"*.inp", "*.stl"}, QDir::Files);
        if (!inpFiles.isEmpty()) {
            QString meshSubDir = tempDir + "/mesh";
            QDir().mkpath(meshSubDir);
            for (const auto& f : inpFiles)
                QFile::copy(meshDir + "/" + f, meshSubDir + "/" + f);
            params["has_mesh"] = true;
        }
    }

    manifest["params"] = params;

    // 写 manifest.json
    QFile manifestFile(tempDir + "/manifest.json");
    manifestFile.open(QIODevice::WriteOnly);
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    manifestFile.close();

    // 复制 logo 作为标识
    QString logoSrc = QCoreApplication::applicationDirPath() + "/resources/Fig/logo.ico";
#ifdef SPINEX_SOURCE_DIR
    if (!QFile::exists(logoSrc))
        logoSrc = QString(SPINEX_SOURCE_DIR) + "/resources/Fig/logo.ico";
#endif
    copyFileIfExists(logoSrc, tempDir + "/icon.ico");

    // ---- 2. 打包为 ZIP（重命名为 .si）----
    QString zipPath = siPath;
    if (QFile::exists(zipPath)) QFile::remove(zipPath);

    // 先压缩为 .zip — 平台分支
    QString zipTemp = siPath + ".zip";
    QProcess zipProc;
    // Linux: 用 zip CLI（在 tempDir 内打包以避免路径前缀）
    zipProc.setWorkingDirectory(tempDir);
    zipProc.setProgram("zip");
    zipProc.setArguments({"-r", zipTemp, "."});
    zipProc.start();
    zipProc.waitForFinished(60000);

    if (zipProc.exitCode() != 0 || !QFile::exists(zipTemp)) {
        std::cerr << "[ProjectIO] ZIP compression failed" << std::endl;
        // 清理
        QDir(tempDir).removeRecursively();
        return false;
    }

    // 重命名 .zip -> .si
    if (QFile::exists(siPath)) QFile::remove(siPath);
    QFile::rename(zipTemp, siPath);

    // 清理临时目录
    QDir(tempDir).removeRecursively();

    return true;
}

// ============================================================================
// 从 .si 文件加载
// ============================================================================
QVariantMap ProjectIO::loadFromSI(const std::string& siFilePath)
{
    QVariantMap result;
    QString siPath = QString::fromStdString(siFilePath);

    if (!QFile::exists(siPath)) {
        std::cerr << "[ProjectIO] File not found: " << siFilePath << std::endl;
        return result;
    }

    // 解压到临时目录（PowerShell 只认 .zip 扩展名，先复制一份）
    QString tempDir = createTempDir("load");
    QString zipCopy = tempDir + "/project.zip";
    QFile::copy(siPath, zipCopy);

    QProcess unzipProc;
    unzipProc.setProgram("unzip");
    unzipProc.setArguments({"-o", zipCopy, "-d", tempDir});
    unzipProc.start();
    unzipProc.waitForFinished(60000);
    QFile::remove(zipCopy);

    if (unzipProc.exitCode() != 0) {
        std::cerr << "[ProjectIO] Failed to extract .si file" << std::endl;
        return result;
    }

    // 读取 manifest.json
    QString manifestPath = tempDir + "/manifest.json";
    if (!QFile::exists(manifestPath)) {
        std::cerr << "[ProjectIO] No manifest.json in .si file" << std::endl;
        QDir(tempDir).removeRecursively();
        return result;
    }

    QFile manifestFile(manifestPath);
    manifestFile.open(QIODevice::ReadOnly);
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();

    // 项目名称
    result["project_name"] = manifest.value("name").toString("Untitled");
    result["si_file_path"] = siPath;
    result["si_temp_dir"] = tempDir;

    // 还原文件路径（指向解压后的临时目录）
    QJsonObject files = manifest.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        QString internalName = it.value().toString();
        QString extractedPath = tempDir + "/" + internalName;
        if (QFile::exists(extractedPath)) {
            result[it.key()] = extractedPath;
        }
    }

    // 还原参数
    QJsonObject params = manifest.value("params").toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it.key() == "selected_labels") {
            QVariantList labels;
            for (const auto& v : it.value().toArray())
                labels.append(v.toInt());
            result["selected_labels"] = labels;
        } else {
            result[it.key()] = it.value().toVariant();
        }
    }

    return result;
}

// ============================================================================
// 预览 .si 文件（只读 manifest，不解压数据）
// ============================================================================
ProjectData ProjectIO::peekSI(const std::string& siFilePath)
{
    ProjectData data;
    // 解压只提取 manifest.json
    QString siPath = QString::fromStdString(siFilePath);
    QString tempDir = createTempDir("peek");
    QString zipCopy = tempDir + "/project.zip";
    QFile::copy(siPath, zipCopy);

    QProcess proc;
    proc.setProgram("unzip");
    proc.setArguments({"-o", zipCopy, "-d", tempDir});
    proc.start();
    proc.waitForFinished(10000);
    QFile::remove(zipCopy);

    QString manifestPath = tempDir + "/manifest.json";
    if (QFile::exists(manifestPath)) {
        QFile f(manifestPath);
        f.open(QIODevice::ReadOnly);
        QJsonObject manifest = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        data.name = manifest.value("name").toString("Untitled").toStdString();
        data.createdAt = manifest.value("created").toString().toStdString();
    }

    QDir(tempDir).removeRecursively();
    return data;
}

// ============================================================================
// ProjectData <-> QVariantMap 转换
// ============================================================================
ProjectData ProjectData::fromVariantMap(const QVariantMap& map)
{
    ProjectData d;
    d.name = map.value("project_name", "Untitled").toString().toStdString();
    d.path = map.value("si_file_path", "").toString().toStdString();
    d.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    return d;
}

QVariantMap ProjectData::toVariantMap() const
{
    QVariantMap m;
    m["project_name"] = QString::fromStdString(name);
    m["si_file_path"] = QString::fromStdString(path);
    return m;
}

// ============================================================================
// 最近项目列表
// ============================================================================
std::vector<ProjectData> ProjectIO::getRecentProjects(const std::string& configDir)
{
    std::vector<ProjectData> projects;
    std::string recentFile = configDir + "/recent_projects.json";
    if (!fs::exists(recentFile)) return projects;

    try {
        std::ifstream file(recentFile);
        json j = json::parse(file);
        if (j.is_array()) {
            for (auto& item : j) {
                ProjectData p;
                p.name = item.value("name", "");
                p.path = item.value("path", "");
                p.lastModified = item.value("last_modified", "");
                projects.push_back(p);
            }
        }
    } catch (...) {}
    return projects;
}

void ProjectIO::addToRecentProjects(const ProjectData& project,
                                     const std::string& configDir)
{
    fs::create_directories(configDir);
    std::string recentFile = configDir + "/recent_projects.json";

    json recentList = json::array();
    if (fs::exists(recentFile)) {
        try {
            std::ifstream file(recentFile);
            recentList = json::parse(file);
        } catch (...) {
            recentList = json::array();
        }
    }

    // 去重
    json newList = json::array();
    for (auto& item : recentList) {
        if (item.value("path", "") != project.path)
            newList.push_back(item);
    }

    json entry;
    entry["name"] = project.name;
    entry["path"] = project.path;
    entry["last_modified"] = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    newList.insert(newList.begin(), entry);

    if (newList.size() > 10)
        newList.erase(newList.begin() + 10, newList.end());

    std::ofstream file(recentFile);
    file << newList.dump(4);
}

// ---- 旧接口兼容 ----
bool ProjectIO::saveProject(const ProjectData& project, const std::string& filePath)
{
    json j;
    j["name"] = project.name;
    j["path"] = project.path;
    j["created_at"] = project.createdAt;
    j["last_modified"] = project.lastModified;
    j["files"] = project.files;
    j["metadata"] = project.metadata;

    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

ProjectData ProjectIO::loadProject(const std::string& filePath)
{
    ProjectData project;
    std::ifstream file(filePath);
    if (!file.is_open()) throw std::runtime_error("Cannot open: " + filePath);
    json j = json::parse(file);
    project.name = j.value("name", "");
    project.path = j.value("path", "");
    project.createdAt = j.value("created_at", "");
    project.lastModified = j.value("last_modified", "");
    if (j.contains("files"))
        for (auto& [k, v] : j["files"].items())
            project.files[k] = v.get<std::string>();
    if (j.contains("metadata"))
        project.metadata = j["metadata"];
    return project;
}
