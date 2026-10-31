/**
 * ============================================================================
 * SharedViewport - 四面板视口（实现）
 * 从 Python ui/shared_viewport.py 1:1 移植
 * ============================================================================
 */

#include "SharedViewport.h"
#include "CTSliceViewer.h"
#include "modules/ModuleBase.h"

#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkCamera.h>
#include <vtkMatrix3x3.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkWindowedSincPolyDataFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkFloatArray.h>
#include <vtkPolyDataNormals.h>
#include <vtkDecimatePro.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>

SharedViewport::SharedViewport(QWidget* parent)
    : QWidget(parent)
    , vtk3DWidget_(nullptr)
    , activeModule_(nullptr)
{
    currentSlice["axial"] = 0;
    currentSlice["sagittal"] = 0;
    currentSlice["coronal"] = 0;

    // 渲染合并定时器：16ms ≈ 60 FPS 上限
    renderCoalesceTimer_ = new QTimer(this);
    renderCoalesceTimer_->setSingleShot(true);
    renderCoalesceTimer_->setInterval(16);
    connect(renderCoalesceTimer_, &QTimer::timeout, this, &SharedViewport::flushDirtyViews);

    initUI();
}

SharedViewport::~SharedViewport() = default;

void SharedViewport::initUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* gridWidget = new QFrame();
    gridWidget->setStyleSheet("background-color: #0d0d0d;");

    gridLayout_ = new QGridLayout(gridWidget);
    gridLayout_->setSpacing(2);
    gridLayout_->setContentsMargins(2, 2, 2, 2);

    // 4 panels matching Python layout
    coronalPanel_ = createCTPanel("coronal",  QString::fromUtf8("Coronal"),  "#1E88E5");
    axialPanel_ = createCTPanel("axial",    QString::fromUtf8("Axial"),    "#00ACC1");
    sagittalPanel_ = createCTPanel("sagittal", QString::fromUtf8("Sagittal"), "#43A047");
    panel3D_ = create3DPanel();

    gridLayout_->addWidget(coronalPanel_, 0, 0);
    gridLayout_->addWidget(axialPanel_, 0, 1);
    gridLayout_->addWidget(sagittalPanel_, 1, 0);
    gridLayout_->addWidget(panel3D_, 1, 1);

    gridLayout_->setRowStretch(0, 1);
    gridLayout_->setRowStretch(1, 1);
    gridLayout_->setColumnStretch(0, 1);
    gridLayout_->setColumnStretch(1, 1);

    layout->addWidget(gridWidget);
}

QWidget* SharedViewport::createCTPanel(const QString& viewName,
                                        const QString& title,
                                        const QString& color)
{
    auto* widget = new QFrame();
    widget->setStyleSheet("background-color: #0d0d0d; border: 1px solid #1a1a1a;");

    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    // Colored title label
    auto* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setStyleSheet(QString(
        "color: %1;"
        "font-weight: bold;"
        "font-size: 10pt;"
        "padding: 3px 8px;"
        "background-color: #0d0d0d;"
        "border-bottom: 1px solid #1e1e1e;"
    ).arg(color));
    layout->addWidget(titleLabel);

    // Image canvas (BrushCanvas = QLabel + mouse events for paint/erase)
    auto* canvas = new BrushCanvas(viewName, widget);
    canvas->setAlignment(Qt::AlignCenter);
    canvas->setStyleSheet("background-color: #000000;");
    canvas->setMinimumSize(200, 180);
    imageCanvas_[viewName] = canvas;
    layout->addWidget(canvas, 1);

    // Connect scroll to slice change
    connect(canvas, &BrushCanvas::scrollSlice,
        [this](const QString& vn, int dir) {
            auto* sl = sliceSlider_.value(vn, nullptr);
            if (sl && sl->isEnabled()) {
                int newVal = std::clamp(sl->value() + dir, sl->minimum(), sl->maximum());
                sl->setValue(newVal);
            }
        });

    // Re-render slice when canvas is resized
    connect(canvas, &BrushCanvas::canvasResized,
        [this](const QString& vn) {
            if (imageData_ && currentSlice.contains(vn)) {
                displaySlice(vn, currentSlice[vn]);
            }
        });

    // Slider frame
    auto* sliderFrame = new QFrame();
    sliderFrame->setStyleSheet("background-color: #111111;");
    auto* sliderLayout = new QVBoxLayout(sliderFrame);
    sliderLayout->setContentsMargins(8, 2, 8, 4);
    sliderLayout->setSpacing(1);

    // Info label
    auto* infoLabel = new QLabel("Slice  0 / 0");
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: #888888; font-size: 8pt;");
    sliceInfoLabel_[viewName] = infoLabel;
    sliderLayout->addWidget(infoLabel);

    // Slider
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setMinimum(0);
    slider->setMaximum(0);
    slider->setValue(0);
    slider->setEnabled(false);
    slider->setStyleSheet(R"(
        QSlider::groove:horizontal {
            background: #2a2a2a; height: 4px; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #cccccc; width: 12px; height: 12px;
            border-radius: 6px; margin: -4px 0;
        }
        QSlider::sub-page:horizontal {
            background: #4db6ac; border-radius: 2px;
        }
    )");

    QString vn = viewName;
    connect(slider, &QSlider::valueChanged,
            [this, vn](int val) { onSliceChange(vn, val); });
    sliceSlider_[viewName] = slider;
    sliderLayout->addWidget(slider);

    layout->addWidget(sliderFrame);
    return widget;
}

