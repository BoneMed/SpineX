/**
 * ============================================================================
 * DataModule - 数据管理模块（实现）
 * 从 Python modules/data_module.py 1:1 移植
 * ============================================================================
 */

#include "DataModule.h"
#include "app/SpineXApp.h"
#include "io/NiftiIO.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QFont>
#include <QBrush>
#include <QColor>
#include <QRegularExpression>
#include <QFileInfo>
#include <QProgressDialog>
#include <QApplication>
#include <QMenu>
#include <QAction>

DataModule::DataModule(QWidget* parent)
    : ModuleBase("Data Management", parent)
{
    initUI();
}

DataModule::~DataModule() = default;

void DataModule::initUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Module header (matches Python _make_module_header)
    mainLayout->addWidget(makeModuleHeader("Data Management"));

    // Inner content area
    auto* inner = new QWidget();
    inner->setStyleSheet("background-color: #f4f6f8;");
    auto* innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(12, 10, 12, 12);
    innerLayout->setSpacing(10);

    // Description
    auto* desc = new QLabel("Manage all loaded data files. Files are grouped by patient ID.");
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #6b7280; font-size: 9pt; padding: 2px 0;");
    innerLayout->addWidget(desc);

    // Splitter: tree (left) | details (right)
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setStyleSheet("QSplitter::handle { background-color: #dde1e7; }");
    innerLayout->addWidget(splitter, 1);

    splitter->addWidget(createDataTreePanel());
    splitter->addWidget(createDataDetailsPanel());
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(inner, 1);
}

QWidget* DataModule::createDataTreePanel()
{
    auto* panel = new QFrame();
    panel->setStyleSheet("background-color: #ffffff;");
    auto* layout = new QVBoxLayout(panel);

    // Toolbar buttons
    auto* toolbar = new QHBoxLayout();

    auto makeBtn = [](const QString& label, const QString& color,
                      const QString& hover) -> QPushButton* {
        auto* btn = new QPushButton(label);
        btn->setStyleSheet(
            QString("QPushButton { background-color: %1; color: white; }"
                    "QPushButton:hover { background-color: %2; }")
                .arg(color, hover));
        return btn;
    };

    auto* addBtn     = makeBtn("Add NIfTI",       "#16a34a", "#15803d");
    auto* addDicomBtn= makeBtn("Add DICOM Folder","#0d9488", "#0f766e");
    auto* removeBtn  = makeBtn("Remove Selected", "#dc2626", "#b91c1c");
    auto* refreshBtn = makeBtn("Refresh",         "#3b82f6", "#2563eb");

    connect(addBtn,      &QPushButton::clicked, this, &DataModule::addData);
    connect(addDicomBtn, &QPushButton::clicked, this, &DataModule::addDicomFolder);
    connect(removeBtn,   &QPushButton::clicked, this, &DataModule::removeSelectedData);
    connect(refreshBtn,  &QPushButton::clicked, this, &DataModule::refreshDataTree);

    toolbar->addWidget(addBtn);
    toolbar->addWidget(addDicomBtn);
    toolbar->addWidget(removeBtn);
    toolbar->addWidget(refreshBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    // Data tree
    dataTree_ = new QTreeWidget();
    dataTree_->setHeaderLabels({"Patient / File", "Type", "Status"});
    dataTree_->setColumnWidth(0, 300);
    dataTree_->setColumnWidth(1, 120);
    dataTree_->setColumnWidth(2, 100);
    dataTree_->setStyleSheet(R"(
        QTreeWidget { font-size: 10pt; }
        QTreeWidget::item { padding: 3px; }
    )");
    dataTree_->setExpandsOnDoubleClick(true);
    dataTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dataTree_, &QTreeWidget::itemSelectionChanged,
            this, &DataModule::onDataSelected);
    connect(dataTree_, &QTreeWidget::customContextMenuRequested,
            this, &DataModule::showTreeContextMenu);

    layout->addWidget(dataTree_);
    return panel;
}

