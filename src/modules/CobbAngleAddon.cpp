/**
 * ============================================================================
 * CobbAngleAddon - Cobb Angle Measurement (implementation)
 * ============================================================================
 * Ported from SpineX-Py tests/test_cobb_angle.py + modules/cobb_angle_addon.py
 */

#include "CobbAngleAddon.h"
#include "app/SpineXApp.h"
#include "ui/BrushCanvas.h"
#include "bridge/PythonRuntime.h"
#include "io/NiftiIO.h"

#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkDiskSource.h>
#include <vtkLineSource.h>
#include <vtkSphereSource.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkCamera.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkArcSource.h>
#include <vtkLineSource.h>
#include <vtkTextActor3D.h>
#include <vtkTextProperty.h>
#include <vtkTubeFilter.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkDecimatePro.h>
#include <vtkPolyDataNormals.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QFont>
#include <QApplication>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QPixmap>

#include <cmath>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <fstream>

#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---- Vertebra names & colors ----

static const QStringList& vertebraNames()
{
    static QStringList names = {
        "C1","C2","C3","C4","C5","C6","C7",
        "T1","T2","T3","T4","T5","T6","T7","T8","T9","T10","T11","T12",
        "L1","L2","L3","L4","L5",
        "S1","S2"
    };
    return names;
}

static const QVector<QColor>& vertebraColors()
{
    static QVector<QColor> colors = {
        QColor(255,0,0),     QColor(0,255,0),     QColor(0,0,255),
        QColor(255,255,0),   QColor(0,255,255),   QColor(255,0,255),
        QColor(255,239,213), QColor(0,0,205),     QColor(205,133,63),
        QColor(210,180,140), QColor(102,205,170), QColor(0,0,128),
        QColor(0,139,139),   QColor(46,139,87),   QColor(255,228,225),
        QColor(106,90,205),  QColor(221,160,221), QColor(233,150,122),
        QColor(165,42,42),   QColor(255,250,250), QColor(147,112,219),
        QColor(218,112,214), QColor(75,0,130),    QColor(255,182,193),
        QColor(60,179,113),  QColor(255,235,205),
    };
    return colors;
}

// ---- Region classification ----

QString CobbAngleAddon::getRegion(int label)
{
    if (label >= 1  && label <= 7)  return "cervical";
    if (label >= 8  && label <= 19) return "thoracic";
    if (label >= 20 && label <= 24) return "lumbar";
    if (label >= 25 && label <= 26) return "sacral";
    return "unknown";
}

// ---- Constructor / UI ----

CobbAngleAddon::CobbAngleAddon(QWidget* parent)
    : ModuleBase("Cobb Angle", parent)
{
    initUI();
}

CobbAngleAddon::~CobbAngleAddon() = default;

void CobbAngleAddon::initUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(createLeftPanel());
}

QWidget* CobbAngleAddon::createLeftPanel()
{
    auto* panel = new QWidget();
    auto* outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    outerLayout->addWidget(makeModuleHeader("Cobb Angle"));

    // ── Page stack: controls (page 0) vs results (page 1) ──
    pageStack_ = new QStackedWidget();

    // ================================================================
    // PAGE 0: Controls
    // ================================================================
    controlsPage_ = new QWidget();
    {
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setStyleSheet("QScrollArea { border: none; }");

        auto* content = new QWidget();
        content->setStyleSheet("background-color: #f4f6f8;");
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(12, 10, 12, 12);
        layout->setSpacing(10);

        auto* tip = new QLabel(
            "  Full-spine Cobb measurement: uses every labelled vertebra in the mask.\n"
            "  Pipeline runs ~30-60 s and reports all clinical curves (≥ 10°).");
        tip->setWordWrap(true);
        tip->setStyleSheet(
            "background-color: #eaf4f8; color: #2c5f6f; padding: 7px 10px;"
            "border-radius: 4px; border-left: 3px solid #2c5f6f; font-size: 9pt;");
        layout->addWidget(tip);

        // Vertebra selection
        vertebraSection_ = new QWidget();
        vertebraSection_->setVisible(false);
        auto* vertOuterLayout = new QVBoxLayout(vertebraSection_);
        vertOuterLayout->setContentsMargins(0, 0, 0, 0);

        auto* vertGroup = new QGroupBox("VERTEBRA SELECTION");
        auto* vertGroupLayout = new QVBoxLayout(vertGroup);
        vertGroupLayout->setSpacing(4);

        QString smallBtnStyle =
            "QPushButton { background-color: #e9ecef; color: #1a2332;"
            " min-height: 26px; font-size: 9pt; padding: 0 10px; }"
            "QPushButton:hover { background-color: #dee2e6; }";

        auto* btnRow = new QHBoxLayout();
        auto* selAllBtn = new QPushButton("Select All");
        selAllBtn->setStyleSheet(smallBtnStyle);
        connect(selAllBtn, &QPushButton::clicked, this, &CobbAngleAddon::selectAllVertebrae);
        auto* selNoneBtn = new QPushButton("Deselect All");
        selNoneBtn->setStyleSheet(smallBtnStyle);
        connect(selNoneBtn, &QPushButton::clicked, this, &CobbAngleAddon::deselectAllVertebrae);
        btnRow->addWidget(selAllBtn);
        btnRow->addWidget(selNoneBtn);
        vertGroupLayout->addLayout(btnRow);

        auto* vertScroll = new QScrollArea();
        vertScroll->setWidgetResizable(true);
        vertScroll->setMaximumHeight(220);
        auto* vertListWidget = new QWidget();
        vertebraListLayout_ = new QVBoxLayout(vertListWidget);
        vertebraListLayout_->setContentsMargins(4, 4, 4, 4);
        vertebraListLayout_->setSpacing(2);
        vertebraListLayout_->addStretch();
        vertScroll->setWidget(vertListWidget);
        vertGroupLayout->addWidget(vertScroll);

        vertOuterLayout->addWidget(vertGroup);
        layout->addWidget(vertebraSection_);

        // Cobb angle tools
        auto* cobbGroup = new QGroupBox("COBB ANGLE TOOLS");
        auto* cobbLayout = new QVBoxLayout(cobbGroup);
        cobbLayout->setSpacing(6);

        // doubleThoracicCb_ removed; full-spine pipeline handles all curves automatically.
        doubleThoracicCb_ = nullptr;

        calculateBtn_ = new QPushButton("Calculate Cobb Angle");
        calculateBtn_->setStyleSheet(
            "QPushButton { background-color: #2c5f6f; color: white; min-height: 32px; font-size: 10pt; }"
            "QPushButton:hover { background-color: #1e4a57; }"
            "QPushButton:pressed { background-color: #163d49; }");
        connect(calculateBtn_, &QPushButton::clicked, this, &CobbAngleAddon::calculateCobbAngle);
        cobbLayout->addWidget(calculateBtn_);

        clearBtn_ = new QPushButton("Clear Measurement");
        clearBtn_->setStyleSheet(
            "QPushButton { background-color: #6b7280; color: white; }"
            "QPushButton:hover { background-color: #4b5563; }");
        connect(clearBtn_, &QPushButton::clicked, this, &CobbAngleAddon::clearMeasurement);
        cobbLayout->addWidget(clearBtn_);

        // View mode toggle: switch the right-hand 3D scene between coronal Cobb
        // (segments + apex highlights) and sagittal parameters (TK/CL/LL/cSVA).
        auto* viewRow = new QHBoxLayout();
        auto* viewLabel = new QLabel("View:");
        viewLabel->setStyleSheet("color: #4b5563; font-size: 9pt;");
        viewRow->addWidget(viewLabel);
        viewCoronalBtn_  = new QPushButton("Coronal");
        viewSagittalBtn_ = new QPushButton("Sagittal");
        viewCoronalBtn_->setCheckable(true);
        viewSagittalBtn_->setCheckable(true);
        viewCoronalBtn_->setChecked(true);
        QString toggleStyle =
            "QPushButton { background-color: #e9ecef; color: #1a2332;"
            " min-height: 26px; font-size: 9pt; padding: 0 12px; border-radius: 4px; }"
            "QPushButton:checked { background-color: #2c5f6f; color: white; }"
            "QPushButton:hover { background-color: #dee2e6; }"
            "QPushButton:checked:hover { background-color: #1e4a57; }";
        viewCoronalBtn_->setStyleSheet(toggleStyle);
        viewSagittalBtn_->setStyleSheet(toggleStyle);
        connect(viewCoronalBtn_, &QPushButton::clicked, [this]() { switchViewMode(false); });
        connect(viewSagittalBtn_, &QPushButton::clicked, [this]() { switchViewMode(true); });
        viewRow->addWidget(viewCoronalBtn_);
        viewRow->addWidget(viewSagittalBtn_);
        viewRow->addStretch();
        cobbLayout->addLayout(viewRow);

        // Per-curve result list (populated dynamically by calculateCobbAngle).
        auto* resultFrame = new QFrame();
        resultFrame->setStyleSheet(
            "background-color: #ffffff; border: 1px solid #dde1e7; border-radius: 6px;");
        auto* resultLayout = new QVBoxLayout(resultFrame);
        resultLayout->setContentsMargins(12, 10, 12, 10);
        resultLayout->setSpacing(4);

        // Default placeholder shown before any calculation has run.
        cobbAngleLbl_ = new QLabel(QString::fromUtf8("No curves yet \u2014 click Calculate."));
        cobbAngleLbl_->setStyleSheet("color: #6b7280; font-size: 10pt; border: none;");
        cobbAngleLbl_->setAlignment(Qt::AlignCenter);
        resultLayout->addWidget(cobbAngleLbl_);

        upperVertLbl_ = nullptr;
        lowerVertLbl_ = nullptr;

        curvesResultLayout_ = new QVBoxLayout();
        curvesResultLayout_->setSpacing(2);
        resultLayout->addLayout(curvesResultLayout_);

        // Sagittal parameters section header (kept slim; rows added dynamically).
        auto* sagSep = new QFrame();
        sagSep->setFrameShape(QFrame::HLine);
        sagSep->setStyleSheet("border: none; max-height: 1px; background: #dde1e7;");
        resultLayout->addWidget(sagSep);
        auto* sagHdr = new QLabel("SAGITTAL PARAMETERS");
        sagHdr->setStyleSheet("color: #6b7280; font-size: 9pt; font-weight: bold;"
                              " border: none; letter-spacing: 1px;");
        sagHdr->setAlignment(Qt::AlignCenter);
        resultLayout->addWidget(sagHdr);

        sagittalResultLayout_ = new QVBoxLayout();
        sagittalResultLayout_->setSpacing(2);
        resultLayout->addLayout(sagittalResultLayout_);

        cobbLayout->addWidget(resultFrame);

        // viewResultsBtn_ removed \u2014 no PNG plot is produced now.
        viewResultsBtn_ = nullptr;

        layout->addWidget(cobbGroup);

        // Log
        auto* logGroup = new QGroupBox("LOG");
        auto* logLayout = new QVBoxLayout(logGroup);
        logText_ = new QTextEdit();
        logText_->setReadOnly(true);
        logText_->setMaximumHeight(120);
        logText_->setStyleSheet("font-size: 8pt; font-family: Consolas, monospace;");
        logLayout->addWidget(logText_);
        layout->addWidget(logGroup);

        layout->addStretch();
        scrollArea->setWidget(content);

        auto* pageLayout = new QVBoxLayout(controlsPage_);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(scrollArea);
    }
    pageStack_->addWidget(controlsPage_);

    // ================================================================
    // PAGE 1: Results (coronal plot)
    // ================================================================
    resultsPage_ = new QWidget();
    {
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setStyleSheet("QScrollArea { border: none; }");

        auto* content = new QWidget();
        content->setStyleSheet("background-color: #f4f6f8;");
        auto* rLayout = new QVBoxLayout(content);
        rLayout->setContentsMargins(8, 8, 8, 8);
        rLayout->setSpacing(8);

        // Back button
        backBtn_ = new QPushButton("\u2190  Back to Controls");
        backBtn_->setStyleSheet(
            "QPushButton { background-color: #6b7280; color: white; min-height: 28px; font-size: 9pt; }"
            "QPushButton:hover { background-color: #4b5563; }");
        connect(backBtn_, &QPushButton::clicked, this, &CobbAngleAddon::showControlsPage);
        rLayout->addWidget(backBtn_);

        // Title
        auto* title = new QLabel("Detected Curves + Cobb Angles");
        title->setFont(QFont("Segoe UI", 10, QFont::Bold));
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("color: #1a2332;");
        rLayout->addWidget(title);

        // Plot image
        plotLabel_ = new QLabel();
        plotLabel_->setAlignment(Qt::AlignCenter);
        plotLabel_->setStyleSheet("background-color: #ffffff; border: 1px solid #dde1e7; border-radius: 4px;");
        plotLabel_->setMinimumHeight(500);
        plotLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        rLayout->addWidget(plotLabel_, 1);

        rLayout->addStretch();
        scrollArea->setWidget(content);

        auto* pageLayout = new QVBoxLayout(resultsPage_);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(scrollArea);
    }
    pageStack_->addWidget(resultsPage_);

    outerLayout->addWidget(pageStack_);
    return panel;
}

