/**
 * BCSelectionDialog - full rewrite with left panel layout
 * Modes: Lasso, Box+Slab, Plane+Slab, Single click, Spread
 */
#include "BCSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <vtkObjectFactory.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkCellData.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkImplicitPlaneRepresentation.h>
#include <vtkCommand.h>
#include <cmath>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

// ---- LassoOverlay ----
LassoOverlay::LassoOverlay(QWidget* p) : QWidget(p) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}
void LassoOverlay::clear() { points.clear(); update(); }
void LassoOverlay::paintEvent(QPaintEvent*) {
    if (points.size() < 2) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.moveTo(points[0]);
    for (int i = 1; i < points.size(); i++) path.lineTo(points[i]);
    path.closeSubpath();
    p.fillPath(path, QColor(100, 180, 255, 40));
    p.setPen(QPen(QColor(60, 130, 255), 2));
    p.drawPath(path);
}

// ---- BCPickInteractorStyle ----
vtkStandardNewMacro(BCPickInteractorStyle);
void BCPickInteractorStyle::OnLeftButtonDown() {
    if (pickMode && dialog) {
        leftDown_ = true; isDrag_ = false;
        int* p = GetInteractor()->GetEventPosition();
        startX_ = p[0]; startY_ = p[1];
        dialog->doDragStart(p[0], p[1]);
    } else vtkInteractorStyleTrackballCamera::OnLeftButtonDown();
}
void BCPickInteractorStyle::OnLeftButtonUp() {
    if (pickMode && dialog) {
        int* p = GetInteractor()->GetEventPosition();
        if (isDrag_) dialog->doDragEnd(startX_, startY_, p[0], p[1]);
        else dialog->doSinglePick(p[0], p[1]);
        leftDown_ = false; isDrag_ = false;
    } else vtkInteractorStyleTrackballCamera::OnLeftButtonUp();
}
void BCPickInteractorStyle::OnMouseMove() {
    if (pickMode && leftDown_ && dialog) {
        int* p = GetInteractor()->GetEventPosition();
        int dx = p[0]-startX_, dy = p[1]-startY_;
        if (!isDrag_ && dx*dx+dy*dy > 25) isDrag_ = true;
        if (isDrag_) dialog->doDragMove(p[0], p[1]);
    }
    vtkInteractorStyleTrackballCamera::OnMouseMove();
}
void BCPickInteractorStyle::OnRightButtonDown() {
    FindPokedRenderer(Interactor->GetEventPosition()[0], Interactor->GetEventPosition()[1]);
    if (CurrentRenderer) StartRotate();
}
void BCPickInteractorStyle::OnRightButtonUp() { EndRotate(); }

// ---- BCSelectionDialog ----
BCSelectionDialog::BCSelectionDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("BC Selection");
    resize(1200, 800);
    setMinimumSize(900, 600);
    initUI();
    initVTK();
}
BCSelectionDialog::~BCSelectionDialog() { hidePlaneWidget(); }

QString BCSelectionDialog::btnStyle(bool active, const QString& color) {
    if (active)
        return QString("QPushButton { padding:6px; font-weight:bold; border-radius:4px;"
                        "background-color:%1; color:white; }").arg(color);
    return "QPushButton { padding:6px; font-weight:bold; border-radius:4px;"
           "background-color:#e5e7eb; color:#374151; }"
           "QPushButton:hover { background-color:#d1d5db; }";
}