QWidget* SharedViewport::create3DPanel()
{
    auto* outer = new QFrame();
    outer->setStyleSheet("background-color: #0d0d0d; border: 1px solid #1a1a1a;");

    auto* outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(1, 1, 1, 1);
    outerLayout->setSpacing(0);

    // Title bar
    auto* titleLabel = new QLabel(QString::fromUtf8("3D"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    titleLabel->setStyleSheet(R"(
        color: #7E57C2;
        font-weight: bold;
        font-size: 10pt;
        padding: 3px 8px;
        background-color: #0d0d0d;
        border-bottom: 1px solid #1e1e1e;
    )");
    outerLayout->addWidget(titleLabel);

    // Body: [left toolbar | VTK plotter]
    auto* body = new QFrame();
    body->setStyleSheet("background-color: #0d0d0d;");
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    // Left toolbar
    auto* toolbar = new QFrame();
    toolbar->setFixedWidth(44);
    toolbar->setStyleSheet(R"(
        QFrame {
            background-color: rgba(20, 20, 30, 200);
            border-right: 1px solid #2a2a3a;
        }
    )");
    auto* tbLayout = new QVBoxLayout(toolbar);
    tbLayout->setContentsMargins(4, 8, 4, 8);
    tbLayout->setSpacing(6);

    btnStyleActive_ = R"(
        QPushButton {
            background-color: #7E57C2;
            color: white;
            border: none;
            border-radius: 6px;
            font-size: 8pt;
            font-weight: bold;
            padding: 4px 2px;
        }
    )";
    btnStyleInactive_ = R"(
        QPushButton {
            background-color: rgba(60, 60, 80, 180);
            color: #aaaaaa;
            border: none;
            border-radius: 6px;
            font-size: 8pt;
            padding: 4px 2px;
        }
        QPushButton:hover {
            background-color: rgba(80, 80, 110, 200);
            color: white;
        }
    )";

    btnSeg_ = new QPushButton("Seg\n3D");
    btnSeg_->setFixedSize(36, 44);
    btnSeg_->setStyleSheet(btnStyleActive_);
    btnSeg_->setToolTip("Show Segmentation 3D");
    connect(btnSeg_, &QPushButton::clicked, [this]() { switch3DMode("segmentation"); });
    tbLayout->addWidget(btnSeg_);

    btnMat_ = new QPushButton("Mat\n3D");
    btnMat_->setFixedSize(36, 44);
    btnMat_->setStyleSheet(btnStyleInactive_);
    btnMat_->setToolTip("Show Material Property 3D");
    connect(btnMat_, &QPushButton::clicked, [this]() { switch3DMode("material"); });
    tbLayout->addWidget(btnMat_);

    btnMesh_ = new QPushButton("Mesh\n3D");
    btnMesh_->setFixedSize(36, 44);
    btnMesh_->setStyleSheet(btnStyleInactive_);
    btnMesh_->setToolTip("Show FEM Mesh 3D");
    connect(btnMesh_, &QPushButton::clicked, [this]() { switch3DMode("mesh"); });
    tbLayout->addWidget(btnMesh_);

    btnStress_ = new QPushButton("S\n3D");
    btnStress_->setFixedSize(36, 44);
    btnStress_->setStyleSheet(btnStyleInactive_);
    btnStress_->setToolTip("Show Stress 3D");
    connect(btnStress_, &QPushButton::clicked, [this]() { switch3DMode("stress"); });
    tbLayout->addWidget(btnStress_);

    tbLayout->addStretch();

    resetBtn_ = new QPushButton(QString::fromUtf8("\u21BA"));
    resetBtn_->setFixedSize(36, 28);
    resetBtn_->setStyleSheet(R"(
        QPushButton {
            background-color: rgba(40, 40, 60, 160);
            color: #888888;
            border: none;
            border-radius: 5px;
            font-size: 12pt;
        }
        QPushButton:hover { color: white; }
    )");
    resetBtn_->setToolTip("Reset camera");
    connect(resetBtn_, &QPushButton::clicked, this, &SharedViewport::reset3DCamera);
    tbLayout->addWidget(resetBtn_);

    bodyLayout->addWidget(toolbar);

    // VTK 3D widget
    vtk3DWidget_ = new QVTKOpenGLNativeWidget(body);
    bodyLayout->addWidget(vtk3DWidget_, 1);

    renderWindow3D_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderWindow3D_->SetMultiSamples(4);  // 4x MSAA 抗锯齿
    vtk3DWidget_->setRenderWindow(renderWindow3D_);

    renderer3D_ = vtkSmartPointer<vtkRenderer>::New();
    renderer3D_->SetBackground(0.05, 0.05, 0.05);    // #0d0d0d
    renderer3D_->SetBackground2(0.1, 0.1, 0.18);     // #1a1a2e
    renderer3D_->GradientBackgroundOn();
    renderWindow3D_->AddRenderer(renderer3D_);

    auto interactor = renderWindow3D_->GetInteractor();
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    interactor->SetInteractorStyle(style);

    outerLayout->addWidget(body, 1);
    return outer;
}