void CobbAngleAddon::showResultsPage()
{
    // Plot already loaded by calculateCobbAngle
    pageStack_->setCurrentIndex(1);
}

void CobbAngleAddon::showControlsPage()
{
    pageStack_->setCurrentIndex(0);
}

void CobbAngleAddon::appendLog(const QString& msg)
{
    logText_->append(msg);
    QApplication::processEvents();
}

// ---- 3D rendering (only checked vertebrae) ----

static const int COBB_VERT_COLORS[][3] = {
    {255,0,0},     {0,255,0},     {0,0,255},
    {255,255,0},   {0,255,255},   {255,0,255},
    {255,239,213}, {0,0,205},     {205,133,63},
    {210,180,140}, {102,205,170}, {0,0,128},
    {0,139,139},   {46,139,87},   {255,228,225},
    {106,90,205},  {221,160,221}, {233,150,122},
    {165,42,42},   {255,250,250}, {147,112,219},
    {218,112,214}, {75,0,130},    {255,182,193},
    {60,179,113},  {255,235,205},
};

void CobbAngleAddon::render3DSelected()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    auto* renderer = viewport->get3DRenderer();
    auto maskData = viewport->maskData_;
    if (!renderer || !maskData) return;

    // Remove old actors
    for (auto it = vertebraActors_.begin(); it != vertebraActors_.end(); ++it) {
        if (it.value()) renderer->RemoveActor(it.value());
    }
    vertebraActors_.clear();

    // Find which labels are checked
    std::set<int> checkedLabels;
    for (auto it = vertebraCheckboxes_.begin(); it != vertebraCheckboxes_.end(); ++it) {
        if (it.value()->isChecked()) checkedLabels.insert(it.key());
    }

    // Generate mesh per checked label
    for (int lbl : checkedLabels) {
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
        smoother->NonManifoldSmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        auto decimator = vtkSmartPointer<vtkDecimatePro>::New();
        decimator->SetInputConnection(smoother->GetOutputPort());
        decimator->SetTargetReduction(0.3);
        decimator->PreserveTopologyOn();
        decimator->Update();

        auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
        normals->SetInputConnection(decimator->GetOutputPort());
        normals->SetFeatureAngle(60.0);
        normals->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(normals->GetOutputPort());
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        int ci = (lbl - 1) % 26;
        actor->GetProperty()->SetColor(
            COBB_VERT_COLORS[ci][0] / 255.0,
            COBB_VERT_COLORS[ci][1] / 255.0,
            COBB_VERT_COLORS[ci][2] / 255.0);
        actor->GetProperty()->SetOpacity(0.85);
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetSpecular(0.3);

        renderer->AddActor(actor);
        vertebraActors_[lbl] = actor;
    }

    renderer->ResetCamera();
    if (viewport->get3DRenderWindow()) viewport->get3DRenderWindow()->Render();
}

void CobbAngleAddon::onVertebraToggled(int label, bool visible)
{
    // Fast path: toggle existing actor visibility
    if (vertebraActors_.contains(label) && vertebraActors_[label]) {
        vertebraActors_[label]->SetVisibility(visible ? 1 : 0);
        if (app_ && app_->sharedViewport() && app_->sharedViewport()->get3DRenderWindow())
            app_->sharedViewport()->get3DRenderWindow()->Render();
        return;
    }

    // Actor doesn't exist yet (was unchecked before), rebuild
    if (visible) render3DSelected();
}

// ---- Vertebra selection ----

void CobbAngleAddon::selectAllVertebrae()
{
    for (auto* cb : vertebraCheckboxes_) cb->setChecked(true);
}

void CobbAngleAddon::deselectAllVertebrae()
{
    for (auto* cb : vertebraCheckboxes_) cb->setChecked(false);
}