void BCSelectionDialog::initUI() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(6,6,6,6);
    mainLayout->setSpacing(6);

    // ---- Left panel ----
    auto* left = new QWidget();
    left->setFixedWidth(210);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0,0,0,0);
    lv->setSpacing(4);

    auto makeBtn = [](const QString& text) {
        auto* b = new QPushButton(text);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(30);
        return b;
    };

    btnRotate_ = makeBtn("Rotate");
    lv->addWidget(btnRotate_);
    lv->addSpacing(6);

    lv->addWidget(new QLabel("--- Select ---"));
    auto* grid1 = new QGridLayout();
    btnLassoBC_ = makeBtn("Lasso BC"); grid1->addWidget(btnLassoBC_, 0, 0);
    btnLassoLoad_ = makeBtn("Lasso Load"); grid1->addWidget(btnLassoLoad_, 0, 1);
    btnBoxBC_ = makeBtn("Box BC"); grid1->addWidget(btnBoxBC_, 1, 0);
    btnBoxLoad_ = makeBtn("Box Load"); grid1->addWidget(btnBoxLoad_, 1, 1);
    btnPlaneBC_ = makeBtn("Plane BC"); grid1->addWidget(btnPlaneBC_, 2, 0);
    btnPlaneLoad_ = makeBtn("Plane Load"); grid1->addWidget(btnPlaneLoad_, 2, 1);
    lv->addLayout(grid1);

    lv->addSpacing(6);
    lv->addWidget(new QLabel("Thickness (mm):"));
    thickSpin_ = new QDoubleSpinBox();
    thickSpin_->setRange(0.5, 30); thickSpin_->setValue(5.0);
    thickSpin_->setSuffix(" mm"); thickSpin_->setSingleStep(0.5);
    lv->addWidget(thickSpin_);

    connect(thickSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
        if (currentMode_ == ModePlaneBC || currentMode_ == ModePlaneLoad)
            applyPlaneSelection();
    });

    lv->addSpacing(6);
    lv->addWidget(new QLabel("--- Tools ---"));
    auto* grid2 = new QGridLayout();
    btnSpreadBC_ = makeBtn("Spread BC"); grid2->addWidget(btnSpreadBC_, 0, 0);
    btnSpreadLoad_ = makeBtn("Spread Load"); grid2->addWidget(btnSpreadLoad_, 0, 1);
    btnClearBC_ = makeBtn("Clear BC"); grid2->addWidget(btnClearBC_, 1, 0);
    btnClearLoad_ = makeBtn("Clear Load"); grid2->addWidget(btnClearLoad_, 1, 1);
    lv->addLayout(grid2);

    lv->addWidget(new QLabel("Spread layers:"));
    spreadSpin_ = new QSpinBox();
    spreadSpin_->setRange(1, 20); spreadSpin_->setValue(3);
    lv->addWidget(spreadSpin_);

    lv->addSpacing(10);
    lv->addWidget(new QLabel("Displacement (mm):"));
    uDispEdit_ = new QLineEdit("1.0");
    lv->addWidget(uDispEdit_);

    lv->addStretch();

    auto* okBtn = makeBtn("Confirm");
    okBtn->setStyleSheet("QPushButton{padding:8px;font-weight:bold;border-radius:4px;"
                          "background-color:#10b981;color:white;}");
    lv->addWidget(okBtn);
    auto* cancelBtn = makeBtn("Cancel");
    cancelBtn->setStyleSheet("QPushButton{padding:8px;border-radius:4px;"
                              "background-color:#e5e7eb;color:#374151;}");
    lv->addWidget(cancelBtn);

    mainLayout->addWidget(left);

    // ---- Right: VTK + status ----
    auto* right = new QVBoxLayout();
    vtkWidget_ = new QVTKOpenGLNativeWidget(this);
    right->addWidget(vtkWidget_, 1);
    lassoOverlay_ = new LassoOverlay(vtkWidget_);
    rubberBand_ = new QRubberBand(QRubberBand::Rectangle, vtkWidget_);

    statusLabel_ = new QLabel("Mode: Rotate  |  BC: 0  |  Load: 0");
    statusLabel_->setStyleSheet("padding:4px; background:#f3f4f6; border-radius:3px; font-size:9pt;");
    right->addWidget(statusLabel_);
    mainLayout->addLayout(right, 1);

    // Connections
    connect(btnRotate_, &QPushButton::clicked, this, [this]{ setSelectMode(ModeRotate); });
    connect(btnLassoBC_, &QPushButton::clicked, this, [this]{ setSelectMode(ModeLassoBC); });
    connect(btnLassoLoad_, &QPushButton::clicked, this, [this]{ setSelectMode(ModeLassoLoad); });
    connect(btnBoxBC_, &QPushButton::clicked, this, [this]{ setSelectMode(ModeBoxBC); });
    connect(btnBoxLoad_, &QPushButton::clicked, this, [this]{ setSelectMode(ModeBoxLoad); });
    connect(btnPlaneBC_, &QPushButton::clicked, this, [this]{ setSelectMode(ModePlaneBC); });
    connect(btnPlaneLoad_, &QPushButton::clicked, this, [this]{ setSelectMode(ModePlaneLoad); });
    connect(btnSpreadBC_, &QPushButton::clicked, this, [this]{ spreadSelection(1, spreadSpin_->value()); });
    connect(btnSpreadLoad_, &QPushButton::clicked, this, [this]{ spreadSelection(2, spreadSpin_->value()); });
    connect(btnClearBC_, &QPushButton::clicked, this, &BCSelectionDialog::clearBC);
    connect(btnClearLoad_, &QPushButton::clicked, this, &BCSelectionDialog::clearLoad);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    setSelectMode(ModeRotate);
}

