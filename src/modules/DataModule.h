/**
 * ============================================================================
 * DataModule - 数据管理模块（对应 modules/data_module.py）
 * ============================================================================
 */

#ifndef SPINEX_DATA_MODULE_H
#define SPINEX_DATA_MODULE_H

#include "ModuleBase.h"

#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QGroupBox>
#include <QMap>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>

class DataModule : public ModuleBase
{
    Q_OBJECT

public:
    explicit DataModule(QWidget* parent = nullptr);
    ~DataModule();

    void refreshFromProject() override;

Q_SIGNALS:
    void dataLoaded(vtkSmartPointer<vtkImageData> ctData);
    void maskLoaded(vtkSmartPointer<vtkImageData> maskData);

private:
    void initUI();
    QWidget* createDataTreePanel();
    QWidget* createDataDetailsPanel();

    // Actions
    void addData();
    void addDicomFolder();
    void removeSelectedData();
    void refreshDataTree();
    void onDataSelected();
    void loadToCTSegmentation();
    void loadToModulusEstimation();
    void showTreeContextMenu(const QPoint& pos);

    // Helper
    static QString extractPatientId(const QString& filename);

    // UI
    QTreeWidget* dataTree_;
    QLabel* detailsLabel_;

    // Patient grouping
    QMap<QString, QTreeWidgetItem*> patientItems_;
};

#endif // SPINEX_DATA_MODULE_H