void CobbAngleAddon::populateVertebraListFromMask(vtkSmartPointer<vtkImageData> maskData)
{
    if (!maskData) return;

    std::set<int> labels;
    int numVoxels = maskData->GetNumberOfPoints();
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    for (int i = 0; i < numVoxels; ++i) {
        int val = static_cast<int>(scalars->GetTuple1(i));
        if (val > 0) labels.insert(val);
    }

    if (labels.empty()) return;

    // Clear existing
    QLayoutItem* child;
    while ((child = vertebraListLayout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    vertebraCheckboxes_.clear();

    const auto& names = vertebraNames();
    const auto& colors = vertebraColors();

    for (int lbl : labels) {
        int idx = (lbl - 1) % colors.size();
        QString name = (lbl - 1 < names.size()) ? names[lbl - 1] : QString("Label %1").arg(lbl);
        QColor color = colors[idx];
        QString hexColor = QString("rgb(%1,%2,%3)").arg(color.red()).arg(color.green()).arg(color.blue());

        auto* cb = new QCheckBox(QString("  %1  (label %2)").arg(name).arg(lbl));
        cb->setChecked(true);
        cb->setStyleSheet(QString(
            "QCheckBox { font-size: 9pt; color: #222; }"
            "QCheckBox::indicator:checked { background-color: %1; border: 1px solid #666; border-radius: 2px; }"
            "QCheckBox::indicator:unchecked { background-color: #ccc; border: 1px solid #999; border-radius: 2px; }")
            .arg(hexColor));
        connect(cb, &QCheckBox::toggled, [this, lbl](bool checked) {
            onVertebraToggled(lbl, checked);
        });
        vertebraListLayout_->insertWidget(vertebraListLayout_->count() - 1, cb);
        vertebraCheckboxes_[lbl] = cb;
    }

    vertebraSection_->setVisible(true);
}

// ============================================================================
// RANSAC line fitting: z = slope * x + intercept (in mm)
// ============================================================================

CobbAngleAddon::LineFit CobbAngleAddon::ransacLine(
    const std::vector<double>& x, const std::vector<double>& z,
    int nIter, double threshMm)
{
    LineFit result;
    int n = static_cast<int>(x.size());
    if (n < 2) return result;

    // If too few points, fall back to least-squares
    if (n < 4) {
        double sumX = 0, sumZ = 0, sumXX = 0, sumXZ = 0;
        for (int i = 0; i < n; ++i) {
            sumX += x[i]; sumZ += z[i];
            sumXX += x[i]*x[i]; sumXZ += x[i]*z[i];
        }
        double dn = static_cast<double>(n);
        double denom = dn * sumXX - sumX * sumX;
        if (std::abs(denom) < 1e-10) return result;
        result.slope = (dn * sumXZ - sumX * sumZ) / denom;
        result.intercept = (sumZ - result.slope * sumX) / dn;
        result.nPoints = n;
        result.inlierRatio = 1.0;
        result.xMin = *std::min_element(x.begin(), x.end());
        result.xMax = *std::max_element(x.begin(), x.end());
        result.valid = true;
        return result;
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, n - 1);

    int bestInliers = 0;
    double bestSlope = 0, bestIntercept = 0;

    for (int iter = 0; iter < nIter; ++iter) {
        int i1 = dist(rng), i2 = dist(rng);
        if (i1 == i2) continue;
        double dx = x[i1] - x[i2];
        if (std::abs(dx) < 1e-10) continue;

        double s = (z[i1] - z[i2]) / dx;
        double b = z[i1] - s * x[i1];

        // Count inliers (perpendicular distance)
        double norm = std::sqrt(1.0 + s * s);
        int inliers = 0;
        for (int k = 0; k < n; ++k) {
            double d = std::abs(z[k] - s * x[k] - b) / norm;
            if (d <= threshMm) ++inliers;
        }

        if (inliers > bestInliers) {
            bestInliers = inliers;
            bestSlope = s;
            bestIntercept = b;
        }
    }

    if (bestInliers < 3) {
        // Fall back to full least-squares
        double sumX = 0, sumZ = 0, sumXX = 0, sumXZ = 0;
        for (int i = 0; i < n; ++i) {
            sumX += x[i]; sumZ += z[i];
            sumXX += x[i]*x[i]; sumXZ += x[i]*z[i];
        }
        double dn = static_cast<double>(n);
        double denom = dn * sumXX - sumX * sumX;
        if (std::abs(denom) < 1e-10) return result;
        result.slope = (dn * sumXZ - sumX * sumZ) / denom;
        result.intercept = (sumZ - result.slope * sumX) / dn;
        result.nPoints = n;
        result.inlierRatio = 1.0;
        result.xMin = *std::min_element(x.begin(), x.end());
        result.xMax = *std::max_element(x.begin(), x.end());
        result.valid = true;
        return result;
    }

    // Refit using only inliers
    double norm = std::sqrt(1.0 + bestSlope * bestSlope);
    std::vector<double> inX, inZ;
    for (int k = 0; k < n; ++k) {
        double d = std::abs(z[k] - bestSlope * x[k] - bestIntercept) / norm;
        if (d <= threshMm) {
            inX.push_back(x[k]);
            inZ.push_back(z[k]);
        }
    }

    int ni = static_cast<int>(inX.size());
    double sumX = 0, sumZ = 0, sumXX = 0, sumXZ = 0;
    for (int i = 0; i < ni; ++i) {
        sumX += inX[i]; sumZ += inZ[i];
        sumXX += inX[i]*inX[i]; sumXZ += inX[i]*inZ[i];
    }
    double dn = static_cast<double>(ni);
    double denom = dn * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-10) return result;

    result.slope = (dn * sumXZ - sumX * sumZ) / denom;
    result.intercept = (sumZ - result.slope * sumX) / dn;

    // Residual
    double totalRes = 0;
    double normFinal = std::sqrt(1.0 + result.slope * result.slope);
    for (int i = 0; i < ni; ++i) {
        totalRes += std::abs(inZ[i] - result.slope * inX[i] - result.intercept) / normFinal;
    }
    result.residual = totalRes / dn;
    result.inlierRatio = static_cast<double>(ni) / static_cast<double>(n);
    result.nPoints = ni;
    result.xMin = *std::min_element(inX.begin(), inX.end());
    result.xMax = *std::max_element(inX.begin(), inX.end());
    result.valid = true;
    return result;
}

// ============================================================================
// Endplate tilt for a single vertebra
// ============================================================================

CobbAngleAddon::VertTilt CobbAngleAddon::getEndplateTilt(
    vtkImageData* mask, int label,
    const double spacing[3], const double origin[3],
    int hDim, int vDim, int apDim,
    double anteriorPct)
{
    // hDim  = horizontal axis in coronal view (L-R, sagittalDim)
    // vDim  = vertical axis in coronal view (I-S, axialDim)
    // apDim = anterior-posterior axis to collapse (coronalDim)

    VertTilt vt;
    vt.label = label;
    const auto& names = vertebraNames();
    int idx = label - 1;
    vt.name = (idx >= 0 && idx < names.size()) ? names[idx] : QString("L%1").arg(label);
    vt.region = getRegion(label);
    if (!mask) return vt;

    int* dims = mask->GetDimensions();
    auto* scalars = mask->GetPointData()->GetScalars();
    if (!scalars) return vt;

    // Collect all voxels of this label, storing the anatomical coordinates
    struct Voxel { int h, ap, v; };  // horizontal, anterior-posterior, vertical
    std::vector<Voxel> voxels;
    voxels.reserve(10000);

    for (int iz = 0; iz < dims[2]; ++iz) {
        for (int iy = 0; iy < dims[1]; ++iy) {
            for (int ix = 0; ix < dims[0]; ++ix) {
                int val = static_cast<int>(scalars->GetTuple1(
                    static_cast<vtkIdType>(iz) * dims[1] * dims[0] +
                    static_cast<vtkIdType>(iy) * dims[0] + ix));
                if (val == label) {
                    int ijk[3] = {ix, iy, iz};
                    voxels.push_back({ijk[hDim], ijk[apDim], ijk[vDim]});
                }
            }
        }
    }

    if (voxels.size() < 10) return vt;

    // Anterior filtering along AP axis
    int apMin = INT_MAX, apMax = INT_MIN;
    for (const auto& v : voxels) {
        apMin = std::min(apMin, v.ap);
        apMax = std::max(apMax, v.ap);
    }
    int apRange = apMax - apMin;
    int apCutoff = apMax - static_cast<int>(std::ceil(apRange * anteriorPct / 100.0));

    std::vector<Voxel> filtered;
    filtered.reserve(voxels.size());
    for (const auto& v : voxels) {
        if (v.ap >= apCutoff) filtered.push_back(v);
    }
    if (filtered.size() < 10) filtered = std::move(voxels);

    // Project onto coronal plane (collapse AP axis):
    // For each H column, find max V (superior) and min V (inferior)
    std::map<int, int> hToVmax, hToVmin;
    double sumH = 0, sumAP = 0, sumV = 0;
    int count = 0;

    for (const auto& v : filtered) {
        auto itMax = hToVmax.find(v.h);
        if (itMax == hToVmax.end() || v.v > itMax->second)
            hToVmax[v.h] = v.v;

        auto itMin = hToVmin.find(v.h);
        if (itMin == hToVmin.end() || v.v < itMin->second)
            hToVmin[v.h] = v.v;

        sumH  += origin[hDim]  + v.h  * spacing[hDim];
        sumAP += origin[apDim] + v.ap * spacing[apDim];
        sumV  += origin[vDim]  + v.v  * spacing[vDim];
        ++count;
    }

    vt.centroidX = sumH / count;    // horizontal center (L-R)
    vt.centroidY = sumAP / count;   // AP center
    vt.centroidZ = sumV / count;    // vertical center (I-S)

    // Build edge point arrays in physical mm
    auto buildEdge = [&](const std::map<int, int>& hToV)
        -> std::pair<std::vector<double>, std::vector<double>>
    {
        std::vector<double> hs, vs;
        for (const auto& [hi, vi] : hToV) {
            hs.push_back(origin[hDim] + hi * spacing[hDim]);
            vs.push_back(origin[vDim] + vi * spacing[vDim]);
        }
        return {hs, vs};
    };

    // Superior endplate (max V = most superior)
    auto [supH, supV] = buildEdge(hToVmax);
    LineFit supFit = ransacLine(supH, supV);

    // Inferior endplate (min V = most inferior)
    auto [infH, infV] = buildEdge(hToVmin);
    LineFit infFit = ransacLine(infH, infV);

    if (supFit.valid) {
        vt.supSlope = supFit.slope;
        vt.supIntercept = supFit.intercept;
        vt.supTilt = std::atan(supFit.slope) * 180.0 / M_PI;
        vt.supXMin = supFit.xMin;
        vt.supXMax = supFit.xMax;
    }
    if (infFit.valid) {
        vt.infSlope = infFit.slope;
        vt.infIntercept = infFit.intercept;
        vt.infTilt = std::atan(infFit.slope) * 180.0 / M_PI;
        vt.infXMin = infFit.xMin;
        vt.infXMax = infFit.xMax;
    }

    double minInlier = std::min(supFit.inlierRatio, infFit.inlierRatio);
    int minPts = std::min(supFit.nPoints, infFit.nPoints);
    if (minInlier < 0.5) vt.flag = "LOW_INLIER";
    else if (minPts < 10) vt.flag = "FEW_PTS";

    vt.valid = supFit.valid && infFit.valid;
    return vt;
}

// ============================================================================
// Compute tilts for all selected vertebrae
// ============================================================================

void CobbAngleAddon::computeAllTilts()
{
    tilts_.clear();
    if (!app_ || !app_->sharedViewport()) return;

    auto maskData = app_->sharedViewport()->maskData_;
    if (!maskData) return;

    double spacing[3], origin[3];
    maskData->GetSpacing(spacing);
    maskData->GetOrigin(origin);

    // Get axis mapping from SharedViewport
    auto* viewport = app_->sharedViewport();
    int hDim  = viewport->sagittalDim();  // horizontal (L-R)
    int vDim  = viewport->axialDim();     // vertical (I-S)
    int apDim = viewport->coronalDim();   // anterior-posterior (to collapse)

    QVector<int> selectedLabels;
    for (auto it = vertebraCheckboxes_.begin(); it != vertebraCheckboxes_.end(); ++it) {
        if (it.value()->isChecked()) selectedLabels.push_back(it.key());
    }
    std::sort(selectedLabels.begin(), selectedLabels.end());

    appendLog(QString("Axes: h(LR)=%1 v(IS)=%2 ap=%3 | spacing=[%4,%5,%6] origin=[%7,%8,%9]")
        .arg(hDim).arg(vDim).arg(apDim)
        .arg(spacing[0],0,'f',2).arg(spacing[1],0,'f',2).arg(spacing[2],0,'f',2)
        .arg(origin[0],0,'f',1).arg(origin[1],0,'f',1).arg(origin[2],0,'f',1));
    appendLog(QString("Computing tilts for %1 vertebrae...").arg(selectedLabels.size()));

    for (int lbl : selectedLabels) {
        VertTilt vt = getEndplateTilt(maskData, lbl, spacing, origin, hDim, vDim, apDim, 50.0);
        if (vt.valid) {
            tilts_.push_back(vt);
            appendLog(QString("  %1: sup=%.1f\u00b0 inf=%.1f\u00b0 %2")
                .arg(vt.name).arg(vt.supTilt).arg(vt.infTilt).arg(vt.flag));
        } else {
            appendLog(QString("  %1: SKIPPED (insufficient data)").arg(vt.name));
        }
    }
}

// ============================================================================
// Cobb angle from two slopes
// ============================================================================

double CobbAngleAddon::cobbFromSlopes(double s1, double s2)
{
    double denom = std::abs(1.0 + s1 * s2);
    if (denom < 1e-10) return 90.0;
    return std::atan(std::abs(s1 - s2) / denom) * 180.0 / M_PI;
}

// ============================================================================
// Find scoliotic curves via inflection point detection
// ============================================================================

void CobbAngleAddon::findCurves(double minCobb)
{
    curves_.clear();
    int n = static_cast<int>(tilts_.size());
    if (n < 2) return;

    // ---- Two-segment approach ----
    // 1. Smooth inferior tilts with 3-point moving average
    // 2. Find the single main inflection point (tilt direction reversal)
    //    → this is the boundary between upper curve and lower curve
    // 3. In each segment find the vertebra pair with max Cobb angle

    std::vector<double> rawTilt(n);
    for (int i = 0; i < n; ++i) rawTilt[i] = tilts_[i].infTilt;

    std::vector<double> smooth(n);
    for (int i = 0; i < n; ++i) {
        int lo = std::max(0, i - 1);
        int hi = std::min(n - 1, i + 1);
        double sum = 0; int cnt = 0;
        for (int k = lo; k <= hi; ++k) { sum += rawTilt[k]; ++cnt; }
        smooth[i] = sum / cnt;
    }

    // Find inflection points (sign change in successive differences)
    std::vector<double> diffs(n - 1);
    for (int i = 0; i < n - 1; ++i) diffs[i] = smooth[i + 1] - smooth[i];

    // Decide whether to split into two segments:
    // - Always split if spanning thoracic + lumbar
    // - Also split if "Double Thoracic" is checked (find inflection within thoracic)
    bool hasThoracic = false, hasLumbar = false;
    for (int i = 0; i < n; ++i) {
        if (tilts_[i].region == "thoracic") hasThoracic = true;
        if (tilts_[i].region == "lumbar")   hasLumbar = true;
    }
    bool doubleThoracic = (doubleThoracicCb_ && doubleThoracicCb_->isChecked());
    bool shouldSplit = (hasThoracic && hasLumbar) || doubleThoracic;

    int bestInflection = -1;
    if (shouldSplit) {
        // Find the inflection point with largest tilt direction change
        double bestDiffMag = 0;
        for (int i = 0; i + 1 < static_cast<int>(diffs.size()); ++i) {
            if (diffs[i] * diffs[i + 1] < 0) {
                double mag = std::abs(diffs[i]) + std::abs(diffs[i + 1]);
                if (mag > bestDiffMag) {
                    bestDiffMag = mag;
                    bestInflection = i + 1;
                }
            }
        }
    }

    // Build segments
    struct Segment { int start, end; };
    std::vector<Segment> segments;

    if (bestInflection > 0 && bestInflection < n - 1) {
        segments.push_back({0, bestInflection});
        segments.push_back({bestInflection, n - 1});
        appendLog(QString("Inflection at %1 -> upper curve [%2-%3], lower curve [%4-%5]")
            .arg(tilts_[bestInflection].name)
            .arg(tilts_[0].name).arg(tilts_[bestInflection].name)
            .arg(tilts_[bestInflection].name).arg(tilts_[n-1].name));
    } else {
        segments.push_back({0, n - 1});
        appendLog("Single curve (all selected vertebrae in same region).");
    }

    // For each segment: Cobb = first vertebra's superior endplate vs last vertebra's inferior endplate
    // The inflection vertebra is SHARED: its inf endplate ends the upper curve,
    // its sup endplate starts the lower curve.
    for (int si = 0; si < static_cast<int>(segments.size()); ++si) {
        const auto& seg = segments[si];
        if (seg.start >= seg.end) continue;

        int upperIdx = seg.start;
        int lowerIdx = seg.end;
        double cobb = cobbFromSlopes(tilts_[upperIdx].supSlope, tilts_[lowerIdx].infSlope);

        CobbCurve cc;
        cc.upperName = tilts_[upperIdx].name;
        cc.lowerName = tilts_[lowerIdx].name;
        cc.upperIdx = upperIdx;
        cc.lowerIdx = lowerIdx;
        cc.cobb = cobb;
        cc.nVert = lowerIdx - upperIdx + 1;

        // Region name
        QString r1 = tilts_[upperIdx].region, r2 = tilts_[lowerIdx].region;
        if (r1 == r2) cc.region = r1;
        else cc.region = r1 + "-" + r2;

        // Convexity
        double meanTilt = 0;
        for (int k = upperIdx; k <= lowerIdx; ++k) meanTilt += tilts_[k].infTilt;
        meanTilt /= (lowerIdx - upperIdx + 1);
        cc.convexity = (meanTilt > 0) ? "right" : "left";

        curves_.push_back(cc);

        appendLog(QString("  Curve: %1 - %2, Cobb = %3 deg, %4, %5")
            .arg(cc.upperName).arg(cc.lowerName)
            .arg(cc.cobb, 0, 'f', 1).arg(cc.region).arg(cc.convexity));
    }

    // Sort curves by position (upper first)
    std::sort(curves_.begin(), curves_.end(),
        [](const CobbCurve& a, const CobbCurve& b) { return a.upperIdx < b.upperIdx; });
}

// ============================================================================
// Main calculate entry point
// ============================================================================

// ============================================================================
// Full-spine Cobb measurement (hybrid: scipy/skimage in Python, VTK in C++)
// ----------------------------------------------------------------------------
// Pipeline:
//   1. Auto-detect every non-zero label in the current mask.
//   2. Spawn python/cobb/run_cobb.py with --labels=<all> --plane=coronal
//      \u2192 it writes meta.json, segments_coronal.json, full_<L>.vtp,
//        top_<L>.vtp, bottom_<L>.vtp into a temp dir.
//   3. Parse JSONs, populate curves_ + tilts_, list segments in result panel.
// ============================================================================
void CobbAngleAddon::calculateCobbAngle()
{
    if (!app_ || !app_->sharedViewport()) {
        QMessageBox::warning(this, "Cobb Angle", "No data loaded.");
        return;
    }
    auto* viewport = app_->sharedViewport();
    if (!viewport->maskData_) {
        QMessageBox::warning(this, "Cobb Angle",
            "No segmentation mask available.\nPlease run CT Segmentation first.");
        return;
    }

    // Auto-detect ALL non-zero labels in the mask (full-spine measurement).
    auto* arr = viewport->maskData_->GetPointData()->GetScalars();
    if (!arr) {
        QMessageBox::warning(this, "Cobb Angle", "Mask volume has no scalar data.");
        return;
    }
    std::set<int> labelSet;
    vtkIdType n = arr->GetNumberOfTuples();
    for (vtkIdType i = 0; i < n; ++i) {
        int v = static_cast<int>(arr->GetTuple1(i));
        if (v > 0) labelSet.insert(v);
    }
    if (labelSet.size() < 2) {
        QMessageBox::warning(this, "Cobb Angle",
            "Need at least 2 labeled vertebrae for measurement.");
        return;
    }
    QStringList labelStrs;
    for (int lbl : labelSet) labelStrs << QString::number(lbl);
    QString labelsCSV = labelStrs.join(",");

    logText_->clear();
    appendLog("=== Full-Spine Cobb Analysis ===");
    appendLog(QString("Labels (%1): %2").arg(labelStrs.size()).arg(labelsCSV));

    // Get mask on disk: prefer the saved project path; otherwise dump the
    // in-memory mask (e.g. just-finished auto-segmentation) to a temp file.
    QString maskPath = app_->currentProject.value("mask_file_path").toString();
    if (maskPath.isEmpty() || !QFile::exists(maskPath)) {
        QString tmpRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QDir().mkpath(tmpRoot);
        maskPath = tmpRoot + "/spinex_cobb_mask.nii.gz";
        if (!NiftiIO::writeNifti(viewport->maskData_.Get(), maskPath.toStdString())) {
            QMessageBox::warning(this, "Cobb Angle",
                "Failed to write in-memory mask to a temporary NIfTI file.");
            return;
        }
        appendLog(QString("In-memory mask dumped to: %1").arg(maskPath));
    } else {
        appendLog(QString("Mask: %1").arg(maskPath));
    }

    // Temp output dir.
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                     + "/spinex_cobb_module";
    QDir tmp(tmpDir);
    if (tmp.exists()) tmp.removeRecursively();
    QDir().mkpath(tmpDir);

    // Resolve python exe + script.
    QString pythonExe = QString::fromStdString(PythonRuntime::instance().pythonExecutable());
    QString scriptPath = QCoreApplication::applicationDirPath() + "/python/cobb/run_cobb.py";
    if (!QFile::exists(scriptPath)) {
        QString srcDir = QString::fromStdString(PythonRuntime::projectSourceDir());
        if (!srcDir.isEmpty())
            scriptPath = srcDir + "/python/cobb/run_cobb.py";
    }

    auto* progress = new QProgressDialog(
        "Running Cobb pipeline (~30-60s)...", QString(), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();
    QApplication::processEvents();

    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::readyReadStandardOutput, [this, process]() {
        QString chunk = QString::fromUtf8(process->readAll());
        for (const QString& line : chunk.split('\n')) {
            QString t = line.trimmed();
            if (!t.isEmpty()) appendLog(t);
        }
    });

    QString plane = "coronal";
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, process, progress, tmpDir, plane](int exitCode, QProcess::ExitStatus) {
            progress->close();
            progress->deleteLater();

            curves_.clear();
            for (auto* w : curveLabels_) delete w;
            curveLabels_.clear();

            if (exitCode != 0) {
                appendLog(QString("Python failed (exit %1).").arg(exitCode));
                QMessageBox::warning(this, "Cobb Angle",
                    "Calculation failed. Check the log.");
                process->deleteLater();
                return;
            }

            // Load JSONs.
            QString metaPath = tmpDir + "/meta.json";
            QString segPath  = tmpDir + QString("/segments_%1.json").arg(plane);
            if (!QFile::exists(metaPath) || !QFile::exists(segPath)) {
                appendLog("Missing meta/segments json \u2014 pipeline produced no output.");
                process->deleteLater();
                return;
            }
            std::ifstream mf(metaPath.toStdString());
            std::ifstream sf(segPath.toStdString());
            nlohmann::json meta, segs;
            try {
                mf >> meta; sf >> segs;
            } catch (const std::exception& e) {
                appendLog(QString("JSON parse error: %1").arg(e.what()));
                process->deleteLater();
                return;
            }

            // Populate tilts_ from meta (sorted by label, with display name).
            tilts_.clear();
            for (const auto& v : meta["vertebrae"]) {
                VertTilt vt;
                vt.label = v["label"].get<int>();
                vt.name  = QString::fromStdString(v["name"].get<std::string>());
                vt.region = getRegion(vt.label);
                vt.centroidX = v["centroid"][0].get<double>();
                vt.centroidY = v["centroid"][1].get<double>();
                vt.centroidZ = v["centroid"][2].get<double>();
                vt.supTilt = v["top_coronal_tilt_deg"].get<double>();
                vt.infTilt = v["bottom_coronal_tilt_deg"].get<double>();
                vt.valid = true;
                tilts_.push_back(vt);
            }
            std::sort(tilts_.begin(), tilts_.end(),
                [](const VertTilt& a, const VertTilt& b) { return a.label < b.label; });

            // Populate curves_ from segments.
            for (const auto& s : segs["segments"]) {
                CobbCurve c;
                c.upperName = QString::fromStdString(s["start_name"].get<std::string>());
                c.lowerName = QString::fromStdString(s["end_name"].get<std::string>());
                c.cobb = s["cobb_deg"].get<double>();
                c.region = QString::fromStdString(s.value("curve_type", std::string("")));
                double signed_ = s["signed_angle_deg"].get<double>();
                c.convexity = (signed_ >= 0) ? "right" : "left";
                c.nVert = s.value("vertebra_count", 0);
                c.apexLabel = s.value("apex_label", 0);
                c.apexName  = QString::fromStdString(s.value("apex_name", std::string("")));
                for (int i = 0; i < (int)tilts_.size(); ++i) {
                    if (tilts_[i].name == c.upperName) c.upperIdx = i;
                    if (tilts_[i].name == c.lowerName) c.lowerIdx = i;
                }
                curves_.push_back(c);
            }

            // Hide the placeholder header label, render dynamic curve list.
            if (cobbAngleLbl_) cobbAngleLbl_->setVisible(false);

            if (curves_.empty()) {
                auto* none = new QLabel(QString::fromUtf8("No clinical curve (\u2265 10\u00b0) detected."));
                none->setStyleSheet("color: #6b7280; font-size: 10pt; border: none;");
                none->setAlignment(Qt::AlignCenter);
                curvesResultLayout_->addWidget(none);
                curveLabels_.push_back(none);
                appendLog("No clinical-grade Cobb segment found.");
            } else {
                // Same SEGMENT_COLORS palette as the cobb_test renderer.
                QColor palette[] = {
                    QColor(0xD7, 0x26, 0x3D), QColor(0x1B, 0x99, 0x8B),
                    QColor(0xF4, 0x60, 0x36), QColor(0x2E, 0x29, 0x4E),
                    QColor(0x8E, 0x5C, 0xF7), QColor(0x3A, 0x86, 0xFF),
                    QColor(0xFF, 0xBE, 0x0B), QColor(0x00, 0xB4, 0xD8),
                };
                for (int ci = 0; ci < (int)curves_.size(); ++ci) {
                    const auto& c = curves_[ci];
                    QColor clr = palette[ci % 8];

                    // Header row: "Curve N    <UEV> \u2013 <LEV>"
                    auto* hdr = new QLabel(
                        QString::fromUtf8("Curve %1    %2 \u2013 %3")
                            .arg(ci + 1).arg(c.upperName).arg(c.lowerName));
                    hdr->setFont(QFont("Segoe UI", 11, QFont::Bold));
                    hdr->setStyleSheet(QString("color: %1; border: none;").arg(clr.name()));
                    hdr->setAlignment(Qt::AlignCenter);
                    curvesResultLayout_->addWidget(hdr);
                    curveLabels_.push_back(hdr);

                    // Cobb angle (large)
                    auto* angle = new QLabel(QString::fromUtf8("Angle:  %1\u00b0").arg(c.cobb, 0, 'f', 1));
                    angle->setFont(QFont("Segoe UI", 13, QFont::Bold));
                    angle->setStyleSheet(QString("color: %1; border: none;").arg(clr.darker(115).name()));
                    angle->setAlignment(Qt::AlignCenter);
                    curvesResultLayout_->addWidget(angle);
                    curveLabels_.push_back(angle);

                    // Apex
                    auto* apex = new QLabel(QString("APEX:  %1")
                        .arg(c.apexName.isEmpty() ? QString::fromUtf8("\u2014") : c.apexName));
                    apex->setStyleSheet("color: #1a2332; font-size: 10pt; border: none;");
                    apex->setAlignment(Qt::AlignCenter);
                    curvesResultLayout_->addWidget(apex);
                    curveLabels_.push_back(apex);

                    // Type / convexity (compact)
                    auto* sub = new QLabel(QString("%1  |  convex %2")
                        .arg(c.region.isEmpty() ? QString::fromUtf8("\u2014") : c.region)
                        .arg(c.convexity));
                    sub->setStyleSheet("color: #6b7280; font-size: 9pt; border: none;");
                    sub->setAlignment(Qt::AlignCenter);
                    curvesResultLayout_->addWidget(sub);
                    curveLabels_.push_back(sub);

                    if (ci < (int)curves_.size() - 1) {
                        auto* sep = new QFrame();
                        sep->setFrameShape(QFrame::HLine);
                        sep->setStyleSheet("border: none; max-height: 1px; background: #dde1e7;");
                        curvesResultLayout_->addWidget(sep);
                        curveLabels_.push_back(sep);
                    }
                    appendLog(QString::fromUtf8("  Curve %1: %2-%3  Cobb=%4\u00b0  apex=%5  type=%6")
                        .arg(ci + 1).arg(c.upperName).arg(c.lowerName)
                        .arg(c.cobb, 0, 'f', 1)
                        .arg(c.apexName.isEmpty() ? QString::fromUtf8("\u2014") : c.apexName)
                        .arg(c.region));
                }
            }

            // ---- Step 4 sagittal parameters & landmarks (optional) ----
            sagittalParams_.clear();
            landmarks_.clear();
            for (auto* w : sagittalLabels_) delete w;
            sagittalLabels_.clear();

            QString sagPath = tmpDir + "/sagittal_params.json";
            QString lmkPath = tmpDir + "/sagittal_landmarks.json";
            if (QFile::exists(sagPath)) {
                std::ifstream sf(sagPath.toStdString());
                nlohmann::json sagJson;
                try { sf >> sagJson; } catch (...) {}
                for (const auto& p : sagJson.value("parameters", nlohmann::json::array())) {
                    SagittalParam sp;
                    sp.name = QString::fromStdString(p.value("parameter", std::string()));
                    sp.description = QString::fromStdString(p.value("description", std::string()));
                    sp.available = p.value("available", false);
                    sp.unit = QString::fromStdString(p.value("unit", std::string("deg")));
                    if (sp.available && !p["value"].is_null())
                        sp.value = p["value"].get<double>();
                    sp.clinicalStatus = QString::fromStdString(p.value("clinical_status", std::string()));
                    sp.normalRange    = QString::fromStdString(p.value("normal_range", std::string()));
                    sp.missingLabels  = QString::fromStdString(p.value("missing_labels", std::string()));
                    sp.startReference = QString::fromStdString(p.value("start_reference", std::string()));
                    sp.endReference   = QString::fromStdString(p.value("end_reference", std::string()));
                    sagittalParams_.push_back(sp);
                }
            }
            if (QFile::exists(lmkPath)) {
                std::ifstream lf(lmkPath.toStdString());
                nlohmann::json lmkJson;
                try { lf >> lmkJson; } catch (...) {}
                for (const auto& l : lmkJson.value("landmarks", nlohmann::json::array())) {
                    Landmark lk;
                    lk.name  = QString::fromStdString(l.value("landmark", std::string()));
                    lk.label = l.value("label", 0);
                    if (l.contains("position") && l["position"].is_array() && l["position"].size() >= 3) {
                        lk.position[0] = l["position"][0].get<double>();
                        lk.position[1] = l["position"][1].get<double>();
                        lk.position[2] = l["position"][2].get<double>();
                    }
                    landmarks_.push_back(lk);
                }
            }

            // Choose a clinical diagnostic line per parameter, conditional on
            // the actual clinical_status returned by Step 4.  Red diagnosis text
            // appears only when the value falls outside the normal band.
            auto sagDiagnosis = [](const QString& param, const QString& status) -> QString {
                if (param == "TK") {
                    if (status == "abnormal_high")
                        return QString::fromUtf8("TK 增大,提示胸椎后凸加重。");
                    if (status == "abnormal_low")
                        return QString::fromUtf8("TK 减小,提示胸椎后凸不足。");
                    if (status == "normal")
                        return QString::fromUtf8("胸椎后凸正常。");
                } else if (param == "CL") {
                    if (status == "abnormal_high")
                        return QString::fromUtf8("CL 增大,提示颈椎前凸过度。");
                    if (status == "abnormal_low")
                        return QString::fromUtf8("CL 减小或反向,提示颈椎后凸。");
                    if (status == "normal")
                        return QString::fromUtf8("颈椎前凸正常。");
                } else if (param == "cSVA") {
                    if (status == "abnormal_high")
                        return QString::fromUtf8("cSVA 增大,提示颈椎矢状面失衡。");
                    if (status == "normal")
                        return QString::fromUtf8("颈椎矢状面平衡。");
                } else if (param == "LL_L1_L5") {
                    if (status == "abnormal_high")
                        return QString::fromUtf8("LL 增大,腰椎前凸过度。");
                    if (status == "abnormal_low")
                        return QString::fromUtf8("LL 减小,腰椎前凸不足,可能矢状面失衡。");
                    if (status == "normal")
                        return QString::fromUtf8("腰椎前凸正常。");
                }
                return QString();
            };

            // Render the 4 sagittal-parameter rows in the result panel.
            for (const auto& sp : sagittalParams_) {
                auto* row = new QLabel();
                QString colorHex;
                if (!sp.available) { colorHex = "#9ca3af"; }
                else if (sp.clinicalStatus == "normal") { colorHex = "#16a34a"; }   // green
                else if (sp.clinicalStatus == "abnormal_low" ||
                         sp.clinicalStatus == "abnormal_high") { colorHex = "#dc2626"; }  // red
                else { colorHex = "#1a2332"; }
                QString text;
                if (sp.available) {
                    QString statusPretty = sp.clinicalStatus;
                    statusPretty.replace("_", " ");
                    text = QString::fromUtf8("<b>%1</b>  %2 %3   <span style='color:#6b7280;'>(%4, %5)</span>")
                        .arg(sp.name)
                        .arg(sp.value, 0, 'f', 1)
                        .arg(sp.unit == "deg" ? QString::fromUtf8("°") : sp.unit)
                        .arg(statusPretty)
                        .arg(sp.normalRange);
                } else {
                    text = QString("<b>%1</b>  unavailable  <span style='color:#9ca3af;'>(missing %2)</span>")
                        .arg(sp.name).arg(sp.missingLabels);
                }
                row->setTextFormat(Qt::RichText);
                row->setText(text);
                row->setStyleSheet(QString("color: %1; font-size: 10pt; border: none;"
                                           " padding: 2px 0;").arg(colorHex));
                sagittalResultLayout_->addWidget(row);
                sagittalLabels_.push_back(row);

                // Diagnostic sub-row picked from the actual clinical status.
                // Red for abnormal, green for normal, hidden when missing.
                if (sp.available) {
                    QString diag = sagDiagnosis(sp.name, sp.clinicalStatus);
                    if (!diag.isEmpty()) {
                        bool isDangerous =
                            (sp.clinicalStatus == "abnormal_low" ||
                             sp.clinicalStatus == "abnormal_high");
                        QString descColor = isDangerous ? "#dc2626" : "#16a34a";
                        auto* desc = new QLabel(diag);
                        desc->setWordWrap(true);
                        desc->setStyleSheet(QString(
                            "color: %1; font-size: 9pt; font-weight: %2;"
                            " border: none; padding: 0 4px 6px 16px;")
                            .arg(descColor)
                            .arg(isDangerous ? "bold" : "normal"));
                        sagittalResultLayout_->addWidget(desc);
                        sagittalLabels_.push_back(desc);
                    }
                }

                appendLog(QString("  %1: %2 %3 (%4)").arg(sp.name)
                    .arg(sp.available ? QString::number(sp.value, 'f', 1) : QString("--"))
                    .arg(sp.unit).arg(sp.clinicalStatus));
            }

            appendLog("=== Done ===");
            // Replace SpineX in-memory vertebra actors with Python full_*.vtp
            // so vertebrae + endplates + cSVA all share one world space.
            replaceVertebraActorsFromVtp();
            // Build sagittal aux actors ONCE — they live in the renderer
            // hidden, switchViewMode just toggles their visibility on click.
            buildSagittalAux();
            switchViewMode(viewSagittal_);
            process->deleteLater();
        });

    if (QFile::exists(scriptPath)) {
        process->start(pythonExe, {
            scriptPath,
            "--seg", maskPath,
            "--labels", labelsCSV,
            "--plane", plane,
            "--with-sagittal",
            "--out", tmpDir,
        });
    } else {
        // .py is stripped from the installer — load run_cobb.pyd via inline import.
        QString pythonDir = QCoreApplication::applicationDirPath() + "/python/cobb";
        QString inlineCode = QString(
            "import sys; sys.path.insert(0, r'%1'); "
            "sys.argv = ['run_cobb','--seg',r'%2','--labels','%3','--plane','%4','--with-sagittal','--out',r'%5']; "
            "import run_cobb; run_cobb.main()"
        ).arg(pythonDir).arg(maskPath).arg(labelsCSV).arg(plane).arg(tmpDir);
        process->start(pythonExe, {"-c", inlineCode});
    }

    if (!process->waitForStarted(5000)) {
        progress->close();
        progress->deleteLater();
        QMessageBox::critical(this, "Cobb Angle", "Failed to start Python process.");
        process->deleteLater();
    }
}