void BCSelectionDialog::initVTK() {
    renderWindow_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    vtkWidget_->setRenderWindow(renderWindow_);
    renderer_ = vtkSmartPointer<vtkRenderer>::New();
    renderer_->SetBackground(0.15, 0.15, 0.2);
    renderWindow_->AddRenderer(renderer_);
    pickStyle_ = vtkSmartPointer<BCPickInteractorStyle>::New();
    pickStyle_->dialog = this; pickStyle_->pickMode = false;
    renderWindow_->GetInteractor()->SetInteractorStyle(pickStyle_);
    cellPicker_ = vtkSmartPointer<vtkCellPicker>::New();
    cellPicker_->SetTolerance(0.001);
}

QPoint BCSelectionDialog::vtkToQt(int vx, int vy) {
    int* sz = renderWindow_->GetSize();
    double dpr = vtkWidget_->devicePixelRatioF();
    return QPoint((int)(vx/dpr), (int)((sz[1]-vy)/dpr));
}

// ---- Set mesh ----
void BCSelectionDialog::setMesh(vtkUnstructuredGrid* mesh) {
    if (!mesh) return;
    mesh_ = vtkSmartPointer<vtkUnstructuredGrid>::New();
    mesh_->DeepCopy(mesh);
    vtkIdType n = mesh_->GetNumberOfCells();
    cellState_.resize(n, 0);
    cellCentroidsX_.resize(n); cellCentroidsY_.resize(n); cellCentroidsZ_.resize(n);
    for (vtkIdType ci = 0; ci < n; ci++) {
        auto* c = mesh_->GetCell(ci);
        double cx=0,cy=0,cz=0; int np = c->GetNumberOfPoints();
        for (int pi=0; pi<np; pi++) {
            double p[3]; mesh_->GetPoint(c->GetPointId(pi), p);
            cx+=p[0]; cy+=p[1]; cz+=p[2];
        }
        cellCentroidsX_[ci]=cx/np; cellCentroidsY_[ci]=cy/np; cellCentroidsZ_[ci]=cz/np;
    }
    buildAdjacency();
    cellLocator_ = vtkSmartPointer<vtkCellLocator>::New();
    cellLocator_->SetDataSet(mesh_); cellLocator_->BuildLocator();
    cellPicker_->AddLocator(cellLocator_);

    colorArray_ = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colorArray_->SetNumberOfComponents(3); colorArray_->SetNumberOfTuples(n);
    for (vtkIdType i=0; i<n; i++) colorArray_->SetTuple3(i, 200,200,200);
    mesh_->GetCellData()->SetScalars(colorArray_);

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(mesh_); mapper->SetScalarModeToUseCellData();
    mapper->SetColorModeToDirectScalars();
    meshActor_ = vtkSmartPointer<vtkActor>::New();
    meshActor_->SetMapper(mapper);
    meshActor_->GetProperty()->SetEdgeVisibility(true);
    meshActor_->GetProperty()->SetEdgeColor(0.3,0.3,0.3);
    meshActor_->GetProperty()->SetLineWidth(0.5);
    meshActor_->GetProperty()->SetAmbient(0.3);
    meshActor_->GetProperty()->SetDiffuse(0.7);
    renderer_->RemoveAllViewProps(); renderer_->AddActor(meshActor_);
    renderer_->ResetCamera(); renderWindow_->Render();
    updateStatusBar();
}

