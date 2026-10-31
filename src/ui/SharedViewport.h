/**
 * ============================================================================
 * SharedViewport - 四面板视口（对应 ui/shared_viewport.py）
 * ============================================================================
 */

#ifndef SPINEX_SHARED_VIEWPORT_H
#define SPINEX_SHARED_VIEWPORT_H

#include <QWidget>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QFrame>
#include <QMap>
#include <QSet>
#include <QTimer>

#include "BrushCanvas.h"
#include <functional>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkImageData.h>

class ModuleBase;
class CTSliceViewer;

class SharedViewport : public QWidget
{
    Q_OBJECT

public:
    explicit SharedViewport(QWidget* parent = nullptr);
    ~SharedViewport();

    // 设置活动模块
    void setActiveModule(ModuleBase* module);

    // CT 数据 — 设置后自动渲染切片到 QLabel canvases
    void setImageData(vtkImageData* imageData);
    void setMaskData(vtkImageData* maskData);

    // Mask overlay control
    void setMaskVisible(bool visible);
    void setMaskOpacity(double opacity);  // 0.0 - 1.0
    bool maskVisible() const { return maskVisible_; }
    double maskOpacity() const { return maskOpacity_; }

    // E_volume overlay (jet colormap)
    void setEVolume(vtkImageData* eVol);
    void setEOverlayVisible(bool visible);
    void setEOverlayOpacity(double opacity);
    vtkSmartPointer<vtkImageData> eVolume_;

    // 3D vertebra rendering from mask
    void render3DVertebrae();

    // Brush API (modules connect to these)
    void enableBrush(std::function<void(const QString&, int, int)> strokeCb,
                     std::function<void(const QString&)> releaseCb = nullptr);
    void disableBrush();

    // Access canvases for coordinate mapping
    BrushCanvas* getCanvas(const QString& viewName) { return imageCanvas_.value(viewName, nullptr); }

    // 3D 渲染器
    vtkRenderer* get3DRenderer();
    vtkGenericOpenGLRenderWindow* get3DRenderWindow();

    // 切面查看器访问（兼容旧接口）
    CTSliceViewer* getSliceViewer(const QString& viewName);

    // 刷新
    void renderAll();
    void reset3DCamera();

    // Slice state (shared across modules like Python)
    QMap<QString, int> currentSlice;

    // Render a 2D slice (public so modules can trigger redraw)
    void displaySlice(const QString& viewName, int sliceIdx);

    // ----- Window / Level (CT brightness + contrast) -----
    // window  = display range width  (smaller → more contrast)
    // level   = center HU value      (smaller → darker, larger → brighter)
    void setWindowLevel(double window, double level);
    double window() const { return ctWindow_; }
    double level()  const { return ctLevel_; }
    // Reset to "auto" derived from the loaded volume's scalar range.
    void resetWindowLevelAuto();

    // 3D mode switching
    QString current3DMode() const { return mode3D_; }
    void switch3DMode(const QString& mode);  // "segmentation" or "material"
    void forceSwitch3DMode(const QString& mode);  // Always clears and switches

    // Layout mode: show only 3D panel or all 4 panels
    void setOnly3DMode(bool only3D);
    bool isOnly3DMode() const { return only3DMode_; }

    // Axis mapping (which VTK dimension corresponds to which anatomical direction)
    int axialDim()    const { return axialDim_; }    // I-S (up-down)
    int sagittalDim() const { return sagittalDim_; }  // L-R (left-right)
    int coronalDim()  const { return coronalDim_; }   // A-P (front-back)
    const bool* flipDims() const { return flipDim_; }

Q_SIGNALS:
    void sliceChanged(const QString& viewName, int value);
    void mode3DChanged(const QString& mode);

private:
    void initUI();
    QWidget* createCTPanel(const QString& viewName,
                           const QString& title, const QString& color);
    QWidget* create3DPanel();

    void onSliceChange(const QString& viewName, int val);

    // CT panels (BrushCanvas = QLabel subclass with mouse events)
    QMap<QString, BrushCanvas*> imageCanvas_;
    QMap<QString, QSlider*> sliceSlider_;
    QMap<QString, QLabel*> sliceInfoLabel_;

public:
    // Stored image data for slice rendering (public for module access)
    vtkSmartPointer<vtkImageData> imageData_;
    vtkSmartPointer<vtkImageData> maskData_;
    // Track loaded file paths to avoid redundant disk reads on module switch
    QString loadedCTPath_;
    QString loadedMaskPath_;
private:
    bool maskVisible_ = true;
    double maskOpacity_ = 0.5;
    bool eOverlayVisible_ = false;
    double eOverlayOpacity_ = 0.5;

    // CT display window + level. -1024 / 0 selected as "auto", real values
    // get assigned in setImageData() based on scalar range.
    double ctWindow_ = -1.0;   // -1 means "auto from scalar range"
    double ctLevel_  = 0.0;

    // 轴向分配：哪个 VTK 维度对应哪个解剖方向
    // 默认：axial=Z(2), sagittal=X(0), coronal=Y(1)（标准 NIfTI RAS）
    // DICOM 转换后可能不同，由 spacing 自动检测
    int axialDim_    = 2;
    int sagittalDim_ = 0;
    int coronalDim_  = 1;
    // 每个体素维度的翻转标记（由方向矩阵自动计算）
    // flipDim_[d] = true 表示显示时需要反转维度 d
    bool flipDim_[3] = {false, false, true};

    // 3D view
    QVTKOpenGLNativeWidget* vtk3DWidget_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow3D_;
    vtkSmartPointer<vtkRenderer> renderer3D_;

    // 3D toolbar buttons
    QPushButton* btnSeg_;
    QPushButton* btnMat_;
    QPushButton* btnMesh_;
    QPushButton* btnStress_;
    QPushButton* resetBtn_;
    QString btnStyleActive_;
    QString btnStyleInactive_;

    ModuleBase* activeModule_;
    QString mode3D_ = "segmentation";  // "segmentation" or "material"
    bool only3DMode_ = false;

    // Panel widgets for layout switching
    QWidget* coronalPanel_ = nullptr;
    QWidget* axialPanel_ = nullptr;
    QWidget* sagittalPanel_ = nullptr;
    QWidget* panel3D_ = nullptr;
    QGridLayout* gridLayout_ = nullptr;

    // 渲染合并：多个属性变更合并为一次渲染（~60 FPS 上限）
    QTimer* renderCoalesceTimer_ = nullptr;
    QSet<QString> dirtyViews_;
    void scheduleViewRender(const QString& viewName);
    void flushDirtyViews();
};

#endif // SPINEX_SHARED_VIEWPORT_H