// ============================================================================
// Recolor 3D vertebrae + draw endplate lines + Cobb labels in 3D
// ============================================================================

// renderCoronalPlot removed — now using Python-generated PNG via calculateCobbAngle

// ============================================================================
// Recolor 3D vertebrae by curve membership
// ============================================================================

void CobbAngleAddon::recolor3DVertebrae()
{
    if (!app_ || !app_->sharedViewport()) return;
    if (tilts_.empty()) return;

    auto* viewport = app_->sharedViewport();
    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Simplified palette (per user request):
    //   SEGMENT_RGB  — one color per curve, 4 cycle through
    //   JUNCTION_RGB — RED, the vertebra shared between two adjacent curves
    //   No special apex highlight any more (apex inherits its segment color).
    static const double SEGMENT_RGB[4][3] = {
        {0x1B/255.0, 0x99/255.0, 0x8B/255.0},   // teal
        {0x3A/255.0, 0x86/255.0, 0xFF/255.0},   // blue
        {0xFF/255.0, 0xBE/255.0, 0x0B/255.0},   // amber
        {0x8E/255.0, 0x5C/255.0, 0xF7/255.0},   // purple
    };
    constexpr double JUNCTION_RGB[3] = {0xD7/255.0, 0x26/255.0, 0x3D/255.0};  // red

    // labelToCurves: label -> ordered list of curve indices it belongs to.
    // sharedLabels:  labels at the boundary between two consecutive curves.
    QMap<int, QVector<int>> labelToCurves;
    QSet<int> sharedLabels;
    for (int ci = 0; ci < static_cast<int>(curves_.size()); ++ci) {
        const auto& c = curves_[ci];
        if (c.upperIdx < 0 || c.lowerIdx < 0) continue;
        for (int ti = c.upperIdx; ti <= c.lowerIdx; ++ti)
            labelToCurves[tilts_[ti].label].push_back(ci);
    }
    for (int ci = 0; ci + 1 < static_cast<int>(curves_.size()); ++ci) {
        if (curves_[ci].lowerName == curves_[ci + 1].upperName) {
            for (const auto& vt : tilts_) {
                if (vt.name == curves_[ci].lowerName) { sharedLabels.insert(vt.label); break; }
            }
        }
    }

    for (auto it = vertebraActors_.begin(); it != vertebraActors_.end(); ++it) {
        int lbl = it.key();
        auto actor = it.value();
        if (!actor) continue;

        if (sharedLabels.contains(lbl)) {
            // Junction = red.
            actor->GetProperty()->SetColor(JUNCTION_RGB[0], JUNCTION_RGB[1], JUNCTION_RGB[2]);
            actor->GetProperty()->SetOpacity(0.85);
        } else if (labelToCurves.contains(lbl)) {
            // Single curve -> its segment color (apex no longer special).
            int ci = labelToCurves[lbl].at(0) % 4;
            actor->GetProperty()->SetColor(SEGMENT_RGB[ci][0],
                                           SEGMENT_RGB[ci][1],
                                           SEGMENT_RGB[ci][2]);
            actor->GetProperty()->SetOpacity(0.65);
        } else {
            // Cervical or any vertebra not part of any curve.
            actor->GetProperty()->SetColor(0.73, 0.73, 0.73);
            actor->GetProperty()->SetOpacity(0.25);
        }
    }

    if (viewport->get3DRenderWindow())
        viewport->get3DRenderWindow()->Render();
}

