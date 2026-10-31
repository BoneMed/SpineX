/**
 * ============================================================================
 * MenuBar - 菜单栏导航（实现）
 * 从 Python ui/menu_bar.py 1:1 移植 (large theme)
 * ============================================================================
 */

#include "MenuBar.h"
#include "app/SpineXApp.h"
#include "app/LicenseManager.h"
#include "app/UpdateChecker.h"
#include "io/ProjectIO.h"
#include "io/NiftiIO.h"
#include "ui/SharedViewport.h"

#include <QMessageBox>
#include <QApplication>
#include <QProcess>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QDialog>
#include <QTextEdit>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFont>
#include <QCoreApplication>

#include <vtkSmartPointer.h>
#include <vtkPoints.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <vtkCellType.h>

MenuBar::MenuBar(SpineXApp* mainWindow)
    : QObject(mainWindow)
    , mainWindow_(mainWindow)
    , modulesMenu_(nullptr)
{
    menubar_ = mainWindow->menuBar();

    applyMenuBarStyle();
    createFileMenu();
    createSaveMenu();
    createModulesMenu();
    createAddonsMenu();
    createHelpMenu();
}

void MenuBar::applyMenuBarStyle()
{
    // "large" theme from Python
    menubar_->setStyleSheet(R"(
        QMenuBar {
            background-color: #ffffff;
            font-size: 18px;
            font-weight: 500;
            padding: 4px 6px;
            spacing: 10px;
            border-bottom: 1px solid #dde1e7;
        }
        QMenuBar::item {
            background-color: transparent;
            color: #1a2332;
            padding: 8px 18px;
            margin: 2px 4px;
            border-radius: 5px;
        }
        QMenuBar::item:selected {
            background-color: #f0f4f8;
            color: #1a2332;
        }
        QMenuBar::item:pressed {
            background-color: #e2e8f0;
            color: #1a2332;
        }
        QMenu {
            font-size: 16px;
            padding: 5px;
            border: 1px solid #dde1e7;
            border-radius: 6px;
            background-color: #ffffff;
            color: #1a2332;
        }
        QMenu::item {
            padding: 8px 28px 8px 14px;
            border-radius: 4px;
            color: #1a2332;
        }
        QMenu::item:selected {
            background-color: #f0f4f8;
            color: #2c5f6f;
        }
        QMenu::separator {
            height: 1px;
            background-color: #dde1e7;
            margin: 4px 10px;
        }
    )");
}

void MenuBar::createFileMenu()
{
    QMenu* fileMenu = menubar_->addMenu("File");

    QAction* newAction = fileMenu->addAction("New Project");
    newAction->setShortcut(QKeySequence("Ctrl+N"));
    connect(newAction, &QAction::triggered, this, &MenuBar::newProject);

    QAction* loadAction = fileMenu->addAction("Load Project");
    loadAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(loadAction, &QAction::triggered, this, &MenuBar::loadProject);

    QAction* dataAction = fileMenu->addAction("Data Loading");
    dataAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(dataAction, &QAction::triggered, this, &MenuBar::dataLoading);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("Exit");
    exitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(exitAction, &QAction::triggered, mainWindow_, &QMainWindow::close);
}

void MenuBar::createSaveMenu()
{
    QMenu* saveMenu = menubar_->addMenu("Save");

    QAction* saveProjAction = saveMenu->addAction("Save Project");
    saveProjAction->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveProjAction, &QAction::triggered, this, &MenuBar::saveProject);

    saveMenu->addSeparator();

    QAction* saveMaskAction = saveMenu->addAction("Save Mask");
    saveMaskAction->setShortcut(QKeySequence("Ctrl+Shift+M"));
    connect(saveMaskAction, &QAction::triggered, this, &MenuBar::saveMask);

    saveMenu->addSeparator();

    QAction* exportBMDAction = saveMenu->addAction("Export BMD Results...");
    connect(exportBMDAction, &QAction::triggered, this, &MenuBar::exportBMDResults);

    QAction* exportMeshAction = saveMenu->addAction("Export Mesh (INP)...");
    connect(exportMeshAction, &QAction::triggered, this, &MenuBar::exportMeshInp);

    QAction* exportStressAction = saveMenu->addAction("Export Stress Results");
    exportStressAction->setEnabled(false);
}

