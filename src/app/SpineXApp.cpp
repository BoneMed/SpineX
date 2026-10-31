/**
 * ============================================================================
 * SpineXApp - 主窗口（实现）
 * 从 Python APP_Main.py 的 SpinalyzsPyQt 1:1 移植
 * ============================================================================
 */

#include "SpineXApp.h"
#include "ui/MenuBar.h"
#include "modules/DataModule.h"
#include "modules/CTSegmentationModule.h"
#include "modules/ModulusEstimationModule.h"
#include "modules/StressPredictionModule.h"
#include "modules/CobbAngleAddon.h"
#include "app/UpdateChecker.h"
#include <QTimer>

#include "LicenseManager.h"
#include "io/ProjectIO.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <iostream>

SpineXApp::SpineXApp(QWidget* parent)
    : QMainWindow(parent)
    , currentModule_(nullptr)
{
    setWindowTitle("SpineX - An expert spine surgery platform");

    // Apply global stylesheet at QApplication level (matches Python)
    qApp->setStyleSheet(config_.stylesheet());

    initUI();
    showWelcomeScreen();

    // 启动 license 运行期定时检查（每60秒）
    auto& lic = LicenseManager::instance();
    connect(&lic, &LicenseManager::licenseExpired, this, [this]() {
        QMessageBox::critical(this, "License Expired",
            "Your license has expired.\n"
            "The application will now close.\n\n"
            "Please contact spinalyzeai@gmail.com to renew.");
        QApplication::quit();
    });
    lic.startPeriodicCheck();

#ifndef SPINEX_INSTITUTE
    // Check for updates 3 seconds after startup (non-blocking, silent if up-to-date).
    // Institute edition disables auto-update — distributed manually.
    QTimer::singleShot(3000, this, [this]() {
        UpdateChecker checker(this);
        checker.checkForUpdate(this, true);
    });
#endif
}

SpineXApp::~SpineXApp() = default;

void SpineXApp::initUI()
{
    // Top-level stacked widget: page 0 = welcome, page 1 = workspace
    stackedWidget_ = new QStackedWidget();
    setCentralWidget(stackedWidget_);

    // Page 0: Welcome screen
    welcomeScreen_ = new WelcomeScreen();
    stackedWidget_->addWidget(welcomeScreen_);

    // Page 1: Workspace = [module control stack | shared viewport]
    workspace_ = new QWidget();
    workspace_->setStyleSheet("background-color: #1a1a1a;");

    auto* wsLayout = new QHBoxLayout(workspace_);
    wsLayout->setContentsMargins(0, 0, 0, 0);
    wsLayout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Horizontal);
    splitter_->setHandleWidth(3);
    splitter_->setStyleSheet("QSplitter::handle { background-color: #333333; }");

    // Left: stacked control panels (fixed ~360-480px)
    moduleControlStack_ = new QStackedWidget();
    moduleControlStack_->setMinimumWidth(360);
    moduleControlStack_->setMaximumWidth(480);
    moduleControlStack_->setStyleSheet("background-color: #ffffff;");
    splitter_->addWidget(moduleControlStack_);

    // Right: shared 4-panel viewport
    sharedViewport_ = new SharedViewport();
    splitter_->addWidget(sharedViewport_);

    splitter_->setStretchFactor(0, 0);   // control panel: fixed
    splitter_->setStretchFactor(1, 1);   // viewport: stretch

    wsLayout->addWidget(splitter_);
    stackedWidget_->addWidget(workspace_);

    // Menu bar (created after UI so it can reference mainWindow)
    menuBar_ = new MenuBar(this);

    // Status bar with copyright notice
    auto* copyrightLabel = new QLabel(
        QString::fromUtf8("  \u00A9 2025\u20132026 \u4E0A\u6D77\u67CF\u6069\u7F8E\u533B\u7597\u79D1\u6280\u6709\u9650\u516C\u53F8 All Rights Reserved.  "));
    copyrightLabel->setStyleSheet(
        "color: #9ca3af; font-size: 11px; padding: 2px 8px;");

    statusBar()->addPermanentWidget(copyrightLabel);
    statusBar()->setStyleSheet(
        "QStatusBar { background-color: #ffffff; border-top: 1px solid #dde1e7; }"
        "QStatusBar::item { border: none; }");
    statusBar()->setSizeGripEnabled(false);
}

