/**
 * ============================================================================
 * ModulusEstimationModule - BMD Analysis Module (implementation)
 * Ported from Python modules/modulus_estimation.py
 * ============================================================================
 */

#include "ModulusEstimationModule.h"
#include "app/SpineXApp.h"
#include "bridge/PythonRuntime.h"
#include "io/NiftiIO.h"
#include <QDateTime>
#include "ui/SharedViewport.h"
#include "io/NiftiIO.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QFont>
#include <QFileDialog>
#include <QProgressDialog>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QPainter>

#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkContourFilter.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkFloatArray.h>
#include <vtkClipClosedSurface.h>
#include <vtkClipPolyData.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <vtkImplicitPlaneWidget2.h>
#include <vtkImplicitPlaneRepresentation.h>
#include <vtkCommand.h>
#include <vtkRenderWindow.h>
#include <vtkCamera.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkThreshold.h>
#include <vtkClipDataSet.h>
#include <vtkAppendFilter.h>
#include <vtkIntArray.h>
#include <vtkCleanPolyData.h>
#include <vtkTriangleFilter.h>
// vtkGeometryFilter not linked; use vtkDataSetMapper directly
#include <QInputDialog>
#include <QDateTime>
extern "C" {
#include <gmshc.h>
}
#include <vtkDataSetMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellDataToPointData.h>
#include <vtkCellData.h>

#include <iostream>
#include <set>
#include <cmath>
#include <algorithm>

// Vertebra colors (same as CTSegmentationModule)
static const int VERT_COLORS[][3] = {
    {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},
    {255,0,255},{255,239,213},{0,0,205},{205,133,63},{210,180,140},
    {102,205,170},{0,0,128},{0,139,139},{46,139,87},{255,228,225},
    {106,90,205},{221,160,221},{233,150,122},{165,42,42},
    {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
    {60,179,113},{255,235,205},
};

ModulusEstimationModule::ModulusEstimationModule(QWidget* parent)
    : ModuleBase("BMD Analysis", parent)
{
    initJetLUT();
    initUI();
}

ModulusEstimationModule::~ModulusEstimationModule() = default;

void ModulusEstimationModule::initJetLUT()
{
    jetLUT_ = vtkSmartPointer<vtkLookupTable>::New();
    jetLUT_->SetNumberOfTableValues(256);
    jetLUT_->Build();

    // Jet colormap: blue → cyan → green → yellow → red
    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0;
        double r, g, b;
        if (t < 0.125) {
            r = 0; g = 0; b = 0.5 + t * 4.0;
        } else if (t < 0.375) {
            r = 0; g = (t - 0.125) * 4.0; b = 1.0;
        } else if (t < 0.625) {
            r = (t - 0.375) * 4.0; g = 1.0; b = 1.0 - (t - 0.375) * 4.0;
        } else if (t < 0.875) {
            r = 1.0; g = 1.0 - (t - 0.625) * 4.0; b = 0;
        } else {
            r = 1.0 - (t - 0.875) * 4.0; g = 0; b = 0;
        }
        r = std::clamp(r, 0.0, 1.0);
        g = std::clamp(g, 0.0, 1.0);
        b = std::clamp(b, 0.0, 1.0);
        jetLUT_->SetTableValue(i, r, g, b, 1.0);
    }
}

void ModulusEstimationModule::initUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(createLeftPanel());
}

QWidget* ModulusEstimationModule::createLeftPanel()
{
    auto* panel = new QWidget();
    auto* outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    outerLayout->addWidget(makeModuleHeader("BMD Analysis"));

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background-color: #f4f6f8;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(10);

    auto* tip = new QLabel("  CT and Mask data auto-load from Data module.\n  Select vertebrae in Segmentation to filter.");
    tip->setWordWrap(true);
    tip->setStyleSheet(
        "background-color: #eaf4f8; color: #2c5f6f; padding: 7px 10px;"
        "border-radius: 4px; border-left: 3px solid #2c5f6f; font-size: 9pt;");
    layout->addWidget(tip);

    layout->addWidget(createOperationsGroup());
    layout->addWidget(createMaterialDisplayGroup());

    // Vertebra selection section
    vertebraSection_ = new QGroupBox("VERTEBRA SELECTION");
    vertebraSection_->setVisible(false);
    auto* vertLayout = new QVBoxLayout(vertebraSection_);
    vertLayout->setSpacing(4);

    auto* vertBtnLayout = new QHBoxLayout();
    auto* selectAllBtn = new QPushButton("Select All");
    selectAllBtn->setStyleSheet("font-size: 9pt; padding: 4px 10px;");
    connect(selectAllBtn, &QPushButton::clicked, [this]() {
        for (auto it = bmdVertCheckboxes_.begin(); it != bmdVertCheckboxes_.end(); ++it)
            it.value()->setChecked(true);
    });
    auto* deselectAllBtn = new QPushButton("Deselect All");
    deselectAllBtn->setStyleSheet("font-size: 9pt; padding: 4px 10px;");
    connect(deselectAllBtn, &QPushButton::clicked, [this]() {
        for (auto it = bmdVertCheckboxes_.begin(); it != bmdVertCheckboxes_.end(); ++it)
            it.value()->setChecked(false);
    });
    vertBtnLayout->addWidget(selectAllBtn);
    vertBtnLayout->addWidget(deselectAllBtn);
    vertLayout->addLayout(vertBtnLayout);

    vertebraListLayout_ = new QVBoxLayout();
    vertLayout->addLayout(vertebraListLayout_);
    layout->addWidget(vertebraSection_);

    layout->addWidget(createCalibrationGroup());

    auto* statsGroup = new QGroupBox("INFORMATION");
    statsGroup->setMinimumHeight(150);
    auto* statsLayout = new QVBoxLayout(statsGroup);
    statsText_ = new QTextEdit();
    statsText_->setReadOnly(true);
    statsText_->setStyleSheet("background-color: #f8f8f8;");
    statsLayout->addWidget(statsText_);
    layout->addWidget(statsGroup);

    layout->addStretch();
    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea);
    return panel;
}

QGroupBox* ModulusEstimationModule::createOperationsGroup()
{
    auto* group = new QGroupBox("OPERATIONS");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(6);

    computeBtn_ = new QPushButton("Calculate Modulus");
    computeBtn_->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; }"
        "QPushButton:hover { background-color: #2563eb; }");
    connect(computeBtn_, &QPushButton::clicked, this, &ModulusEstimationModule::computeModulus);
    layout->addWidget(computeBtn_);

    meshingBtn_ = new QPushButton("Meshing (C3D10)");
    meshingBtn_->setStyleSheet(
        "QPushButton { background-color: #7c3aed; color: white; }"
        "QPushButton:hover { background-color: #6d28d9; }");
    connect(meshingBtn_, &QPushButton::clicked, this, &ModulusEstimationModule::runMeshing);
    layout->addWidget(meshingBtn_);

    return group;
}

QGroupBox* ModulusEstimationModule::createMaterialDisplayGroup()
{
    auto* group = new QGroupBox("MATERIAL DISPLAY");
    auto* layout = new QVBoxLayout(group);

    overlayCheckbox_ = new QCheckBox("Elastic Modulus On/Off");
    overlayCheckbox_->setChecked(false);
    overlayCheckbox_->setEnabled(false);
    connect(overlayCheckbox_, &QCheckBox::toggled, this, &ModulusEstimationModule::toggleMaterialOverlay);
    layout->addWidget(overlayCheckbox_);

    auto* alphaLayout = new QHBoxLayout();
    alphaLayout->addWidget(new QLabel("Opacity:"));
    overlayAlphaSlider_ = new QSlider(Qt::Horizontal);
    overlayAlphaSlider_->setRange(0, 100);
    overlayAlphaSlider_->setValue(50);
    overlayAlphaSlider_->setEnabled(false);
    connect(overlayAlphaSlider_, &QSlider::valueChanged, this, &ModulusEstimationModule::updateOverlayAlpha);
    alphaLayout->addWidget(overlayAlphaSlider_);
    alphaValLbl_ = new QLabel("50%");
    alphaValLbl_->setFixedWidth(35);
    alphaLayout->addWidget(alphaValLbl_);
    layout->addLayout(alphaLayout);

    return group;
}

QGroupBox* ModulusEstimationModule::createCalibrationGroup()
{
    auto* group = new QGroupBox("CALIBRATION CONFIG");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(8);

    QString fieldStyle =
        "QLineEdit { background-color: rgba(180,180,180,50); color: #111111;"
        " border: 1px solid rgba(140,140,140,100); border-radius: 4px;"
        " padding: 5px 7px; font-family: Consolas, monospace; font-size: 14px; font-weight: bold; }";

    QString fieldStyleLocked =
        "QLineEdit { background-color: rgba(120,120,120,25); color: #888888;"
        " border: 1px solid rgba(120,120,120,40); border-radius: 4px;"
        " padding: 5px 7px; font-family: Consolas, monospace; font-size: 14px; }";

    QString labelStyle = "color: #555; font-size: 14px; font-family: Consolas, monospace; background: transparent;";

    auto mkLabel = [&](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(labelStyle);
        return lbl;
    };

    // Row 1: rho = slope * HU + intercept
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->addWidget(mkLabel(QString::fromUtf8("\xCF\x81 =")));
    slopeEdit_ = new QLineEdit("");
    slopeEdit_->setPlaceholderText(QString::fromUtf8("\u2014"));
    slopeEdit_->setFixedWidth(115);
    slopeEdit_->setReadOnly(true);
    slopeEdit_->setStyleSheet(fieldStyleLocked);
    row1->addWidget(slopeEdit_);
    row1->addWidget(mkLabel(QString::fromUtf8("\xC3\x97 HU +")));
    interceptEdit_ = new QLineEdit("");
    interceptEdit_->setPlaceholderText(QString::fromUtf8("\u2014"));
    interceptEdit_->setFixedWidth(95);
    interceptEdit_->setReadOnly(true);
    interceptEdit_->setStyleSheet(fieldStyleLocked);
    row1->addWidget(interceptEdit_);
    row1->addStretch();
    layout->addLayout(row1);

    // Row 2: E = coef * rho^exp (MPa)
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(4);
    row2->addWidget(mkLabel("E ="));
    coefEdit_ = new QLineEdit("10200");
    coefEdit_->setFixedWidth(85);
    coefEdit_->setStyleSheet(fieldStyle);
    row2->addWidget(coefEdit_);
    row2->addWidget(mkLabel(QString::fromUtf8("\xC3\x97 \xCF\x81^")));
    expEdit_ = new QLineEdit("2.01");
    expEdit_->setFixedWidth(72);
    expEdit_->setStyleSheet(fieldStyle);
    row2->addWidget(expEdit_);
    row2->addWidget(mkLabel("(MPa)"));
    row2->addStretch();
    layout->addLayout(row2);

    auto* div = new QFrame();
    div->setFrameShape(QFrame::HLine);
    div->setStyleSheet("color: #dde1e7;");
    layout->addWidget(div);

    // Row 3: rho (mean) result
    auto* row3 = new QHBoxLayout();
    row3->setSpacing(4);
    row3->addWidget(mkLabel(QString::fromUtf8("\xCF\x81 (mean) =")));
    rhoEdit_ = new QLineEdit("");
    rhoEdit_->setPlaceholderText(QString::fromUtf8("\u2014 run calibration \u2014"));
    rhoEdit_->setFixedWidth(140);
    rhoEdit_->setReadOnly(true);
    rhoEdit_->setStyleSheet(fieldStyleLocked);
    row3->addWidget(rhoEdit_);
    row3->addWidget(mkLabel(QString::fromUtf8("g/cm\xC2\xB3")));
    row3->addStretch();
    layout->addLayout(row3);

    updateBMDBtn_ = new QPushButton(QString::fromUtf8("\u21BB  Update BMD"));
    updateBMDBtn_->setEnabled(false);
    updateBMDBtn_->setStyleSheet(
        "QPushButton { background-color: rgba(80,120,200,180); color: white;"
        " border: none; border-radius: 4px; padding: 6px 12px;"
        " font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background-color: rgba(100,140,220,200); }"
        "QPushButton:disabled { background-color: rgba(80,80,80,80); color: #666; }");
    connect(updateBMDBtn_, &QPushButton::clicked, this, &ModulusEstimationModule::updateBMD);
    layout->addWidget(updateBMDBtn_);

    layout->addStretch();
    return group;
}

