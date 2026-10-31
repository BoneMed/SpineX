/**
 * ============================================================================
 * CTSegmentationModule - CT分割模块（实现）
 * 从 Python modules/ct_segmentation.py 移植
 * ============================================================================
 */

#include "CTSegmentationModule.h"
#include "app/SpineXApp.h"
#include "bridge/PythonRuntime.h"
#include "ui/SharedViewport.h"
#include "io/NiftiIO.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QFont>
#include <QPainter>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QFile>

#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkFloatArray.h>
#include <vtkDecimatePro.h>
#include <vtkPolyDataNormals.h>
#include <vtkClipPolyData.h>
#include <vtkClipClosedSurface.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <vtkImplicitPlaneWidget2.h>
#include <vtkImplicitPlaneRepresentation.h>
#include <vtkCommand.h>
#include <vtkRenderWindow.h>
#include <vtkCamera.h>

#include <iostream>
#include <set>

// Helper: clip polydata with a plane and cap the cut surface (solid look like PyVista)
static vtkSmartPointer<vtkPolyData> clipWithCap(
    vtkPolyData* input, double normal[3], double origin[3])
{
    // Build plane collection for vtkClipClosedSurface
    auto plane = vtkSmartPointer<vtkPlane>::New();
    plane->SetNormal(normal);
    plane->SetOrigin(origin);

    auto planes = vtkSmartPointer<vtkPlaneCollection>::New();
    planes->AddItem(plane);

    auto clipper = vtkSmartPointer<vtkClipClosedSurface>::New();
    clipper->SetInputData(input);
    clipper->SetClippingPlanes(planes);
    clipper->SetActivePlaneId(0);
    clipper->SetScalarModeToNone();
    clipper->GenerateFacesOn();  // Cap the cut
    clipper->Update();

    return clipper->GetOutput();
}

// 26 vertebrae colors matching Python _VERTEBRA_COLORS
const QVector<QColor>& CTSegmentationModule::vertebraColors()
{
    static QVector<QColor> colors = {
        QColor(255,0,0),     QColor(0,128,0),     QColor(0,0,255),       // C1-C3
        QColor(255,255,0),   QColor(0,255,255),   QColor(255,0,255),     // C4-C6
        QColor(255,228,196), QColor(0,0,139),     QColor(205,133,63),    // C7,T1,T2
        QColor(210,180,140), QColor(102,205,170), QColor(0,0,128),       // T3-T5
        QColor(0,139,139),   QColor(143,188,143), QColor(240,128,128),   // T6-T8
        QColor(123,104,238), QColor(221,160,221), QColor(255,69,0),      // T9-T11
        QColor(240,230,140), QColor(255,255,255), QColor(147,112,219),   // T12,L1,L2
        QColor(218,112,214), QColor(75,0,130),    QColor(255,182,193),   // L3-L5
        QColor(32,178,170),  QColor(255,235,205),                        // S1-S2
    };
    return colors;
}

const QStringList& CTSegmentationModule::vertebraNames()
{
    static QStringList names = {
        "C1","C2","C3","C4","C5","C6","C7",
        "T1","T2","T3","T4","T5","T6","T7","T8","T9","T10","T11","T12",
        "L1","L2","L3","L4","L5",
        "S1","S2"
    };
    return names;
}

CTSegmentationModule::CTSegmentationModule(QWidget* parent)
    : ModuleBase("Segmentation", parent)
    , brushMode_("off")
    , brushRadius_(3)
    , activePaintLabel_(1)
{
    initUI();
}

CTSegmentationModule::~CTSegmentationModule() = default;

void CTSegmentationModule::initUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(createLeftPanel());
}

