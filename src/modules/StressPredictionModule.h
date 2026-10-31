/**
 * ============================================================================
 * StressPredictionModule - Strain/Stress Distribution Prediction
 * Uses Hybrid DualEncoder (DualEncoderUNet_Sparse) via Python bridge
 * ============================================================================
 */

#ifndef SPINEX_STRESS_PREDICTION_MODULE_H
#define SPINEX_STRESS_PREDICTION_MODULE_H

#include "ModuleBase.h"

#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QGroupBox>
#include <QSlider>
#include <QCheckBox>
#include <QLineEdit>
#include <QProgressBar>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkActor.h>
#include <vtkPlane.h>
#include <vtkImplicitPlaneWidget2.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkUnstructuredGrid.h>

class InferenceWorker;

class StressPredictionModule : public ModuleBase
{
    Q_OBJECT

public:
    explicit StressPredictionModule(QWidget* parent = nullptr);
    ~StressPredictionModule();

    void refreshFromProject() override;

private:
    void initUI();
    QWidget* createLeftPanel();
    QGroupBox* createOperationsGroup();
    QGroupBox* createDataStatusGroup();
    QGroupBox* createDisplayGroup();
    QGroupBox* createStressStatsGroup();

    // Operations
    void loadNPZ();
    void openBCSelection();
    void startPrediction();

    // BC Selection helpers
    void generateNPZFromBC(const QVector<float>& isBC,
                           const QVector<float>& isLoad, double uDisp);

    // Display
    void toggleStressOverlay(bool checked);
    void updateOverlayAlpha(int val);

    // 3D visualization
    void build3DStressView();
    void setupClipPlaneWidget();
    void doStressClip(double nx, double ny, double nz,
                      double ox, double oy, double oz);
    void onClipPlaneChanged(vtkObject*, unsigned long, void*);

    // Status
    void updateStatusDisplay();
    void updateStressStats();

    // Logging
    void appendLog(const QString& msg);

    // Inference callback
    void onPredictionFinished();
    void onPredictionError(const QString& err);

    // Jet LUT
    void initJetLUT();

    // ---- UI ----
    QPushButton* bcSelectBtn_;
    QPushButton* loadNpzBtn_;
    QPushButton* predictBtn_;
    QProgressBar* progressBar_;

    QLabel* statusLabel_;

    QCheckBox* overlayCheckbox_;
    QSlider* overlayAlphaSlider_;
    QLabel* alphaValLbl_;

    QTextEdit* statsText_;
    QTextEdit* logText_;

    // ---- State ----
    QString npzPath_;
    bool hasNpzData_ = false;
    bool hasBCData_ = false;
    int nBCElements_ = 0;
    int nLoadElements_ = 0;
    double uDisp_ = 1.0;
    bool hasResult_ = false;
    bool showStressOverlay_ = false;
    double overlayAlpha_ = 0.5;

    // Stress result stored as vtkImageData (R³ grid)
    vtkSmartPointer<vtkImageData> stressVolume_;
    double stressMin_ = 0.0, stressMax_ = 0.0;
    double bboxMin_[3] = {0, 0, 0};
    double bboxMax_[3] = {0, 0, 0};
    int gridRes_ = 256;

    // 3D clip visualization
    vtkSmartPointer<vtkUnstructuredGrid> stressGrid_;
    vtkSmartPointer<vtkActor> clipActor_;
    vtkSmartPointer<vtkPlane> clipPlane_;
    vtkSmartPointer<vtkImplicitPlaneWidget2> planeWidget_;
    vtkSmartPointer<vtkLookupTable> jetLUT_;
    vtkSmartPointer<vtkScalarBarActor> scalarBar_;
    double clipNormal_[3] = {1.0, 0.0, 0.0};
    double clipOrigin_[3] = {0.0, 0.0, 0.0};

    // Inference worker
    InferenceWorker* worker_ = nullptr;
};

#endif // SPINEX_STRESS_PREDICTION_MODULE_H