// ============================================================================
// View mode switching: coronal (Cobb segments) vs sagittal (TK/CL/LL/cSVA)
// ============================================================================

// Apply the cached coronal color scheme to vertebra actors (no redraw of new
// geometry). Pulled out of recolor3DVertebrae so switchViewMode can call it
// cheaply each toggle without re-running it from scratch.
void CobbAngleAddon::render3DCoronal()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Show vertebrae, hide sagittal aux actors.
    for (auto it = vertebraActors_.begin(); it != vertebraActors_.end(); ++it)
        if (it.value()) it.value()->SetVisibility(1);
    for (auto& a : sagittalAuxActors_) if (a) a->SetVisibility(0);

    recolor3DVertebrae();  // applies per-vertebra coronal colors (cached state)

    if (!tilts_.empty() && renderer->GetActiveCamera()) {
        double cx = 0, cy = 0, cz = 0;
        for (const auto& t : tilts_) { cx += t.centroidX; cy += t.centroidY; cz += t.centroidZ; }
        double n = (double)tilts_.size();
        cx /= n; cy /= n; cz /= n;
        auto* cam = renderer->GetActiveCamera();
        cam->SetFocalPoint(cx, cy, cz);
        cam->SetPosition(cx, cy - 800, cz);
        cam->SetViewUp(0, 0, 1);
        renderer->ResetCameraClippingRange();
    }
    if (viewport->get3DRenderWindow()) viewport->get3DRenderWindow()->Render();
}