QWidget* CTSegmentationModule::createLeftPanel()
{
    auto* panel = new QWidget();
    auto* outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    outerLayout->addWidget(makeModuleHeader("Segmentation"));

    // Scrollable content
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background-color: #f4f6f8;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(10);

    // Info tip
    auto* tip = new QLabel("CT data is auto-loaded from Data module.");
    tip->setWordWrap(true);
    tip->setStyleSheet(
        "color: #2c5f6f; font-size: 9pt; padding: 8px;"
        "background-color: #e8f4f8; border-left: 3px solid #2c5f6f; border-radius: 3px;");
    layout->addWidget(tip);

    // ── MANUAL EDIT ──
    auto* editGroup = new QGroupBox("MANUAL EDIT");
    auto* editLayout = new QVBoxLayout(editGroup);

    auto* modeLayout = new QHBoxLayout();
    QString btnBaseStyle =
        "QPushButton { min-height: 32px; border-radius: 5px; font-size: 9pt; font-weight: bold; }";
    QString offStyle = btnBaseStyle +
        "QPushButton { background-color: #e9ecef; color: #1a2332; border: 1px solid #dde1e7; }"
        "QPushButton:hover { background-color: #dee2e6; }";
    QString paintActiveStyle = btnBaseStyle +
        "QPushButton { background-color: #16a34a; color: white; border: none; }";
    QString eraseActiveStyle = btnBaseStyle +
        "QPushButton { background-color: #dc2626; color: white; border: none; }";

    btnPaint_ = new QPushButton("Paint");
    btnPaint_->setStyleSheet(offStyle);
    btnPaint_->setEnabled(false);
    connect(btnPaint_, &QPushButton::clicked, [this]() { setBrushMode("paint"); });

    btnErase_ = new QPushButton("Erase");
    btnErase_->setStyleSheet(offStyle);
    btnErase_->setEnabled(false);
    connect(btnErase_, &QPushButton::clicked, [this]() { setBrushMode("erase"); });

    btnBrushOff_ = new QPushButton("Off");
    btnBrushOff_->setStyleSheet(offStyle);
    connect(btnBrushOff_, &QPushButton::clicked, [this]() { setBrushMode("off"); });

    modeLayout->addWidget(btnPaint_);
    modeLayout->addWidget(btnErase_);
    modeLayout->addWidget(btnBrushOff_);
    editLayout->addLayout(modeLayout);

    // Brush size
    auto* brushLayout = new QHBoxLayout();
    brushLayout->addWidget(new QLabel("Brush Size:"));
    brushSizeSlider_ = new QSlider(Qt::Horizontal);
    brushSizeSlider_->setRange(1, 15);
    brushSizeSlider_->setValue(3);
    brushSizeSlider_->setEnabled(false);
    connect(brushSizeSlider_, &QSlider::valueChanged, this, &CTSegmentationModule::onBrushSizeChanged);
    brushLayout->addWidget(brushSizeSlider_);
    brushSizeValLbl_ = new QLabel("3");
    brushSizeValLbl_->setFixedWidth(20);
    brushLayout->addWidget(brushSizeValLbl_);
    editLayout->addLayout(brushLayout);

    layout->addWidget(editGroup);

    // ── AUTO SEGMENTATION (DL) ──
    auto* dlGroup = new QGroupBox("AUTO SEGMENTATION (DL)");
    auto* dlLayout = new QVBoxLayout(dlGroup);

    segmentBtn_ = new QPushButton(QString::fromUtf8("\U0001F512 Segment CT"));
    segmentBtn_->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; min-height: 40px;"
        " font-size: 11pt; font-weight: bold; border-radius: 6px; }"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:disabled { background-color: #d1d5db; color: #9ca3af; }");
    connect(segmentBtn_, &QPushButton::clicked, this, &CTSegmentationModule::requestDLSegmentation);
    dlLayout->addWidget(segmentBtn_);

    layout->addWidget(dlGroup);

    // ── MASK DISPLAY ──
    auto* dispGroup = new QGroupBox("MASK DISPLAY");
    auto* dispLayout = new QVBoxLayout(dispGroup);

    showMaskCheckbox_ = new QCheckBox("Show Mask Overlay");
    showMaskCheckbox_->setChecked(true);
    connect(showMaskCheckbox_, &QCheckBox::toggled, this, &CTSegmentationModule::onShowMaskToggled);
    dispLayout->addWidget(showMaskCheckbox_);

    auto* opLayout = new QHBoxLayout();
    opLayout->addWidget(new QLabel("Opacity:"));
    opacitySlider_ = new QSlider(Qt::Horizontal);
    opacitySlider_->setRange(0, 10);
    opacitySlider_->setValue(7);
    connect(opacitySlider_, &QSlider::valueChanged, this, &CTSegmentationModule::onOpacityChanged);
    opLayout->addWidget(opacitySlider_);
    opacityValLbl_ = new QLabel("70%");
    opacityValLbl_->setFixedWidth(35);
    opLayout->addWidget(opacityValLbl_);
    dispLayout->addLayout(opLayout);

    layout->addWidget(dispGroup);

    // ── CT WINDOW / LEVEL (brightness + contrast) ──
    auto* wlGroup = new QGroupBox("CT BRIGHTNESS / CONTRAST");
    auto* wlLayout = new QVBoxLayout(wlGroup);

    // Brightness = Level (HU)
    auto* lvlRow = new QHBoxLayout();
    lvlRow->addWidget(new QLabel("Brightness (Level):"));
    levelSlider_ = new QSlider(Qt::Horizontal);
    levelSlider_->setRange(-1000, 2000);   // HU
    levelSlider_->setValue(400);
    lvlRow->addWidget(levelSlider_);
    levelValLbl_ = new QLabel("400");
    levelValLbl_->setFixedWidth(45);
    lvlRow->addWidget(levelValLbl_);
    wlLayout->addLayout(lvlRow);

    // Contrast = Window (HU width)
    auto* winRow = new QHBoxLayout();
    winRow->addWidget(new QLabel("Contrast (Window):"));
    windowSlider_ = new QSlider(Qt::Horizontal);
    windowSlider_->setRange(50, 4000);
    windowSlider_->setValue(2000);
    winRow->addWidget(windowSlider_);
    windowValLbl_ = new QLabel("2000");
    windowValLbl_->setFixedWidth(45);
    winRow->addWidget(windowValLbl_);
    wlLayout->addLayout(winRow);

    // Quick presets
    auto* presetRow = new QHBoxLayout();
    auto presetStyle = QString(
        "QPushButton { background-color: #e9ecef; color: #1a2332; border: 1px solid #dde1e7;"
        "              padding: 2px 8px; font-size: 8pt; min-height: 22px; }"
        "QPushButton:hover { background-color: #dee2e6; }");
    auto applyPreset = [this](int w, int l) {
        if (windowSlider_) windowSlider_->setValue(w);
        if (levelSlider_)  levelSlider_->setValue(l);
    };
    auto* btnBone = new QPushButton("Bone (W2000 L400)");
    btnBone->setStyleSheet(presetStyle);
    connect(btnBone, &QPushButton::clicked, [applyPreset]() { applyPreset(2000, 400); });
    presetRow->addWidget(btnBone);
    auto* btnSoft = new QPushButton("Soft (W400 L40)");
    btnSoft->setStyleSheet(presetStyle);
    connect(btnSoft, &QPushButton::clicked, [applyPreset]() { applyPreset(400, 40); });
    presetRow->addWidget(btnSoft);
    auto* btnAuto = new QPushButton("Auto");
    btnAuto->setStyleSheet(presetStyle);
    connect(btnAuto, &QPushButton::clicked, [this]() {
        if (app_ && app_->sharedViewport()) {
            app_->sharedViewport()->resetWindowLevelAuto();
            if (windowSlider_) windowSlider_->setValue((int)app_->sharedViewport()->window());
            if (levelSlider_)  levelSlider_->setValue((int)app_->sharedViewport()->level());
        }
    });
    presetRow->addWidget(btnAuto);
    wlLayout->addLayout(presetRow);

    auto pushWL = [this]() {
        if (!app_ || !app_->sharedViewport()) return;
        int w = windowSlider_ ? windowSlider_->value() : 2000;
        int l = levelSlider_  ? levelSlider_->value()  : 400;
        if (windowValLbl_) windowValLbl_->setText(QString::number(w));
        if (levelValLbl_)  levelValLbl_->setText(QString::number(l));
        app_->sharedViewport()->setWindowLevel((double)w, (double)l);
    };
    connect(windowSlider_, &QSlider::valueChanged, this, [pushWL]() { pushWL(); });
    connect(levelSlider_,  &QSlider::valueChanged, this, [pushWL]() { pushWL(); });

    layout->addWidget(wlGroup);

    // ── VERTEBRA SELECTION (hidden until segmentation) ──
    vertebraSection_ = new QWidget();
    vertebraSection_->setVisible(false);
    auto* vertLayout = new QVBoxLayout(vertebraSection_);
    vertLayout->setContentsMargins(0, 0, 0, 0);

    auto* vertGroup = new QGroupBox("VERTEBRA SELECTION");
    auto* vertGroupLayout = new QVBoxLayout(vertGroup);

    auto* vertBtnLayout = new QHBoxLayout();
    auto* selectAllBtn = new QPushButton("Select All");
    selectAllBtn->setStyleSheet(
        "QPushButton { background-color: #e9ecef; color: #1a2332; border: 1px solid #dde1e7; }");
    connect(selectAllBtn, &QPushButton::clicked, this, &CTSegmentationModule::selectAllVertebrae);
    auto* deselectAllBtn = new QPushButton("Deselect All");
    deselectAllBtn->setStyleSheet(
        "QPushButton { background-color: #e9ecef; color: #1a2332; border: 1px solid #dde1e7; }");
    connect(deselectAllBtn, &QPushButton::clicked, this, &CTSegmentationModule::deselectAllVertebrae);
    vertBtnLayout->addWidget(selectAllBtn);
    vertBtnLayout->addWidget(deselectAllBtn);
    vertGroupLayout->addLayout(vertBtnLayout);

    // Scrollable vertebra list
    auto* vertScroll = new QScrollArea();
    vertScroll->setWidgetResizable(true);
    vertScroll->setMaximumHeight(220);
    auto* vertListWidget = new QWidget();
    vertebraListLayout_ = new QVBoxLayout(vertListWidget);
    vertebraListLayout_->setContentsMargins(4, 4, 4, 4);
    vertebraListLayout_->setSpacing(2);
    vertScroll->setWidget(vertListWidget);
    vertGroupLayout->addWidget(vertScroll);

    vertLayout->addWidget(vertGroup);
    layout->addWidget(vertebraSection_);

    // ── INFORMATION LOG ──
    auto* logGroup = new QGroupBox("INFORMATION");
    auto* logLayout = new QVBoxLayout(logGroup);
    logText_ = new QTextEdit();
    logText_->setReadOnly(true);
    logText_->setMaximumHeight(120);
    logText_->setStyleSheet("font-family: 'Courier New'; font-size: 9pt;");
    logLayout->addWidget(logText_);
    layout->addWidget(logGroup);

    layout->addStretch();

    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea);
    return panel;
}