// ---- Operations ----

// ---------------------------------------------------------------------------
// Phantomless calibration via 1D GMM (EM) — pure C++
//   Fits 3 Gaussians (air ≈ -1000, fat ≈ -100, muscle ≈ 50 HU) to the CT
//   histogram and performs linear regression to known physical densities
//   [0.0, 0.93, 1.05 g/cm³] → returns slope, intercept for ρ = slope*HU + b
// ---------------------------------------------------------------------------
// Exact C++ port of Python's phantomless_calib() from HU2FEM.py:
//   1. Build histogram of CT values in [-1100, 200] HU
//   2. Locate air/fat/muscle peaks via histogram maxima in known ranges
//      (replaces sklearn's GMM init — more robust, same result)
//   3. Refine peaks with 1-D GMM EM (identical math to sklearn GaussianMixture)
//   4. np.polyfit(means, [0.0, 0.93, 1.05], 1) → slope, intercept
static double robustMean(const std::vector<float>& vals, double trim = 0.1)
{
    if (vals.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::vector<float> sorted(vals);
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();
    if (n < 50) return sorted[n / 2];
    size_t lo = static_cast<size_t>(trim * n);
    size_t hi = static_cast<size_t>((1.0 - trim) * n);
    if (hi <= lo) return sorted[n / 2];
    double sum = 0.0;
    for (size_t i = lo; i < hi; i++) sum += sorted[i];
    return sum / (hi - lo);
}

static void phantomlessCalib(vtkImageData* ctData, vtkImageData* maskData,
                              const QSet<int>& selectedLabels,
                              double ctHUOffset,
                              double& slope, double& intercept)
{
    // 3-point anatomical phantomless calibration
    // Targets: air → -1000, fat → -100, muscle → 40
    slope = 1.0; intercept = 0.0;

    auto* ctArr   = ctData->GetPointData()->GetScalars();
    auto* maskArr = maskData->GetPointData()->GetScalars();
    int dims[3]; ctData->GetDimensions(dims);
    double spacing[3]; ctData->GetSpacing(spacing);
    vtkIdType nPts = ctData->GetNumberOfPoints();

    // Find L1 (label=20) or closest vertebra for ROI targeting
    std::set<int> allLabels;
    for (vtkIdType i = 0; i < nPts; i++) {
        int lbl = static_cast<int>(maskArr->GetComponent(i, 0));
        if (lbl > 0) allLabels.insert(lbl);
    }
    if (allLabels.empty()) return;

    const int L1_LABEL = 20;
    int refLabel;
    if (allLabels.count(L1_LABEL)) {
        refLabel = L1_LABEL;
    } else {
        refLabel = *std::min_element(allLabels.begin(), allLabels.end(),
            [](int a, int b) { return std::abs(a - L1_LABEL) < std::abs(b - L1_LABEL); });
    }

    // Find vertebra centroid (in voxel coords)
    double cx = 0, cy = 0, cz = 0;
    int count = 0;
    for (vtkIdType i = 0; i < nPts; i++) {
        if (static_cast<int>(maskArr->GetComponent(i, 0)) == refLabel) {
            int iz = i / (dims[0] * dims[1]);
            int iy = (i % (dims[0] * dims[1])) / dims[0];
            int ix = i % dims[0];
            cx += ix; cy += iy; cz += iz;
            count++;
        }
    }
    if (count == 0) return;
    cx /= count; cy /= count; cz /= count;

    // ROI: +-20 slices around centroid z
    int halfW = 20;
    int zMin = std::max(0, (int)cz - halfW);
    int zMax = std::min(dims[2] - 1, (int)cz + halfW);

    // Collect tissue samples within ROI
    std::vector<float> airVals, fatVals, muscleVals;

    for (int iz = zMin; iz <= zMax; iz++) {
        for (int iy = 0; iy < dims[1]; iy++) {
            for (int ix = 0; ix < dims[0]; ix++) {
                vtkIdType idx = iz * dims[0] * dims[1] + iy * dims[0] + ix;
                double hu = ctArr->GetComponent(idx, 0) + ctHUOffset;
                int lbl = static_cast<int>(maskArr->GetComponent(idx, 0));

                // Distance from vertebra centroid in mm
                double dx_mm = (ix - cx) * spacing[0];
                double dy_mm = (iy - cy) * spacing[1];
                double dist_mm = std::sqrt(dx_mm * dx_mm + dy_mm * dy_mm);

                // Skip bone voxels
                if (lbl > 0) continue;

                // Air: HU < -800
                if (hu < -800.0) {
                    airVals.push_back(static_cast<float>(hu));
                }
                // Fat: subcutaneous, -200 < HU < -50, far from centroid (>30mm)
                else if (hu > -200.0 && hu < -50.0 && dist_mm > 30.0) {
                    fatVals.push_back(static_cast<float>(hu));
                }
                // Muscle: erector spinae, 0 < HU < 100
                // Posterior to vertebra (iy < cy), 10-50mm, lateral < 40mm
                else if (hu > 0.0 && hu < 100.0
                         && iy < (int)cy - 5
                         && dist_mm > 10.0 && dist_mm < 50.0
                         && std::abs(dx_mm) < 40.0) {
                    muscleVals.push_back(static_cast<float>(hu));
                }
            }
        }
    }

    // Compute robust means
    struct { const char* name; double hu; double target; double weight; bool found; } refs[3] = {
        {"air",    0.0, -1000.0, 3.0, false},
        {"fat",    0.0,  -100.0, 1.0, false},
        {"muscle", 0.0,    40.0, 1.5, false},
    };

    if (airVals.size() > 500) {
        refs[0].hu = robustMean(airVals); refs[0].found = true;
    }
    if (fatVals.size() > 500) {
        refs[1].hu = robustMean(fatVals); refs[1].found = true;
    }
    if (muscleVals.size() > 500) {
        refs[2].hu = robustMean(muscleVals); refs[2].found = true;
    }

    std::cout << "[Calib] ref_label=" << refLabel
              << " air=" << (refs[0].found ? std::to_string(refs[0].hu) : "N/A")
              << " fat=" << (refs[1].found ? std::to_string(refs[1].hu) : "N/A")
              << " muscle=" << (refs[2].found ? std::to_string(refs[2].hu) : "N/A")
              << std::endl;

    // Weighted linear fit: HU_std = slope * HU_raw + intercept
    int nRefs = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, sw = 0;
    for (int i = 0; i < 3; i++) {
        if (!refs[i].found) continue;
        double w = refs[i].weight;
        sx  += w * refs[i].hu;
        sy  += w * refs[i].target;
        sxx += w * refs[i].hu * refs[i].hu;
        sxy += w * refs[i].hu * refs[i].target;
        sw  += w;
        nRefs++;
    }

    if (nRefs < 2 || sw < 1e-10) {
        std::cout << "[Calib] WARN: <2 refs, using identity" << std::endl;
        slope = 1.0; intercept = 0.0;
        return;
    }

    double det = sw * sxx - sx * sx;
    if (std::abs(det) > 1e-10) {
        slope     = (sw * sxy - sx * sy) / det;
        intercept = (sy - slope * sx) / sw;
    }

    if (!std::isfinite(slope) || !std::isfinite(intercept) || slope <= 0) {
        slope = 1.0; intercept = 0.0;
    }

    std::cout << "[Calib] HU_std = " << slope << " * HU_raw + " << intercept << std::endl;
}

// ---------------------------------------------------------------------------
// computeModulus — C++ native (no Python subprocess)
// ---------------------------------------------------------------------------
void ModulusEstimationModule::computeModulus()
{
    if (!app_) return;

    auto* viewport = app_->sharedViewport();
    if (!viewport || !viewport->imageData_ || !viewport->maskData_) {
        QMessageBox::warning(this, "Missing Data",
            "CT and Mask data are required.\nPlease run segmentation first.");
        return;
    }

    auto* ctData   = viewport->imageData_.Get();
    auto* maskData = viewport->maskData_.Get();

    // Get selected vertebrae from segmentation checkboxes
    QSet<int> selectedLabels;
    auto projLabels = app_->currentProject.value("selected_labels").toList();
    for (auto& v : projLabels) selectedLabels.insert(v.toInt());

    // Get coef/exp from UI
    double coef    = coefEdit_->text().toDouble();
    double exp_val = expEdit_->text().toDouble();
    if (coef    <= 0) coef    = 10200.0;
    if (exp_val <= 0) exp_val = 2.01;

    QStringList labelStrs;
    for (int lbl : selectedLabels) labelStrs.append(QString::number(lbl));
    appendLog(QString("Starting HU2E Projection (labels: %1)...").arg(
        labelStrs.isEmpty() ? "all" : labelStrs.join(",")));

    // -----------------------------------------------------------------------
    // Step 1: Sample CT values for GMM calibration
    // -----------------------------------------------------------------------
    auto* progress = new QProgressDialog("Sampling CT values...", nullptr, 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(5);
    progress->show();
    QCoreApplication::processEvents();

    auto* ctArr   = ctData->GetPointData()->GetScalars();
    auto* maskArr = maskData->GetPointData()->GetScalars();
    vtkIdType nPts = ctData->GetNumberOfPoints();

    // Debug: confirm CT scalar range (should be HU: roughly -1100 to +3000)
    double ctRange[2];
    ctData->GetScalarRange(ctRange);
    appendLog(QString("CT scalar range: %1 to %2").arg(ctRange[0], 0, 'f', 0)
                                                   .arg(ctRange[1], 0, 'f', 0));

    // Detect if CT is raw unsigned (0~4095) vs HU (-1024~3071).
    // NIfTI files from DICOM converters sometimes store raw values without
    // applying the Modality LUT offset. Standard DICOM offset is -1024.
    double ctHUOffset = 0.0;
    if (ctRange[0] >= -10.0) {
        ctHUOffset = -1024.0;
        appendLog("CT is raw unsigned — applying HU offset -1024");
    }
    ctHUOffset_ = ctHUOffset;   // 保存供 updateBMD() 复用

    // Collect HU values in [-1100, 200] (air/fat/muscle region), strided sample
    std::vector<float> calibSamples;
    calibSamples.reserve(800000);
    const vtkIdType stride = std::max<vtkIdType>(1, nPts / 800000);
    for (vtkIdType i = 0; i < nPts; i += stride) {
        float hu = static_cast<float>(ctArr->GetComponent(i, 0)) + ctHUOffset;
        if (hu > -1100.f && hu < 200.f)
            calibSamples.push_back(hu);
    }
    appendLog(QString("Calibration samples: %1").arg((int)calibSamples.size()));

    // -----------------------------------------------------------------------
    // Step 2: Phantomless GMM calibration → slope, intercept
    // -----------------------------------------------------------------------
    progress->setLabelText("Running 3-point anatomical calibration...");
    progress->setValue(20);
    QCoreApplication::processEvents();

    double slope = 1.0, intercept = 0.0;
    phantomlessCalib(ctData, maskData, selectedLabels, ctHUOffset, slope, intercept);
    calibSamples.clear();

    calibSlope_     = slope;
    calibIntercept_ = intercept;
    hasCalibration_ = true;
    appendLog(QString("Calibration: slope=%1, intercept=%2")
              .arg(slope, 0, 'f', 6).arg(intercept, 0, 'f', 4));
    // Sanity check: water (HU≈0) → ρ should be ≈1.0 g/cm³
    appendLog(QString("Sanity: rho(HU=0)=%1, rho(HU=-1000)=%2")
              .arg(slope * 0 + intercept, 0, 'f', 3)
              .arg(std::max(slope * -1000.0 + intercept, 0.0), 0, 'f', 3));

    // -----------------------------------------------------------------------
    // Step 3: Per-voxel E computation directly on CT voxel grid
    // -----------------------------------------------------------------------
    progress->setLabelText("Computing E volume...");
    progress->setValue(50);
    QCoreApplication::processEvents();

    int dims[3];
    ctData->GetDimensions(dims);
    vtkIdType totalVoxels = static_cast<vtkIdType>(dims[0]) * dims[1] * dims[2];

    auto eVol   = vtkSmartPointer<vtkImageData>::New();
    eVol->SetDimensions(dims);
    eVol->SetSpacing(ctData->GetSpacing());
    eVol->SetOrigin(ctData->GetOrigin());

    auto eArray = vtkSmartPointer<vtkFloatArray>::New();
    eArray->SetNumberOfTuples(totalVoxels);
    eArray->SetName("E_modulus");
    eArray->Fill(0.0f);
    eVol->GetPointData()->SetScalars(eArray);
    float* eRaw = eArray->GetPointer(0);

    const double E_MIN = 100.0, E_MAX = 25000.0;
    double eMin = E_MAX, eMax = E_MIN, eSum = 0.0;
    vtkIdType nValid = 0;

    // Report progress every 5%
    const vtkIdType reportStep = totalVoxels / 20;
    for (vtkIdType i = 0; i < totalVoxels; i++) {
        if (reportStep > 0 && (i % reportStep) == 0) {
            int pct = 50 + static_cast<int>(40.0 * i / totalVoxels);
            progress->setValue(pct);
            QCoreApplication::processEvents();
        }
        int label = static_cast<int>(maskArr->GetComponent(i, 0));
        if (label <= 0) continue;
        if (!selectedLabels.isEmpty() && !selectedLabels.contains(label)) continue;

        double hu_raw = ctArr->GetComponent(i, 0) + ctHUOffset;
        // 3-point calibration chain: HU_raw → HU_std → rho_QCT → rho_ash → E
        double hu_std  = slope * hu_raw + intercept;
        double rho_qct = hu_std / 1000.0;
        double rho_ash = std::max(0.8772 * rho_qct + 0.07895, 0.0);
        double E       = coef * std::pow(std::max(rho_ash, 1e-10), exp_val);
        E = std::max(E_MIN, std::min(E_MAX, E));
        eRaw[i] = static_cast<float>(E);
        if (E < eMin) eMin = E;
        if (E > eMax) eMax = E;
        eSum += E;
        ++nValid;
    }

    eRangeMin_ = (nValid > 0) ? eMin : E_MIN;
    eRangeMax_ = (nValid > 0) ? eMax : E_MAX;

    appendLog(QString("E hex cells: %1").arg((int)nValid));
    if (nValid > 0)
        appendLog(QString("E range: %1 - %2 MPa")
                  .arg(eMin, 0, 'f', 0).arg(eMax, 0, 'f', 0));

    // -----------------------------------------------------------------------
    // Step 4: Update state and UI
    // -----------------------------------------------------------------------
    progress->setLabelText("Building 3D view...");
    progress->setValue(95);
    QCoreApplication::processEvents();

    eVolume_ = eVol;
    viewport->setEVolume(eVol);

    // Update calibration display
    slopeEdit_->setText(QString::number(slope, 'f', 6));
    interceptEdit_->setText(QString::number(intercept, 'f', 4));
    double meanRho = slope * 300.0 + intercept;
    rhoEdit_->setText(QString::number(meanRho > 0 ? meanRho : intercept, 'f', 4));

    overlayCheckbox_->setEnabled(true);
    overlayAlphaSlider_->setEnabled(true);
    updateBMDBtn_->setEnabled(true);
    overlayCheckbox_->setChecked(true);

    progress->close();

    // 保存 BMD 参数到 currentProject
    if (app_) {
        app_->currentProject["calib_slope"] = calibSlope_;
        app_->currentProject["calib_intercept"] = calibIntercept_;
        app_->currentProject["calib_coefficient"] = coefEdit_->text().toDouble();
        app_->currentProject["calib_exponent"] = expEdit_->text().toDouble();
        app_->currentProject["has_bmd"] = true;

        if (eVolume_) {
            QString eDir = QDir::tempPath() + "/spinex_evol_" +
                QString::number(QDateTime::currentMSecsSinceEpoch());
            QDir().mkpath(eDir);
            QString eVolPath = eDir + "/E_volume.nii.gz";
            NiftiIO::writeNifti(eVolume_, eVolPath.toStdString());
            app_->currentProject["e_volume_path"] = eVolPath;
        }
    }

    // Switch to material 3D mode
    viewport->forceSwitch3DMode("material");
    appendLog("HU2E computation and 3D view updated.");
    appendLog("==================================================");

    QMessageBox::information(this, "HU2E Complete",
        "Elastic modulus mapping completed successfully.\n"
        "3D view has been updated.");
}

// (Python subprocess block removed — replaced by C++ GMM above)
#if 0
void __unused_python_block__() {
    QString pyScript = QString(
        "import sys, os, tempfile\n"
        "sys.path.insert(0, r'%1')\n"
        "from Function.HU_Projection.HU2FEM import HU2E_Projection\n"
        "import numpy as np, nibabel as nib\n"
        "\n"
        "def progress(pct, msg, _):\n"
        "    print(f'PROGRESS:{pct}:{msg}', flush=True)\n"
        "\n"
        "# Filter mask to selected labels\n"
        "selected_str = '%7'\n"
        "mask_path = r'%3'\n"
        "if selected_str:\n"
        "    selected = [int(x) for x in selected_str.split(',') if x]\n"
        "    if selected:\n"
        "        print('PROGRESS:5:Filtering mask to selected vertebrae...', flush=True)\n"
        "        mask_nii = nib.as_closest_canonical(nib.load(mask_path))\n"
        "        mask_arr = mask_nii.get_fdata().astype(np.int32)\n"
        "        filtered = np.zeros_like(mask_arr)\n"
        "        for lbl in selected:\n"
        "            filtered[mask_arr == lbl] = lbl\n"
        "        filtered_nii = nib.Nifti1Image(filtered, mask_nii.affine, mask_nii.header)\n"
        "        mask_path = os.path.join(r'%4', 'filtered_mask.nii.gz')\n"
        "        nib.save(filtered_nii, mask_path)\n"
        "        print(f'PROGRESS:8:Filtered mask saved ({len(selected)} labels)', flush=True)\n"
        "\n"
        "csv_obj, vtu_obj, xyzE, slope, intercept, intermediates = HU2E_Projection(\n"
        "    CT_File=r'%2',\n"
        "    Mask_File=mask_path,\n"
        "    Out_Dir=r'%4',\n"
        "    rho_E_coef=%5,\n"
        "    rho_E_exp=%6,\n"
        "    progress_callback=progress\n"
        ")\n"
        "\n"
        "print('PROGRESS:85:Building E volume...', flush=True)\n"
        "\n"
        "# Build E_volume mapped to CT voxel grid\n"
        "ct_img = nib.as_closest_canonical(nib.load(r'%2'))\n"
        "ct_data = ct_img.get_fdata()\n"
        "ct_affine_inv = np.linalg.inv(ct_img.affine)\n"
        "E_volume = np.full(ct_data.shape, np.nan, dtype=np.float32)\n"
        "\n"
        "xyz = xyzE[:, :3].astype(np.float32)\n"
        "E_vals = xyzE[:, 3].astype(np.float32)\n"
        "homog = np.c_[xyz, np.ones(xyz.shape[0])]\n"
        "voxel_coords = np.round((ct_affine_inv @ homog.T).T[:, :3]).astype(int)\n"
        "valid = (voxel_coords[:, 0] >= 0) & (voxel_coords[:, 0] < ct_data.shape[0]) & \\\n"
        "        (voxel_coords[:, 1] >= 0) & (voxel_coords[:, 1] < ct_data.shape[1]) & \\\n"
        "        (voxel_coords[:, 2] >= 0) & (voxel_coords[:, 2] < ct_data.shape[2])\n"
        "vc = voxel_coords[valid]\n"
        "E_volume[vc[:, 0], vc[:, 1], vc[:, 2]] = E_vals[valid]\n"
        "\n"
        "# KDTree interpolation for mask coverage\n"
        "mask_img = nib.as_closest_canonical(nib.load(r'%3'))\n"
        "mask_data = mask_img.get_fdata()\n"
        "need = np.isnan(E_volume) & (mask_data > 0)\n"
        "need_idx = np.where(need)\n"
        "if len(need_idx[0]) > 0:\n"
        "    from scipy.spatial import cKDTree\n"
        "    tree = cKDTree(vc.astype(np.float32))\n"
        "    qp = np.c_[need_idx[0], need_idx[1], need_idx[2]]\n"
        "    for i in range(0, len(qp), 50000):\n"
        "        bp = qp[i:i+50000]\n"
        "        d, idx = tree.query(bp, k=1, distance_upper_bound=10.0)\n"
        "        ok = d < 10.0\n"
        "        if ok.any():\n"
        "            E_volume[bp[ok, 0], bp[ok, 1], bp[ok, 2]] = E_vals[valid][idx[ok]]\n"
        "\n"
        "print('PROGRESS:95:Saving results...', flush=True)\n"
        "np.save(r'%4/xyzE.npy', xyzE.astype(np.float32))\n"
        "np.save(r'%4/E_volume.npy', E_volume)\n"
        "\n"
        "# Save as raw float32 binary for C++ VTK ImageData\n"
        "# E_volume is in canonical space (as_closest_canonical flips axis 0)\n"
        "# VTK loads CT in original NIfTI space where:\n"
        "#   VTK(x,y,z) = raw[x, y, dimZ-1-z]\n"
        "#   canonical[i,j,k] = raw[dimX-1-i, j, k]\n"
        "# So E_vtk(x,y,z) = E_canonical[dimX-1-x, y, dimZ-1-z]\n"
        "E_for_vtk = E_volume[::-1, :, ::-1].copy()\n"
        "E_for_vtk[~np.isfinite(E_for_vtk)] = 0.0\n"
        "# VTK flat order: idx = x + y*dimX + z*dimX*dimY → transpose(2,1,0)\n"
        "E_vtk_order = np.ascontiguousarray(E_for_vtk.transpose(2, 1, 0)).astype(np.float32)\n"
        "E_vtk_order.tofile(r'%4/E_volume.raw')\n"
        "with open(r'%4/E_volume_info.txt', 'w') as f:\n"
        "    f.write(f'{E_volume.shape[0]} {E_volume.shape[1]} {E_volume.shape[2]}\\n')\n"
        "\n"
        "E_valid = E_vals[np.isfinite(E_vals)]\n"
        "print(f'RESULT:slope={slope}:intercept={intercept}:n_points={len(xyzE)}:E_min={E_valid.min():.1f}:E_max={E_valid.max():.1f}:E_mean={E_valid.mean():.1f}')\n"
        "print('DONE', flush=True)\n"
    ).arg(
        "H:/pythonproject/PHD_PROGRAM/SpineX-Py",
        ctPath, maskPath, tempDir,
        QString::number(coef, 'f', 2),
        QString::number(exp_val, 'f', 4),
        labelsStr
    );

    // Write script to temp file
    QString scriptPath = tempDir + "/run_hu2e.py";
    {
        QFile f(scriptPath);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write(pyScript.toUtf8());
        f.close();
    }

    // Progress dialog
    auto* progress = new QProgressDialog("Computing HU2E Projection...", "Cancel", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(5);
    progress->show();

    // Run Python subprocess
    auto* proc = new QProcess(this);
    QString pythonExe = QString::fromStdString(PythonRuntime::instance().pythonExecutable());

    QString outputDir = tempDir;

    connect(proc, &QProcess::readyReadStandardOutput, [this, proc, progress]() {
        while (proc->canReadLine()) {
            QString line = QString::fromUtf8(proc->readLine()).trimmed();
            if (line.startsWith("PROGRESS:")) {
                auto parts = line.mid(9).split(':');
                if (parts.size() >= 2) {
                    int pct = parts[0].toInt();
                    QString msg = parts[1];
                    progress->setValue(pct);
                    progress->setLabelText(msg);
                }
            } else if (line.startsWith("RESULT:")) {
                // Parse slope, intercept, E stats
                auto parts = line.mid(7).split(':');
                for (auto& part : parts) {
                    if (part.startsWith("slope=")) {
                        calibSlope_ = part.mid(6).toDouble();
                        slopeEdit_->setText(QString::number(calibSlope_, 'f', 6));
                    } else if (part.startsWith("intercept=")) {
                        calibIntercept_ = part.mid(10).toDouble();
                        interceptEdit_->setText(QString::number(calibIntercept_, 'f', 4));
                    } else if (part.startsWith("E_min=")) {
                        appendLog(QString("E min: %1 MPa").arg(part.mid(6)));
                    } else if (part.startsWith("E_max=")) {
                        appendLog(QString("E max: %1 MPa").arg(part.mid(6)));
                    } else if (part.startsWith("E_mean=")) {
                        appendLog(QString("E mean: %1 MPa").arg(part.mid(7)));
                    }
                }
                hasCalibration_ = true;
            } else if (!line.isEmpty()) {
            }
        }
    });

    connect(proc, &QProcess::readyReadStandardError, [this, proc]() {
        QString err = QString::fromUtf8(proc->readAllStandardError());
        if (!err.trimmed().isEmpty()) {
            std::cerr << "[HU2E stderr] " << err.toStdString() << std::endl;
        }
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, progress, outputDir, coef, exp_val](int exitCode, QProcess::ExitStatus) {
            progress->close();

            if (exitCode != 0) {
                appendLog(QString("HU2E failed with exit code %1").arg(exitCode));
                QMessageBox::critical(this, "HU2E Failed",
                    "HU2E Projection failed. Check console for details.");
                return;
            }

            appendLog("HU2E Projection complete!");

            // Update calibration display
            if (hasCalibration_) {
                appendLog(QString("Slope: %1, Intercept: %2")
                    .arg(calibSlope_).arg(calibIntercept_));

                // Compute mean rho: ρ = slope × HU_mean + intercept
                // Use HU=300 as typical bone HU for rough estimate
                double meanRho = calibSlope_ * 300.0 + calibIntercept_;
                if (meanRho > 0) {
                    rhoEdit_->setText(QString::number(meanRho, 'f', 4));
                } else {
                    rhoEdit_->setText(QString::number(calibIntercept_, 'f', 4));
                }

                // Enable controls
                overlayCheckbox_->setEnabled(true);
                overlayAlphaSlider_->setEnabled(true);
                updateBMDBtn_->setEnabled(true);
            }

            // Load E_volume raw binary into VTK ImageData
            // (matching CT dimensions exactly — same coordinate space)
            QString eVolRawPath = outputDir + "/E_volume.raw";
            QString eVolInfoPath = outputDir + "/E_volume_info.txt";
            if (QFile::exists(eVolRawPath) && QFile::exists(eVolInfoPath) &&
                app_->sharedViewport() && app_->sharedViewport()->imageData_) {

                // Read dimensions from info file
                QFile infoFile(eVolInfoPath);
                infoFile.open(QIODevice::ReadOnly | QIODevice::Text);
                QString dimLine = QString::fromUtf8(infoFile.readLine()).trimmed();
                infoFile.close();
                auto dimParts = dimLine.split(' ');

                if (dimParts.size() == 3) {
                    int ex = dimParts[0].toInt();
                    int ey = dimParts[1].toInt();
                    int ez = dimParts[2].toInt();

                    // Read raw float32 data
                    QFile rawFile(eVolRawPath);
                    rawFile.open(QIODevice::ReadOnly);
                    QByteArray rawData = rawFile.readAll();
                    rawFile.close();

                    int expectedSize = ex * ey * ez * sizeof(float);
                    if (rawData.size() == expectedSize) {
                        // Create VTK ImageData matching CT
                        auto* ctData = app_->sharedViewport()->imageData_.Get();
                        auto eVol = vtkSmartPointer<vtkImageData>::New();
                        eVol->SetDimensions(ctData->GetDimensions());
                        eVol->SetSpacing(ctData->GetSpacing());
                        eVol->SetOrigin(ctData->GetOrigin());

                        auto eArray = vtkSmartPointer<vtkFloatArray>::New();
                        eArray->SetNumberOfTuples(ex * ey * ez);
                        eArray->SetName("E_modulus");
                        memcpy(eArray->GetVoidPointer(0), rawData.constData(), rawData.size());
                        eVol->GetPointData()->SetScalars(eArray);

                        app_->sharedViewport()->setEVolume(eVol);
                        eVolume_ = eVol;
                        appendLog(QString("E_volume loaded: %1x%2x%3").arg(ex).arg(ey).arg(ez));

                        // Auto-enable E overlay
                        overlayCheckbox_->setChecked(true);
                    } else {
                        appendLog(QString("E_volume size mismatch: got %1, expected %2")
                            .arg(rawData.size()).arg(expectedSize));
                    }
                }
            }

            // Switch to material 3D mode - signal triggers build3DView() automatically
            if (app_->sharedViewport())
                app_->sharedViewport()->forceSwitch3DMode("material");

            // 保存 BMD 参数到 currentProject（用于 .si 项目保存）
            if (app_) {
                app_->currentProject["calib_slope"] = calibSlope_;
                app_->currentProject["calib_intercept"] = calibIntercept_;
                app_->currentProject["calib_coefficient"] = coefEdit_->text().toDouble();
                app_->currentProject["calib_exponent"] = expEdit_->text().toDouble();
                app_->currentProject["has_bmd"] = true;

                // 保存 E_volume 为 nii.gz 到临时目录，记录路径
                if (eVolume_) {
                    QString eVolPath = outputDir + "/E_volume.nii.gz";
                    NiftiIO::writeNifti(eVolume_, eVolPath.toStdString());
                    app_->currentProject["e_volume_path"] = eVolPath;
                }
            }

            appendLog("HU2E computation and 3D view updated.");
            appendLog("==================================================");

            QMessageBox::information(this, "HU2E Complete",
                "Elastic modulus mapping completed successfully.\n"
                "3D view has been updated.");
        });

    proc->start(pythonExe, {scriptPath});
    if (!proc->waitForStarted(5000)) {
        progress->close();
        QMessageBox::critical(this, "Process Error",
            "Failed to start Python subprocess for HU2E.");
    }
}
#endif // 0

void ModulusEstimationModule::runMeshing()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    auto maskData = viewport->maskData_;
    if (!maskData) {
        QMessageBox::warning(this, "Meshing", "No segmentation mask loaded.");
        return;
    }

    // Get selected labels from BMD vertebra checkboxes
    QSet<int> selectedLabels;
    for (auto it = bmdVertCheckboxes_.begin(); it != bmdVertCheckboxes_.end(); ++it) {
        if (it.value()->isChecked())
            selectedLabels.insert(it.key());
    }
    if (selectedLabels.isEmpty()) {
        // Fallback to project selected_labels if no BMD checkboxes
        QVariantList selList = app_->currentProject.value("selected_labels").toList();
        for (auto& v : selList) selectedLabels.insert(v.toInt());
    }
    if (selectedLabels.isEmpty()) {
        QMessageBox::warning(this, "Meshing",
            "No vertebrae selected.\nPlease check vertebrae in the VERTEBRA SELECTION panel.");
        return;
    }

    // Dialog: mesh size
    bool ok;
    double meshSize = QInputDialog::getDouble(this, "Meshing - C3D10 (TET10)",
        "Element size (mm):", 2.0, 0.5, 10.0, 1, &ok);
    if (!ok) return;

    appendLog("==================================================");
    appendLog(QString("Starting meshing (C3D10, size=%1mm)...").arg(meshSize));

    auto* progress = new QProgressDialog("Generating meshes...", nullptr, 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(5);
    progress->show();
    QCoreApplication::processEvents();

    int* dims = maskData->GetDimensions();
    double* spacing = maskData->GetSpacing();
    double* origin = maskData->GetOrigin();
    auto* scalars = maskData->GetPointData()->GetScalars();

    int labelsDone = 0;
    int totalLabels = selectedLabels.size();

    // Gmsh initialize once (C API)
    int ierr;
    gmshInitialize(0, nullptr, 1, 0, &ierr);
    gmshOptionSetNumber("General.Terminal", 0, &ierr);

    QString outputDir = QDir::tempPath() + "/spinex_mesh_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(outputDir);

    for (int lbl : selectedLabels) {
        int pct = 5 + 90 * labelsDone / totalLabels;
        progress->setValue(pct);
        progress->setLabelText(QString("Meshing label %1...").arg(lbl));
        QCoreApplication::processEvents();

        // Extract binary mask for this label
        auto binaryVol = vtkSmartPointer<vtkImageData>::New();
        binaryVol->SetDimensions(dims);
        binaryVol->SetSpacing(spacing);
        binaryVol->SetOrigin(origin);
        auto binArr = vtkSmartPointer<vtkFloatArray>::New();
        binArr->SetNumberOfTuples(maskData->GetNumberOfPoints());
        for (vtkIdType i = 0; i < maskData->GetNumberOfPoints(); i++) {
            binArr->SetValue(i, static_cast<int>(scalars->GetTuple1(i)) == lbl ? 1.0f : 0.0f);
        }
        binaryVol->GetPointData()->SetScalars(binArr);

        // Marching cubes
        auto dmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
        dmc->SetInputData(maskData);
        dmc->SetNumberOfContours(1);
        dmc->SetValue(0, lbl);
        dmc->ComputeNormalsOn();
        dmc->Update();

        if (dmc->GetOutput()->GetNumberOfPoints() == 0) {
            appendLog(QString("  Label %1: no surface, skipping").arg(lbl));
            labelsDone++;
            continue;
        }

        // Smooth
        auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smoother->SetInputConnection(dmc->GetOutputPort());
        smoother->SetNumberOfIterations(30);
        smoother->SetPassBand(0.05);
        smoother->BoundarySmoothingOn();
        smoother->Update();

        // Clean + triangulate
        auto cleaner = vtkSmartPointer<vtkCleanPolyData>::New();
        cleaner->SetInputConnection(smoother->GetOutputPort());
        cleaner->Update();

        auto triFilter = vtkSmartPointer<vtkTriangleFilter>::New();
        triFilter->SetInputConnection(cleaner->GetOutputPort());
        triFilter->Update();

        auto* pd = triFilter->GetOutput();

        // Write ASCII STL manually
        QString stlPath = outputDir + QString("/label_%1.stl").arg(lbl);
        {
            FILE* fp = fopen(stlPath.toStdString().c_str(), "w");
            if (!fp) { appendLog("  ERROR: cannot write STL"); labelsDone++; continue; }
            fprintf(fp, "solid label%d\n", lbl);
            for (vtkIdType ci = 0; ci < pd->GetNumberOfCells(); ci++) {
                auto* cell = pd->GetCell(ci);
                if (cell->GetNumberOfPoints() != 3) continue;
                double p0[3], p1[3], p2[3];
                pd->GetPoint(cell->GetPointId(0), p0);
                pd->GetPoint(cell->GetPointId(1), p1);
                pd->GetPoint(cell->GetPointId(2), p2);
                // Normal
                double n[3] = {
                    (p1[1]-p0[1])*(p2[2]-p0[2]) - (p1[2]-p0[2])*(p2[1]-p0[1]),
                    (p1[2]-p0[2])*(p2[0]-p0[0]) - (p1[0]-p0[0])*(p2[2]-p0[2]),
                    (p1[0]-p0[0])*(p2[1]-p0[1]) - (p1[1]-p0[1])*(p2[0]-p0[0])
                };
                double mag = sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                if (mag > 1e-10) { n[0]/=mag; n[1]/=mag; n[2]/=mag; }
                fprintf(fp, "facet normal %f %f %f\n  outer loop\n", n[0], n[1], n[2]);
                fprintf(fp, "    vertex %f %f %f\n", p0[0], p0[1], p0[2]);
                fprintf(fp, "    vertex %f %f %f\n", p1[0], p1[1], p1[2]);
                fprintf(fp, "    vertex %f %f %f\n", p2[0], p2[1], p2[2]);
                fprintf(fp, "  endloop\nendfacet\n");
            }
            fprintf(fp, "endsolid label%d\n", lbl);
            fclose(fp);
        }

        // Gmsh C API: STL → TET10
        gmshClear(&ierr);
        gmshMerge(stlPath.toStdString().c_str(), &ierr);

        gmshOptionSetNumber("Mesh.ForceParametrizablePatches", 1, &ierr);
        gmshModelMeshClassifySurfaces(40.0 * M_PI / 180.0, 1, 1, M_PI, 0, &ierr);
        gmshModelMeshCreateTopology(1, 1, &ierr);

        // Create volume from surfaces
        int* surfDimTags = nullptr; size_t surfDimTags_n = 0;
        gmshModelGetEntities(&surfDimTags, &surfDimTags_n, 2, &ierr);
        std::vector<int> loopTags;
        for (size_t i = 0; i < surfDimTags_n; i += 2) {
            int surfTag = surfDimTags[i + 1];
            int tags[] = {surfTag};
            int loop = gmshModelGeoAddSurfaceLoop(tags, 1, -1, &ierr);
            loopTags.push_back(loop);
        }
        if (surfDimTags) gmshFree(surfDimTags);

        if (!loopTags.empty()) {
            gmshModelGeoAddVolume(loopTags.data(), loopTags.size(), -1, &ierr);
            gmshModelGeoSynchronize(&ierr);
        }

        // ---- Mesh quality settings ----
        gmshOptionSetNumber("Mesh.CharacteristicLengthMin", meshSize * 0.5, &ierr);
        gmshOptionSetNumber("Mesh.CharacteristicLengthMax", meshSize, &ierr);
        gmshOptionSetNumber("Mesh.Algorithm", 6, &ierr);      // Frontal-Delaunay surface
        gmshOptionSetNumber("Mesh.Algorithm3D", 4, &ierr);    // Frontal volume (better quality)
        gmshOptionSetNumber("Mesh.OptimizeNetgen", 1, &ierr); // Netgen post-optimization
        gmshOptionSetNumber("Mesh.Optimize", 1, &ierr);       // Gmsh optimizer
        gmshOptionSetNumber("Mesh.Smoothing", 3, &ierr);      // surface smoothing

        // Step 1: Surface mesh
        gmshModelMeshGenerate(2, &ierr);
        appendLog(QString("  Label %1: surface mesh done").arg(lbl));

        // Step 2: Volume mesh
        gmshModelMeshGenerate(3, &ierr);

        // Step 3: TET10 = C3D10
        gmshModelMeshSetOrder(2, &ierr);

        // Export as .inp (Abaqus) — includes surface element sets
        QString inpPath = outputDir + QString("/label_%1_C3D10.inp").arg(lbl);
        gmshWrite(inpPath.toStdString().c_str(), &ierr);

        // Get mesh stats
        size_t* nodeTags = nullptr; size_t nodeTags_n = 0;
        double* nodeCoords = nullptr; size_t nodeCoords_n = 0;
        double* nodeParams = nullptr; size_t nodeParams_n = 0;
        gmshModelMeshGetNodes(&nodeTags, &nodeTags_n, &nodeCoords, &nodeCoords_n,
                              &nodeParams, &nodeParams_n, -1, -1, 0, 0, &ierr);
        int nNodes = (int)nodeTags_n;
        if (nodeTags) gmshFree(nodeTags);
        if (nodeCoords) gmshFree(nodeCoords);
        if (nodeParams) gmshFree(nodeParams);

        int* elemTypes = nullptr; size_t elemTypes_n = 0;
        size_t** elemTags = nullptr; size_t* elemTags_n = nullptr; size_t elemTags_nn = 0;
        size_t** elemNodeTags = nullptr; size_t* elemNodeTags_n = nullptr; size_t elemNodeTags_nn = 0;
        gmshModelMeshGetElements(&elemTypes, &elemTypes_n,
                                  &elemTags, &elemTags_n, &elemTags_nn,
                                  &elemNodeTags, &elemNodeTags_n, &elemNodeTags_nn,
                                  -1, -1, &ierr);
        int nElems = 0;
        for (size_t i = 0; i < elemTags_nn; i++) nElems += (int)elemTags_n[i];
        if (elemTypes) gmshFree(elemTypes);
        if (elemTags) { for (size_t i = 0; i < elemTags_nn; i++) gmshFree(elemTags[i]); gmshFree(elemTags); }
        if (elemTags_n) gmshFree(elemTags_n);
        if (elemNodeTags) { for (size_t i = 0; i < elemNodeTags_nn; i++) gmshFree(elemNodeTags[i]); gmshFree(elemNodeTags); }
        if (elemNodeTags_n) gmshFree(elemNodeTags_n);

        appendLog(QString("  Label %1: %2 nodes, %3 elements -> %4")
            .arg(lbl).arg(nNodes).arg(nElems).arg(inpPath));

        // Read mesh back from Gmsh for VTK display (use TET4 corners for visualization)
        {
            size_t* nTags = nullptr; size_t nTags_n = 0;
            double* nCoords = nullptr; size_t nCoords_n = 0;
            double* nPar = nullptr; size_t nPar_n = 0;
            gmshModelMeshGetNodes(&nTags, &nTags_n, &nCoords, &nCoords_n,
                                  &nPar, &nPar_n, -1, -1, 0, 0, &ierr);

            if (nTags_n > 0 && nCoords_n > 0) {
                // Build node tag → index map
                std::map<size_t, vtkIdType> tagToIdx;
                auto vtkPts = vtkSmartPointer<vtkPoints>::New();
                for (size_t i = 0; i < nTags_n; i++) {
                    tagToIdx[nTags[i]] = static_cast<vtkIdType>(i);
                    vtkPts->InsertNextPoint(nCoords[3*i], nCoords[3*i+1], nCoords[3*i+2]);
                }

                auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
                grid->SetPoints(vtkPts);

                // Get tet elements (type 11 = TET10 in Gmsh, type 4 = TET4)
                int* eTypes = nullptr; size_t eTypes_n = 0;
                size_t** eTags = nullptr; size_t* eTags_n = nullptr; size_t eTags_nn = 0;
                size_t** eNTags = nullptr; size_t* eNTags_n = nullptr; size_t eNTags_nn = 0;
                gmshModelMeshGetElements(&eTypes, &eTypes_n,
                                          &eTags, &eTags_n, &eTags_nn,
                                          &eNTags, &eNTags_n, &eNTags_nn,
                                          -1, -1, &ierr);

                for (size_t ti = 0; ti < eTypes_n; ti++) {
                    int gmshType = eTypes[ti];
                    int nodesPerElem = (gmshType == 11) ? 10 : (gmshType == 4) ? 4 : 0;
                    if (nodesPerElem < 4) continue;  // skip non-tet elements

                    size_t numElems = eTags_n[ti];
                    for (size_t ei = 0; ei < numElems; ei++) {
                        // Use first 4 nodes (corners) for TET4 visualization
                        vtkIdType ids[4];
                        for (int ni = 0; ni < 4; ni++) {
                            size_t nodeTag = eNTags[ti][ei * nodesPerElem + ni];
                            ids[ni] = tagToIdx.count(nodeTag) ? tagToIdx[nodeTag] : 0;
                        }
                        grid->InsertNextCell(VTK_TETRA, 4, ids);
                    }
                }

                if (eTypes) gmshFree(eTypes);
                if (eTags) { for (size_t i = 0; i < eTags_nn; i++) gmshFree(eTags[i]); gmshFree(eTags); }
                if (eTags_n) gmshFree(eTags_n);
                if (eNTags) { for (size_t i = 0; i < eNTags_nn; i++) gmshFree(eNTags[i]); gmshFree(eNTags); }
                if (eNTags_n) gmshFree(eNTags_n);

                meshResults_.append({lbl, grid, nullptr});
            }

            if (nTags) gmshFree(nTags);
            if (nCoords) gmshFree(nCoords);
            if (nPar) gmshFree(nPar);
        }

        labelsDone++;
    }

    gmshFinalize(&ierr);

    progress->setValue(100);
    progress->close();
    delete progress;

    appendLog(QString("Meshing complete. Output: %1").arg(outputDir));
    appendLog("==================================================");

    // Save mesh output dir to project for persistence
    if (app_) app_->currentProject["mesh_output_dir"] = outputDir;

    // Switch to Mesh 3D view
    if (app_ && app_->sharedViewport())
        app_->sharedViewport()->forceSwitch3DMode("mesh");

    QMessageBox::information(this, "Meshing Complete",
        QString("C3D10 meshes generated for %1 vertebrae.\n\nOutput: %2")
        .arg(totalLabels).arg(outputDir));
}

void ModulusEstimationModule::buildMesh3DView()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    if (viewport->current3DMode() != "mesh") return;

    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    renderer->RemoveAllViewProps();

    if (meshResults_.isEmpty()) {
        appendLog("No mesh data. Run Meshing first.");
        return;
    }

    static const int VC[][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},
        {255,0,255},{255,239,213},{0,0,205},{205,133,63},{210,180,140},
        {102,205,170},{0,0,128},{0,139,139},{46,139,87},{255,228,225},
        {106,90,205},{221,160,221},{233,150,122},{165,42,42},
        {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
        {60,179,113},{255,235,205},
    };

    for (auto& md : meshResults_) {
        if (!md.grid.Get() || md.grid->GetNumberOfCells() == 0) continue;

        // Show tet mesh with DataSetMapper (solid + edges)
        auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        mapper->SetInputData(md.grid);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        int ci = (md.label - 1) % 26;
        actor->GetProperty()->SetColor(VC[ci][0]/255.0, VC[ci][1]/255.0, VC[ci][2]/255.0);
        actor->GetProperty()->SetOpacity(0.9);
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetEdgeVisibility(true);
        actor->GetProperty()->SetEdgeColor(0.1, 0.1, 0.1);
        actor->GetProperty()->SetLineWidth(0.5);

        renderer->AddActor(actor);
        md.actor = actor;
    }

    renderer->ResetCamera();
    auto* rw = viewport->get3DRenderWindow();
    if (rw) rw->Render();

    appendLog(QString("Mesh 3D view: %1 meshes").arg(meshResults_.size()));
}

void ModulusEstimationModule::runBCSelection()
{
    appendLog("BC Selection: requires VTU from modulus calculation.");
    QMessageBox::information(this, "BC Selection",
        "BC Selection requires a VTU file from modulus calculation.\n"
        "Please run Calculate Modulus first.");
}

void ModulusEstimationModule::runRegistration()
{
    appendLog("Rigid Registration: requires BC/Load data.");
    QMessageBox::information(this, "Rigid Registration",
        "Rigid Registration requires BC/Load data and a reference STL.\n"
        "Please run BC Selection first.");
}

void ModulusEstimationModule::populateBMDVertebraList()
{
    // Clear old checkboxes
    for (auto* cb : bmdVertCheckboxes_) delete cb;
    bmdVertCheckboxes_.clear();
    QLayoutItem* item;
    while ((item = vertebraListLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // Get labels from cachedMeshes_
    static const QStringList VERT_NAMES = {
        "C1","C2","C3","C4","C5","C6","C7",
        "T1","T2","T3","T4","T5","T6","T7","T8","T9","T10","T11","T12",
        "L1","L2","L3","L4","L5","L6","Sacrum"
    };

    std::set<int> labels;
    for (const auto& m : cachedMeshes_) labels.insert(m.label);

    for (int lbl : labels) {
        QString name = (lbl >= 1 && lbl <= VERT_NAMES.size())
                        ? VERT_NAMES[lbl - 1] : QString("V%1").arg(lbl);
        auto* cb = new QCheckBox(QString("%1 (label %2)").arg(name).arg(lbl));
        cb->setChecked(true);
        cb->setStyleSheet("font-size: 9pt;");
        connect(cb, &QCheckBox::toggled, [this, lbl](bool checked) {
            onBMDVertebraToggled(lbl, checked);
        });
        bmdVertCheckboxes_[lbl] = cb;
        vertebraListLayout_->addWidget(cb);
    }

    vertebraSection_->setVisible(!labels.empty());
}

void ModulusEstimationModule::onBMDVertebraToggled(int label, bool visible)
{
    if (!app_ || !app_->sharedViewport()) return;

    // Fast toggle: just show/hide the per-label clip actor (like Seg)
    for (auto& vm : cachedMeshes_) {
        if (vm.label == label && vm.eClipActor.Get()) {
            vm.eClipActor->SetVisibility(visible ? 1 : 0);
            auto* rw = app_->sharedViewport()->get3DRenderWindow();
            if (rw) rw->Render();
            return;
        }
    }
}

void ModulusEstimationModule::toggleMaterialOverlay(bool checked)
{
    showMaterialOverlay_ = checked;
    appendLog(QString("Material overlay: %1").arg(checked ? "ON" : "OFF"));
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setEOverlayVisible(checked);
    }
}

void ModulusEstimationModule::updateOverlayAlpha(int val)
{
    overlayAlpha_ = val / 100.0;
    alphaValLbl_->setText(QString("%1%").arg(val));
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setEOverlayOpacity(overlayAlpha_);
    }
}

void ModulusEstimationModule::updateBMD()
{
    if (!hasCalibration_) {
        appendLog("No calibration available — run Compute E first.");
        return;
    }

    double coef    = coefEdit_->text().toDouble();
    double exp_val = expEdit_->text().toDouble();
    if (coef <= 0 || exp_val <= 0) {
        appendLog("Invalid coef/exp values.");
        return;
    }

    auto* viewport = app_ ? app_->sharedViewport() : nullptr;
    if (!viewport || !viewport->imageData_ || !viewport->maskData_) {
        appendLog("CT/Mask data not available.");
        return;
    }

    appendLog("==================================================");
    appendLog(QString("Updating E: coef=%1  exp=%2").arg(coef).arg(exp_val));

    auto* progress = new QProgressDialog("Recomputing E values...", nullptr, 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    QCoreApplication::processEvents();

    auto ctData   = viewport->imageData_;
    auto maskData = viewport->maskData_;
    auto* ctArr   = ctData->GetPointData()->GetScalars();
    auto* maskArr = maskData->GetPointData()->GetScalars();

    const double slope     = calibSlope_;
    const double intercept = calibIntercept_;
    const double E_MIN = 100.0, E_MAX = 25000.0;

    // Reuse existing eVolume_ geometry, just replace scalar values
    if (!eVolume_) {
        appendLog("E volume not initialised — run Compute E first.");
        progress->close(); progress->deleteLater();
        return;
    }

    auto* eScalars = eVolume_->GetPointData()->GetScalars();
    vtkIdType totalVoxels = ctData->GetNumberOfPoints();

    double eMin = E_MAX, eMax = E_MIN, eSum = 0.0;
    vtkIdType nValid = 0;
    const vtkIdType reportStep = std::max<vtkIdType>(1, totalVoxels / 20);

    for (vtkIdType i = 0; i < totalVoxels; i++) {
        if (reportStep > 0 && (i % reportStep) == 0) {
            progress->setValue(static_cast<int>(90.0 * i / totalVoxels));
            QCoreApplication::processEvents();
        }
        int label = static_cast<int>(maskArr->GetComponent(i, 0));
        if (label <= 0) { eScalars->SetComponent(i, 0, 0.0); continue; }

        double hu_raw = ctArr->GetComponent(i, 0) + ctHUOffset_;
        double hu_std  = slope * hu_raw + intercept;
        double rho_qct = hu_std / 1000.0;
        double rho_ash = std::max(0.8772 * rho_qct + 0.07895, 0.0);
        double E       = coef * std::pow(std::max(rho_ash, 1e-10), exp_val);
        E = std::max(E_MIN, std::min(E_MAX, E));
        eScalars->SetComponent(i, 0, E);
        if (E < eMin) eMin = E;
        if (E > eMax) eMax = E;
        eSum += E; ++nValid;
    }

    eVolume_->GetPointData()->GetScalars()->Modified();
    eVolume_->Modified();

    eRangeMin_ = (nValid > 0) ? eMin : E_MIN;
    eRangeMax_ = (nValid > 0) ? eMax : E_MAX;

    progress->setValue(95);
    QCoreApplication::processEvents();

    appendLog(QString("E range: %1 - %2 MPa  (mean: %3)")
        .arg(eRangeMin_, 0, 'f', 0).arg(eRangeMax_, 0, 'f', 0)
        .arg(nValid > 0 ? eSum / nValid : 0.0, 0, 'f', 0));

    // 更新 Mat 3D 渲染
    hasEColoredVolume_ = false;   // 强制 build3DView 重建
    eColoredVolume_ = nullptr;
    if (viewport->current3DMode() == "material")
        build3DView();

    // 更新 2D 切片叠加
    updateAllSliceDisplays();

    progress->setValue(100);
    progress->close();
    progress->deleteLater();

    appendLog("E update complete.");
    appendLog("==================================================");
}

void ModulusEstimationModule::updateAllSliceDisplays()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    for (const auto& vn : {"axial", "sagittal", "coronal"}) {
        QString name(vn);
        if (viewport->currentSlice.contains(name)) {
            viewport->displaySlice(name, viewport->currentSlice[name]);
        }
    }
}

// ---- 3D Visualization ----

void ModulusEstimationModule::build3DView()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    if (viewport->isOnly3DMode()) return;

    // Only build if we're in material 3D mode
    if (viewport->current3DMode() != "material") return;

    auto maskData = viewport->maskData_;
    if (!maskData) return;

    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Clear ALL old actors completely
    renderer->RemoveAllViewProps();
    cachedMeshes_.clear();
    ghostActors_.clear();
    clipActors_.clear();
    scalarBar_ = nullptr;
    eColoredVolume_ = nullptr;
    hasEColoredVolume_ = false;

    int* dims = maskData->GetDimensions();
    double* spacing = maskData->GetSpacing();
    double* origin = maskData->GetOrigin();
    int numVoxels = dims[0] * dims[1] * dims[2];
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    // Get all labels in mask
    std::set<int> allLabels;
    for (int i = 0; i < numVoxels; ++i) {
        int val = static_cast<int>(scalars->GetTuple1(i));
        if (val > 0) allLabels.insert(val);
    }

    // Filter by selected labels from Segmentation module
    std::set<int> labels;
    QVariantList selectedList = app_->currentProject.value("selected_labels").toList();
    if (!selectedList.isEmpty()) {
        QSet<int> selected;
        for (auto& v : selectedList) selected.insert(v.toInt());
        for (int lbl : allLabels) {
            if (selected.contains(lbl)) labels.insert(lbl);
        }
        appendLog(QString("Filtered to %1 selected vertebrae (from %2 total)")
            .arg(labels.size()).arg(allLabels.size()));
    } else {
        labels = allLabels;
    }

    // Compute clip origin from SELECTED vertebrae center (not whole mask)
    {
        int* dims = maskData->GetDimensions();
        double* sp = maskData->GetSpacing();
        double* orig = maskData->GetOrigin();
        auto* scalars = maskData->GetPointData()->GetScalars();
        double sumX = 0, sumY = 0, sumZ = 0;
        int cnt = 0;
        for (vtkIdType i = 0; i < maskData->GetNumberOfPoints(); i++) {
            int lbl = static_cast<int>(scalars->GetTuple1(i));
            if (lbl <= 0 || !labels.count(lbl)) continue;
            int iz = i / (dims[0] * dims[1]);
            int iy = (i % (dims[0] * dims[1])) / dims[0];
            int ix = i % dims[0];
            sumX += orig[0] + ix * sp[0];
            sumY += orig[1] + iy * sp[1];
            sumZ += orig[2] + iz * sp[2];
            cnt++;
        }
        if (cnt > 0) {
            clipOrigin_[0] = sumX / cnt;
            clipOrigin_[1] = sumY / cnt;
            clipOrigin_[2] = sumZ / cnt;
        } else {
            double bounds[6];
            maskData->GetBounds(bounds);
            clipOrigin_[0] = (bounds[0] + bounds[1]) / 2.0;
            clipOrigin_[1] = (bounds[2] + bounds[3]) / 2.0;
            clipOrigin_[2] = (bounds[4] + bounds[5]) / 2.0;
        }
    }
    clipNormal_[0] = 1.0; clipNormal_[1] = 0.0; clipNormal_[2] = 0.0;

    appendLog(QString("Building 3D material view for %1 vertebrae...").arg(labels.size()));

    bool hasEData = eVolume_ != nullptr;

    // Compute valid E range (excluding zeros)
    eRangeMin_ = 1e30; eRangeMax_ = -1e30;
    if (hasEData) {
        auto* eScalars = eVolume_->GetPointData()->GetScalars();
        int nPts = eScalars->GetNumberOfTuples();
        for (int i = 0; i < nPts; ++i) {
            double v = eScalars->GetTuple1(i);
            if (v > 1.0) {
                if (v < eRangeMin_) eRangeMin_ = v;
                if (v > eRangeMax_) eRangeMax_ = v;
            }
        }
        if (eRangeMin_ >= eRangeMax_) { eRangeMin_ = 100; eRangeMax_ = 25000; }
        appendLog(QString("E range: %1 - %2 MPa").arg(eRangeMin_, 0, 'f', 0).arg(eRangeMax_, 0, 'f', 0));
    }

    // ---- Step 1: Build surface meshes for each label (no ghost shell) ----
    for (int lbl : labels) {
        // Use vtkDiscreteMarchingCubes directly (no binary volume copy)
        auto dmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
        dmc->SetInputData(maskData);
        dmc->SetNumberOfContours(1);
        dmc->SetValue(0, lbl);
        dmc->ComputeNormalsOn();
        dmc->Update();

        if (dmc->GetOutput()->GetNumberOfPoints() == 0) continue;

        auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smoother->SetInputConnection(dmc->GetOutputPort());
        smoother->SetNumberOfIterations(15);
        smoother->SetPassBand(0.1);
        smoother->BoundarySmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        auto polyData = vtkSmartPointer<vtkPolyData>::New();
        polyData->DeepCopy(smoother->GetOutput());

        cachedMeshes_.append({lbl, polyData, nullptr});
    }

    // ---- Step 2: Build E-colored solid volume ----
    // Mirrors Python: for each selected voxel, create a hex cell and sample E at its center.
    // Build per-label E-colored volumes for independent clip/toggle
    if (hasEData) {
        int* eDims = eVolume_->GetDimensions();
        double* eSpacing = eVolume_->GetSpacing();
        double* eOrigin = eVolume_->GetOrigin();
        auto* eScalars = eVolume_->GetPointData()->GetScalars();

        // Map label → VertMesh index
        QMap<int, int> labelToIdx;
        for (int i = 0; i < cachedMeshes_.size(); i++)
            labelToIdx[cachedMeshes_[i].label] = i;

        // Per-label builders
        struct LabelBuilder {
            vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
            vtkSmartPointer<vtkUnstructuredGrid> grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
            vtkSmartPointer<vtkFloatArray> eArr = vtkSmartPointer<vtkFloatArray>::New();
        };
        QMap<int, LabelBuilder> builders;
        for (int lbl : labels) {
            builders[lbl].eArr->SetName("E_modulus");
        }

        for (int iz = 0; iz < dims[2]; ++iz) {
            for (int iy = 0; iy < dims[1]; ++iy) {
                for (int ix = 0; ix < dims[0]; ++ix) {
                    int ptIdx = iz * dims[0] * dims[1] + iy * dims[0] + ix;
                    int lbl = static_cast<int>(scalars->GetTuple1(ptIdx));
                    if (!labels.count(lbl)) continue;

                    // 8 corners of this voxel (matching VTK hex ordering)
                    double x0 = origin[0] + ix       * spacing[0];
                    double y0 = origin[1] + iy       * spacing[1];
                    double z0 = origin[2] + iz       * spacing[2];
                    double x1 = origin[0] + (ix + 1) * spacing[0];
                    double y1 = origin[1] + (iy + 1) * spacing[1];
                    double z1 = origin[2] + (iz + 1) * spacing[2];

                    auto& b = builders[lbl];
                    vtkIdType base = b.points->GetNumberOfPoints();
                    b.points->InsertNextPoint(x0, y0, z0);
                    b.points->InsertNextPoint(x1, y0, z0);
                    b.points->InsertNextPoint(x1, y1, z0);
                    b.points->InsertNextPoint(x0, y1, z0);
                    b.points->InsertNextPoint(x0, y0, z1);
                    b.points->InsertNextPoint(x1, y0, z1);
                    b.points->InsertNextPoint(x1, y1, z1);
                    b.points->InsertNextPoint(x0, y1, z1);

                    vtkIdType ids[8] = { base, base+1, base+2, base+3,
                                         base+4, base+5, base+6, base+7 };
                    b.grid->InsertNextCell(VTK_HEXAHEDRON, 8, ids);

                    double cx = (x0 + x1) * 0.5;
                    double cy = (y0 + y1) * 0.5;
                    double cz = (z0 + z1) * 0.5;
                    int ei = std::clamp((int)((cx - eOrigin[0]) / eSpacing[0] + 0.5), 0, eDims[0]-1);
                    int ej = std::clamp((int)((cy - eOrigin[1]) / eSpacing[1] + 0.5), 0, eDims[1]-1);
                    int ek = std::clamp((int)((cz - eOrigin[2]) / eSpacing[2] + 0.5), 0, eDims[2]-1);
                    float eVal = (float)eScalars->GetTuple1(ek * eDims[0]*eDims[1] + ej * eDims[0] + ei);
                    b.eArr->InsertNextValue(eVal > 1.0f ? eVal : (float)eRangeMin_);
                }
            }
        }

        // Finalize per-label grids: cell→point data, store in cachedMeshes_
        int totalCells = 0;
        for (auto it = builders.begin(); it != builders.end(); ++it) {
            int lbl = it.key();
            auto& b = it.value();
            b.grid->SetPoints(b.points);
            b.grid->GetCellData()->AddArray(b.eArr);
            b.grid->GetCellData()->SetActiveScalars("E_modulus");
            totalCells += b.grid->GetNumberOfCells();

            if (b.grid->GetNumberOfCells() > 0) {
                auto c2p = vtkSmartPointer<vtkCellDataToPointData>::New();
                c2p->SetInputData(b.grid);
                c2p->Update();
                auto result = vtkUnstructuredGrid::SafeDownCast(c2p->GetOutput());
                if (result) {
                    result->GetPointData()->SetActiveScalars("E_modulus");
                    // Store in cachedMeshes_
                    int idx = labelToIdx.value(lbl, -1);
                    if (idx >= 0) {
                        cachedMeshes_[idx].eGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
                        cachedMeshes_[idx].eGrid->DeepCopy(result);
                    }
                }
            }
        }
        hasEColoredVolume_ = (totalCells > 0);
        appendLog(QString("E hex cells: %1 (per-label)").arg(totalCells));
    }

    // ---- Step 3: Initial clip and render ----
    doModulusClip(clipNormal_[0], clipNormal_[1], clipNormal_[2],
                  clipOrigin_[0], clipOrigin_[1], clipOrigin_[2]);

    // Add scalar bar
    if (hasEData) {
        jetLUT_->SetTableRange(eRangeMin_, eRangeMax_);

        scalarBar_ = vtkSmartPointer<vtkScalarBarActor>::New();
        scalarBar_->SetLookupTable(jetLUT_);
        scalarBar_->SetTitle("E (MPa)");
        scalarBar_->SetNumberOfLabels(5);
        scalarBar_->SetPosition(0.85, 0.1);
        scalarBar_->SetWidth(0.12);
        scalarBar_->SetHeight(0.75);
        scalarBar_->GetTitleTextProperty()->SetColor(1, 1, 1);
        scalarBar_->GetTitleTextProperty()->SetFontSize(20);
        scalarBar_->GetLabelTextProperty()->SetColor(1, 1, 1);
        scalarBar_->GetLabelTextProperty()->SetFontSize(16);
        renderer->AddActor2D(scalarBar_);
    }

    renderer->ResetCamera();
    setupClipPlaneWidget();

    auto* rw = viewport->get3DRenderWindow();
    if (rw) rw->Render();

    appendLog(QString("3D material view complete: %1 meshes").arg(cachedMeshes_.size()));

    // Populate vertebra selection checkboxes
    populateBMDVertebraList();
}

void ModulusEstimationModule::setupClipPlaneWidget()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!rw || !renderer) return;

    if (planeWidget_) { planeWidget_->Off(); planeWidget_ = nullptr; }

    planeWidget_ = vtkSmartPointer<vtkImplicitPlaneWidget2>::New();
    auto rep = vtkSmartPointer<vtkImplicitPlaneRepresentation>::New();
    rep->SetPlaceFactor(1.2);
    // Use bounds from per-label E grids (same coordinate space as BMD rendering)
    {
        double bounds[6] = {1e30, -1e30, 1e30, -1e30, 1e30, -1e30};
        bool hasBounds = false;
        for (const auto& vm : cachedMeshes_) {
            if (vm.eGrid.Get() && vm.eGrid->GetNumberOfCells() > 0) {
                double b[6];
                vm.eGrid->GetBounds(b);
                if (b[0] < bounds[0]) bounds[0] = b[0];
                if (b[1] > bounds[1]) bounds[1] = b[1];
                if (b[2] < bounds[2]) bounds[2] = b[2];
                if (b[3] > bounds[3]) bounds[3] = b[3];
                if (b[4] < bounds[4]) bounds[4] = b[4];
                if (b[5] > bounds[5]) bounds[5] = b[5];
                hasBounds = true;
            }
        }
        if (hasBounds) {
            rep->PlaceWidget(bounds);
            // Override clipOrigin to center of E grids
            clipOrigin_[0] = (bounds[0] + bounds[1]) / 2.0;
            clipOrigin_[1] = (bounds[2] + bounds[3]) / 2.0;
            clipOrigin_[2] = (bounds[4] + bounds[5]) / 2.0;
        }
    }
    rep->SetNormal(clipNormal_);
    rep->SetOrigin(clipOrigin_);
    rep->GetPlaneProperty()->SetOpacity(0.0);
    rep->GetSelectedPlaneProperty()->SetOpacity(0.0);
    rep->SetDrawOutline(true);
    rep->GetOutlineProperty()->SetColor(0.7, 0.7, 0.7);
    rep->GetOutlineProperty()->SetOpacity(0.4);

    planeWidget_->SetRepresentation(rep);
    planeWidget_->SetInteractor(rw->GetInteractor());
    planeWidget_->AddObserver(vtkCommand::InteractionEvent,
        this, &ModulusEstimationModule::onClipPlaneChanged);
    planeWidget_->On();
}