namespace {
// Sagittal parameter colors mirror PARAMETER_COLORS in step4_sagittal_parameters.py.
struct ParamColor { const char* param; double r, g, b; };
static const ParamColor PARAM_COLORS[] = {
    {"TK",       0xD7/255.0, 0x26/255.0, 0x3D/255.0},
    {"CL",       0x3A/255.0, 0x86/255.0, 0xFF/255.0},
    {"LL_L1_L5", 0x1B/255.0, 0x99/255.0, 0x8B/255.0},
    {"cSVA",     0xFF/255.0, 0xBE/255.0, 0x0B/255.0},
};
static void rgbForParam(const QString& name, double rgb[3]) {
    for (const auto& pc : PARAM_COLORS) {
        if (name == pc.param) { rgb[0] = pc.r; rgb[1] = pc.g; rgb[2] = pc.b; return; }
    }
    rgb[0] = 0.5; rgb[1] = 0.5; rgb[2] = 0.5;
}
// Sagittal "endplate spec": parameter -> (label, which) endpoint pair.
struct EndpointSpec { const char* param; int label; const char* which; };
static const EndpointSpec ENDPOINT_SPECS[] = {
    {"TK",       11, "top"   },  // T4
    {"TK",       19, "bottom"},  // T12
    {"CL",        2, "bottom"},  // C2
    {"CL",        7, "bottom"},  // C7
    {"LL_L1_L5", 20, "top"   },  // L1
    {"LL_L1_L5", 24, "bottom"},  // L5
};
}