void BCSelectionDialog::buildAdjacency() {
    if (!mesh_) return;
    vtkIdType n = mesh_->GetNumberOfCells();
    adjacency_.resize(n);
    std::unordered_map<vtkIdType, std::vector<int>> n2c;
    for (vtkIdType ci=0; ci<n; ci++) {
        auto* c = mesh_->GetCell(ci);
        for (int pi=0; pi<c->GetNumberOfPoints(); pi++)
            n2c[c->GetPointId(pi)].push_back(ci);
    }
    for (vtkIdType ci=0; ci<n; ci++) {
        std::unordered_set<int> nb;
        auto* c = mesh_->GetCell(ci);
        for (int pi=0; pi<c->GetNumberOfPoints(); pi++)
            for (int nc : n2c[c->GetPointId(pi)])
                if (nc!=ci) nb.insert(nc);
        adjacency_[ci] = QVector<int>(nb.begin(), nb.end());
    }
    adjacencyBuilt_ = true;
}

// ---- Mode ----
void BCSelectionDialog::setSelectMode(SelectMode mode) {
    if ((currentMode_==ModePlaneBC||currentMode_==ModePlaneLoad) &&
        mode!=ModePlaneBC && mode!=ModePlaneLoad)
        hidePlaneWidget();

    currentMode_ = mode;
    bool isLassoOrBox = (mode==ModeLassoBC||mode==ModeLassoLoad||mode==ModeBoxBC||mode==ModeBoxLoad);
    if (pickStyle_) pickStyle_->pickMode = isLassoOrBox;

    // Update all button styles
    btnRotate_->setStyleSheet(btnStyle(mode==ModeRotate));
    btnLassoBC_->setStyleSheet(btnStyle(mode==ModeLassoBC, "#2563eb"));
    btnLassoLoad_->setStyleSheet(btnStyle(mode==ModeLassoLoad, "#dc2626"));
    btnBoxBC_->setStyleSheet(btnStyle(mode==ModeBoxBC, "#2563eb"));
    btnBoxLoad_->setStyleSheet(btnStyle(mode==ModeBoxLoad, "#dc2626"));
    btnPlaneBC_->setStyleSheet(btnStyle(mode==ModePlaneBC, "#2563eb"));
    btnPlaneLoad_->setStyleSheet(btnStyle(mode==ModePlaneLoad, "#dc2626"));

    vtkWidget_->setCursor(isLassoOrBox ? Qt::CrossCursor : Qt::ArrowCursor);
    if (mode==ModePlaneBC) showPlaneWidget(1);
    else if (mode==ModePlaneLoad) showPlaneWidget(2);
    updateStatusBar();
}

// ---- Single pick ----
void BCSelectionDialog::doSinglePick(int x, int y) {
    if (!mesh_||!cellPicker_||currentMode_==ModeRotate) return;
    cellPicker_->Pick(x, y, 0, renderer_);
    vtkIdType id = cellPicker_->GetCellId();
    if (id<0||id>=cellState_.size()) return;
    int tgt = 0;
    if (currentMode_==ModeLassoBC||currentMode_==ModeBoxBC) tgt=1;
    else if (currentMode_==ModeLassoLoad||currentMode_==ModeBoxLoad) tgt=2;
    if (tgt==0) return;
    cellState_[id] = (cellState_[id]==tgt) ? 0 : tgt;
    updateMeshColors(); renderWindow_->Render(); updateStatusBar();
}

// ---- Drag start/move/end — dispatches to lasso or box ----
void BCSelectionDialog::doDragStart(int x, int y) {
    if (currentMode_==ModeLassoBC||currentMode_==ModeLassoLoad) {
        lassoVTK_.clear(); lassoVTK_.append(QPoint(x,y));
        lassoOverlay_->setGeometry(vtkWidget_->rect());
        lassoOverlay_->clear();
        lassoOverlay_->points.append(vtkToQt(x,y));
        lassoOverlay_->raise(); lassoOverlay_->show();
    } else {
        QPoint qt = vtkToQt(x,y);
        rubberBandOrigin_ = qt;
        rubberBand_->setGeometry(QRect(qt, QSize()));
        rubberBand_->show();
    }
}
void BCSelectionDialog::doDragMove(int x, int y) {
    if (currentMode_==ModeLassoBC||currentMode_==ModeLassoLoad) {
        lassoVTK_.append(QPoint(x,y));
        lassoOverlay_->points.append(vtkToQt(x,y));
        lassoOverlay_->update();
    } else {
        rubberBand_->setGeometry(QRect(rubberBandOrigin_, vtkToQt(x,y)).normalized());
    }
}
void BCSelectionDialog::doDragEnd(int x0, int y0, int x1, int y1) {
    if (currentMode_==ModeLassoBC||currentMode_==ModeLassoLoad) {
        lassoOverlay_->clear(); lassoOverlay_->hide();
        if (lassoVTK_.size()>=3) lassoSelectCells();
        lassoVTK_.clear();
    } else {
        rubberBand_->hide();
        if (std::abs(x1-x0)>5||std::abs(y1-y0)>5)
            boxSlabSelect(x0, y0, x1, y1);
    }
}