void CTSegmentationModule::setBrushMode(const QString& mode)
{
    brushMode_ = mode;
    QString offStyle =
        "QPushButton { background-color: #e9ecef; color: #1a2332; border: 1px solid #dde1e7;"
        " min-height: 32px; border-radius: 5px; font-size: 9pt; font-weight: bold; }"
        "QPushButton:hover { background-color: #dee2e6; }";
    QString activeStyle =
        "QPushButton { background-color: #2c5f6f; color: white; border: none;"
        " min-height: 32px; border-radius: 5px; font-size: 9pt; font-weight: bold; }";

    btnPaint_->setStyleSheet(mode == "paint" ? activeStyle : offStyle);
    btnErase_->setStyleSheet(mode == "erase" ? activeStyle : offStyle);
    btnBrushOff_->setStyleSheet(mode == "off" ? activeStyle : offStyle);

    // Connect/disconnect brush on shared viewport
    if (app_ && app_->sharedViewport()) {
        if (mode == "paint" || mode == "erase") {
            app_->sharedViewport()->enableBrush(
                [this](const QString& vn, int cx, int cy) { applyBrush(vn, cx, cy); },
                [this](const QString& vn) { onBrushRelease(vn); }
            );
        } else {
            app_->sharedViewport()->disableBrush();
        }
    }

    appendLog(QString("Brush mode: %1").arg(mode));
}

void CTSegmentationModule::setActivePaintLabel(int label)
{
    activePaintLabel_ = label;
    const auto& colors = vertebraColors();

    for (auto it = paintLabelBtns_.begin(); it != paintLabelBtns_.end(); ++it) {
        int lbl = it.key();
        auto* btn = it.value();
        int idx = (lbl - 1) % colors.size();
        QColor c = colors[idx];
        if (lbl == label) {
            btn->setStyleSheet(QString(
                "QPushButton { background: transparent; color: rgb(%1,%2,%3); "
                "border: none; font-size: 12pt; font-weight: bold; padding: 0; }")
                .arg(c.red()).arg(c.green()).arg(c.blue()));
        } else {
            btn->setStyleSheet(
                "QPushButton { background: transparent; color: #ccc; border: none; font-size: 12pt; padding: 0; }"
                "QPushButton:hover { color: #888; }");
        }
    }
}

void CTSegmentationModule::applyBrush(const QString& viewName, int cx, int cy)
{
    if (brushMode_ == "off" || !app_ || !app_->sharedViewport()) return;

    auto* viewport = app_->sharedViewport();
    auto maskData = viewport->maskData_;
    if (!maskData) return;

    int* dims = maskData->GetDimensions();
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    auto* canvas = viewport->getCanvas(viewName);
    if (!canvas) return;

    // --- Coordinate mapping: must match displaySlice exactly ---
    // displaySlice uses: hAxis, vAxis, viewAxis with flipDim_
    int viewAxis, hAxis, vAxis;
    if (viewName == "axial") {
        viewAxis = viewport->axialDim(); hAxis = viewport->sagittalDim(); vAxis = viewport->coronalDim();
    } else if (viewName == "sagittal") {
        viewAxis = viewport->sagittalDim(); hAxis = viewport->coronalDim(); vAxis = viewport->axialDim();
    } else {
        viewAxis = viewport->coronalDim(); hAxis = viewport->sagittalDim(); vAxis = viewport->axialDim();
    }

    int imgW = dims[hAxis];   // displaySlice dispW
    int imgH = dims[vAxis];   // displaySlice dispH

    // Account for zoom and pan from BrushCanvas
    int cw = canvas->width(), ch = canvas->height();
    if (cw <= 0 || ch <= 0) return;

    double baseScale = std::min((double)cw / imgW, (double)ch / imgH);
    double zoom = canvas->zoomFactor();
    double totalScale = baseScale * zoom;
    double zoomedW = imgW * totalScale;
    double zoomedH = imgH * totalScale;
    QPoint pan = canvas->panOffset();
    double offX = (cw - zoomedW) / 2.0 + pan.x();
    double offY = (ch - zoomedH) / 2.0 + pan.y();

    // Widget pixel → source image pixel (matching displaySlice's (col, row))
    double relX = (cx - offX) / totalScale;
    double relY = (cy - offY) / totalScale;
    if (relX < 0 || relY < 0 || relX >= imgW || relY >= imgH) return;

    int imgCol = (int)relX;
    int imgRow = (int)relY;
    imgCol = std::clamp(imgCol, 0, imgW - 1);
    imgRow = std::clamp(imgRow, 0, imgH - 1);

    // displaySlice does: vCol = flipDim[hAxis] ? dims[hAxis]-1-col : col
    // Reverse that to get voxel index
    const bool* flipDim = viewport->flipDims();
    bool flipH = flipDim[hAxis];
    bool flipV = flipDim[vAxis];
    int vCol = flipH ? (dims[hAxis] - 1 - imgCol) : imgCol;
    int vRow = flipV ? (dims[vAxis] - 1 - imgRow) : imgRow;

    int sliceIdx = viewport->currentSlice.value(viewName, 0);
    bool flipView = flipDim[viewAxis];
    int sliceVoxel = flipView ? (dims[viewAxis] - 1 - sliceIdx) : sliceIdx;

    int paintVal = (brushMode_ == "paint") ? activePaintLabel_ : 0;
    int r = brushRadius_;

    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;

            int vc = vCol + dx;
            int vr = vRow + dy;

            int voxIdx[3];
            voxIdx[hAxis] = vc;
            voxIdx[vAxis] = vr;
            voxIdx[viewAxis] = sliceVoxel;

            int ix = voxIdx[0], iy = voxIdx[1], iz = voxIdx[2];
            if (ix < 0 || ix >= dims[0] || iy < 0 || iy >= dims[1] || iz < 0 || iz >= dims[2])
                continue;

            int idx = iz * dims[0] * dims[1] + iy * dims[0] + ix;
            if (brushMode_ == "erase") {
                int oldVal = static_cast<int>(scalars->GetTuple1(idx));
                if (oldVal > 0) erasedLabels_.insert(oldVal);
            }
            scalars->SetTuple1(idx, paintVal);
        }
    }

    maskData->Modified();

    // Instant visual preview: draw colored circle on the pixmap
    directPaintPreview(viewName, cx, cy);
}

