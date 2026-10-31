/**
 * ============================================================================
 * CobbAngleAddon - Cobb角测量模块
 * ============================================================================
 * Algorithm ported from SpineX-Py tests/test_cobb_angle.py:
 *   1. For each vertebra, keep anterior 50% to isolate vertebral body
 *   2. Project onto coronal plane (collapse Y/AP axis)
 *   3. RANSAC fit endplate lines on superior/inferior boundaries
 *   4. Detect scoliotic curves via inflection points in tilt progression
 *   5. Compute Cobb angles for thoracic, thoracic-lumbar, lumbar regions
 */

#ifndef SPINEX_COBB_ANGLE_ADDON_H
#define SPINEX_COBB_ANGLE_ADDON_H

#include "ModuleBase.h"

#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QMap>
#include <QVector>
#include <QTextEdit>
#include <QStackedWidget>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkActor.h>

#include <vector>

class CobbAngleAddon : public ModuleBase
{
    Q_OBJECT

public:
    explicit CobbAngleAddon(QWidget* parent = nullptr);
    ~CobbAngleAddon();

    void refreshFromProject() override;

private:
    void initUI();
    QWidget* createLeftPanel();

    // Vertebra selection
    void selectAllVertebrae();
    void deselectAllVertebrae();
    void populateVertebraListFromMask(vtkSmartPointer<vtkImageData> maskData);

    // ---- Core algorithm types ----

    // RANSAC line fit result: z = slope * x + intercept (in mm)
    struct LineFit {
        double slope = 0;
        double intercept = 0;
        double residual = 0;
        double inlierRatio = 0;
        int nPoints = 0;
        double xMin = 0, xMax = 0;   // extent in mm
        bool valid = false;
    };

    // Per-vertebra endplate tilt info
    struct VertTilt {
        int label = 0;
        QString name;
        QString region;        // "cervical","thoracic","lumbar","sacral"
        double supTilt = 0;    // superior endplate tilt (degrees)
        double infTilt = 0;    // inferior endplate tilt (degrees)
        double supSlope = 0, infSlope = 0;
        double supIntercept = 0, infIntercept = 0;
        double supXMin = 0, supXMax = 0;
        double infXMin = 0, infXMax = 0;
        double centroidX = 0;  // horizontal center in mm (VTK physical)
        double centroidY = 0;  // AP center in mm (VTK physical)
        double centroidZ = 0;  // vertical center in mm (VTK physical)
        QString flag;          // "", "LOW_INLIER", "FEW_PTS"
        bool valid = false;
    };

    // Detected scoliotic curve
    struct CobbCurve {
        QString upperName, lowerName;
        int upperIdx = -1, lowerIdx = -1;  // index into tilts_ vector
        double cobb = 0;
        QString region;       // "thoracic", "lumbar", "thoracic-lumbar"
        QString convexity;    // "left" or "right"
        int nVert = 0;
        QString apexName;     // apex vertebra (e.g. "T8")
        int apexLabel = 0;    // apex label number
    };

    // Sagittal clinical parameter (Step 4: TK / CL / LL_L1_L5 / cSVA)
    struct SagittalParam {
        QString name;            // e.g. "TK"
        QString description;     // human-readable description
        bool available = false;
        double value = 0;
        QString unit;            // "deg" or "mm"
        QString clinicalStatus;  // normal / abnormal_low / abnormal_high / missing_required_vertebrae
        QString normalRange;     // e.g. "41-48 deg"
        QString missingLabels;
        QString startReference;  // e.g. "T4 top"
        QString endReference;    // e.g. "T12 bottom"
    };

    // Step 4 sagittal landmark (e.g. C2 body center, C7 posterior corner)
    struct Landmark {
        QString name;
        int label = 0;
        double position[3] = {0, 0, 0};
    };

    // ---- Algorithm functions ----
    LineFit ransacLine(const std::vector<double>& x, const std::vector<double>& z,
                       int nIter = 500, double threshMm = 1.5);
    VertTilt getEndplateTilt(vtkImageData* mask, int label,
                            const double spacing[3], const double origin[3],
                            int hDim, int vDim, int apDim,
                            double anteriorPct = 50.0);
    void computeAllTilts();
    void findCurves(double minCobb = 5.0);
    static double cobbFromSlopes(double s1, double s2);
    static QString getRegion(int label);

    // 3D rendering
    void render3DSelected();                   // render only checked vertebrae
    void onVertebraToggled(int label, bool visible);

    // Cobb angle
    void calculateCobbAngle();
    void clearMeasurement();
    void recolor3DVertebrae();   // recolor + draw endplate lines in 3D
    void renderCoronalPlot();    // draw coronal projection plot
    void showResultsPage();
    void showControlsPage();

    // Logging
    void appendLog(const QString& msg);

    // UI
    QWidget* vertebraSection_;
    QVBoxLayout* vertebraListLayout_;
    QMap<int, QCheckBox*> vertebraCheckboxes_;

    QPushButton* calculateBtn_;
    QPushButton* clearBtn_;
    QCheckBox* doubleThoracicCb_;

    QLabel* cobbAngleLbl_;
    QLabel* upperVertLbl_;
    QLabel* lowerVertLbl_;

    // Multi-curve result labels
    QVBoxLayout* curvesResultLayout_;
    QVector<QWidget*> curveLabels_;

    QTextEdit* logText_;

    // Page switching (controls vs results)
    QStackedWidget* pageStack_;
    QWidget* controlsPage_;
    QWidget* resultsPage_;
    QLabel* plotLabel_;
    QPushButton* viewResultsBtn_;
    QPushButton* backBtn_;

    // Algorithm state
    std::vector<VertTilt> tilts_;
    std::vector<CobbCurve> curves_;
    std::vector<SagittalParam> sagittalParams_;
    std::vector<Landmark> landmarks_;

    // 3D actor tracking (per-label)
    QMap<int, vtkSmartPointer<vtkActor>> vertebraActors_;

    // Per-vertebra endplate / fitted-plane meshes loaded from Python output.
    // Keyed by vertebra label so the sagittal view can highlight specific
    // endpoints (C2/C7/T4/T12/L1/L5) without re-running the pipeline.
    QMap<int, vtkSmartPointer<vtkActor>> topEndplateActors_;
    QMap<int, vtkSmartPointer<vtkActor>> bottomEndplateActors_;

    // Extra actors created for the sagittal view (cSVA spheres + lines + endplate
    // highlights). Cleared and rebuilt every time the view mode toggles.
    std::vector<vtkSmartPointer<vtkActor>> sagittalAuxActors_;

    // View mode toggle: false = coronal Cobb (default), true = sagittal params.
    QPushButton* viewCoronalBtn_ = nullptr;
    QPushButton* viewSagittalBtn_ = nullptr;
    bool viewSagittal_ = false;

    // Sagittal-side result section in the left panel.
    QVBoxLayout* sagittalResultLayout_ = nullptr;
    QVector<QWidget*> sagittalLabels_;

    void render3DCoronal();
    void render3DSagittal();
    void replaceVertebraActorsFromVtp();   // load full_<label>.vtp into vertebraActors_
    void buildSagittalAux();   // build endplate + cSVA actors once, then cache
    void switchViewMode(bool sagittal);
};

#endif // SPINEX_COBB_ANGLE_ADDON_H