// ---- Lasso select (CellPicker grid sampling) ----
bool BCSelectionDialog::pointInPolygon(int px, int py, const QVector<QPoint>& poly) {
    int n=poly.size(); if(n<3) return false;
    bool inside=false;
    for (int i=0,j=n-1; i<n; j=i++) {
        int yi=poly[i].y(),yj=poly[j].y(),xi=poly[i].x(),xj=poly[j].x();
        if (((yi>py)!=(yj>py)) && (px < (xj-xi)*(py-yi)/(double)(yj-yi)+xi))
            inside=!inside;
    }
    return inside;
}
void BCSelectionDialog::lassoSelectCells() {
    if (!mesh_||!renderer_) return;
    int tgt = (currentMode_==ModeLassoBC) ? 1 : 2;
    QVector<QPoint> poly = lassoVTK_;
    if (poly.size()>150) {
        QVector<QPoint> ds; double step=(double)poly.size()/150.0;
        for (double i=0; i<poly.size(); i+=step) ds.append(poly[(int)i]);
        poly = ds;
    }
    int minX=INT_MAX,maxX=INT_MIN,minY=INT_MAX,maxY=INT_MIN;
    for (auto& p:poly) { minX=std::min(minX,p.x()); maxX=std::max(maxX,p.x());
                          minY=std::min(minY,p.y()); maxY=std::max(maxY,p.y()); }
    std::set<vtkIdType> sel;
    for (int y=minY; y<=maxY; y+=2)
        for (int x=minX; x<=maxX; x+=2) {
            if (!pointInPolygon(x,y,poly)) continue;
            cellPicker_->Pick(x,y,0,renderer_);
            vtkIdType id=cellPicker_->GetCellId();
            if (id>=0) sel.insert(id);
        }
    for (vtkIdType id:sel) if(id<cellState_.size()) cellState_[id]=tgt;
    updateMeshColors(); renderWindow_->Render(); updateStatusBar();
}