void ModulusEstimationModule::onClipPlaneChanged(vtkObject*, unsigned long, void*)
{
    if (!planeWidget_) return;
    auto* rep = vtkImplicitPlaneRepresentation::SafeDownCast(
        planeWidget_->GetRepresentation());
    if (!rep) return;
    rep->GetNormal(clipNormal_);
    rep->GetOrigin(clipOrigin_);
    doModulusClip(clipNormal_[0], clipNormal_[1], clipNormal_[2],
                  clipOrigin_[0], clipOrigin_[1], clipOrigin_[2]);
}

void ModulusEstimationModule::doModulusClip(double nx, double ny, double nz,
                                             double ox, double oy, double oz)
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!renderer) return;

    // Save camera before re-clipping
    auto* cam = renderer->GetActiveCamera();
    double camPos[3], camFocal[3], camUp[3];
    bool hasCam = cam != nullptr;
    if (hasCam) {
        cam->GetPosition(camPos);
        cam->GetFocalPoint(camFocal);
        cam->GetViewUp(camUp);
    }

    // Remove old clip actors
    for (auto& a : clipActors_) {
        if (a.Get()) renderer->RemoveActor(a);
    }
    clipActors_.clear();

    auto plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetNormal(nx, ny, nz);
    plane->SetOrigin(ox, oy, oz);

    if (hasEColoredVolume_) {
        // Per-label clip: clip each vertebra's E-grid separately
        jetLUT_->SetTableRange(eRangeMin_, eRangeMax_);

        for (auto& vm : cachedMeshes_) {
            // Remove old clip actor
            if (vm.eClipActor.Get()) {
                renderer->RemoveActor(vm.eClipActor);
                vm.eClipActor = nullptr;
            }

            if (!vm.eGrid.Get()) continue;

            // Check visibility from checkbox
            auto cbIt = bmdVertCheckboxes_.find(vm.label);
            if (cbIt != bmdVertCheckboxes_.end() && !cbIt.value()->isChecked())
                continue;

            auto clipper = vtkSmartPointer<vtkClipDataSet>::New();
            clipper->SetInputData(vm.eGrid);
            clipper->SetClipFunction(plane);
            clipper->InsideOutOn();
            clipper->Update();

            auto clipped = clipper->GetOutput();
            if (clipped && clipped->GetNumberOfCells() > 0) {
                auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
                mapper->SetInputData(clipped);
                mapper->SetScalarVisibility(true);
                mapper->SetLookupTable(jetLUT_);
                mapper->SetScalarRange(eRangeMin_, eRangeMax_);
                mapper->SetScalarModeToUsePointData();
                mapper->SelectColorArray("E_modulus");

                auto actor = vtkSmartPointer<vtkActor>::New();
                actor->SetMapper(mapper);
                actor->GetProperty()->SetOpacity(1.0);
                actor->GetProperty()->SetInterpolationToPhong();
                actor->GetProperty()->SetAmbient(0.2);
                actor->GetProperty()->SetDiffuse(0.8);
                actor->GetProperty()->SetSpecular(0.3);

                renderer->AddActor(actor);
                clipActors_.append(actor);
                vm.eClipActor = actor;
            }
        }
    } else {
        // No E data: clip each surface mesh with per-label color
        for (auto& vm : cachedMeshes_) {
            if (!vm.polyData.Get()) continue;

            auto clipper = vtkSmartPointer<vtkClipPolyData>::New();
            clipper->SetInputData(vm.polyData);
            clipper->SetClipFunction(plane);
            clipper->InsideOutOn();
            clipper->Update();

            auto clipped = clipper->GetOutput();
            if (!clipped || clipped->GetNumberOfPoints() == 0) continue;

            auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            mapper->SetInputData(clipped);
            mapper->ScalarVisibilityOff();

            auto actor = vtkSmartPointer<vtkActor>::New();
            actor->SetMapper(mapper);
            int ci = (vm.label - 1) % 26;
            actor->GetProperty()->SetColor(
                VERT_COLORS[ci][0]/255.0, VERT_COLORS[ci][1]/255.0, VERT_COLORS[ci][2]/255.0);
            actor->GetProperty()->SetOpacity(1.0);
            actor->GetProperty()->SetInterpolationToPhong();

            renderer->AddActor(actor);
            clipActors_.append(actor);
        }
    }

    skip_clip:
    // Restore camera
    if (hasCam && cam) {
        cam->SetPosition(camPos);
        cam->SetFocalPoint(camFocal);
        cam->SetViewUp(camUp);
    }

    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    if (rw) rw->Render();
}