void CTSegmentationModule::directPaintPreview(const QString& viewName, int cx, int cy)
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* canvas = app_->sharedViewport()->getCanvas(viewName);
    if (!canvas) return;

    QPixmap pm = canvas->pixmap();
    if (pm.isNull()) return;

    // Compute canvas-space brush radius
    auto* viewport = app_->sharedViewport();
    int* dims = viewport->imageData_ ? viewport->imageData_->GetDimensions() : nullptr;
    if (!dims) return;

    int origH, origW;
    if (viewName == "axial") { origH = dims[0]; origW = dims[1]; }
    else if (viewName == "sagittal") { origH = dims[1]; origW = dims[2]; }
    else { origH = dims[0]; origW = dims[2]; }
    int rotH = origW, rotW = origH;

    int cw = canvas->width(), ch = canvas->height();
    double scale = std::min(static_cast<double>(cw) / rotW,
                            static_cast<double>(ch) / rotH);
    double canvasR = std::max(2.0, brushRadius_ * scale);

    // Get brush color
    QColor color;
    if (brushMode_ == "paint") {
        const auto& colors = vertebraColors();
        int idx = (activePaintLabel_ - 1) % colors.size();
        color = colors[idx];
        color.setAlpha(140);
    } else {
        color = QColor(0, 0, 0, 160);
    }

    QPixmap result(pm);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    // cx,cy are relative to canvas, but pixmap is centered — adjust
    double offX = (cw - pm.width()) / 2.0;
    double offY = (ch - pm.height()) / 2.0;
    painter.drawEllipse(QPointF(cx - offX, cy - offY), canvasR, canvasR);
    painter.end();

    // Use QLabel::setPixmap directly (not BrushCanvas override)
    static_cast<QLabel*>(canvas)->setPixmap(result);
}

void CTSegmentationModule::onBrushRelease(const QString& viewName)
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();

    // Properly redraw the slice with correct mask composite
    int sliceIdx = viewport->currentSlice.value(viewName, 0);
    viewport->displaySlice(viewName, sliceIdx);

    // Rebuild only the affected label's 3D mesh
    if (brushMode_ == "paint") {
        rebuildSingleMesh(activePaintLabel_);
    } else {
        // Erase: only rebuild the labels that were actually erased
        for (int lbl : erasedLabels_) {
            rebuildSingleMesh(lbl);
        }
        erasedLabels_.clear();
    }
}

