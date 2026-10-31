/**
 * ============================================================================
 * CTSegmentationModule - CT分割模块（对应 modules/ct_segmentation.py）
 * ============================================================================
 */

#ifndef SPINEX_CT_SEGMENTATION_MODULE_H
#define SPINEX_CT_SEGMENTATION_MODULE_H

#include "ModuleBase.h"

#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QTextEdit>
#include <QScrollArea>
#include <QGroupBox>
#include <QMap>
#include <QSet>
#include <QVector>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkActor.h>
#include <vtkPolyData.h>
#include <vtkPlane.h>
#include <vtkImplicitPlaneWidget2.h>

class CTSegmentationModule : public ModuleBase
{
    Q_OBJECT

public:
    explicit CTSegmentationModule(QWidget* parent = nullptr);
    ~CTSegmentationModule();

    void refreshFromProject() override;

private:
    void initUI();
    QWidget* createLeftPanel();

    // Brush mode
    void setBrushMode(const QString& mode);
    void onBrushSizeChanged(int val);
    void applyBrush(const QString& viewName, int cx, int cy);
    void onBrushRelease(const QString& viewName);
    void directPaintPreview(const QString& viewName, int cx, int cy);
    void onVertebraToggled(int label, bool visible);
    void setActivePaintLabel(int label);
    void buildCached3D();
    void rebuildSingleMesh(int label);
    void doClip(double nx, double ny, double nz, double ox, double oy, double oz);
    void setupClipPlaneWidget();
    void onClipPlaneChanged(vtkObject*, unsigned long, void*);

    // Cached 3D meshes: per-label unclipped polydata + clipped actors
    struct VertMesh {
        int label;
        vtkSmartPointer<vtkPolyData> polyData;  // unclipped surface
        vtkSmartPointer<vtkActor> actor;         // clipped actor in scene
    };
    QVector<VertMesh> cachedMeshes_;

    // Clip plane state
    vtkSmartPointer<vtkPlane> clipPlane_;
    vtkSmartPointer<vtkImplicitPlaneWidget2> planeWidget_;
    double clipNormal_[3] = {1.0, 0.0, 0.0};
    double clipOrigin_[3] = {0.0, 0.0, 0.0};

    // Segmentation
    void requestDLSegmentation();

    // Mask display
    void onShowMaskToggled(bool checked);
    void onOpacityChanged(int val);

    // Vertebra
    void populateVertebraList();
    void populateVertebraListFromMask(vtkSmartPointer<vtkImageData> maskData);
    void selectAllVertebrae();
    void deselectAllVertebrae();

    // Logging
    void appendLog(const QString& msg);

    // UI components
    QPushButton* btnPaint_;
    QPushButton* btnErase_;
    QPushButton* btnBrushOff_;
    QSlider* brushSizeSlider_;
    QLabel* brushSizeValLbl_;

    QPushButton* segmentBtn_;

    QCheckBox* showMaskCheckbox_;
    QSlider* opacitySlider_;
    QLabel* opacityValLbl_;

    // CT brightness/contrast (window/level)
    QSlider* windowSlider_ = nullptr;
    QLabel*  windowValLbl_ = nullptr;
    QSlider* levelSlider_  = nullptr;
    QLabel*  levelValLbl_  = nullptr;

    QWidget* vertebraSection_;
    QVBoxLayout* vertebraListLayout_;
    QMap<int, QCheckBox*> vertebraCheckboxes_;
    QMap<int, QPushButton*> paintLabelBtns_;  // ● paint selector per label

    QTextEdit* logText_;

    // State
    QString brushMode_;  // "paint", "erase", "off"
    int brushRadius_;
    QSet<int> erasedLabels_;  // labels erased during current stroke
    int activePaintLabel_;

    // Vertebra colors (matches Python _VERTEBRA_COLORS)
    static const QVector<QColor>& vertebraColors();
    static const QStringList& vertebraNames();
};

#endif // SPINEX_CT_SEGMENTATION_MODULE_H
