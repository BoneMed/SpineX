/**
 * ============================================================================
 * ModulusEstimationModule - BMD Analysis (ported from modulus_estimation.py)
 * ============================================================================
 */

#ifndef SPINEX_MODULUS_ESTIMATION_MODULE_H
#define SPINEX_MODULUS_ESTIMATION_MODULE_H

#include "ModuleBase.h"

#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QGroupBox>
#include <QVector>
#include <QSet>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkActor.h>
#include <vtkPolyData.h>
#include <vtkPlane.h>
#include <vtkImplicitPlaneWidget2.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkUnstructuredGrid.h>

class ModulusEstimationModule : public ModuleBase
{
    Q_OBJECT

public:
    explicit ModulusEstimationModule(QWidget* parent = nullptr);
    ~ModulusEstimationModule();

    void refreshFromProject() override;

    // Public access for StressPredictionModule
    struct MeshData {
        int label;
        vtkSmartPointer<vtkUnstructuredGrid> grid;
        vtkSmartPointer<vtkActor> actor;
    };
    const QVector<MeshData>& meshResults() const { return meshResults_; }
    vtkImageData* eVolume() const { return eVolume_.Get(); }

private:
    void initUI();
    QWidget* createLeftPanel();
    QGroupBox* createOperationsGroup();
    QGroupBox* createMaterialDisplayGroup();
    QGroupBox* createCalibrationGroup();

    // Operations
    void computeModulus();
    void runMeshing();
    void runBCSelection();
    void runRegistration();

    // Display
    void toggleMaterialOverlay(bool checked);
    void updateOverlayAlpha(int val);
    void updateBMD();

    // 3D visualization
    void build3DView();
    void doModulusClip(double nx, double ny, double nz,
                       double ox, double oy, double oz);
    void setupClipPlaneWidget();
    void onClipPlaneChanged(vtkObject*, unsigned long, void*);

    // E_volume overlay on 2D slices
    void updateAllSliceDisplays();

    // Logging
    void appendLog(const QString& msg);

    // UI
    QPushButton* computeBtn_;
    QPushButton* meshingBtn_;
    QPushButton* bcSelectionBtn_;

    QCheckBox* overlayCheckbox_;
    QSlider* overlayAlphaSlider_;
    QLabel* alphaValLbl_;

    QLineEdit* slopeEdit_;
    QLineEdit* interceptEdit_;
    QLineEdit* coefEdit_;
    QLineEdit* expEdit_;
    QLineEdit* rhoEdit_;
    QPushButton* updateBMDBtn_;

    QTextEdit* statsText_;

    // Vertebra selection
    QGroupBox* vertebraSection_;
    QVBoxLayout* vertebraListLayout_;
    QMap<int, QCheckBox*> bmdVertCheckboxes_;
    void populateBMDVertebraList();
    void onBMDVertebraToggled(int label, bool visible);

    // State
    bool showMaterialOverlay_ = false;
    double overlayAlpha_ = 0.5;

    // E_volume: 3D elastic modulus array stored as vtkImageData
    vtkSmartPointer<vtkImageData> eVolume_;

    // 3D mesh data
    struct VertMesh {
        int label;
        vtkSmartPointer<vtkPolyData> polyData;
        vtkSmartPointer<vtkActor> actor;            // surface mesh actor (unused for now)
        vtkSmartPointer<vtkActor> eClipActor;       // per-label E-colored clip actor
        vtkSmartPointer<vtkUnstructuredGrid> eGrid;  // per-label E volume
    };
    QVector<VertMesh> cachedMeshes_;        // ghost shell surface meshes
    QVector<vtkSmartPointer<vtkActor>> ghostActors_;
    QVector<vtkSmartPointer<vtkActor>> clipActors_;  // clipped volume actors

    // E-colored volume for solid clip (like Python's threshold + clip)
    vtkSmartPointer<vtkUnstructuredGrid> eColoredVolume_;
    bool hasEColoredVolume_ = false;
    double eRangeMin_ = 100, eRangeMax_ = 25000;

    // Mesh 3D view data (TET10 meshes per label) — struct defined in public
    QVector<MeshData> meshResults_;
    void buildMesh3DView();

    // Clip plane
    vtkSmartPointer<vtkPlane> clipPlane_;
    vtkSmartPointer<vtkImplicitPlaneWidget2> planeWidget_;
    vtkSmartPointer<vtkLookupTable> jetLUT_;
    vtkSmartPointer<vtkScalarBarActor> scalarBar_;
    double clipNormal_[3] = {1.0, 0.0, 0.0};
    double clipOrigin_[3] = {0.0, 0.0, 0.0};

    // Calibration results
    double calibSlope_ = 0.0;
    double calibIntercept_ = 0.0;
    bool hasCalibration_ = false;
    double ctHUOffset_ = 0.0;   // -1024 if CT is raw unsigned, else 0

    // Jet colormap helper
    void initJetLUT();
};

#endif // SPINEX_MODULUS_ESTIMATION_MODULE_H