void CTSegmentationModule::rebuildSingleMesh(int label)
{
    if (!app_ || !app_->sharedViewport()) return;
    // Only rebuild if in segmentation 3D mode
    if (app_->sharedViewport()->current3DMode() != "segmentation") return;
    auto* viewport = app_->sharedViewport();
    auto maskData = viewport->maskData_;
    if (!maskData) return;
    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Preserve camera position
    vtkCamera* cam = renderer->GetActiveCamera();
    double camPos[3], camFoc[3], camUp[3];
    bool hasCam = false;
    if (cam) {
        cam->GetPosition(camPos);
        cam->GetFocalPoint(camFoc);
        cam->GetViewUp(camUp);
        hasCam = true;
    }

    int* dims = maskData->GetDimensions();
    double* spacing = maskData->GetSpacing();
    int numVoxels = dims[0] * dims[1] * dims[2];
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    // Remove old entry for this label
    for (int i = 0; i < cachedMeshes_.size(); ++i) {
        if (cachedMeshes_[i].label == label) {
            if (cachedMeshes_[i].actor.Get())
                renderer->RemoveActor(cachedMeshes_[i].actor);
            cachedMeshes_.removeAt(i);
            break;
        }
    }

    // Rebuild mesh using vtkDiscreteMarchingCubes (no binary volume copy needed)
    auto dmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    dmc->SetInputData(maskData);
    dmc->SetNumberOfContours(1);
    dmc->SetValue(0, label);
    dmc->ComputeNormalsOn();
    dmc->Update();

    if (dmc->GetOutput()->GetNumberOfPoints() > 0) {
        auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smoother->SetInputConnection(dmc->GetOutputPort());
        smoother->SetNumberOfIterations(15);
        smoother->SetPassBand(0.1);
        smoother->BoundarySmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        auto polyData = vtkSmartPointer<vtkPolyData>::New();
        polyData->DeepCopy(smoother->GetOutput());

        // Clip with capped surface
        vtkSmartPointer<vtkActor> actor;
        auto clipped = clipWithCap(polyData, clipNormal_, clipOrigin_);
        vtkPolyData* displayData = (clipped && clipped->GetNumberOfPoints() > 0)
            ? clipped.Get() : polyData.Get();

        {
            auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            mapper->SetInputData(displayData);
            mapper->ScalarVisibilityOff();
            actor = vtkSmartPointer<vtkActor>::New();
            actor->SetMapper(mapper);
        }

        static const int VC[][3] = {
            {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},
            {255,0,255},{255,239,213},{0,0,205},{205,133,63},{210,180,140},
            {102,205,170},{0,0,128},{0,139,139},{46,139,87},{255,228,225},
            {106,90,205},{221,160,221},{233,150,122},{165,42,42},
            {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
            {60,179,113},{255,235,205},
        };
        int ci = (label - 1) % 26;
        actor->GetProperty()->SetColor(VC[ci][0]/255.0, VC[ci][1]/255.0, VC[ci][2]/255.0);
        actor->GetProperty()->SetOpacity(1.0);
        actor->GetProperty()->SetInterpolationToPhong();
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetSpecular(0.3);

        auto cbIt = vertebraCheckboxes_.find(label);
        if (cbIt != vertebraCheckboxes_.end() && !cbIt.value()->isChecked()) {
            actor->SetVisibility(0);
        }

        renderer->AddActor(actor);
        cachedMeshes_.append({label, polyData, actor});
    }

    // Restore camera
    if (hasCam && cam) {
        cam->SetPosition(camPos);
        cam->SetFocalPoint(camFoc);
        cam->SetViewUp(camUp);
    }

    auto* rw = viewport->get3DRenderWindow();
    if (rw) rw->Render();
}

void CTSegmentationModule::onBrushSizeChanged(int val)
{
    brushRadius_ = val;
    brushSizeValLbl_->setText(QString::number(val));
}

void CTSegmentationModule::requestDLSegmentation()
{
    if (!app_) return;

    QString ctFilePath = app_->currentProject.value("ct_file_path").toString();
    if (ctFilePath.isEmpty()) {
        QMessageBox::warning(this, "Warning",
            "No CT data loaded.\nPlease load CT data in Data module first.");
        return;
    }

    appendLog("==================================================");
    appendLog("Starting two-stage segmentation (MSG-UNet)...");
    appendLog(QString("CT: %1").arg(ctFilePath));

    QString tmpDir = QDir::tempPath() + "/spinex_seg_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(tmpDir);

    auto* progress = new QProgressDialog("Initializing...", "Cancel", 0, 100, this);
    progress->setWindowTitle("Segmentation Progress");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->show();
    QApplication::processEvents();

    QString pythonExe = QString::fromStdString(PythonRuntime::instance().pythonExecutable());

    // Try .py first (development), then fall back to .pyd wrapper (installed)
    QString scriptPath = QCoreApplication::applicationDirPath() + "/python/run_segmentation.py";
    if (!QFile::exists(scriptPath)) {
        QString srcDir = QString::fromStdString(PythonRuntime::projectSourceDir());
        if (!srcDir.isEmpty())
            scriptPath = srcDir + "/python/run_segmentation.py";
    }

    auto* process    = new QProcess(this);
    auto* outputPath = new QString();
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::readyReadStandardOutput,
        [this, process, progress, outputPath]() {
            for (const QString& line :
                 QString::fromUtf8(process->readAll()).split('\n')) {
                QString t = line.trimmed();
                if (t.startsWith("PROGRESS:")) {
                    QStringList p = t.mid(9).split(':', Qt::KeepEmptyParts);
                    if (p.size() >= 2) {
                        int pct = p[0].toInt();
                        QString msg = p.mid(1).join(':');
                        progress->setValue(pct);
                        progress->setLabelText(msg);
                        appendLog(QString("[%1%] %2").arg(pct).arg(msg));
                    }
                } else if (t.startsWith("OUTPUT:")) {
                    *outputPath = t.mid(7).trimmed();
                } else if (!t.isEmpty()) {
                    appendLog(t);
                }
            }
        });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, process, progress, tmpDir, outputPath](int exitCode, QProcess::ExitStatus status) {
            progress->setValue(100);
            progress->close();
            progress->deleteLater();

            if (exitCode != 0 || status != QProcess::NormalExit) {
                appendLog(QString("Segmentation failed (exit code %1)").arg(exitCode));
                QMessageBox::critical(this, "Segmentation Failed",
                    QString("Segmentation process exited with code %1.\n"
                            "Check the log for details.").arg(exitCode));
                delete outputPath;
                process->deleteLater();
                return;
            }

            QString maskPath = *outputPath;
            if (maskPath.isEmpty() || !QFile::exists(maskPath)) {
                QDir dir(tmpDir);
                QStringList masks = dir.entryList({"*_mask.nii.gz","*_mask.nii"}, QDir::Files);
                if (!masks.isEmpty())
                    maskPath = dir.absoluteFilePath(masks.first());
            }

            if (maskPath.isEmpty() || !QFile::exists(maskPath)) {
                appendLog("Could not find mask output file");
                QMessageBox::critical(this, "Segmentation Failed",
                    "Segmentation completed but mask file not found.");
                delete outputPath;
                process->deleteLater();
                return;
            }

            appendLog(QString("Mask file: %1").arg(maskPath));
            app_->currentProject["mask_file_path"] = maskPath;

            try {
                auto maskVtk = NiftiIO::readNifti(maskPath.toStdString());
                if (maskVtk) {
                    int* dims = maskVtk->GetDimensions();
                    appendLog(QString("Mask loaded: %1 x %2 x %3")
                        .arg(dims[0]).arg(dims[1]).arg(dims[2]));
                    auto* viewport = app_->sharedViewport();
                    if (viewport) {
                        viewport->setMaskData(maskVtk);
                        buildCached3D();
                    }
                    btnPaint_->setEnabled(true);
                    btnErase_->setEnabled(true);
                    btnBrushOff_->setEnabled(true);
                    brushSizeSlider_->setEnabled(true);
                    showMaskCheckbox_->setChecked(true);
                    populateVertebraListFromMask(maskVtk);
                }
            } catch (const std::exception& e) {
                appendLog(QString("Mask load error: %1").arg(e.what()));
            }

            appendLog("==================================================");
            appendLog("Segmentation complete!");
            QMessageBox::information(this, "Segmentation Complete",
                "CT segmentation completed successfully!\n"
                "Mask overlay is now visible on CT slices.");

            delete outputPath;
            process->deleteLater();
        });

    connect(progress, &QProgressDialog::canceled, [process]() { process->kill(); });

    // 注入模型解密密钥
    {
        QByteArray key = qgetenv("SPINEX_MODEL_KEY");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SPINEX_MODEL_KEY", QString::fromLatin1(key));
        process->setProcessEnvironment(env);
    }

    // If .py exists, run it directly; otherwise use -c to import the .pyd module
    if (QFile::exists(scriptPath)) {
        process->start(pythonExe, {scriptPath, ctFilePath, tmpDir});
    } else {
        // .pyd is installed — use inline python to import and call main()
        QString pythonDir = QCoreApplication::applicationDirPath() + "/python";
        QString inlineCode = QString(
            "import sys; sys.path.insert(0, r'%1'); "
            "sys.argv = ['run_segmentation', r'%2', r'%3']; "
            "import run_segmentation; run_segmentation.main()"
        ).arg(pythonDir).arg(ctFilePath).arg(tmpDir);
        process->start(pythonExe, {"-c", inlineCode});
    }

    if (!process->waitForStarted(5000)) {
        progress->close();
        progress->deleteLater();
        appendLog("Failed to start Python segmentation process");
        QMessageBox::critical(this, "Error",
            "Failed to start Python process.\n"
            "Check that BoneMed conda environment is accessible.");
        delete outputPath;
        process->deleteLater();
    }
}