// ---- Render coalescing ----

void SharedViewport::scheduleViewRender(const QString& viewName)
{
    dirtyViews_.insert(viewName);
    if (!renderCoalesceTimer_->isActive())
        renderCoalesceTimer_->start();
}

void SharedViewport::flushDirtyViews()
{
    for (const auto& vn : dirtyViews_) {
        if (currentSlice.contains(vn))
            displaySlice(vn, currentSlice[vn]);
    }
    dirtyViews_.clear();
}

// ---- Slice change ----

void SharedViewport::onSliceChange(const QString& viewName, int val)
{
    currentSlice[viewName] = val;
    scheduleViewRender(viewName);
    Q_EMIT sliceChanged(viewName, val);
}

// ---- Data ----

void SharedViewport::setImageData(vtkImageData* imageData)
{
    if (!imageData) return;
    imageData_ = imageData;

    int* dims = imageData->GetDimensions();
    double* sp = imageData->GetSpacing();
    // 用 affine 矩阵（sform/qform）判断轴向 — 这是最可靠的方法
    // NIfTI RAS 约定：矩阵列 j = 体素轴 j 在世界坐标中的方向
    //   行 0 = R/L，行 1 = A/P，行 2 = S/I
    // 找到每个体素轴最对齐的解剖方向 → sagittal(R/L)、coronal(A/P)、axial(S/I)
    vtkMatrix3x3* dirMatrix = imageData->GetDirectionMatrix();
    bool usedAffine = false;

    if (dirMatrix) {
        // 贪心分配：每次取绝对值最大的 (worldDir, voxelAxis) 对
        bool taken[3] = {false, false, false};
        int axMap[3]  = {-1, -1, -1};
        for (int iter = 0; iter < 3; iter++) {
            double best = -1;
            int bw = -1, bv = -1;
            for (int wd = 0; wd < 3; wd++) {
                if (axMap[wd] >= 0) continue;
                for (int va = 0; va < 3; va++) {
                    if (taken[va]) continue;
                    double v = std::abs(dirMatrix->GetElement(wd, va));
                    if (v > best) { best = v; bw = wd; bv = va; }
                }
            }
            if (bw >= 0) { axMap[bw] = bv; taken[bv] = true; }
        }
        if (axMap[0] >= 0 && axMap[1] >= 0 && axMap[2] >= 0) {
            sagittalDim_ = axMap[0];   // 世界 R/L → 矢状（切片轴）
            coronalDim_  = axMap[1];   // 世界 A/P → 冠状（切片轴）
            axialDim_    = axMap[2];   // 世界 S/I → 轴位（切片轴）
            // 根据方向矩阵计算每个维度的翻转
            // 显示约定：从上往下(S→I)，从前往后(A→P)，从左往右(L→R)
            //   R/L 轴：正=递增→Right，Left 在 index 0 → 不翻转
            //           负=递增→Left，Right 在 index 0 → 翻转让 Left 在左
            //   A/P 轴：正=递增→Anterior → 翻转让 Anterior 在上方/左侧
            //   S/I 轴：正=递增→Superior → 翻转让 Superior 在上方
            double rlSign = dirMatrix->GetElement(0, sagittalDim_);
            double apSign = dirMatrix->GetElement(1, coronalDim_);
            double siSign = dirMatrix->GetElement(2, axialDim_);
            flipDim_[sagittalDim_] = (rlSign < 0);   // L→R：负号时翻转
            flipDim_[coronalDim_] = (apSign > 0);     // A→P：正号时翻转
            flipDim_[axialDim_]   = (siSign > 0);     // S→I：正号时翻转
            usedAffine = true;
        }
    }

    if (!usedAffine) {
        // 默认：标准 NIfTI RAS 约定
        flipDim_[0] = false; flipDim_[1] = true; flipDim_[2] = true;
        // 退而求其次：spacing 最大的轴 = 切片方向 = 轴位
        if (sp[0] >= sp[1] && sp[0] >= sp[2]) {
            axialDim_ = 0; sagittalDim_ = 1; coronalDim_ = 2;
        } else if (sp[1] > sp[0] && sp[1] >= sp[2]) {
            axialDim_ = 1; sagittalDim_ = 0; coronalDim_ = 2;
        } else {
            axialDim_ = 2; sagittalDim_ = 0; coronalDim_ = 1;
        }
    }

    // Setup sliders for each view axis
    struct ViewConfig { QString name; int dimIndex; };
    ViewConfig views[] = {
        {"axial",    axialDim_},
        {"sagittal", sagittalDim_},
        {"coronal",  coronalDim_},
    };

    for (auto& v : views) {
        int numSlices = dims[v.dimIndex];
        int midSlice = numSlices / 2;

        auto* slider = sliceSlider_.value(v.name, nullptr);
        if (slider) {
            slider->blockSignals(true);
            slider->setMinimum(0);
            slider->setMaximum(numSlices - 1);
            slider->setValue(midSlice);
            slider->setEnabled(true);
            slider->blockSignals(false);
        }
        currentSlice[v.name] = midSlice;
    }

    // Pick a sensible default Window/Level for CT bone visualization unless a
    // caller has already pinned a value via setWindowLevel(...).
    if (ctWindow_ <= 0) {
        resetWindowLevelAuto();
    }

    // Defer initial render until layout is finalized so canvas->size() is correct
    QTimer::singleShot(0, this, [this]() {
        for (const auto& vn : {"axial", "sagittal", "coronal"}) {
            QString name(vn);
            if (currentSlice.contains(name))
                displaySlice(name, currentSlice[name]);
        }
    });
}