// ---- Page switching ----

void SpineXApp::showWelcomeScreen()
{
    stackedWidget_->setCurrentIndex(0);
}

void SpineXApp::showWorkspace()
{
    stackedWidget_->setCurrentIndex(1);
}

// ---- Module management (lazy creation, matches Python get_or_create_module) ----

void SpineXApp::switchToModule(const QString& moduleKey)
{
    ModuleBase* module = nullptr;

    auto it = moduleInstances_.find(moduleKey);
    if (it != moduleInstances_.end()) {
        module = it->second;
    } else {
        // Lazy-create module on first access
        if (moduleKey == "data_module") {
            module = new DataModule(this);
        } else if (moduleKey == "ct_segmentation") {
            module = new CTSegmentationModule(this);
        } else if (moduleKey == "modulus_estimation") {
            module = new ModulusEstimationModule(this);
        } else if (moduleKey == "stress_prediction") {
            module = new StressPredictionModule(this);
        } else if (moduleKey == "cobb_angle") {
            module = new CobbAngleAddon(this);
        } else {
            std::cerr << "[SpineXApp] Unknown module: "
                      << moduleKey.toStdString() << std::endl;
            return;
        }

        module->setApp(this);
        moduleInstances_[moduleKey] = module;
        moduleControlStack_->addWidget(module);
    }

    // Activate
    showWorkspace();
    moduleControlStack_->setCurrentWidget(module);
    currentModule_ = module;

    // Data module takes full width — no shared viewport needed (matches Python)
    bool isData = (moduleKey == "data_module");
    setViewportVisible(!isData);

    if (!isData) {
        sharedViewport_->setActiveModule(module);
        // Restore 4-panel layout unless entering Cobb angle (which uses 3D only)
        if (moduleKey != "cobb_angle") {
            sharedViewport_->setOnly3DMode(false);
        }
    }

    // Refresh from project data
    module->refreshFromProject();
}

// ---- Viewport visibility ----

void SpineXApp::setViewportVisible(bool visible)
{
    sharedViewport_->setVisible(visible);
    if (visible) {
        moduleControlStack_->setMaximumWidth(480);
    } else {
        moduleControlStack_->setMaximumWidth(16777215);  // unlimited
    }
}

ModuleBase* SpineXApp::moduleInstance(const QString& key) const
{
    auto it = moduleInstances_.find(key);
    return (it != moduleInstances_.end()) ? it->second : nullptr;
}

// ---- Close event: prompt to save ----

void SpineXApp::closeEvent(QCloseEvent* event)
{
    // 如果有数据，提醒保存
    if (!currentProject.value("ct_file_path").toString().isEmpty()) {
        auto reply = QMessageBox::question(this, "Quit SpineX",
            "Do you want to save the project before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }

        if (reply == QMessageBox::Save) {
            QString siPath = currentProject.value("si_file_path").toString();
            if (siPath.isEmpty()) {
                // 没有设过保存路径，让用户输入名称和选文件夹
                bool ok;
                QString name = QInputDialog::getText(this, "Save Project",
                    "Project name:", QLineEdit::Normal,
                    currentProject.value("project_name", "Untitled").toString(), &ok);
                if (!ok || name.isEmpty()) { event->ignore(); return; }

                QString parentDir = QFileDialog::getExistingDirectory(this,
                    "Select folder to save project in");
                if (parentDir.isEmpty()) { event->ignore(); return; }

                QString projectDir = parentDir + "/" + name;
                QDir().mkpath(projectDir);
                siPath = projectDir + "/" + name + ".si";

                currentProject["project_name"] = name;
                currentProject["project_dir"] = projectDir;
                currentProject["si_file_path"] = siPath;
            }
            ProjectIO::saveAsSI(currentProject, siPath.toStdString());
        }
    }

    event->accept();
}