QWidget* DataModule::createDataDetailsPanel()
{
    auto* panel = new QFrame();
    panel->setStyleSheet("background-color: #f4f6f8;");
    auto* layout = new QVBoxLayout(panel);

    // Details title
    auto* detailsTitle = new QLabel("Data Details");
    QFont titleFont("Arial", 12, QFont::Bold);
    detailsTitle->setFont(titleFont);
    detailsTitle->setStyleSheet("color: #2c5f6f; padding: 5px;");
    layout->addWidget(detailsTitle);

    // Details group
    auto* detailsGroup = new QGroupBox("INFORMATION");
    auto* detailsLayout = new QVBoxLayout(detailsGroup);

    detailsLabel_ = new QLabel("Select a data item to view details");
    detailsLabel_->setWordWrap(true);
    detailsLabel_->setStyleSheet("padding: 10px;");
    detailsLabel_->setTextFormat(Qt::RichText);
    detailsLayout->addWidget(detailsLabel_);
    layout->addWidget(detailsGroup);

    // Actions group
    auto* actionsGroup = new QGroupBox("ACTIONS");
    auto* actionsLayout = new QVBoxLayout(actionsGroup);

    auto makeBtnStyled = [](const QString& label, const QString& color,
                            const QString& hover) -> QPushButton* {
        auto* btn = new QPushButton(label);
        btn->setStyleSheet(
            QString("QPushButton { background-color: %1; color: white; border-radius: 6px;"
                    " min-height: 36px; font-size: 10pt; font-weight: bold; padding: 0 16px; }"
                    "QPushButton:hover { background-color: %2; }")
                .arg(color, hover));
        return btn;
    };

    auto* loadToCtBtn = makeBtnStyled("Load to Segmentation", "#2c5f6f", "#1e4a57");
    connect(loadToCtBtn, &QPushButton::clicked, this, &DataModule::loadToCTSegmentation);
    actionsLayout->addWidget(loadToCtBtn);

    auto* loadToModulusBtn = makeBtnStyled("Load to BMD Analysis", "#3b82f6", "#2563eb");
    connect(loadToModulusBtn, &QPushButton::clicked, this, &DataModule::loadToModulusEstimation);
    actionsLayout->addWidget(loadToModulusBtn);

    layout->addWidget(actionsGroup);
    layout->addStretch();

    return panel;
}

// ---- Actions ----

void DataModule::addData()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Select Data Files", "",
        "Medical Images (*.nii *.nii.gz *.dcm *.mha *.mhd);;All files (*.*)");

    if (files.isEmpty()) return;

    // For now, add each file to tree directly
    for (const QString& filePath : files) {
        QFileInfo fi(filePath);
        QString patientId = extractPatientId(fi.fileName());

        // Get or create patient node
        QTreeWidgetItem* patientItem;
        if (patientItems_.contains(patientId)) {
            patientItem = patientItems_[patientId];
        } else {
            patientItem = new QTreeWidgetItem({patientId, "", ""});
            QFont boldFont("Arial", 10, QFont::Bold);
            patientItem->setFont(0, boldFont);
            patientItem->setForeground(0, QBrush(QColor("#2c5f6f")));
            dataTree_->addTopLevelItem(patientItem);
            patientItems_[patientId] = patientItem;
        }

        // Determine file type
        QString fileType = "Volume";
        QString lowerName = fi.fileName().toLower();
        if (lowerName.contains("mask") || lowerName.contains("seg")) {
            fileType = "Mask";
        }

        // Create file node
        QString status = fi.exists() ? QString::fromUtf8("\u2713 Loaded") : QString::fromUtf8("\u2717 Missing");
        auto* fileItem = new QTreeWidgetItem({fi.fileName(), fileType, status});
        fileItem->setData(0, Qt::UserRole, filePath);
        fileItem->setForeground(2, QBrush(fi.exists() ? QColor(Qt::green) : QColor(Qt::red)));
        patientItem->addChild(fileItem);
        patientItem->setExpanded(true);
    }
}