// CT brightness/contrast controls — refresh every visible slice immediately.
void SharedViewport::setWindowLevel(double window, double level)
{
    if (window < 1.0) window = 1.0;
    ctWindow_ = window;
    ctLevel_  = level;
    if (!imageData_) return;
    for (const auto& vn : {"axial", "sagittal", "coronal"}) {
        QString name(vn);
        if (currentSlice.contains(name)) displaySlice(name, currentSlice[name]);
    }
}

void SharedViewport::resetWindowLevelAuto()
{
    if (!imageData_) {
        ctWindow_ = -1.0; ctLevel_ = 0.0;
        return;
    }
    double r[2]; imageData_->GetScalarRange(r);
    double rmin = r[0], rmax = r[1];
    // Heuristic: if the volume looks like signed CT HU (contains < -100), use a
    // bone-window default (W=2000, L=400). Otherwise span the full scalar range.
    if (rmin < -100) {
        ctWindow_ = 2000.0;
        ctLevel_  = 400.0;
    } else {
        ctWindow_ = (rmax - rmin > 1.0) ? (rmax - rmin) : 1.0;
        ctLevel_  = 0.5 * (rmin + rmax);
    }
}

void SharedViewport::setMaskData(vtkImageData* maskData)
{
    if (!maskData) return;
    maskData_ = maskData;
    int* dims = maskData->GetDimensions();
    // Re-render all current slices with mask overlay
    for (const auto& vn : {"axial", "sagittal", "coronal"}) {
        QString name(vn);
        if (currentSlice.contains(name)) {
            displaySlice(name, currentSlice[name]);
        }
    }
}

void SharedViewport::setMaskVisible(bool visible)
{
    maskVisible_ = visible;
    for (const auto& vn : {"axial", "sagittal", "coronal"})
        scheduleViewRender(QString(vn));
}

void SharedViewport::setMaskOpacity(double opacity)
{
    maskOpacity_ = std::clamp(opacity, 0.0, 1.0);
    for (const auto& vn : {"axial", "sagittal", "coronal"})
        scheduleViewRender(QString(vn));
}

void SharedViewport::setEVolume(vtkImageData* eVol)
{
    eVolume_ = eVol;
}

void SharedViewport::setEOverlayVisible(bool visible)
{
    eOverlayVisible_ = visible;
    for (const auto& vn : {"axial", "sagittal", "coronal"})
        scheduleViewRender(QString(vn));
}

void SharedViewport::setEOverlayOpacity(double opacity)
{
    eOverlayOpacity_ = std::clamp(opacity, 0.0, 1.0);
    if (eOverlayVisible_) {
        for (const auto& vn : {"axial", "sagittal", "coronal"})
            scheduleViewRender(QString(vn));
    }
}