void CTSegmentationModule::onShowMaskToggled(bool checked)
{
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setMaskVisible(checked);
    }
    appendLog(QString("Mask overlay: %1").arg(checked ? "ON" : "OFF"));
}

void CTSegmentationModule::onOpacityChanged(int val)
{
    opacityValLbl_->setText(QString("%1%").arg(val * 10));
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setMaskOpacity(val / 10.0);
    }
}

void CTSegmentationModule::populateVertebraListFromMask(vtkSmartPointer<vtkImageData> maskData)
{
    if (!maskData) return;

    // Extract unique non-zero labels from mask
    std::set<int> labels;
    int numVoxels = maskData->GetNumberOfPoints();
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    for (int i = 0; i < numVoxels; ++i) {
        int val = static_cast<int>(scalars->GetTuple1(i));
        if (val > 0) labels.insert(val);
    }

    if (labels.empty()) {
        appendLog("No vertebrae found in mask.");
        return;
    }

    appendLog(QString("Found %1 vertebrae in mask.").arg(labels.size()));

    // Clear existing list
    QLayoutItem* child;
    while ((child = vertebraListLayout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    vertebraCheckboxes_.clear();
    paintLabelBtns_.clear();

    const auto& names = vertebraNames();
    const auto& colors = vertebraColors();

    for (int lbl : labels) {
        int idx = (lbl - 1) % colors.size();
        QString name = (lbl - 1 < names.size()) ? names[lbl - 1] : QString("Label %1").arg(lbl);
        QColor color = colors[idx];
        QString hexColor = QString("rgb(%1,%2,%3)").arg(color.red()).arg(color.green()).arg(color.blue());

        auto* row = new QWidget();
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        // Paint label selector button (colored dot)
        auto* paintBtn = new QPushButton(QString::fromUtf8("\u25CF"));
        paintBtn->setFixedSize(22, 22);
        paintBtn->setToolTip(QString("Select %1 as paint label").arg(name));
        paintBtn->setStyleSheet(
            "QPushButton { background: transparent; color: #ccc; border: none; font-size: 12pt; padding: 0; }"
            "QPushButton:hover { color: #888; }");
        connect(paintBtn, &QPushButton::clicked, [this, lbl]() { setActivePaintLabel(lbl); });
        paintLabelBtns_[lbl] = paintBtn;
        rowLayout->addWidget(paintBtn);

        // Visibility checkbox — 如果有保存的 selected_labels，按保存的状态恢复
        auto* cb = new QCheckBox(QString("  %1  (label %2)").arg(name).arg(lbl));
        bool shouldCheck = true;
        if (app_) {
            QVariantList saved = app_->currentProject.value("selected_labels").toList();
            if (!saved.isEmpty()) {
                shouldCheck = false;
                for (const auto& v : saved) {
                    if (v.toInt() == lbl) { shouldCheck = true; break; }
                }
            }
        }
        cb->setChecked(shouldCheck);
        cb->setStyleSheet(QString(
            "QCheckBox { font-size: 9pt; color: #222; }"
            "QCheckBox::indicator:checked { background-color: %1; border: 1px solid #666; border-radius: 2px; }"
            "QCheckBox::indicator:unchecked { background-color: #ccc; border: 1px solid #999; border-radius: 2px; }")
            .arg(hexColor));
        connect(cb, &QCheckBox::toggled, [this, lbl](bool checked) {
            onVertebraToggled(lbl, checked);
        });
        vertebraCheckboxes_[lbl] = cb;
        rowLayout->addWidget(cb, 1);

        vertebraListLayout_->addWidget(row);
    }

    vertebraSection_->setVisible(true);

    // Auto-select first label for painting
    if (!labels.empty()) {
        setActivePaintLabel(*labels.begin());
    }

    // 只在没有保存的 selected_labels 时才默认全选
    if (app_ && app_->currentProject.value("selected_labels").toList().isEmpty()) {
        QVariantList selectedList;
        for (int lbl : labels) selectedList.append(lbl);
        app_->currentProject["selected_labels"] = selectedList;
    }
}

void CTSegmentationModule::buildCached3D()
{
    if (!app_ || !app_->sharedViewport()) return;

    auto* viewport = app_->sharedViewport();

    // Only build if we're in segmentation 3D mode AND viewport is in normal 4-panel mode
    // (when in 3D-only mode, another module like Cobb Angle controls the 3D view)
    if (viewport->current3DMode() != "segmentation") return;
    if (viewport->isOnly3DMode()) return;

    auto maskData = viewport->maskData_;
    if (!maskData) return;

    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    // Clear EVERYTHING from 3D scene (removes any leftover Mat actors too)
    renderer->RemoveAllViewProps();
    cachedMeshes_.clear();

    int* dims = maskData->GetDimensions();
    double* spacing = maskData->GetSpacing();
    int numVoxels = dims[0] * dims[1] * dims[2];
    auto* scalars = maskData->GetPointData()->GetScalars();
    if (!scalars) return;

    // Find unique labels
    std::set<int> allLabels;
    for (int i = 0; i < numVoxels; ++i) {
        int val = static_cast<int>(scalars->GetTuple1(i));
        if (val > 0) allLabels.insert(val);
    }

    // Only build meshes for CHECKED vertebrae (matching Python _vertebra_visible)
    std::set<int> labels;
    for (int lbl : allLabels) {
        auto cbIt = vertebraCheckboxes_.find(lbl);
        if (cbIt != vertebraCheckboxes_.end() && cbIt.value()->isChecked()) {
            labels.insert(lbl);
        } else if (cbIt == vertebraCheckboxes_.end()) {
            // No checkbox yet (first build) — include all
            labels.insert(lbl);
        }
    }

    appendLog(QString("Building 3D meshes for %1 vertebrae...").arg(labels.size()));

    static const int VC[][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},
        {255,0,255},{255,239,213},{0,0,205},{205,133,63},{210,180,140},
        {102,205,170},{0,0,128},{0,139,139},{46,139,87},{255,228,225},
        {106,90,205},{221,160,221},{233,150,122},{165,42,42},
        {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
        {60,179,113},{255,235,205},
    };

    // Compute center of all masks for clip plane origin
    double bounds[6];
    maskData->GetBounds(bounds);
    clipOrigin_[0] = (bounds[0] + bounds[1]) / 2.0;
    clipOrigin_[1] = (bounds[2] + bounds[3]) / 2.0;
    clipOrigin_[2] = (bounds[4] + bounds[5]) / 2.0;
    clipNormal_[0] = 1.0; clipNormal_[1] = 0.0; clipNormal_[2] = 0.0;

    // Create clip plane
    clipPlane_ = vtkSmartPointer<vtkPlane>::New();
    clipPlane_->SetNormal(clipNormal_);
    clipPlane_->SetOrigin(clipOrigin_);

    // Single vtkDiscreteMarchingCubes for ALL labels at once
    auto dmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
    dmc->SetInputData(maskData);
    {
        int idx = 0;
        for (int lbl : labels) dmc->SetValue(idx++, lbl);
    }
    dmc->ComputeNormalsOn();
    dmc->Update();

    // Per-label: extract from combined mesh, smooth, decimate, create actor
    for (int lbl : labels) {
        // Extract this label using DiscreteMarchingCubes on single value
        auto singleDmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
        singleDmc->SetInputData(maskData);
        singleDmc->SetNumberOfContours(1);
        singleDmc->SetValue(0, lbl);
        singleDmc->ComputeNormalsOn();
        singleDmc->Update();

        if (singleDmc->GetOutput()->GetNumberOfPoints() == 0) continue;

        // Smooth
        auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smoother->SetInputConnection(singleDmc->GetOutputPort());
        smoother->SetNumberOfIterations(15);
        smoother->SetPassBand(0.1);
        smoother->BoundarySmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        // Store unclipped polydata
        auto polyData = vtkSmartPointer<vtkPolyData>::New();
        polyData->DeepCopy(smoother->GetOutput());

        // Clip with capped surface
        auto clipped = clipWithCap(polyData, clipNormal_, clipOrigin_);
        vtkPolyData* displayData = (clipped && clipped->GetNumberOfPoints() > 0)
            ? clipped.Get() : polyData.Get();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(displayData);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        int ci = (lbl - 1) % 26;
        actor->GetProperty()->SetColor(VC[ci][0]/255.0, VC[ci][1]/255.0, VC[ci][2]/255.0);
        actor->GetProperty()->SetOpacity(1.0);
        actor->GetProperty()->SetInterpolationToPhong();
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetSpecular(0.3);

        renderer->AddActor(actor);
        cachedMeshes_.append({lbl, polyData, actor});
    }

    renderer->ResetCamera();

    // Setup interactive clip plane widget
    setupClipPlaneWidget();

    auto* rw = viewport->get3DRenderWindow();
    if (rw) rw->Render();

    appendLog(QString("3D rendering complete: %1 meshes cached").arg(cachedMeshes_.size()));
}

void CTSegmentationModule::setupClipPlaneWidget()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!rw || !renderer) return;

    // Remove old widget if any
    if (planeWidget_) {
        planeWidget_->Off();
        planeWidget_ = nullptr;
    }

    planeWidget_ = vtkSmartPointer<vtkImplicitPlaneWidget2>::New();

    auto rep = vtkSmartPointer<vtkImplicitPlaneRepresentation>::New();
    rep->SetPlaceFactor(1.0);
    if (app_->sharedViewport()->maskData_) {
        double bounds[6];
        app_->sharedViewport()->maskData_->GetBounds(bounds);
        rep->PlaceWidget(bounds);
    }
    rep->SetNormal(clipNormal_);
    rep->SetOrigin(clipOrigin_);
    rep->SetEdgeColor(0.5, 0.5, 0.5);
    rep->GetPlaneProperty()->SetOpacity(0.0);       // No colored fill on the plane
    rep->GetSelectedPlaneProperty()->SetOpacity(0.0);
    rep->SetDrawOutline(true);
    rep->GetOutlineProperty()->SetColor(0.7, 0.7, 0.7);
    rep->GetOutlineProperty()->SetOpacity(0.4);

    planeWidget_->SetRepresentation(rep);
    planeWidget_->SetInteractor(rw->GetInteractor());

    // Callback for when plane is moved
    planeWidget_->AddObserver(vtkCommand::InteractionEvent,
        this, &CTSegmentationModule::onClipPlaneChanged);

    planeWidget_->On();
}