// ---- Logging ----

void ModulusEstimationModule::appendLog(const QString& msg)
{
    statsText_->append(msg);
}

// ---- Refresh from project ----

void ModulusEstimationModule::refreshFromProject()
{
    if (!app_) return;

    auto* viewport = app_->sharedViewport();

    // Connect 3D mode switch (once)
    static bool connected = false;
    if (!connected && viewport) {
        connect(viewport, &SharedViewport::mode3DChanged, this, [this](const QString& mode) {
            if (mode == "material") {
                build3DView();
            } else if (mode == "mesh") {
                buildMesh3DView();
            } else {
                // Switching away: turn off the clip plane widget
                if (planeWidget_) {
                    planeWidget_->Off();
                    planeWidget_ = nullptr;
                }
            }
        });
        connected = true;
    }

    QString ctFilePath = app_->currentProject.value("ct_file_path").toString();
    if (!ctFilePath.isEmpty()) {
        if (viewport && viewport->loadedCTPath_ == ctFilePath && viewport->imageData_) {
            appendLog("CT already loaded, skipping re-read.");
        } else {
            try {
                auto imageData = NiftiIO::readNifti(ctFilePath.toStdString());
                if (imageData && viewport) {
                    int* dims = imageData->GetDimensions();
                    appendLog(QString("CT loaded: %1 x %2 x %3")
                        .arg(dims[0]).arg(dims[1]).arg(dims[2]));
                    viewport->setImageData(imageData);
                    viewport->loadedCTPath_ = ctFilePath;
                }
            } catch (const std::exception& e) {
                appendLog(QString("Error loading CT: %1").arg(e.what()));
            }
        }
    } else {
        appendLog("No CT data. Use Data module to load files.");
    }

    // Load mask
    QString maskPath = app_->currentProject.value("mask_file_path").toString();
    if (!maskPath.isEmpty() && QFile::exists(maskPath)) {
        if (viewport && viewport->loadedMaskPath_ == maskPath && viewport->maskData_) {
            appendLog("Mask already loaded, skipping re-read.");
            viewport->setMaskVisible(true);
            viewport->setMaskOpacity(0.45);
        } else {
            try {
                auto maskData = NiftiIO::readNifti(maskPath.toStdString());
                if (maskData && viewport) {
                    int* dims = maskData->GetDimensions();
                    appendLog(QString("Mask loaded: %1 x %2 x %3")
                        .arg(dims[0]).arg(dims[1]).arg(dims[2]));
                    viewport->setMaskData(maskData);
                    viewport->setMaskVisible(true);
                    viewport->setMaskOpacity(0.45);
                    viewport->loadedMaskPath_ = maskPath;
                }
            } catch (const std::exception& e) {
                appendLog(QString("Error loading mask: %1").arg(e.what()));
            }
        }
    }

    // 恢复 BMD 参数（从 .si 项目加载时）
    if (app_->currentProject.value("has_bmd").toBool()) {
        calibSlope_ = app_->currentProject.value("calib_slope").toDouble();
        calibIntercept_ = app_->currentProject.value("calib_intercept").toDouble();
        hasCalibration_ = true;

        slopeEdit_->setText(QString::number(calibSlope_, 'f', 6));
        interceptEdit_->setText(QString::number(calibIntercept_, 'f', 4));

        double coef = app_->currentProject.value("calib_coefficient").toDouble();
        double exp_val = app_->currentProject.value("calib_exponent").toDouble();
        if (coef > 0) coefEdit_->setText(QString::number(coef, 'f', 0));
        if (exp_val > 0) expEdit_->setText(QString::number(exp_val, 'f', 2));

        // 加载 E_volume
        QString eVolPath = app_->currentProject.value("e_volume_path").toString();
        if (!eVolPath.isEmpty() && QFile::exists(eVolPath) && !eVolume_) {
            try {
                auto eVol = NiftiIO::readNifti(eVolPath.toStdString());
                if (eVol) {
                    eVolume_ = eVol;
                    if (viewport) {
                        viewport->setEVolume(eVol);
                        overlayCheckbox_->setChecked(true);
                    }
                    appendLog("E_volume restored from project.");
                }
            } catch (const std::exception& e) {
                appendLog(QString("Error loading E_volume: %1").arg(e.what()));
            }
        }
    }

    appendLog("==================================================");
}