// ANSYS-style colormap: t in [0,1] → (r,g,b) in [0,255]
// 0.0=#0000ff  0.2=#00ffff  0.4=#00ff00  0.6=#ffff00  0.8=#ff8000  1.0=#ff0000
static void jetColor(double t, int& r, int& g, int& b)
{
    t = std::clamp(t, 0.0, 1.0);
    // 6 stops: blue→cyan→green→yellow→orange→red
    struct { double pos; int r, g, b; } stops[] = {
        {0.0, 0,0,255}, {0.2, 0,255,255}, {0.4, 0,255,0},
        {0.6, 255,255,0}, {0.8, 255,128,0}, {1.0, 255,0,0}
    };
    int n = 6;
    for (int i = 0; i < n - 1; i++) {
        if (t <= stops[i+1].pos) {
            double f = (t - stops[i].pos) / (stops[i+1].pos - stops[i].pos);
            r = (int)(stops[i].r + f * (stops[i+1].r - stops[i].r));
            g = (int)(stops[i].g + f * (stops[i+1].g - stops[i].g));
            b = (int)(stops[i].b + f * (stops[i+1].b - stops[i].b));
            return;
        }
    }
    r = 255; g = 0; b = 0;
}

// ---- Vertebra color table (matches Python _VERTEBRA_COLORS) ----
static const int VERT_COLORS[][3] = {
    {255,0,0},   {0,255,0},   {0,0,255},   {255,255,0}, {0,255,255},
    {255,0,255}, {255,239,213},{0,0,205},   {205,133,63},{210,180,140},
    {102,205,170},{0,0,128},  {0,139,139}, {46,139,87}, {255,228,225},
    {106,90,205},{221,160,221},{233,150,122},{165,42,42},
    {255,250,250},{147,112,219},{218,112,214},{75,0,130},{255,182,193},
    {60,179,113},{255,235,205},
};
static const int NUM_VERT_COLORS = 26;

// Helper: read a scalar from vtkImageData at (ix,iy,iz)
static inline double readVoxel(vtkImageData* data, int ix, int iy, int iz, int* dims, int scalarType, void* ptr)
{
    int idx = iz * dims[0] * dims[1] + iy * dims[0] + ix;
    switch (scalarType) {
    case VTK_SHORT:          return static_cast<short*>(ptr)[idx];
    case VTK_UNSIGNED_SHORT: return static_cast<unsigned short*>(ptr)[idx];
    case VTK_FLOAT:          return static_cast<float*>(ptr)[idx];
    case VTK_DOUBLE:         return static_cast<double*>(ptr)[idx];
    case VTK_INT:            return static_cast<int*>(ptr)[idx];
    case VTK_UNSIGNED_CHAR:  return static_cast<unsigned char*>(ptr)[idx];
    case VTK_CHAR:           return static_cast<char*>(ptr)[idx];
    default:                 return data->GetScalarComponentAsDouble(ix, iy, iz, 0);
    }
}

// ---- Slice rendering with mask overlay ----

