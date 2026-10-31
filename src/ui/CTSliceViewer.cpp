/**
 * ============================================================================
 * CTSliceViewer - CT 切片查看器（实现）
 * ============================================================================
 */

#include "CTSliceViewer.h"

#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyleImage.h>
#include <vtkCamera.h>

#include <QVBoxLayout>
#include <QHBoxLayout>

// ---- 方向名称的辅助函数 ----
// static 函数只在这个文件内可见（类似 Python 的模块级函数）
static QString orientationName(SliceOrientation o)
{
    switch (o) {
        case SliceOrientation::Axial:    return "轴位 (Axial)";
        case SliceOrientation::Sagittal: return "矢状 (Sagittal)";
        case SliceOrientation::Coronal:  return "冠状 (Coronal)";
    }
    return "";
}

// ---- 构造函数 ----
CTSliceViewer::CTSliceViewer(SliceOrientation orientation, QWidget* parent)
    : QWidget(parent)
    , orientation_(orientation)
{
    setupUI();
    setupVTK();
    sliceRenderer_.setOrientation(orientation);
}

CTSliceViewer::~CTSliceViewer() = default;

// ---- 设置 UI ----
void CTSliceViewer::setupUI()
{
    // 主布局
    auto* layout = new QVBoxLayout(this);  // new 分配堆内存（Qt 会自动释放子组件）
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // 信息标签（显示方向和切片号）
    infoLabel_ = new QLabel(orientationName(orientation_));
    infoLabel_->setStyleSheet(
        "color: #cccccc; background-color: #1a1a1a; padding: 2px 8px; "
        "font-size: 11px; font-weight: bold;"
    );
    infoLabel_->setFixedHeight(22);
    layout->addWidget(infoLabel_);

    // VTK 渲染窗口
    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(vtkWidget_, 1);      // stretch factor = 1，填充剩余空间

    // 底部：切片滑块
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(4, 0, 4, 0);

    sliceSlider_ = new QSlider(Qt::Horizontal);
    sliceSlider_->setMinimum(0);
    sliceSlider_->setMaximum(0);
    sliceSlider_->setValue(0);
    sliceSlider_->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 4px; }"
        "QSlider::handle:horizontal { background: #00aaff; width: 12px; "
        "margin: -4px 0; border-radius: 6px; }"
    );
    bottomLayout->addWidget(sliceSlider_);

    layout->addLayout(bottomLayout);

    // 连接信号和槽
    // connect() 是 Qt 的核心机制：当信号发出时，自动调用对应的槽函数
    // 类似 Python 的 slider.valueChanged.connect(self.on_slider_changed)
    connect(sliceSlider_, &QSlider::valueChanged,
            this, &CTSliceViewer::onSliderChanged);
}

// ---- 设置 VTK ----
void CTSliceViewer::setupVTK()
{
    // 创建 OpenGL 渲染窗口
    renderWindow_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    vtkWidget_->setRenderWindow(renderWindow_);

    // 添加切片渲染器
    renderWindow_->AddRenderer(sliceRenderer_.getRenderer());

    // 设置 2D 图像交互样式（只有平移和缩放，没有旋转）
    auto interactor = renderWindow_->GetInteractor();
    auto style = vtkSmartPointer<vtkInteractorStyleImage>::New();
    interactor->SetInteractorStyle(style);
}

// ---- 设置 CT 数据 ----
void CTSliceViewer::setImageData(vtkImageData* imageData)
{
    if (!imageData) return;

    sliceRenderer_.setImageData(imageData);

    // 更新滑块范围
    int minSlice, maxSlice;
    sliceRenderer_.getSliceRange(minSlice, maxSlice);
    sliceSlider_->setRange(minSlice, maxSlice);
    sliceSlider_->setValue(sliceRenderer_.getSliceIndex());

    // 更新信息标签
    infoLabel_->setText(QString("%1  [%2/%3]")
        .arg(orientationName(orientation_))
        .arg(sliceRenderer_.getSliceIndex())
        .arg(maxSlice));

    render();
}

// ---- 设置掩膜数据 ----
void CTSliceViewer::setMaskData(vtkImageData* maskData)
{
    maskOverlay_.setMaskData(maskData);
    // 将掩膜 Actor 添加到渲染器
    sliceRenderer_.getRenderer()->AddActor(maskOverlay_.getMaskActor());
    render();
}

// ---- 切片控制 ----
int CTSliceViewer::getSliceIndex() const
{
    return sliceRenderer_.getSliceIndex();
}

void CTSliceViewer::setSliceIndex(int index)
{
    sliceRenderer_.setSliceIndex(index);

    // 更新滑块（不触发信号，避免循环调用）
    sliceSlider_->blockSignals(true);      // 暂时阻断信号
    sliceSlider_->setValue(index);
    sliceSlider_->blockSignals(false);     // 恢复信号

    // 更新标签
    int minSlice, maxSlice;
    sliceRenderer_.getSliceRange(minSlice, maxSlice);
    infoLabel_->setText(QString("%1  [%2/%3]")
        .arg(orientationName(orientation_))
        .arg(index)
        .arg(maxSlice));

    render();
}

SliceOrientation CTSliceViewer::getOrientation() const { return orientation_; }

// ---- 窗宽窗位 ----
void CTSliceViewer::setWindowLevel(double window, double level)
{
    sliceRenderer_.setWindowLevel(window, level);
    render();
}

// ---- 获取渲染器 ----
vtkRenderer* CTSliceViewer::getRenderer()
{
    return sliceRenderer_.getRenderer();
}

// ---- 刷新渲染 ----
void CTSliceViewer::render()
{
    if (renderWindow_) {
        renderWindow_->Render();
    }
}

// ---- 滑块改变 ----
void CTSliceViewer::onSliderChanged(int value)
{
    sliceRenderer_.setSliceIndex(value);

    int minSlice, maxSlice;
    sliceRenderer_.getSliceRange(minSlice, maxSlice);
    infoLabel_->setText(QString("%1  [%2/%3]")
        .arg(orientationName(orientation_))
        .arg(value)
        .arg(maxSlice));

    render();

    // 发出信号通知其他组件
    Q_EMIT sliceChanged(orientation_, value);
}