void DataModule::addDicomFolder()
{
    QString folderPath = QFileDialog::getExistingDirectory(
        this, "Select DICOM Folder", "",
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (folderPath.isEmpty()) return;

    // 显示进度条（不确定进度，旋转动画）
    QProgressDialog progress("Loading DICOM...", QString(), 0, 0, this);
    progress.setWindowTitle("Loading DICOM");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.show();
    QApplication::processEvents();

    // VTK 不是线程安全的，同步执行，进度条显示忙碌状态
    QString tempNiftiPath;
    try {
        std::string result = NiftiIO::dicomToTempNifti(folderPath.toStdString());
        tempNiftiPath = QString::fromStdString(result);
    } catch (const std::exception& e) {
        progress.close();
        QMessageBox::critical(this, "DICOM Error",
            QString("Failed to read DICOM folder:\n%1").arg(e.what()));
        return;
    }
    progress.close();

    // 用文件夹名作为显示名
    QFileInfo folderInfo(folderPath);
    QString displayName = folderInfo.fileName() + " [DICOM]";
    QString patientId = extractPatientId(folderInfo.fileName());

    // 添加到树
    QTreeWidgetItem* patientItem;
    if (patientItems_.contains(patientId)) {
        patientItem = patientItems_[patientId];
    } else {
        patientItem = new QTreeWidgetItem({patientId, "", ""});
        QFont boldFont("Arial", 10, QFont::Bold);
        patientItem->setFont(0, boldFont);
        patientItem->setForeground(0, QBrush(QColor("#2c5f6f")));
        dataTree_->addTopLevelItem(patientItem);
        patientItems_[patientId] = patientItem;
    }

    auto* fileItem = new QTreeWidgetItem({displayName, "DICOM Volume",
                                          QString::fromUtf8("\u2713 Loaded")});
    fileItem->setData(0, Qt::UserRole, tempNiftiPath);
    fileItem->setForeground(2, QBrush(QColor(Qt::green)));
    patientItem->addChild(fileItem);
    patientItem->setExpanded(true);
}

void DataModule::removeSelectedData()
{
    auto selectedItems = dataTree_->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "No data selected");
        return;
    }

    auto reply = QMessageBox::question(
        this, "Confirm Remove",
        "Are you sure you want to remove the selected data?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    for (auto* item : selectedItems) {
        if (!item->parent()) {
            // Patient node — remove entire group
            QString patientId = item->text(0);
            patientItems_.remove(patientId);
            int idx = dataTree_->indexOfTopLevelItem(item);
            dataTree_->takeTopLevelItem(idx);
        } else {
            // File node
            auto* parent = item->parent();
            parent->removeChild(item);

            if (parent->childCount() == 0) {
                QString patientId = parent->text(0);
                patientItems_.remove(patientId);
                int idx = dataTree_->indexOfTopLevelItem(parent);
                dataTree_->takeTopLevelItem(idx);
            }
        }
    }

    QMessageBox::information(this, "Success", "Data removed successfully");
}

void DataModule::refreshDataTree()
{
    // For now just clear and let user re-add
    // In the full version this would pull from app->currentProject
    dataTree_->clear();
    patientItems_.clear();
}

void DataModule::onDataSelected()
{
    auto selectedItems = dataTree_->selectedItems();
    if (selectedItems.isEmpty()) {
        detailsLabel_->setText("Select a data item to view details");
        return;
    }

    auto* item = selectedItems[0];

    if (!item->parent()) {
        // Patient node
        QString patientId = item->text(0);
        int fileCount = item->childCount();

        QString text = QString("<b>Patient ID:</b> %1<br>"
                               "<b>Number of Files:</b> %2<br><br>"
                               "<b>Files:</b><br>")
                           .arg(patientId).arg(fileCount);

        for (int i = 0; i < fileCount; ++i) {
            auto* child = item->child(i);
            text += QString::fromUtf8("\u2022 %1 (%2)<br>")
                        .arg(child->text(0), child->text(1));
        }
        detailsLabel_->setText(text);
    } else {
        // File node
        QString filePath = item->data(0, Qt::UserRole).toString();
        QFileInfo fi(filePath);
        QString patientId = item->parent() ? item->parent()->text(0) : "Unknown";
        QString fileSize = fi.exists()
            ? QString("%1 MB").arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 2)
            : "N/A";

        QString text = QString(
            "<b>Patient ID:</b> %1<br>"
            "<b>File Name:</b> %2<br>"
            "<b>Type:</b> %3<br>"
            "<b>Path:</b> %4<br>"
            "<b>Size:</b> %5<br>"
            "<b>Status:</b> %6")
            .arg(patientId, fi.fileName(), item->text(1),
                 filePath, fileSize,
                 fi.exists() ? "Exists" : "Missing");

        detailsLabel_->setText(text);
    }
}

void DataModule::showTreeContextMenu(const QPoint& pos)
{
    auto* item = dataTree_->itemAt(pos);
    if (!item || !item->parent()) return;  // only on file nodes

    QMenu menu(this);
    auto* setVolume = menu.addAction("Set type: Volume (CT)");
    auto* setMask   = menu.addAction("Set type: Mask");
    const QString currentType = item->text(1).toLower();
    setVolume->setCheckable(true);
    setMask->setCheckable(true);
    setVolume->setChecked(!currentType.contains("mask"));
    setMask->setChecked(currentType.contains("mask"));

    QAction* chosen = menu.exec(dataTree_->viewport()->mapToGlobal(pos));
    if (chosen == setVolume) {
        item->setText(1, "Volume");
    } else if (chosen == setMask) {
        item->setText(1, "Mask");
    }
}