void SharedViewport::displaySlice(const QString& viewName, int sliceIdx)
{
    if (!imageData_) return;

    auto* canvas = imageCanvas_.value(viewName, nullptr);
    auto* info = sliceInfoLabel_.value(viewName, nullptr);
    if (!canvas) return;

    int* dims = imageData_->GetDimensions();

    // 根据存储的轴向分配确定各视图的维度
    // viewAxis = 切片轴，hAxis = 水平轴，vAxis = 垂直轴
    int viewAxis, hAxis, vAxis;
    if (viewName == "axial") {
        viewAxis = axialDim_; hAxis = sagittalDim_; vAxis = coronalDim_;
    } else if (viewName == "sagittal") {
        viewAxis = sagittalDim_; hAxis = coronalDim_; vAxis = axialDim_;
    } else {  // coronal
        viewAxis = coronalDim_; hAxis = sagittalDim_; vAxis = axialDim_;
    }
    int origH = dims[hAxis];
    int origW = dims[vAxis];
    int totalSlices = dims[viewAxis];
    sliceIdx = std::clamp(sliceIdx, 0, totalSlices - 1);

    // After rot90(k=1) on (origH, origW): display size = (origW, origH)
    // display_rows = origW, display_cols = origH
    int dispH = origW;  // number of rows in displayed image
    int dispW = origH;  // number of cols in displayed image

    double scalarRange[2];
    imageData_->GetScalarRange(scalarRange);
    // Window/Level: vmin = level - window/2, vmax = level + window/2.
    // ctWindow_ < 0 means "auto" — fall back to full scalar span.
    double vmin, vmax;
    if (ctWindow_ > 0) {
        vmin = ctLevel_ - 0.5 * ctWindow_;
        vmax = ctLevel_ + 0.5 * ctWindow_;
    } else {
        vmin = scalarRange[0];
        vmax = scalarRange[1];
    }
    double range = (vmax - vmin > 1.0) ? (vmax - vmin) : 1.0;

    void* ctPtr = imageData_->GetScalarPointer();
    int ctType = imageData_->GetScalarType();

    // Check if mask data matches dimensions
    bool hasMask = maskVisible_ && maskData_ != nullptr;
    int* maskDims = hasMask ? maskData_->GetDimensions() : nullptr;
    void* maskPtr = hasMask ? maskData_->GetScalarPointer() : nullptr;
    int maskType = hasMask ? maskData_->GetScalarType() : 0;

    if (hasMask && (maskDims[0] != dims[0] || maskDims[1] != dims[1] || maskDims[2] != dims[2])) {
        hasMask = false;
    }

    // E_volume overlay (jet colormap) — supports different dims via spatial mapping
    bool hasE = eOverlayVisible_ && eVolume_ != nullptr;
    int* eDims = hasE ? eVolume_->GetDimensions() : nullptr;
    void* ePtr = hasE ? eVolume_->GetScalarPointer() : nullptr;
    int eType = hasE ? eVolume_->GetScalarType() : 0;
    double eMin = 0, eMax = 1, eRange = 1;
    bool eSameDims = false;        // true if E dims match CT dims (fast path)
    double eOrigin[3] = {0,0,0}, eSpacing[3] = {1,1,1};
    if (hasE) {
        double eScalarRange[2];
        eVolume_->GetScalarRange(eScalarRange);
        eMin = eScalarRange[0];
        eMax = eScalarRange[1];
        eRange = eMax - eMin;
        if (eRange < 1e-8) eRange = 1.0;  // avoid divide by zero
        eSameDims = (eDims[0] == dims[0] && eDims[1] == dims[1] && eDims[2] == dims[2]);
        if (!eSameDims) {
            double* eo = eVolume_->GetOrigin();
            double* es = eVolume_->GetSpacing();
            eOrigin[0] = eo[0]; eOrigin[1] = eo[1]; eOrigin[2] = eo[2];
            eSpacing[0] = es[0]; eSpacing[1] = es[1]; eSpacing[2] = es[2];
        }
    }

    QImage img(dispW, dispH, QImage::Format_RGB888);
    double alpha = maskOpacity_;
    double invAlpha = 1.0 - alpha;
    double eAlpha = eOverlayOpacity_;
    double eInvAlpha = 1.0 - eAlpha;

    // 预计算 slice 轴的体素索引
    int sliceVoxel = flipDim_[viewAxis] ? (dims[viewAxis] - 1 - sliceIdx) : sliceIdx;

    // 使用 scanLine 直接写入像素（比 setPixel 快 10-100 倍）
    for (int row = 0; row < dispH; ++row) {
        uchar* scanLine = img.scanLine(row);
        int vRow = flipDim_[vAxis] ? (dims[vAxis] - 1 - row) : row;

        for (int col = 0; col < dispW; ++col) {
            int vCol = flipDim_[hAxis] ? (dims[hAxis] - 1 - col) : col;

            int voxIdx[3];
            voxIdx[hAxis]    = vCol;
            voxIdx[vAxis]    = vRow;
            voxIdx[viewAxis] = sliceVoxel;

            int ix = voxIdx[0], iy = voxIdx[1], iz = voxIdx[2];

            double val = readVoxel(imageData_, ix, iy, iz, dims, ctType, ctPtr);
            int gray = static_cast<int>(std::clamp((val - vmin) / range * 255.0, 0.0, 255.0));

            int r = gray, g = gray, b = gray;

            if (hasE) {
                double eVal = 0;
                if (eSameDims) {
                    eVal = readVoxel(eVolume_, ix, iy, iz, eDims, eType, ePtr);
                } else {
                    // Spatial mapping: CT voxel → world → E voxel
                    double* ctOrig = imageData_->GetOrigin();
                    double* ctSp   = imageData_->GetSpacing();
                    double wx = ctOrig[0] + ix * ctSp[0];
                    double wy = ctOrig[1] + iy * ctSp[1];
                    double wz = ctOrig[2] + iz * ctSp[2];
                    int ex = (int)((wx - eOrigin[0]) / eSpacing[0] + 0.5);
                    int ey = (int)((wy - eOrigin[1]) / eSpacing[1] + 0.5);
                    int ez = (int)((wz - eOrigin[2]) / eSpacing[2] + 0.5);
                    if (ex >= 0 && ex < eDims[0] && ey >= 0 && ey < eDims[1]
                        && ez >= 0 && ez < eDims[2])
                        eVal = readVoxel(eVolume_, ex, ey, ez, eDims, eType, ePtr);
                }
                if (std::isfinite(eVal) && std::abs(eVal) > 1e-6) {
                    double t = std::clamp((eVal - eMin) / eRange, 0.0, 1.0);
                    int jr, jg, jb;
                    jetColor(t, jr, jg, jb);
                    r = static_cast<int>(gray * eInvAlpha + jr * eAlpha);
                    g = static_cast<int>(gray * eInvAlpha + jg * eAlpha);
                    b = static_cast<int>(gray * eInvAlpha + jb * eAlpha);
                }
            } else if (hasMask) {
                int label = static_cast<int>(readVoxel(maskData_, ix, iy, iz, maskDims, maskType, maskPtr));
                if (label > 0) {
                    int ci = (label - 1) % NUM_VERT_COLORS;
                    r = static_cast<int>(gray * invAlpha + VERT_COLORS[ci][0] * alpha);
                    g = static_cast<int>(gray * invAlpha + VERT_COLORS[ci][1] * alpha);
                    b = static_cast<int>(gray * invAlpha + VERT_COLORS[ci][2] * alpha);
                }
            }

            // RGB888: 3 bytes per pixel
            scanLine[col * 3]     = static_cast<uchar>(r);
            scanLine[col * 3 + 1] = static_cast<uchar>(g);
            scanLine[col * 3 + 2] = static_cast<uchar>(b);
        }
    }

    QPixmap pixmap = QPixmap::fromImage(img);
    // Store source pixmap and apply zoom (preserves current zoom level)
    canvas->setSourcePixmap(pixmap);

    if (info) {
        info->setText(QString("Slice  %1 / %2").arg(sliceIdx + 1).arg(totalSlices));
    }
}