void MenuBar::createModulesMenu()
{
    modulesMenu_ = menubar_->addMenu(QString::fromUtf8("Modules: Welcome to SpineX \u25BC"));

    QAction* homeAction = modulesMenu_->addAction(QString::fromUtf8("\U0001F3E0 Home"));
    connect(homeAction, &QAction::triggered, this, &MenuBar::goToHome);

    modulesMenu_->addSeparator();

    dataAction_ = modulesMenu_->addAction(QString::fromUtf8("\U0001F4CA Data"));
    connect(dataAction_, &QAction::triggered, this, &MenuBar::openDataModule);

    modulesMenu_->addSeparator();

    ctAction_ = modulesMenu_->addAction("Segmentation");
    connect(ctAction_, &QAction::triggered, this, &MenuBar::openCTSegmentation);

    modulusAction_ = modulesMenu_->addAction("BMD Analysis");
    connect(modulusAction_, &QAction::triggered, this, &MenuBar::openModulusEstimation);

    stressAction_ = modulesMenu_->addAction("Strain Distribution Prediction");
    stressAction_->setEnabled(true);
    connect(stressAction_, &QAction::triggered, this, &MenuBar::openStressPrediction);

    applyModulePermissions();
}

void MenuBar::applyModulePermissions()
{
    auto& lm = LicenseManager::instance();
    if (dataAction_)    dataAction_->setVisible(lm.isModuleAllowed("data"));
    if (ctAction_)      ctAction_->setVisible(lm.isModuleAllowed("segmentation"));
    if (modulusAction_) modulusAction_->setVisible(lm.isModuleAllowed("bmd"));
    if (stressAction_)  stressAction_->setVisible(lm.isModuleAllowed("strain"));
    if (cobbAction_)    cobbAction_->setVisible(lm.isModuleAllowed("cobb"));
}

void MenuBar::createAddonsMenu()
{
    QMenu* addonsMenu = menubar_->addMenu("Add-ons");

    cobbAction_ = addonsMenu->addAction("Cobb Angle Measurement");
    cobbAction_->setEnabled(true);
    connect(cobbAction_, &QAction::triggered, this, &MenuBar::openCobbAngleAddon);
    applyModulePermissions();  // re-apply to include cobbAction_
}

void MenuBar::createHelpMenu()
{
    QMenu* helpMenu = menubar_->addMenu("Help");

    QAction* docAction = helpMenu->addAction("Documentation");
    docAction->setShortcut(QKeySequence("F1"));
    connect(docAction, &QAction::triggered, this, &MenuBar::showHelp);

    QAction* aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &MenuBar::showAbout);

    QAction* licensesAction = helpMenu->addAction("Third-Party Licenses");
    connect(licensesAction, &QAction::triggered, this, &MenuBar::showThirdPartyLicenses);

    helpMenu->addSeparator();

    QAction* signOutAction = helpMenu->addAction("Sign Out");
    connect(signOutAction, &QAction::triggered, this, &MenuBar::signOut);
}

void MenuBar::updateMenuTitle(const QString& title)
{
    if (modulesMenu_) {
        modulesMenu_->setTitle(QString("Modules: %1 \u25BC").arg(title));
    }
}

// ---- Module navigation ----

void MenuBar::goToHome()
{
    mainWindow_->showWelcomeScreen();
    updateMenuTitle("Welcome to SpineX");
}

void MenuBar::openDataModule()
{
    mainWindow_->switchToModule("data_module");
    updateMenuTitle("Data");
}