// ---- Box + Slab ----
void BCSelectionDialog::boxSlabSelect(int x0, int y0, int x1, int y1) {
    if (!mesh_||!renderer_) return;
    int tgt = (currentMode_==ModeBoxBC) ? 1 : 2;
    int other = (tgt==1) ? 2 : 1;
    double thickness = thickSpin_->value();

    // Pick surface at box center
    int cx=(x0+x1)/2, cy=(y0+y1)/2;
    cellPicker_->Pick(cx, cy, 0, renderer_);
    if (cellPicker_->GetCellId()<0) return;
    double* pp = cellPicker_->GetPickPosition();
    double surfPt[3]={pp[0],pp[1],pp[2]};

    // Camera view direction
    vtkCamera* cam = renderer_->GetActiveCamera();
    double cp[3],cf[3]; cam->GetPosition(cp); cam->GetFocalPoint(cf);
    double vd[3]={cf[0]-cp[0],cf[1]-cp[1],cf[2]-cp[2]};
    double len=std::sqrt(vd[0]*vd[0]+vd[1]*vd[1]+vd[2]*vd[2]);
    if(len<1e-10) return;
    vd[0]/=len; vd[1]/=len; vd[2]/=len;

    // Unproject box corners onto plane at surfPt
    int corners[4][2]={{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
    double wc[4][3];
    for (int i=0; i<4; i++) {
        renderer_->SetDisplayPoint(corners[i][0],corners[i][1],0);
        renderer_->DisplayToWorld();
        double* w0=renderer_->GetWorldPoint();
        double near[3]={w0[0]/w0[3],w0[1]/w0[3],w0[2]/w0[3]};
        renderer_->SetDisplayPoint(corners[i][0],corners[i][1],1);
        renderer_->DisplayToWorld();
        double* w1=renderer_->GetWorldPoint();
        double far[3]={w1[0]/w1[3],w1[1]/w1[3],w1[2]/w1[3]};
        double ray[3]={far[0]-near[0],far[1]-near[1],far[2]-near[2]};
        double denom=ray[0]*vd[0]+ray[1]*vd[1]+ray[2]*vd[2];
        if(std::abs(denom)<1e-10) denom=1e-10;
        double t=((surfPt[0]-near[0])*vd[0]+(surfPt[1]-near[1])*vd[1]+(surfPt[2]-near[2])*vd[2])/denom;
        wc[i][0]=near[0]+t*ray[0]; wc[i][1]=near[1]+t*ray[1]; wc[i][2]=near[2]+t*ray[2];
    }

    double u[3]={wc[1][0]-wc[0][0],wc[1][1]-wc[0][1],wc[1][2]-wc[0][2]};
    double v[3]={wc[3][0]-wc[0][0],wc[3][1]-wc[0][1],wc[3][2]-wc[0][2]};
    double uL=u[0]*u[0]+u[1]*u[1]+u[2]*u[2];
    double vL=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
    if(uL<1e-10||vL<1e-10) return;

    vtkIdType n=cellState_.size();
    for (vtkIdType ci=0; ci<n; ci++) {
        double px=cellCentroidsX_[ci]-surfPt[0];
        double py=cellCentroidsY_[ci]-surfPt[1];
        double pz=cellCentroidsZ_[ci]-surfPt[2];
        double depth = px*vd[0]+py*vd[1]+pz*vd[2];
        if (depth<-0.5||depth>thickness) continue;
        double dx=cellCentroidsX_[ci]-wc[0][0];
        double dy=cellCentroidsY_[ci]-wc[0][1];
        double dz=cellCentroidsZ_[ci]-wc[0][2];
        double uc=(dx*u[0]+dy*u[1]+dz*u[2])/uL;
        double vc=(dx*v[0]+dy*v[1]+dz*v[2])/vL;
        if (uc>=0&&uc<=1&&vc>=0&&vc<=1&&cellState_[ci]!=other)
            cellState_[ci]=tgt;
    }
    updateMeshColors(); renderWindow_->Render(); updateStatusBar();
}

// ---- Plane select (draggable vtkImplicitPlaneWidget2) ----
void BCSelectionDialog::showPlaneWidget(int tgt) {
    if (!mesh_||!renderer_||!renderWindow_) return;
    planeTargetState_ = tgt;
    if (planeWidget_) planeWidget_->Off();
    planeWidget_ = vtkSmartPointer<vtkImplicitPlaneWidget2>::New();
    auto rep = vtkSmartPointer<vtkImplicitPlaneRepresentation>::New();
    double bounds[6]; mesh_->GetBounds(bounds);
    rep->SetPlaceFactor(1.2); rep->PlaceWidget(bounds);
    rep->SetNormal(0,1,0);
    double ctr[3]={(bounds[0]+bounds[1])/2,(bounds[2]+bounds[3])/2,(bounds[4]+bounds[5])/2};
    rep->SetOrigin(ctr);
    rep->OutlineTranslationOn();  // let user drag the box to reposition
    rep->SetEdgeColor(tgt==1?0.2:0.9, tgt==1?0.4:0.2, tgt==1?1.0:0.2);
    planeWidget_->SetRepresentation(rep);
    planeWidget_->SetInteractor(renderWindow_->GetInteractor());
    planeWidget_->AddObserver(vtkCommand::EndInteractionEvent,
        this, &BCSelectionDialog::onPlaneInteraction);
    planeWidget_->On();
}
void BCSelectionDialog::hidePlaneWidget() {
    if (planeWidget_) { planeWidget_->Off(); planeWidget_=nullptr; }
}
void BCSelectionDialog::onPlaneInteraction(vtkObject*, unsigned long, void*) {
    applyPlaneSelection();
}
void BCSelectionDialog::applyPlaneSelection() {
    if (!planeWidget_||!mesh_||planeTargetState_==0) return;
    auto* rep = dynamic_cast<vtkImplicitPlaneRepresentation*>(planeWidget_->GetRepresentation());
    if (!rep) return;
    double normal[3], origin[3];
    rep->GetNormal(normal); rep->GetOrigin(origin);
    int tgt = planeTargetState_;
    int other = (tgt==1)?2:1;
    double thickness = thickSpin_->value();

    // Get the widget's bounding box — user can drag this to limit selection area
    double widgetBounds[6];
    rep->GetWidgetBounds(widgetBounds);

    vtkIdType n = cellState_.size();
    for (vtkIdType ci=0; ci<n; ci++) {
        if (cellState_[ci]==tgt) cellState_[ci]=0;

        double cx = cellCentroidsX_[ci];
        double cy = cellCentroidsY_[ci];
        double cz = cellCentroidsZ_[ci];

        // Check: centroid inside the widget's bounding box?
        if (cx < widgetBounds[0] || cx > widgetBounds[1] ||
            cy < widgetBounds[2] || cy > widgetBounds[3] ||
            cz < widgetBounds[4] || cz > widgetBounds[5])
            continue;

        // Signed distance along plane normal
        double dist = (cx-origin[0])*normal[0]
                     +(cy-origin[1])*normal[1]
                     +(cz-origin[2])*normal[2];

        if (dist > 0 && dist < thickness && cellState_[ci] != other)
            cellState_[ci] = tgt;
    }
    updateMeshColors(); renderWindow_->Render(); updateStatusBar();
}

// ---- Spread ----
void BCSelectionDialog::spreadSelection(int tgt, int layers) {
    if (!adjacencyBuilt_||cellState_.isEmpty()) return;
    std::queue<int> q; QVector<int> d(cellState_.size(),-1);
    for (int i=0;i<cellState_.size();i++)
        if(cellState_[i]==tgt){q.push(i);d[i]=0;}
    while(!q.empty()){
        int ci=q.front();q.pop();
        if(d[ci]>=layers)continue;
        for(int ni:adjacency_[ci])
            if(d[ni]<0){d[ni]=d[ci]+1;cellState_[ni]=tgt;if(d[ni]<layers)q.push(ni);}
    }
    updateMeshColors(); renderWindow_->Render(); updateStatusBar();
}

// ---- Colors / Clear / Status / Results ----
void BCSelectionDialog::updateMeshColors() {
    if (!colorArray_||cellState_.isEmpty()) return;
    for (int i=0;i<cellState_.size();i++) {
        switch(cellState_[i]) {
            case 1: colorArray_->SetTuple3(i,50,100,240); break;
            case 2: colorArray_->SetTuple3(i,230,50,50); break;
            default: colorArray_->SetTuple3(i,200,200,200);
        }
    }
    colorArray_->Modified(); if(mesh_) mesh_->Modified();
}
void BCSelectionDialog::clearBC() {
    for(int i=0;i<cellState_.size();i++) if(cellState_[i]==1) cellState_[i]=0;
    updateMeshColors(); if(renderWindow_) renderWindow_->Render(); updateStatusBar();
}
void BCSelectionDialog::clearLoad() {
    for(int i=0;i<cellState_.size();i++) if(cellState_[i]==2) cellState_[i]=0;
    updateMeshColors(); if(renderWindow_) renderWindow_->Render(); updateStatusBar();
}
void BCSelectionDialog::updateStatusBar() {
    int nBC=0,nLoad=0;
    for(int s:cellState_){if(s==1)nBC++;else if(s==2)nLoad++;}
    QString m;
    switch(currentMode_){
        case ModeRotate: m="Rotate"; break;
        case ModeLassoBC: m="Lasso BC"; break;
        case ModeLassoLoad: m="Lasso Load"; break;
        case ModeBoxBC: m="Box BC"; break;
        case ModeBoxLoad: m="Box Load"; break;
        case ModePlaneBC: m="Plane BC"; break;
        case ModePlaneLoad: m="Plane Load"; break;
    }
    statusLabel_->setText(QString("Mode: %1  |  BC: %2  |  Load: %3  |  Total: %4")
        .arg(m).arg(nBC).arg(nLoad).arg(cellState_.size()));
}
QVector<float> BCSelectionDialog::getIsBC() const {
    QVector<float> r(cellState_.size(),0.f);
    for(int i=0;i<cellState_.size();i++) if(cellState_[i]==1) r[i]=1.f;
    return r;
}
QVector<float> BCSelectionDialog::getIsLoad() const {
    QVector<float> r(cellState_.size(),0.f);
    for(int i=0;i<cellState_.size();i++) if(cellState_[i]==2) r[i]=1.f;
    return r;
}
double BCSelectionDialog::getUDisp() const { return uDispEdit_->text().toDouble(); }