// ---- 3D vertebra rendering (marching cubes, per-label colors) ----

void SharedViewport::render3DVertebrae()
{
    if (!maskData_ || !renderer3D_) return;
    if (only3DMode_) return;

    renderer3D_->RemoveAllViewProps();

    int numVoxels = maskData_->GetNumberOfPoints();
    auto* scalars = maskData_->GetPointData()->GetScalars();
    if (!scalars) return;

    // Find unique labels
    std::set<int> labels;
    for (int i = 0; i < numVoxels; ++i) {
        int val = static_cast<int>(scalars->GetTuple1(i));
        if (val > 0) labels.insert(val);
    }
    if (labels.empty()) return;

    // Per-label vtkDiscreteMarchingCubes (no binary volume copy needed)
    for (int lbl : labels) {
        auto dmc = vtkSmartPointer<vtkDiscreteMarchingCubes>::New();
        dmc->SetInputData(maskData_);
        dmc->SetNumberOfContours(1);
        dmc->SetValue(0, lbl);
        dmc->ComputeNormalsOn();
        dmc->Update();

        if (dmc->GetOutput()->GetNumberOfPoints() == 0) continue;

        // Smooth (windowed sinc is faster than Laplacian for same quality)
        auto smoother = vtkSmartPointer<vtkWindowedSincPolyDataFilter>::New();
        smoother->SetInputConnection(dmc->GetOutputPort());
        smoother->SetNumberOfIterations(15);
        smoother->SetPassBand(0.1);
        smoother->BoundarySmoothingOn();
        smoother->NonManifoldSmoothingOn();
        smoother->NormalizeCoordinatesOn();
        smoother->Update();

        // Decimate to reduce triangle count
        auto decimator = vtkSmartPointer<vtkDecimatePro>::New();
        decimator->SetInputConnection(smoother->GetOutputPort());
        decimator->SetTargetReduction(0.3);  // reduce 30%
        decimator->PreserveTopologyOn();
        decimator->Update();

        // Normals for better lighting
        auto normals = vtkSmartPointer<vtkPolyDataNormals>::New();
        normals->SetInputConnection(decimator->GetOutputPort());
        normals->SetFeatureAngle(60.0);
        normals->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(normals->GetOutputPort());
        mapper->ScalarVisibilityOff();

        // LODActor: auto low-detail during interaction for smooth rotation
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);

        int ci = (lbl - 1) % NUM_VERT_COLORS;
        actor->GetProperty()->SetColor(
            VERT_COLORS[ci][0] / 255.0,
            VERT_COLORS[ci][1] / 255.0,
            VERT_COLORS[ci][2] / 255.0);
        actor->GetProperty()->SetOpacity(0.85);
        actor->GetProperty()->SetAmbient(0.2);
        actor->GetProperty()->SetDiffuse(0.8);
        actor->GetProperty()->SetSpecular(0.3);

        renderer3D_->AddActor(actor);
    }

    renderer3D_->ResetCamera();
    if (renderWindow3D_) renderWindow3D_->Render();

}

