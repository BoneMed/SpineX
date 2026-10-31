/**
 * ============================================================================
 * BCSelectionDialog - 3D Interactive BC/Load Selection
 *
 * Modes: Lasso (fine-tune), Box+Slab, Plane+Slab (draggable), Single click
 * Layout: buttons on left panel, 3D view on right
 * ============================================================================
 */

#ifndef SPINEX_BC_SELECTION_DIALOG_H
#define SPINEX_BC_SELECTION_DIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QRubberBand>
#include <QVector>
#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkSmartPointer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkActor.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>
#include <vtkCellPicker.h>
#include <vtkCellLocator.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkImplicitPlaneWidget2.h>

class BCSelectionDialog;

class LassoOverlay : public QWidget
{
public:
    explicit LassoOverlay(QWidget* parent);
    QVector<QPoint> points;
    void clear();
protected:
    void paintEvent(QPaintEvent* event) override;
};

class BCPickInteractorStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static BCPickInteractorStyle* New();
    vtkTypeMacro(BCPickInteractorStyle, vtkInteractorStyleTrackballCamera);
    BCSelectionDialog* dialog = nullptr;
    bool pickMode = false;  // lasso or box mode active
    void OnLeftButtonDown() override;
    void OnLeftButtonUp() override;
    void OnMouseMove() override;
    void OnRightButtonDown() override;
    void OnRightButtonUp() override;
private:
    bool leftDown_ = false;
    int startX_ = 0, startY_ = 0;
    bool isDrag_ = false;
};

class BCSelectionDialog : public QDialog
{
    Q_OBJECT
public:
    // Lasso=freeform, Box=rectangle+slab, Plane=interactive plane+slab
    enum SelectMode { ModeRotate=0, ModeLassoBC=1, ModeLassoLoad=2,
                      ModeBoxBC=3, ModeBoxLoad=4,
                      ModePlaneBC=5, ModePlaneLoad=6 };

    explicit BCSelectionDialog(QWidget* parent = nullptr);
    ~BCSelectionDialog();
    void setMesh(vtkUnstructuredGrid* mesh);

    // Called from interactor
    void doSinglePick(int x, int y);
    void doDragStart(int x, int y);
    void doDragMove(int x, int y);
    void doDragEnd(int x0, int y0, int x1, int y1);

    QVector<float> getIsBC() const;
    QVector<float> getIsLoad() const;
    double getUDisp() const;

private:
    void initUI();
    void initVTK();
    void updateMeshColors();
    void updateStatusBar();
    void setSelectMode(SelectMode mode);
    void clearBC();
    void clearLoad();
    void spreadSelection(int targetState, int layers);
    void buildAdjacency();
    QPoint vtkToQt(int vtkX, int vtkY);

    // Lasso
    void lassoSelectCells();
    static bool pointInPolygon(int px, int py, const QVector<QPoint>& poly);

    // Box + Slab
    void boxSlabSelect(int x0, int y0, int x1, int y1);

    // Plane
    void showPlaneWidget(int targetState);
    void hidePlaneWidget();
    void applyPlaneSelection();
    void onPlaneInteraction(vtkObject*, unsigned long, void*);

    // UI - left panel
    QPushButton* btnRotate_;
    QPushButton* btnLassoBC_;
    QPushButton* btnLassoLoad_;
    QPushButton* btnBoxBC_;
    QPushButton* btnBoxLoad_;
    QPushButton* btnPlaneBC_;
    QPushButton* btnPlaneLoad_;
    QPushButton* btnSpreadBC_;
    QPushButton* btnSpreadLoad_;
    QPushButton* btnClearBC_;
    QPushButton* btnClearLoad_;
    QDoubleSpinBox* thickSpin_;
    QSpinBox* spreadSpin_;
    QLineEdit* uDispEdit_;
    QLabel* statusLabel_;

    // Overlays
    QVTKOpenGLNativeWidget* vtkWidget_;
    LassoOverlay* lassoOverlay_;
    QRubberBand* rubberBand_;
    QPoint rubberBandOrigin_;

    // VTK
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    vtkSmartPointer<vtkRenderer> renderer_;
    vtkSmartPointer<vtkActor> meshActor_;
    vtkSmartPointer<vtkCellPicker> cellPicker_;
    vtkSmartPointer<vtkCellLocator> cellLocator_;
    vtkSmartPointer<vtkUnsignedCharArray> colorArray_;
    vtkSmartPointer<BCPickInteractorStyle> pickStyle_;
    vtkSmartPointer<vtkImplicitPlaneWidget2> planeWidget_;
    int planeTargetState_ = 0;

    // State
    SelectMode currentMode_ = ModeRotate;
    vtkSmartPointer<vtkUnstructuredGrid> mesh_;
    QVector<int> cellState_;
    QVector<QPoint> lassoVTK_;  // lasso polygon in VTK coords
    QVector<double> cellCentroidsX_, cellCentroidsY_, cellCentroidsZ_;
    QVector<QVector<int>> adjacency_;
    bool adjacencyBuilt_ = false;
    QString btnStyle(bool active, const QString& color = "#3b82f6");
};

#endif