// Collect selected (or expanded patient's) file items, classify by tree "Type" column.
// Returns {ctPath, maskPath}; either may be empty if not selected.
static QPair<QString, QString> collectSelectedPaths(QTreeWidget* tree)
{
    QString ctPath, maskPath;
    QList<QTreeWidgetItem*> items = tree->selectedItems();
    // If a patient (top-level) is selected alone, pull its file children
    QList<QTreeWidgetItem*> files;
    for (auto* it : items) {
        if (it->parent()) {
            files.append(it);
        } else {
            for (int i = 0; i < it->childCount(); ++i) files.append(it->child(i));
        }
    }
    for (auto* it : files) {
        QString path = it->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) continue;
        const QString type = it->text(1).toLower();
        if (type.contains("mask")) {
            if (maskPath.isEmpty()) maskPath = path;
        } else {
            if (ctPath.isEmpty()) ctPath = path;
        }
    }
    return {ctPath, maskPath};
}

void DataModule::loadToCTSegmentation()
{
    if (dataTree_->selectedItems().isEmpty()) {
        QMessageBox::warning(this, "Warning", "No data selected");
        return;
    }
    auto [ctPath, maskPath] = collectSelectedPaths(dataTree_);
    if (ctPath.isEmpty() && maskPath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "No file path found for selected item");
        return;
    }

    if (app_) {
        if (!ctPath.isEmpty())   app_->currentProject["ct_file_path"]   = ctPath;
        if (!maskPath.isEmpty()) app_->currentProject["mask_file_path"] = maskPath;
        app_->dataVersion++;
        app_->switchToModule("ct_segmentation");
    }
}

void DataModule::loadToModulusEstimation()
{
    if (dataTree_->selectedItems().isEmpty()) {
        QMessageBox::warning(this, "Warning", "No data selected");
        return;
    }
    auto [ctPath, maskPath] = collectSelectedPaths(dataTree_);
    if (ctPath.isEmpty() && maskPath.isEmpty()) {
        QMessageBox::warning(this, "Warning", "No file path found for selected item");
        return;
    }

    if (app_) {
        if (!ctPath.isEmpty())   app_->currentProject["ct_file_path"]   = ctPath;
        if (!maskPath.isEmpty()) app_->currentProject["mask_file_path"] = maskPath;
        app_->dataVersion++;
        app_->switchToModule("modulus_estimation");
    }
}

// ---- Patient ID extraction (matches Python extract_patient_id) ----

QString DataModule::extractPatientId(const QString& filename)
{
    QString name = filename;
    name.replace(".nii.gz", "").replace(".nii", "");

    // Pattern1: L1_504, L2_305
    QRegularExpression re1("(L\\d+_\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto match1 = re1.match(name);
    if (match1.hasMatch()) return match1.captured(1).toUpper();

    // Pattern2: pure numeric ID
    QRegularExpression re2("(\\d{3,})");
    auto match2 = re2.match(name);
    if (match2.hasMatch()) return "Patient_" + match2.captured(1);

    // Pattern3: letter + digits
    QRegularExpression re3("([A-Z]\\d+[_-]\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto match3 = re3.match(name);
    if (match3.hasMatch()) return match3.captured(1).toUpper();

    return "Unknown_Patient";
}

// ---- Refresh from project ----

void DataModule::refreshFromProject()
{
    // 不要清空树 — 树已经包含用户添加的数据
    // 只需确保 currentProject 中的文件也在树中显示
    if (!app_) return;

    auto addFileToTree = [&](const QString& filePath, const QString& fileType) {
        if (filePath.isEmpty() || !QFile::exists(filePath)) return;

        // 检查是否已在树中
        for (int i = 0; i < dataTree_->topLevelItemCount(); ++i) {
            auto* patient = dataTree_->topLevelItem(i);
            for (int j = 0; j < patient->childCount(); ++j) {
                if (patient->child(j)->data(0, Qt::UserRole).toString() == filePath)
                    return;  // 已存在
            }
        }

        // 添加到树
        QFileInfo fi(filePath);
        QString patientId = extractPatientId(fi.fileName());
        QTreeWidgetItem* patientItem;
        if (patientItems_.contains(patientId)) {
            patientItem = patientItems_[patientId];
        } else {
            patientItem = new QTreeWidgetItem({patientId, "", ""});
            QFont boldFont("Arial", 10, QFont::Bold);
            patientItem->setFont(0, boldFont);
            patientItem->setForeground(0, QBrush(QColor("#2c5f6f")));
            dataTree_->addTopLevelItem(patientItem);
            patientItems_[patientId] = patientItem;
        }

        auto* fileItem = new QTreeWidgetItem({fi.fileName(), fileType,
                                              QString::fromUtf8("\u2713 Loaded")});
        fileItem->setData(0, Qt::UserRole, filePath);
        fileItem->setForeground(2, QBrush(QColor(Qt::green)));
        patientItem->addChild(fileItem);
        patientItem->setExpanded(true);
    };

    addFileToTree(app_->currentProject.value("ct_file_path").toString(), "Volume");
    addFileToTree(app_->currentProject.value("mask_file_path").toString(), "Mask");
}