// ---- Brush API ----

void SharedViewport::enableBrush(std::function<void(const QString&, int, int)> strokeCb,
                                  std::function<void(const QString&)> releaseCb)
{
    for (auto it = imageCanvas_.begin(); it != imageCanvas_.end(); ++it) {
        auto* canvas = it.value();
        canvas->setBrushActive(true);
        // Disconnect any old connections
        disconnect(canvas, &BrushCanvas::brushStroke, nullptr, nullptr);
        disconnect(canvas, &BrushCanvas::brushReleased, nullptr, nullptr);
        if (strokeCb) {
            connect(canvas, &BrushCanvas::brushStroke, strokeCb);
        }
        if (releaseCb) {
            connect(canvas, &BrushCanvas::brushReleased, releaseCb);
        }
    }
}

void SharedViewport::disableBrush()
{
    for (auto it = imageCanvas_.begin(); it != imageCanvas_.end(); ++it) {
        auto* canvas = it.value();
        canvas->setBrushActive(false);
        disconnect(canvas, &BrushCanvas::brushStroke, nullptr, nullptr);
        disconnect(canvas, &BrushCanvas::brushReleased, nullptr, nullptr);
    }
}

// ---- Module management ----

void SharedViewport::setActiveModule(ModuleBase* module)
{
    activeModule_ = module;
}

// ---- Layout mode: 3D only vs 4 panels ----

void SharedViewport::setOnly3DMode(bool only3D)
{
    only3DMode_ = only3D;
    if (only3D) {
        coronalPanel_->setVisible(false);
        axialPanel_->setVisible(false);
        sagittalPanel_->setVisible(false);
        // Hide Seg/Mat 3D buttons to prevent overwriting module's 3D content
        btnSeg_->setVisible(false);
        btnMat_->setVisible(false);
        btnStress_->setVisible(false);
        // Move 3D panel to span the entire grid
        gridLayout_->removeWidget(panel3D_);
        gridLayout_->addWidget(panel3D_, 0, 0, 2, 2);
    } else {
        // Restore 4-panel layout
        gridLayout_->removeWidget(panel3D_);
        coronalPanel_->setVisible(true);
        axialPanel_->setVisible(true);
        sagittalPanel_->setVisible(true);
        btnSeg_->setVisible(true);
        btnMat_->setVisible(true);
        btnStress_->setVisible(true);
        gridLayout_->addWidget(coronalPanel_, 0, 0);
        gridLayout_->addWidget(axialPanel_, 0, 1);
        gridLayout_->addWidget(sagittalPanel_, 1, 0);
        gridLayout_->addWidget(panel3D_, 1, 1);
    }
}

// ---- 3D ----

vtkRenderer* SharedViewport::get3DRenderer() { return renderer3D_; }
vtkGenericOpenGLRenderWindow* SharedViewport::get3DRenderWindow() { return renderWindow3D_; }

CTSliceViewer* SharedViewport::getSliceViewer(const QString& /*viewName*/)
{
    // In the new Python-matching design, we use QLabel canvases, not CTSliceViewer.
    // This is kept for backward compat but returns nullptr.
    return nullptr;
}

void SharedViewport::renderAll()
{
    if (renderWindow3D_) renderWindow3D_->Render();
}

void SharedViewport::reset3DCamera()
{
    if (renderer3D_) {
        renderer3D_->ResetCamera();
        if (renderWindow3D_) renderWindow3D_->Render();
    }
}

void SharedViewport::switch3DMode(const QString& mode)
{
    if (mode3D_ == mode) return;
    forceSwitch3DMode(mode);
}

void SharedViewport::forceSwitch3DMode(const QString& mode)
{
    // Block mode switches when in 3D-only mode (e.g. Cobb Angle controls its own 3D)
    if (only3DMode_) return;

    mode3D_ = mode;

    // Update button styles
    btnSeg_->setStyleSheet(mode == "segmentation" ? btnStyleActive_ : btnStyleInactive_);
    btnMat_->setStyleSheet(mode == "material" ? btnStyleActive_ : btnStyleInactive_);
    btnMesh_->setStyleSheet(mode == "mesh" ? btnStyleActive_ : btnStyleInactive_);
    btnStress_->setStyleSheet(mode == "stress" ? btnStyleActive_ : btnStyleInactive_);

    // Clear current 3D scene completely
    if (renderer3D_) {
        renderer3D_->RemoveAllViewProps();
        if (renderWindow3D_) renderWindow3D_->Render();
    }

    // Notify modules to rebuild their 3D view
    Q_EMIT mode3DChanged(mode);
}