void CTSegmentationModule::onClipPlaneChanged(vtkObject*, unsigned long, void*)
{
    if (!planeWidget_) return;
    if (app_ && app_->sharedViewport() && app_->sharedViewport()->isOnly3DMode()) return;

    auto* rep = vtkImplicitPlaneRepresentation::SafeDownCast(
        planeWidget_->GetRepresentation());
    if (!rep) return;

    rep->GetNormal(clipNormal_);
    rep->GetOrigin(clipOrigin_);

    doClip(clipNormal_[0], clipNormal_[1], clipNormal_[2],
           clipOrigin_[0], clipOrigin_[1], clipOrigin_[2]);
}

void CTSegmentationModule::doClip(double nx, double ny, double nz,
                                   double ox, double oy, double oz)
{
    if (!app_ || !app_->sharedViewport()) return;
    // Only clip in segmentation mode and not when another module controls 3D
    if (app_->sharedViewport()->current3DMode() != "segmentation") return;
    if (app_->sharedViewport()->isOnly3DMode()) return;
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!renderer) return;

    // Update clip plane
    if (!clipPlane_) {
        clipPlane_ = vtkSmartPointer<vtkPlane>::New();
    }
    clipPlane_->SetNormal(nx, ny, nz);
    clipPlane_->SetOrigin(ox, oy, oz);

    static const int VC[][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},
        {255,0,255},{255,239,213},{0,0,205},{205,133,63},{210,180,140},
        {102,205,170},{0,0,128},{0,139,139},{46,139,87},{255,228,225},
        {106,90,205},{221,160,221},{233,150,122},{165,42,42},
        {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
        {60,179,113},{255,235,205},
    };

    // Re-clip each mesh
    for (auto& vm : cachedMeshes_) {
        if (!vm.polyData.Get()) continue;

        // Check visibility
        auto cbIt = vertebraCheckboxes_.find(vm.label);
        bool visible = (cbIt != vertebraCheckboxes_.end()) ? cbIt.value()->isChecked() : true;

        // Remove old actor
        if (vm.actor.Get()) {
            renderer->RemoveActor(vm.actor);
        }

        if (!visible) {
            vm.actor = nullptr;
            continue;
        }

        double normal[3] = {nx, ny, nz};
        double origin[3] = {ox, oy, oz};
        auto clipped = clipWithCap(vm.polyData, normal, origin);
        if (!clipped || clipped->GetNumberOfPoints() == 0) {
            // Show unclipped if clip removes everything
            auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            mapper->SetInputData(vm.polyData);
            mapper->ScalarVisibilityOff();
            auto actor = vtkSmartPointer<vtkActor>::New();
            actor->SetMapper(mapper);
            int ci = (vm.label - 1) % 26;
            actor->GetProperty()->SetColor(VC[ci][0]/255.0, VC[ci][1]/255.0, VC[ci][2]/255.0);
            actor->GetProperty()->SetOpacity(1.0);
            actor->GetProperty()->SetInterpolationToPhong();
            actor->GetProperty()->SetAmbient(0.2);
            actor->GetProperty()->SetDiffuse(0.8);
            actor->GetProperty()->SetSpecular(0.3);
            renderer->AddActor(actor);
            vm.actor = actor;
            continue;
        }

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(clipped);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        int ci = (vm.label - 1) % 26;
        actor->GetProperty()->SetColor(VC[ci][0]/255.0, VC[ci][1]/255.0, VC[ci][2]/255.0);
        actor->GetProperty()->SetOpacity(1.0);
        actor->GetProperty()->SetInterpolationToPhong();
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetSpecular(0.3);

        renderer->AddActor(actor);
        vm.actor = actor;
    }

    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    if (rw) rw->Render();
}