void MenuBar::openCTSegmentation()
{
    mainWindow_->switchToModule("ct_segmentation");
    updateMenuTitle("Segmentation");
}

void MenuBar::openModulusEstimation()
{
    mainWindow_->switchToModule("modulus_estimation");
    updateMenuTitle("BMD Analysis");
}

void MenuBar::openStressPrediction()
{
    mainWindow_->switchToModule("stress_prediction");
    updateMenuTitle("Strain Prediction");
}

// ---- File operations ----

void MenuBar::newProject()
{
    // 如果当前有未保存的数据，先提醒
    if (!mainWindow_->currentProject.value("ct_file_path").toString().isEmpty()) {
        auto reply = QMessageBox::question(mainWindow_, "New Project",
            "Create a new project?\nUnsaved changes will be lost.",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }

    // 输入项目名称
    bool ok;
    QString name = QInputDialog::getText(mainWindow_, "New Project",
        "Project name:", QLineEdit::Normal, "New Project", &ok);
    if (!ok || name.isEmpty()) return;

    // 选择保存位置
    QString dir = QFileDialog::getExistingDirectory(mainWindow_,
        "Select folder to create project in",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (dir.isEmpty()) return;

    // 创建项目文件夹
    QString projectDir = dir + "/" + name;
    QDir().mkpath(projectDir);

    // 清除当前项目数据
    mainWindow_->currentProject.clear();
    mainWindow_->currentProject["project_name"] = name;
    mainWindow_->currentProject["project_dir"] = projectDir;
    mainWindow_->currentProject["si_file_path"] = projectDir + "/" + name + ".si";
    mainWindow_->dataVersion++;

    // 切换到 Data 模块
    mainWindow_->switchToModule("data_module");
    updateMenuTitle("Data");

    mainWindow_->setWindowTitle(QString("SpineX - %1").arg(name));
}

void MenuBar::loadProject()
{
    QString filePath = QFileDialog::getOpenFileName(mainWindow_,
        "Open SpineX Project", QString(),
        "SpineX Project (*.si);;All Files (*)");
    if (filePath.isEmpty()) return;

    QVariantMap loaded = ProjectIO::loadFromSI(filePath.toStdString());
    if (loaded.isEmpty()) {
        QMessageBox::critical(mainWindow_, "Load Project",
            "Failed to load project file.");
        return;
    }

    // 将加载的数据合并到 currentProject
    for (auto it = loaded.begin(); it != loaded.end(); ++it) {
        mainWindow_->currentProject[it.key()] = it.value();
    }

    // 预加载所有模块（确保 Seg 和 BMD 的 3D 都渲染）
    mainWindow_->switchToModule("ct_segmentation");
    mainWindow_->switchToModule("modulus_estimation");

    // 最终停在 Data 模块
    mainWindow_->switchToModule("data_module");
    updateMenuTitle("Data");

    mainWindow_->setWindowTitle(QString("SpineX - %1")
        .arg(loaded.value("project_name", "Untitled").toString()));

    QMessageBox::information(mainWindow_, "Load Project",
        QString("Project \"%1\" loaded successfully.")
            .arg(loaded.value("project_name", "Untitled").toString()));
}

void MenuBar::dataLoading()
{
    mainWindow_->switchToModule("data_module");
    updateMenuTitle("Data");
}

void MenuBar::saveProject()
{
    // 检查是否有数据可保存
    if (mainWindow_->currentProject.value("ct_file_path").toString().isEmpty()) {
        QMessageBox::warning(mainWindow_, "Save Project",
            "No data to save.\nPlease load CT data first.");
        return;
    }

    // 获取项目名称
    QString defaultName = mainWindow_->currentProject.value("project_name", "Untitled").toString();
    bool ok;
    QString name = QInputDialog::getText(mainWindow_, "Save Project",
        "Project name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.isEmpty()) return;

    // 选择保存位置（选文件夹，不是文件）
    QString parentDir = QFileDialog::getExistingDirectory(mainWindow_,
        "Select folder to save project in",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (parentDir.isEmpty()) return;

    // 创建项目文件夹
    QString projectDir = parentDir + "/" + name;
    QDir().mkpath(projectDir);
    QString filePath = projectDir + "/" + name + ".si";

    mainWindow_->currentProject["project_name"] = name;
    mainWindow_->currentProject["project_dir"] = projectDir;
    mainWindow_->currentProject["si_file_path"] = filePath;

    // 保存
    bool success = ProjectIO::saveAsSI(mainWindow_->currentProject,
                                        filePath.toStdString());
    if (success) {
        mainWindow_->setWindowTitle(QString("SpineX - %1").arg(name));
        QMessageBox::information(mainWindow_, "Save Project",
            QString("Project saved successfully.\n%1").arg(filePath));
    } else {
        QMessageBox::critical(mainWindow_, "Save Project",
            "Failed to save project.");
    }
}

void MenuBar::saveMask()
{
    auto* viewport = mainWindow_->sharedViewport();
    if (!viewport || !viewport->maskData_) {
        QMessageBox::warning(mainWindow_, "Save Mask",
            "No segmentation mask available.\nPlease run CT Segmentation first.");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(mainWindow_,
        "Save Segmentation Mask", "mask.nii.gz",
        "NIfTI (*.nii.gz *.nii)");
    if (filePath.isEmpty()) return;

    bool ok = NiftiIO::writeNifti(viewport->maskData_, filePath.toStdString());
    if (ok) {
        mainWindow_->currentProject["mask_file_path"] = filePath;
        QMessageBox::information(mainWindow_, "Save Mask",
            QString("Mask saved successfully.\n%1").arg(filePath));
    } else {
        QMessageBox::critical(mainWindow_, "Save Mask", "Failed to save mask.");
    }
}

void MenuBar::exportBMDResults()
{
    auto* viewport = mainWindow_->sharedViewport();
    if (!viewport || !viewport->eVolume_) {
        QMessageBox::warning(mainWindow_, "Export BMD",
            "No BMD results available.\nPlease run BMD Analysis first.");
        return;
    }

    QStringList formats = { "VTK Unstructured Grid (*.vtu)", "CSV (*.csv)" };
    bool ok = false;
    QString chosen = QInputDialog::getItem(mainWindow_,
        "Export BMD", "Select export format:", formats, 0, false, &ok);
    if (!ok) return;
    bool asCsv = chosen.startsWith("CSV");

    QString defaultName = asCsv ? "bmd_results.csv" : "bmd_results.vtu";
    QString filter      = asCsv ? "CSV (*.csv)"     : "VTK Unstructured Grid (*.vtu)";
    QString filePath = QFileDialog::getSaveFileName(mainWindow_,
        "Export BMD Results", defaultName, filter);
    if (filePath.isEmpty()) return;

    // 收集 mask 区域内有效 E 值
    auto* eVol = viewport->eVolume_.Get();
    auto* maskData = viewport->maskData_.Get();
    int* dims = eVol->GetDimensions();
    double* spacing = eVol->GetSpacing();
    double* origin = eVol->GetOrigin();

    struct Entry { double x, y, z; float e; };
    std::vector<Entry> entries;
    entries.reserve(1 << 20);

    for (int z = 0; z < dims[2]; ++z) {
        for (int y = 0; y < dims[1]; ++y) {
            for (int x = 0; x < dims[0]; ++x) {
                double e = eVol->GetScalarComponentAsDouble(x, y, z, 0);
                if (!std::isfinite(e) || e <= 0) continue;
                if (maskData) {
                    int label = static_cast<int>(
                        maskData->GetScalarComponentAsDouble(x, y, z, 0));
                    if (label <= 0) continue;
                }
                entries.push_back({
                    origin[0] + x * spacing[0],
                    origin[1] + y * spacing[1],
                    origin[2] + z * spacing[2],
                    static_cast<float>(e)
                });
            }
        }
    }

    if (asCsv) {
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(mainWindow_, "Export BMD",
                QString("Failed to open file for writing:\n%1").arg(filePath));
            return;
        }
        QTextStream out(&f);
        out << "element ID,E\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            out << (i + 1) << "," << entries[i].e << "\n";
        }
        f.close();
    } else {
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto eArray = vtkSmartPointer<vtkFloatArray>::New();
        eArray->SetName("Elastic Modulus (MPa)");
        eArray->SetNumberOfComponents(1);
        auto prArray = vtkSmartPointer<vtkFloatArray>::New();
        prArray->SetName("Poissons Ratio");
        prArray->SetNumberOfComponents(1);

        for (const auto& e : entries) {
            points->InsertNextPoint(e.x, e.y, e.z);
            eArray->InsertNextValue(e.e);
            prArray->InsertNextValue(0.3f);  // 骨骼泊松比 ≈ 0.3
        }

        auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        grid->SetPoints(points);
        grid->GetPointData()->AddArray(eArray);
        grid->GetPointData()->AddArray(prArray);
        for (vtkIdType i = 0; i < points->GetNumberOfPoints(); ++i) {
            grid->InsertNextCell(VTK_VERTEX, 1, &i);
        }

        auto writer = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
        writer->SetFileName(filePath.toStdString().c_str());
        writer->SetInputData(grid);
        writer->Write();
    }

    QMessageBox::information(mainWindow_, "Export BMD",
        QString("BMD results exported.\n%1\n\n%2 elements")
            .arg(filePath).arg(static_cast<qulonglong>(entries.size())));
}

void MenuBar::exportMeshInp()
{
    QString meshDir = mainWindow_->currentProject.value("mesh_output_dir").toString();
    if (meshDir.isEmpty() || !QDir(meshDir).exists()) {
        QMessageBox::warning(mainWindow_, "Export Mesh",
            "No mesh available.\nPlease run BMD Analysis → Meshing first.");
        return;
    }

    QDir src(meshDir);
    QStringList inpFiles = src.entryList({"label_*_C3D10.inp"}, QDir::Files, QDir::Name);
    if (inpFiles.isEmpty()) {
        QMessageBox::warning(mainWindow_, "Export Mesh",
            QString("No mesh .inp files found in:\n%1").arg(meshDir));
        return;
    }

    QString destDir = QFileDialog::getExistingDirectory(mainWindow_,
        "Select folder to export mesh INP files", QString(),
        QFileDialog::ShowDirsOnly);
    if (destDir.isEmpty()) return;

    int copied = 0, failed = 0;
    QStringList failures;
    for (const QString& name : inpFiles) {
        QString srcPath = src.filePath(name);
        QString dstPath = destDir + "/" + name;
        if (QFile::exists(dstPath)) QFile::remove(dstPath);
        if (QFile::copy(srcPath, dstPath)) {
            ++copied;
        } else {
            ++failed;
            failures << name;
        }
    }

    QString summary = QString("Exported %1 mesh file(s) to:\n%2").arg(copied).arg(destDir);
    if (failed) summary += QString("\n\nFailed to copy %1 file(s):\n%2")
        .arg(failed).arg(failures.join("\n"));
    QMessageBox::information(mainWindow_, "Export Mesh", summary);
}

void MenuBar::exportStressResults()
{
    QMessageBox::information(mainWindow_, "Export Stress Results",
        "No stress results available.\nPlease run Strain Prediction first.");
}

// ---- Help ----

void MenuBar::showHelp()
{
    QMessageBox::information(mainWindow_, "Documentation",
        "Documentation will be available soon.\n"
        "For now, please refer to the user manual.");
}

void MenuBar::showAbout()
{
    QMessageBox aboutBox(mainWindow_);
    aboutBox.setWindowTitle("About SpineX");
    aboutBox.setIconPixmap(QPixmap());
    aboutBox.setTextFormat(Qt::RichText);
#ifdef SPINEX_INSTITUTE
    const QString editionLabel =
        "<div style='color:#dc2626;font-weight:bold;font-size:12px;margin-top:2px;'>"
        "Institute Edition (Internal Use Only)</div>";
#else
    const QString editionLabel = QString();
#endif
    aboutBox.setText(QString(
        "<div style='text-align: center;'>"
        "<h2 style='color: #2c5f6f; margin-bottom: 4px;'>SpineX v%1</h2>").arg(UpdateChecker::currentVersion()) +
        editionLabel +
        QString(
        "<p style='color: #6b7280; font-size: 14px;'>An expert spine analysis platform</p>"
        "<hr style='border: 1px solid #dde1e7;'>"
        "<p style='font-size: 13px;'>"
        "<b>Developed by:</b> \u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8<br>"
        "<b>Contact:</b> <a href='mailto:spinalyzeai@gmail.com' style='color: #2c5f6f;'>spinalyzeai@gmail.com</a>"
        "</p>"
        "<hr style='border: 1px solid #dde1e7;'>"
        "<p style='color: #ef4444; font-size: 11px; font-weight: bold;'>"
        "FOR RESEARCH USE ONLY<br>"
        "Not intended for clinical diagnosis or treatment."
        "</p>"
        "<hr style='border: 1px solid #dde1e7;'>"
        "<p style='color: #9ca3af; font-size: 11px;'>"
        "\u00A9 2025\u20132026 \u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8 All Rights Reserved.<br>"
        "Unauthorized reproduction or distribution is prohibited."
        "</p>"
        "</div>"));
    aboutBox.exec();
}

void MenuBar::showThirdPartyLicenses()
{
    // Try to find the file relative to executable, then source tree
    QString path;
    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/THIRD_PARTY_LICENSES.txt",
        QCoreApplication::applicationDirPath() + "/../../../THIRD_PARTY_LICENSES.txt",
    };
    for (const auto& c : candidates) {
        if (QFile::exists(c)) { path = c; break; }
    }

    if (path.isEmpty()) {
        QMessageBox::information(mainWindow_, "Third-Party Licenses",
            "License file not found.\n\n"
            "SpineX uses the following open-source libraries:\n\n"
            "  Qt 6 (LGPL v3)\n"
            "  VTK (BSD 3-Clause)\n"
            "  pybind11 (BSD 3-Clause)\n"
            "  nlohmann/json (MIT)\n"
            "  nnU-Net (Apache 2.0)\n"
            "  OpenSSL (Apache 2.0)\n"
            "  zlib, libpng, libjpeg-turbo, FreeType,\n"
            "  HarfBuzz, GLEW, fmt, pugixml, LZ4, brotli, pcre2\n\n"
            "See THIRD_PARTY_LICENSES.txt for details.");
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QDialog dlg(mainWindow_);
    dlg.setWindowTitle("Third-Party Licenses");
    dlg.resize(600, 500);
    auto* layout = new QVBoxLayout(&dlg);

    auto* text = new QTextEdit();
    text->setReadOnly(true);
    text->setPlainText(content);
    text->setFont(QFont("Consolas", 9));
    layout->addWidget(text);

    auto* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

void MenuBar::signOut()
{
    auto reply = QMessageBox::question(mainWindow_, "Sign Out",
        "Sign out of your account?\nThe application will restart.",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    LicenseManager::instance().logout();
    // 重启应用
    QProcess::startDetached(QApplication::applicationFilePath(), QApplication::arguments());
    QApplication::quit();
}

// ---- Add-ons ----

void MenuBar::openCobbAngleAddon()
{
    mainWindow_->switchToModule("cobb_angle");
    updateMenuTitle("Cobb Angle");
}