// Build the sagittal-only auxiliary actors (endplate highlights + cSVA lines)
// ONCE after Calculate finishes. They live in the renderer permanently, with
// SetVisibility flipped on/off by switchViewMode. Subsequent toggles do not
// reload .vtp files.
void CobbAngleAddon::render3DSagittal()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Tint each endpoint vertebra by its clinical parameter colour.
    // C2/C7 share between CL and cSVA — colour them as CL (blue) since the
    // cSVA viz is already drawn as a separate yellow line.
    QMap<int, std::array<double, 3>> tint;
    auto tintFromParam = [&](const char* p, int label) {
        double rgb[3]; rgbForParam(p, rgb);
        tint.insert(label, {rgb[0], rgb[1], rgb[2]});
    };
    auto paramAvailable = [&](const QString& name) {
        return std::any_of(sagittalParams_.begin(), sagittalParams_.end(),
            [&](const SagittalParam& s) { return s.name == name && s.available; });
    };
    if (paramAvailable("CL"))       { tintFromParam("CL", 2); tintFromParam("CL", 7); }
    if (paramAvailable("TK"))       { tintFromParam("TK", 11); tintFromParam("TK", 19); }
    if (paramAvailable("LL_L1_L5")) { tintFromParam("LL_L1_L5", 20); tintFromParam("LL_L1_L5", 24); }

    for (auto it = vertebraActors_.begin(); it != vertebraActors_.end(); ++it) {
        auto actor = it.value();
        if (!actor) continue;
        actor->SetVisibility(1);
        if (tint.contains(it.key())) {
            const auto& c = tint[it.key()];
            actor->GetProperty()->SetColor(c[0], c[1], c[2]);
            actor->GetProperty()->SetOpacity(0.85);
        } else {
            actor->GetProperty()->SetColor(0.85, 0.85, 0.85);
            actor->GetProperty()->SetOpacity(0.10);
        }
    }
    // cSVA aux actors (yellow plumb + horizontal lines).
    for (auto& a : sagittalAuxActors_) if (a) a->SetVisibility(1);

    // Camera: lateral side view (looking from +X with +Z up).
    if (!tilts_.empty() && renderer->GetActiveCamera()) {
        double cx = 0, cy = 0, cz = 0;
        for (const auto& t : tilts_) { cx += t.centroidX; cy += t.centroidY; cz += t.centroidZ; }
        double n = (double)tilts_.size();
        cx /= n; cy /= n; cz /= n;
        auto* cam = renderer->GetActiveCamera();
        cam->SetFocalPoint(cx, cy, cz);
        cam->SetPosition(cx + 800, cy, cz);
        cam->SetViewUp(0, 0, 1);
        renderer->ResetCameraClippingRange();
    }
    if (viewport->get3DRenderWindow())
        viewport->get3DRenderWindow()->Render();
}