void CTSegmentationModule::onVertebraToggled(int label, bool visible)
{
    // Save selected (checked) labels to project for other modules to use
    if (app_) {
        QVariantList selectedList;
        for (auto it = vertebraCheckboxes_.begin(); it != vertebraCheckboxes_.end(); ++it) {
            if (it.value()->isChecked()) {
                selectedList.append(it.key());
            }
        }
        app_->currentProject["selected_labels"] = selectedList;
    }

    // Only update 3D if in segmentation mode and not in 3D-only mode
    if (!app_ || !app_->sharedViewport() ||
        app_->sharedViewport()->current3DMode() != "segmentation" ||
        app_->sharedViewport()->isOnly3DMode()) {
        return;
    }

    // Fast path: just toggle actor visibility (no re-clip needed)
    for (auto& vm : cachedMeshes_) {
        if (vm.label == label && vm.actor.Get()) {
            vm.actor->SetVisibility(visible ? 1 : 0);
            auto* rw = app_->sharedViewport()->get3DRenderWindow();
            if (rw) rw->Render();
            return;
        }
    }

    // If actor not found (label was just enabled), rebuild
    buildCached3D();
}

void CTSegmentationModule::populateVertebraList()
{
    // Clear existing
    QLayoutItem* child;
    while ((child = vertebraListLayout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    vertebraCheckboxes_.clear();

    const auto& names = vertebraNames();
    const auto& colors = vertebraColors();

    for (int i = 0; i < names.size(); ++i) {
        auto* row = new QHBoxLayout();

        // Color indicator
        auto* colorLbl = new QLabel(QString::fromUtf8("\u25CF"));
        colorLbl->setStyleSheet(QString("color: rgb(%1,%2,%3); font-size: 12pt;")
            .arg(colors[i].red()).arg(colors[i].green()).arg(colors[i].blue()));
        colorLbl->setFixedWidth(20);
        row->addWidget(colorLbl);

        // Checkbox
        auto* cb = new QCheckBox(names[i]);
        cb->setChecked(true);
        vertebraCheckboxes_[i + 1] = cb;
        row->addWidget(cb);

        row->addStretch();

        auto* rowWidget = new QWidget();
        rowWidget->setLayout(row);
        vertebraListLayout_->addWidget(rowWidget);
    }

    vertebraSection_->setVisible(true);
    btnPaint_->setEnabled(true);
    btnErase_->setEnabled(true);
    brushSizeSlider_->setEnabled(true);
}

void CTSegmentationModule::selectAllVertebrae()
{
    for (auto it = vertebraCheckboxes_.begin(); it != vertebraCheckboxes_.end(); ++it)
        it.value()->setChecked(true);
}

void CTSegmentationModule::deselectAllVertebrae()
{
    for (auto it = vertebraCheckboxes_.begin(); it != vertebraCheckboxes_.end(); ++it)
        it.value()->setChecked(false);
}

void CTSegmentationModule::appendLog(const QString& msg)
{
    logText_->append(msg);
}

void CTSegmentationModule::refreshFromProject()
{
    if (!app_) return;

    auto* viewport = app_->sharedViewport();

    // Connect 3D mode switch (once)
    static bool connected = false;
    if (!connected && viewport) {
        connect(viewport, &SharedViewport::mode3DChanged, this, [this](const QString& mode) {
            if (mode == "segmentation") {
                buildCached3D();
            }
        });
        connected = true;
    }

    // Load CT data
    QString ctFilePath = app_->currentProject.value("ct_file_path").toString();
    if (!ctFilePath.isEmpty()) {
        if (viewport && viewport->loadedCTPath_ == ctFilePath && viewport->imageData_) {
            appendLog("CT already loaded, skipping re-read.");
        } else {
            try {
                auto imageData = NiftiIO::readNifti(ctFilePath.toStdString());
                if (imageData) {
                    int* dims = imageData->GetDimensions();
                    appendLog(QString("CT loaded: %1 x %2 x %3")
                        .arg(dims[0]).arg(dims[1]).arg(dims[2]));
                    if (viewport) {
                        viewport->setImageData(imageData);
                        viewport->loadedCTPath_ = ctFilePath;
                    }
                }
            } catch (const std::exception& e) {
                appendLog(QString("Error loading CT: %1").arg(e.what()));
            }
        }
    } else {
        appendLog("No CT data. Use Data module to load files.");
    }

    // Load mask data if available
    QString maskFilePath = app_->currentProject.value("mask_file_path").toString();
    if (!maskFilePath.isEmpty() && QFile::exists(maskFilePath)) {
        if (viewport && viewport->loadedMaskPath_ == maskFilePath && viewport->maskData_) {
            appendLog("Mask already loaded, skipping re-read.");
            viewport->forceSwitch3DMode("segmentation");
        } else {
        try {
            auto maskData = NiftiIO::readNifti(maskFilePath.toStdString());
            if (maskData && viewport) {
                int* dims = maskData->GetDimensions();
                appendLog(QString("Mask loaded: %1 x %2 x %3")
                    .arg(dims[0]).arg(dims[1]).arg(dims[2]));
                viewport->setMaskData(maskData);
                viewport->loadedMaskPath_ = maskFilePath;
                // Force switch to segmentation 3D mode
                viewport->forceSwitch3DMode("segmentation");

                // Enable brush tools
                btnPaint_->setEnabled(true);
                btnErase_->setEnabled(true);
                btnBrushOff_->setEnabled(true);
                brushSizeSlider_->setEnabled(true);

                // Populate vertebra list from mask labels
                populateVertebraListFromMask(maskData);
            }
        } catch (const std::exception& e) {
            appendLog(QString("Error loading mask: %1").arg(e.what()));
        }
        } // end else (new load)
    }

    appendLog("==================================================");
}