void CobbAngleAddon::replaceVertebraActorsFromVtp()
{
    // Use the per-vertebra full-body meshes Python wrote to /spinex_cobb_module/.
    // This puts vertebra geometry in the same world space as the sagittal aux
    // actors (endplate VTPs + cSVA lines) so they overlap correctly.
    if (!app_ || !app_->sharedViewport()) return;
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!renderer) return;

    // Drop the old SpineX-generated vertebra actors first.
    for (auto it = vertebraActors_.begin(); it != vertebraActors_.end(); ++it)
        if (it.value()) renderer->RemoveActor(it.value());
    vertebraActors_.clear();

    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                     + "/spinex_cobb_module";
    for (const auto& vt : tilts_) {
        QString fullPath = QString("%1/full_%2.vtp").arg(tmpDir).arg(vt.label);
        if (!QFile::exists(fullPath)) continue;
        auto rd = vtkSmartPointer<vtkXMLPolyDataReader>::New();
        rd->SetFileName(fullPath.toStdString().c_str());
        rd->Update();
        auto poly = rd->GetOutput();
        if (!poly || poly->GetNumberOfPoints() == 0) continue;

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(0.7, 0.7, 0.7);   // overridden by switchViewMode
        actor->GetProperty()->SetOpacity(0.85);
        actor->GetProperty()->SetInterpolationToPhong();
        renderer->AddActor(actor);
        vertebraActors_.insert(vt.label, actor);
    }
}

void CobbAngleAddon::buildSagittalAux()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!renderer) return;

    // Drop any previous sagittal aux actors (Calculate may be re-run on a new case).
    for (auto& a : sagittalAuxActors_) renderer->RemoveActor(a);
    sagittalAuxActors_.clear();

    // No endplate slabs anymore — the whole vertebra is tinted in render3DSagittal.
    // Only cSVA lines are auxiliary geometry that don't fit on a vertebra.

    // cSVA: yellow plumb line from C2 body center down to C7-Z, then horizontal
    // line over to C7 posterosuperior corner.  Two thin lines, no spheres.
    std::array<double, 3> c2{}; bool hasC2 = false;
    std::array<double, 3> c7{}; bool hasC7 = false;
    for (const auto& lk : landmarks_) {
        if (lk.name == "C2_body_center") {
            c2[0] = lk.position[0]; c2[1] = lk.position[1]; c2[2] = lk.position[2]; hasC2 = true;
        } else if (lk.name == "C7_posterosuperior_body_corner") {
            c7[0] = lk.position[0]; c7[1] = lk.position[1]; c7[2] = lk.position[2]; hasC7 = true;
        }
    }
    if (hasC2 && hasC7) {
        double rgb[3]; rgbForParam("cSVA", rgb);
        auto addLine = [&](double a[3], double b[3], int width, bool dashed) {
            auto src = vtkSmartPointer<vtkLineSource>::New();
            src->SetPoint1(a[0], a[1], a[2]);
            src->SetPoint2(b[0], b[1], b[2]);
            src->Update();
            auto m = vtkSmartPointer<vtkPolyDataMapper>::New();
            m->SetInputConnection(src->GetOutputPort());
            auto act = vtkSmartPointer<vtkActor>::New();
            act->SetMapper(m);
            act->GetProperty()->SetColor(rgb[0], rgb[1], rgb[2]);
            act->GetProperty()->SetLineWidth(width);
            if (dashed) {
                act->GetProperty()->SetLineStipplePattern(0xF0F0);
                act->GetProperty()->SetLineStippleRepeatFactor(2);
            }
            act->SetVisibility(0);
            renderer->AddActor(act);
            sagittalAuxActors_.push_back(act);
        };
        double a[3] = {c2[0], c2[1], c2[2]};
        double b[3] = {c2[0], c2[1], c7[2]};      // plumb endpoint at C7 height
        double cend[3] = {c2[0], c7[1], c7[2]};   // C7 corner projected onto C2-X
        addLine(a, b, 3, true);   // plumb (dashed)
        addLine(b, cend, 5, false); // horizontal (solid, thicker)
    }
}

void CobbAngleAddon::switchViewMode(bool sagittal)
{
    viewSagittal_ = sagittal;
    if (viewCoronalBtn_)  viewCoronalBtn_->setChecked(!sagittal);
    if (viewSagittalBtn_) viewSagittalBtn_->setChecked(sagittal);
    if (sagittal) render3DSagittal();
    else          render3DCoronal();
}

// ============================================================================
// Clear
// ============================================================================

void CobbAngleAddon::clearMeasurement()
{
    if (cobbAngleLbl_) {
        cobbAngleLbl_->setText(QString::fromUtf8("No curves yet \u2014 click Calculate."));
        cobbAngleLbl_->setVisible(true);
    }
    for (auto* lbl : curveLabels_) delete lbl;
    curveLabels_.clear();
    for (auto* lbl : sagittalLabels_) delete lbl;
    sagittalLabels_.clear();

    tilts_.clear();
    curves_.clear();
    sagittalParams_.clear();
    landmarks_.clear();
    if (app_ && app_->sharedViewport()) {
        if (auto* r = app_->sharedViewport()->get3DRenderer()) {
            for (auto& a : sagittalAuxActors_) r->RemoveActor(a);
        }
    }
    sagittalAuxActors_.clear();

    // Clear 3D: remove everything and re-render the full vertebra set.
    if (app_ && app_->sharedViewport()) {
        auto* renderer = app_->sharedViewport()->get3DRenderer();
        if (renderer) renderer->RemoveAllViewProps();
        vertebraActors_.clear();
        render3DSelected();
    }

    showControlsPage();
    logText_->clear();
}

// ============================================================================
// Refresh from project
// ============================================================================

void CobbAngleAddon::refreshFromProject()
{
    if (!app_) return;

    auto* viewport = app_->sharedViewport();
    if (!viewport) return;

    // Switch to 3D-only mode (hide CT slice panels)
    viewport->setOnly3DMode(true);

    // Load mask and render checked vertebrae
    if (viewport->maskData_) {
        populateVertebraListFromMask(viewport->maskData_);
        // Clear any existing 3D content and render only our selected vertebrae
        auto* renderer = viewport->get3DRenderer();
        if (renderer) renderer->RemoveAllViewProps();
        render3DSelected();
    }
}
