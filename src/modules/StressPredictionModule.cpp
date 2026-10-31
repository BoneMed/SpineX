/**
 * ============================================================================
 * StressPredictionModule - Strain/Stress Distribution Prediction (impl)
 * Uses Hybrid DualEncoder (DualEncoderUNet_Sparse) via Python bridge
 * ============================================================================
 */

// pybind11 must come before Qt headers
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#ifdef slots
#undef slots
#endif

#include "StressPredictionModule.h"
#include "ModulusEstimationModule.h"
#include "app/SpineXApp.h"
#include "bridge/PythonRuntime.h"
#include "io/NiftiIO.h"
#include "bridge/InferenceWorker.h"
#include "bridge/VolumeExchange.h"
#include "ui/SharedViewport.h"
#include "ui/BCSelectionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QFont>
#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QProcess>
#include <QProgressDialog>
#include <QApplication>
#include <QCoreApplication>

#include <vtkPointData.h>
#include <vtkFloatArray.h>
#include <vtkCellData.h>
#include <vtkThreshold.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkImplicitPlaneWidget2.h>
#include <vtkImplicitPlaneRepresentation.h>
#include <vtkRenderWindow.h>
#include <vtkCamera.h>
#include <vtkCommand.h>

#include <cmath>
#include <algorithm>

StressPredictionModule::StressPredictionModule(QWidget* parent)
    : ModuleBase("Strain Prediction", parent)
{
    initJetLUT();
    initUI();
}

StressPredictionModule::~StressPredictionModule()
{
    if (planeWidget_) {
        planeWidget_->Off();
        planeWidget_ = nullptr;
    }
}

// ============================================================
// Jet LUT
// ============================================================
// ANSYS-style colormap: blue→cyan→green→yellow→orange→red
void StressPredictionModule::initJetLUT()
{
    jetLUT_ = vtkSmartPointer<vtkLookupTable>::New();
    jetLUT_->SetNumberOfTableValues(256);
    jetLUT_->Build();

    // 6 color stops matching ANSYS
    struct { double pos; double r, g, b; } stops[] = {
        {0.0, 0,0,1}, {0.2, 0,1,1}, {0.4, 0,1,0},
        {0.6, 1,1,0}, {0.8, 1,0.5,0}, {1.0, 1,0,0}
    };

    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0;
        double r = 1, g = 0, b = 0;
        for (int s = 0; s < 5; s++) {
            if (t <= stops[s+1].pos) {
                double f = (t - stops[s].pos) / (stops[s+1].pos - stops[s].pos);
                r = stops[s].r + f * (stops[s+1].r - stops[s].r);
                g = stops[s].g + f * (stops[s+1].g - stops[s].g);
                b = stops[s].b + f * (stops[s+1].b - stops[s].b);
                break;
            }
        }
        jetLUT_->SetTableValue(i, r, g, b, 1.0);
    }
}

// ============================================================
// UI
// ============================================================
void StressPredictionModule::initUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(createLeftPanel());
}

QWidget* StressPredictionModule::createLeftPanel()
{
    auto* panel = new QWidget();
    auto* outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    outerLayout->addWidget(makeModuleHeader("Strain Prediction"));

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; }");

    auto* content = new QWidget();
    content->setStyleSheet("background-color: #f4f6f8;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(10);

    // Device info
    QString deviceStr = "CPU";
#ifdef __has_include
    deviceStr = "CUDA (auto-detect)";
#endif
    auto* tip = new QLabel(QString("  Model: Hybrid DualEncoder"));
    tip->setStyleSheet(
        "background-color: #eaf4f8; color: #2c5f6f; padding: 7px 10px;"
        "border-radius: 4px; border-left: 3px solid #2c5f6f; font-size: 9pt;");
    layout->addWidget(tip);

    layout->addWidget(createOperationsGroup());
    layout->addWidget(createDataStatusGroup());
    layout->addWidget(createDisplayGroup());
    layout->addWidget(createStressStatsGroup());

    // Log
    auto* infoGroup = new QGroupBox("LOG");
    auto* infoLayout = new QVBoxLayout(infoGroup);
    logText_ = new QTextEdit();
    logText_->setReadOnly(true);
    logText_->setMaximumHeight(200);
    logText_->setStyleSheet("QTextEdit { font-family: 'Courier New'; font-size: 9pt; }");
    infoLayout->addWidget(logText_);
    layout->addWidget(infoGroup);

    layout->addStretch();
    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea);
    return panel;
}

QGroupBox* StressPredictionModule::createOperationsGroup()
{
    auto* group = new QGroupBox("OPERATIONS");
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(6);

    QString btnStyle =
        "QPushButton { padding: 8px; font-weight: bold; border-radius: 4px; }";

    bcSelectBtn_ = new QPushButton("BC Selection (from Mesh)");
    bcSelectBtn_->setStyleSheet(btnStyle +
        "QPushButton { background-color: #7c3aed; color: white; }"
        "QPushButton:hover { background-color: #6d28d9; }");
    bcSelectBtn_->setFocusPolicy(Qt::NoFocus);
    connect(bcSelectBtn_, &QPushButton::clicked, this, &StressPredictionModule::openBCSelection);
    layout->addWidget(bcSelectBtn_);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #d1d5db;");
    layout->addWidget(sep);

    loadNpzBtn_ = new QPushButton("Load NPZ (Manual)");
    loadNpzBtn_->setStyleSheet(btnStyle +
        "QPushButton { background-color: #6b7280; color: white; }"
        "QPushButton:hover { background-color: #4b5563; }");
    loadNpzBtn_->setFocusPolicy(Qt::NoFocus);
    connect(loadNpzBtn_, &QPushButton::clicked, this, &StressPredictionModule::loadNPZ);
    layout->addWidget(loadNpzBtn_);

    predictBtn_ = new QPushButton("Start Prediction");
    predictBtn_->setStyleSheet(btnStyle +
        "QPushButton { background-color: #3b82f6; color: white; }"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:disabled { background-color: #9ca3af; }");
    predictBtn_->setFocusPolicy(Qt::NoFocus);
    predictBtn_->setEnabled(false);
    connect(predictBtn_, &QPushButton::clicked, this, &StressPredictionModule::startPrediction);
    layout->addWidget(predictBtn_);

    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, 0);  // indeterminate
    progressBar_->setVisible(false);
    progressBar_->setMaximumHeight(6);
    progressBar_->setStyleSheet("QProgressBar { border: none; background: #e5e7eb; }"
        "QProgressBar::chunk { background: #3b82f6; }");
    layout->addWidget(progressBar_);

    return group;
}

QGroupBox* StressPredictionModule::createDataStatusGroup()
{
    auto* group = new QGroupBox("DATA STATUS");
    auto* layout = new QVBoxLayout(group);

    statusLabel_ = new QLabel();
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(
        "padding: 10px; background-color: #f8f9fa; border: 2px solid #dee2e6;"
        "border-radius: 5px; color: #666; font-size: 10pt;");
    layout->addWidget(statusLabel_);
    updateStatusDisplay();

    return group;
}

QGroupBox* StressPredictionModule::createDisplayGroup()
{
    auto* group = new QGroupBox("DISPLAY");
    auto* layout = new QVBoxLayout(group);

    overlayCheckbox_ = new QCheckBox("Show Strain Overlay on 2D Slices");
    overlayCheckbox_->setEnabled(false);
    connect(overlayCheckbox_, &QCheckBox::toggled,
            this, &StressPredictionModule::toggleStressOverlay);
    layout->addWidget(overlayCheckbox_);

    auto* alphaRow = new QHBoxLayout();
    alphaRow->addWidget(new QLabel("Opacity:"));
    overlayAlphaSlider_ = new QSlider(Qt::Horizontal);
    overlayAlphaSlider_->setRange(0, 100);
    overlayAlphaSlider_->setValue(50);
    connect(overlayAlphaSlider_, &QSlider::valueChanged,
            this, &StressPredictionModule::updateOverlayAlpha);
    alphaRow->addWidget(overlayAlphaSlider_);
    alphaValLbl_ = new QLabel("50%");
    alphaValLbl_->setFixedWidth(40);
    alphaRow->addWidget(alphaValLbl_);
    layout->addLayout(alphaRow);

    return group;
}

QGroupBox* StressPredictionModule::createStressStatsGroup()
{
    auto* group = new QGroupBox("STRAIN STATISTICS");
    auto* layout = new QVBoxLayout(group);

    statsText_ = new QTextEdit();
    statsText_->setReadOnly(true);
    statsText_->setMaximumHeight(120);
    statsText_->setStyleSheet(
        "QTextEdit { font-family: 'Courier New'; font-size: 9pt; }");
    statsText_->setText("No prediction results yet.");
    layout->addWidget(statsText_);

    return group;
}

// ============================================================
// BC Selection
// ============================================================
void StressPredictionModule::openBCSelection()
{
    if (!app_) return;

    // Get mesh data from ModulusEstimation module
    auto* modMod = dynamic_cast<ModulusEstimationModule*>(
        app_->moduleInstance("modulus_estimation"));
    if (!modMod || modMod->meshResults().isEmpty()) {
        QMessageBox::warning(this, "BC Selection",
            "No mesh data available.\n"
            "Please run Meshing in BMD Analysis first.");
        return;
    }

    // Merge all label meshes into one unstructured grid
    auto mergedMesh = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto mergedPts = vtkSmartPointer<vtkPoints>::New();
    vtkIdType ptOffset = 0;

    // Also store per-element centroid and E value
    std::vector<double> centroids_x, centroids_y, centroids_z;
    std::vector<double> eValues;

    auto* eVol = modMod->eVolume();

    for (auto& md : modMod->meshResults()) {
        if (!md.grid || md.grid->GetNumberOfCells() == 0) continue;
        auto* pts = md.grid->GetPoints();
        vtkIdType nPts = pts->GetNumberOfPoints();

        for (vtkIdType i = 0; i < nPts; i++) {
            double p[3];
            pts->GetPoint(i, p);
            mergedPts->InsertNextPoint(p);
        }

        for (vtkIdType ci = 0; ci < md.grid->GetNumberOfCells(); ci++) {
            auto* cell = md.grid->GetCell(ci);
            vtkIdType nCellPts = cell->GetNumberOfPoints();
            std::vector<vtkIdType> ids(nCellPts);
            for (vtkIdType pi = 0; pi < nCellPts; pi++) {
                ids[pi] = cell->GetPointId(pi) + ptOffset;
            }
            mergedMesh->InsertNextCell(cell->GetCellType(), nCellPts, ids.data());

            // Compute centroid
            double cx = 0, cy = 0, cz = 0;
            for (vtkIdType pi = 0; pi < nCellPts; pi++) {
                double p[3];
                pts->GetPoint(cell->GetPointId(pi), p);
                cx += p[0]; cy += p[1]; cz += p[2];
            }
            cx /= nCellPts; cy /= nCellPts; cz /= nCellPts;
            centroids_x.push_back(cx);
            centroids_y.push_back(cy);
            centroids_z.push_back(cz);

            // Sample E from eVolume at centroid
            double E = 100.0;  // default
            if (eVol) {
                int ijk[3];
                double pcoords[3];
                double pt[3] = {cx, cy, cz};
                int inBounds = eVol->ComputeStructuredCoordinates(pt, ijk, pcoords);
                if (inBounds) {
                    E = eVol->GetScalarComponentAsDouble(ijk[0], ijk[1], ijk[2], 0);
                    if (E < 1.0) E = 1.0;
                }
            }
            eValues.push_back(E);
        }

        ptOffset += nPts;
    }

    mergedMesh->SetPoints(mergedPts);
    int nElems = mergedMesh->GetNumberOfCells();
    appendLog(QString("Merged mesh: %1 elements, %2 nodes")
        .arg(nElems).arg(mergedPts->GetNumberOfPoints()));

    if (nElems == 0) {
        QMessageBox::warning(this, "BC Selection", "Merged mesh has no elements.");
        return;
    }

    // Open BC Selection dialog
    BCSelectionDialog dlg(this);
    dlg.setMesh(mergedMesh);

    if (dlg.exec() != QDialog::Accepted) {
        appendLog("BC Selection cancelled.");
        return;
    }

    auto isBC = dlg.getIsBC();
    auto isLoad = dlg.getIsLoad();
    double uDisp = dlg.getUDisp();

    int nBC = 0, nLoad = 0;
    for (float v : isBC) if (v > 0.5f) nBC++;
    for (float v : isLoad) if (v > 0.5f) nLoad++;

    if (nBC == 0 || nLoad == 0) {
        QMessageBox::warning(this, "BC Selection",
            QString("BC: %1 elements, Load: %2 elements\n"
                    "Both BC and Load must be selected.").arg(nBC).arg(nLoad));
        return;
    }

    nBCElements_ = nBC;
    nLoadElements_ = nLoad;
    uDisp_ = uDisp;
    hasBCData_ = true;
    appendLog(QString("BC: %1, Load: %2, u_disp: %3 mm")
        .arg(nBC).arg(nLoad).arg(uDisp));

    // Save NPZ via Python (numpy)
    // Build NPZ: centroids(N,3), E(N,), is_bc(N,), is_load(N,), u_disp
    QString outDir = QDir::tempPath() + "/spinex_stress_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(outDir);
    npzPath_ = outDir + "/element_data.npz";

    // Write via Python numpy
    {
        if (!PythonRuntime::instance().isInitialized()) {
            QMessageBox::critical(this, "Error", "Python not initialized.");
            return;
        }

        // Import pybind11 in this scope
        namespace py = pybind11;
        py::gil_scoped_acquire acquire;

        try {
            py::module_ np = py::module_::import("numpy");

            // Build numpy arrays
            auto pyCentroids = py::array_t<float>({(int)centroids_x.size(), 3});
            auto cBuf = pyCentroids.mutable_unchecked<2>();
            for (size_t i = 0; i < centroids_x.size(); i++) {
                cBuf(i, 0) = (float)centroids_x[i];
                cBuf(i, 1) = (float)centroids_y[i];
                cBuf(i, 2) = (float)centroids_z[i];
            }

            auto pyE = py::array_t<float>(eValues.size());
            auto eBuf = pyE.mutable_unchecked<1>();
            for (size_t i = 0; i < eValues.size(); i++)
                eBuf(i) = (float)eValues[i];

            auto pyBC = py::array_t<float>(isBC.size());
            auto bcBuf = pyBC.mutable_unchecked<1>();
            for (int i = 0; i < isBC.size(); i++)
                bcBuf(i) = isBC[i];

            auto pyLoad = py::array_t<float>(isLoad.size());
            auto ldBuf = pyLoad.mutable_unchecked<1>();
            for (int i = 0; i < isLoad.size(); i++)
                ldBuf(i) = isLoad[i];

            auto pyUDisp = py::array_t<float>(1);
            pyUDisp.mutable_at(0) = (float)uDisp;

            np.attr("savez_compressed")(
                npzPath_.toStdString(),
                py::arg("centroids") = pyCentroids,
                py::arg("E") = pyE,
                py::arg("is_bc") = pyBC,
                py::arg("is_load") = pyLoad,
                py::arg("u_disp") = pyUDisp
            );

            appendLog(QString("NPZ saved: %1").arg(npzPath_));
            hasNpzData_ = true;
            predictBtn_->setEnabled(true);
            updateStatusDisplay();

        } catch (py::error_already_set& e) {
            appendLog(QString("Python error: %1").arg(e.what()));
            QMessageBox::critical(this, "Error",
                QString("Failed to save NPZ:\n%1").arg(e.what()));
        }
    }
}

// ============================================================
// Load NPZ
// ============================================================
void StressPredictionModule::loadNPZ()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load Element NPZ Data", QString(),
        "NumPy files (*.npz);;All files (*)");
    if (path.isEmpty()) return;

    npzPath_ = path;
    hasNpzData_ = true;
    hasResult_ = false;

    predictBtn_->setEnabled(true);
    updateStatusDisplay();

    appendLog(QString("Loaded NPZ: %1").arg(path));
}

// ============================================================
// Start Prediction (QProcess subprocess, like segmentation)
// ============================================================
void StressPredictionModule::startPrediction()
{
    if (!hasNpzData_) {
        QMessageBox::warning(this, "Prediction",
            "No element data loaded.\nPlease run BC Selection or load NPZ first.");
        return;
    }

    if (!PythonRuntime::instance().isInitialized()) {
        QMessageBox::critical(this, "Error", "Python runtime not initialized.");
        return;
    }

    appendLog("==================================================");
    appendLog("Starting strain prediction...");

    // Output directory
    QString outDir = QDir::tempPath() + "/spinex_stress_result_" +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(outDir);

    // Progress dialog (modal, like segmentation)
    auto* progress = new QProgressDialog("Predicting...", "Cancel", 0, 100, this);
    progress->setWindowTitle("Strain Prediction");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    progress->show();
    QApplication::processEvents();

    // Python exe
    QString pythonExe = QString::fromStdString(PythonRuntime::instance().pythonExecutable());

    // Script path
    QString scriptPath = QCoreApplication::applicationDirPath() + "/python/run_stress.py";
    if (!QFile::exists(scriptPath)) {
        QString srcDir = QString::fromStdString(PythonRuntime::projectSourceDir());
        if (!srcDir.isEmpty())
            scriptPath = srcDir + "/python/run_stress.py";
    }

    // Launch subprocess
    auto* process = new QProcess(this);
    auto* outputPath = new QString();
    process->setProcessChannelMode(QProcess::MergedChannels);

    // Read stdout: parse PROGRESS: protocol (show simple message)
    connect(process, &QProcess::readyReadStandardOutput,
        [this, process, progress, outputPath]() {
            for (const QString& line :
                 QString::fromUtf8(process->readAll()).split('\n')) {
                QString t = line.trimmed();
                if (t.startsWith("PROGRESS:")) {
                    QStringList p = t.mid(9).split(':', Qt::KeepEmptyParts);
                    if (p.size() >= 2) {
                        int pct = p[0].toInt();
                        progress->setValue(pct);
                        progress->setLabelText("Predicting...");
                    }
                } else if (t.startsWith("OUTPUT:")) {
                    *outputPath = t.mid(7).trimmed();
                } else if (t.startsWith("ERROR:")) {
                    appendLog(QString("ERROR: %1").arg(t.mid(6)));
                } else if (!t.isEmpty()) {
                    appendLog(t);
                }
            }
        });

    // Process finished
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, process, progress, outDir, outputPath](int exitCode, QProcess::ExitStatus status) {
            progress->setValue(100);
            progress->close();
            progress->deleteLater();

            if (exitCode != 0 || status != QProcess::NormalExit) {
                appendLog(QString("Prediction failed (exit code %1)").arg(exitCode));
                QMessageBox::critical(this, "Prediction Failed",
                    QString("Prediction process exited with code %1.\n"
                            "Check the log for details.").arg(exitCode));
                delete outputPath;
                process->deleteLater();
                predictBtn_->setEnabled(true);
                bcSelectBtn_->setEnabled(true);
                loadNpzBtn_->setEnabled(true);
                return;
            }

            // Load result NPZ via Python numpy
            QString resultPath = *outputPath;
            if (resultPath.isEmpty())
                resultPath = outDir + "/stress_result.npz";

            appendLog(QString("Loading result: %1").arg(resultPath));

            if (!QFile::exists(resultPath)) {
                appendLog("ERROR: result file not found");
                QMessageBox::critical(this, "Error", "Result NPZ file not found.");
                delete outputPath;
                process->deleteLater();
                predictBtn_->setEnabled(true);
                bcSelectBtn_->setEnabled(true);
                loadNpzBtn_->setEnabled(true);
                return;
            }

            // Load result NPZ and build vtkImageData
            {
                namespace py = pybind11;
                py::gil_scoped_acquire acquire;
                try {
                    py::module_ np = py::module_::import("numpy");
                    py::object data = np.attr("load")(resultPath.toStdString());
                    py::array_t<float> stressArr = data["stress_mpa"].cast<py::array_t<float>>();
                    py::array_t<float> bboxMinArr = data["bbox_min"].cast<py::array_t<float>>();
                    py::array_t<float> bboxMaxArr = data["bbox_max"].cast<py::array_t<float>>();

                    auto bMin = bboxMinArr.unchecked<1>();
                    auto bMax = bboxMaxArr.unchecked<1>();

                    double spacing[3], origin[3];
                    for (int i = 0; i < 3; i++) {
                        origin[i] = (double)bMin(i);
                        double ext = (double)(bMax(i) - bMin(i));
                        spacing[i] = ext / gridRes_;
                        if (spacing[i] < 1e-8) spacing[i] = 1.0;
                    }

                    stressVolume_ = VolumeExchange::numpyToVtkImage(stressArr, spacing, origin);
                    if (stressVolume_ && stressVolume_->GetPointData()->GetScalars())
                        stressVolume_->GetPointData()->GetScalars()->SetName("Stress");

                    onPredictionFinished();
                } catch (py::error_already_set& e) {
                    appendLog(QString("Python error: %1").arg(e.what()));
                    QMessageBox::critical(this, "Error", e.what());
                }
            }

            delete outputPath;
            process->deleteLater();
        });

    // Disable buttons
    predictBtn_->setEnabled(false);
    bcSelectBtn_->setEnabled(false);
    loadNpzBtn_->setEnabled(false);

    // Start process
    QStringList args;
    args << scriptPath << npzPath_ << outDir
         << QString::number(gridRes_) << "128";
    appendLog(QString("Running: %1 %2").arg(pythonExe, args.join(" ")));
    process->start(pythonExe, args);
}

void StressPredictionModule::onPredictionFinished()
{
    hasResult_ = true;
    progressBar_->setVisible(false);
    predictBtn_->setEnabled(true);
    bcSelectBtn_->setEnabled(true);
    loadNpzBtn_->setEnabled(true);
    overlayCheckbox_->setEnabled(true);

    // Get stress range
    if (stressVolume_) {
        auto* scalars = stressVolume_->GetPointData()->GetScalars();
        if (scalars) {
            double range[2];
            scalars->GetRange(range);
            stressMin_ = range[0];
            stressMax_ = range[1];
        }
    }

    updateStatusDisplay();
    updateStressStats();

    appendLog("==================================================");
    appendLog(QString("Prediction complete! Strain range: [%1, %2] MPa")
        .arg(stressMin_, 0, 'f', 4).arg(stressMax_, 0, 'f', 4));

    // Save stress volume as NIfTI for project persistence
    if (app_ && stressVolume_) {
        QString stressNiiPath = QDir::tempPath() + "/spinex_stress_vol_" +
            QString::number(QDateTime::currentMSecsSinceEpoch()) + ".nii.gz";
        NiftiIO::writeNifti(stressVolume_, stressNiiPath.toStdString());
        app_->currentProject["stress_volume_path"] = stressNiiPath;
        app_->currentProject["has_stress"] = true;
        app_->currentProject["stress_min"] = stressMin_;
        app_->currentProject["stress_max"] = stressMax_;
        if (hasBCData_) {
            app_->currentProject["bc_count"] = nBCElements_;
            app_->currentProject["load_count"] = nLoadElements_;
            app_->currentProject["u_disp"] = uDisp_;
        }
        if (!npzPath_.isEmpty())
            app_->currentProject["element_npz_path"] = npzPath_;
        appendLog(QString("Stress volume saved: %1").arg(stressNiiPath));
    }

    // Resample stress volume to CT dimensions for 2D overlay (same as BMD approach)
    if (app_ && app_->sharedViewport() && stressVolume_) {
        auto* viewport = app_->sharedViewport();
        auto* ctData = viewport->imageData_.Get();
        if (ctData) {
            int ctDims[3];
            ctData->GetDimensions(ctDims);

            // Use mask data for the physical transform (mesh was generated from mask,
            // which has the NIfTI sform DirectionMatrix)
            auto* refData = viewport->maskData_.Get();
            if (!refData) refData = ctData;

            // Create overlay with same dims/spacing/origin/direction as CT
            auto stressOverlay = vtkSmartPointer<vtkImageData>::New();
            stressOverlay->SetDimensions(ctDims);
            stressOverlay->SetSpacing(ctData->GetSpacing());
            stressOverlay->SetOrigin(ctData->GetOrigin());

            vtkIdType totalVoxels = (vtkIdType)ctDims[0] * ctDims[1] * ctDims[2];
            auto sArr = vtkSmartPointer<vtkFloatArray>::New();
            sArr->SetNumberOfTuples(totalVoxels);
            sArr->SetName("Stress");
            sArr->Fill(0.0f);
            stressOverlay->GetPointData()->SetScalars(sArr);
            float* sRaw = sArr->GetPointer(0);

            // Stress volume spatial params (simple grid, no direction matrix)
            double* eOrig = stressVolume_->GetOrigin();
            double* eSp = stressVolume_->GetSpacing();
            int* eDims = stressVolume_->GetDimensions();
            auto* eScalars = stressVolume_->GetPointData()->GetScalars();

            // Use refData (mask)'s full transform: handles DirectionMatrix + origin + spacing
            // refData->TransformContinuousIndexToPhysicalPoint(ijk, world)
            // maps voxel (ix,iy,iz) → world (wx,wy,wz) including rotation from sform
            int nSampled = 0;
            for (int iz = 0; iz < ctDims[2]; iz++) {
                for (int iy = 0; iy < ctDims[1]; iy++) {
                    for (int ix = 0; ix < ctDims[0]; ix++) {
                        // CT voxel → physical world (uses DirectionMatrix!)
                        double ijk[3] = {(double)ix, (double)iy, (double)iz};
                        double world[3];
                        refData->TransformContinuousIndexToPhysicalPoint(ijk, world);

                        // Physical world → stress volume voxel (simple, no direction)
                        int ex = (int)((world[0] - eOrig[0]) / eSp[0] + 0.5);
                        int ey = (int)((world[1] - eOrig[1]) / eSp[1] + 0.5);
                        int ez = (int)((world[2] - eOrig[2]) / eSp[2] + 0.5);
                        if (ex < 0 || ex >= eDims[0] || ey < 0 || ey >= eDims[1]
                            || ez < 0 || ez >= eDims[2]) continue;

                        vtkIdType eIdx = ex + ey * eDims[0] + ez * (vtkIdType)eDims[0] * eDims[1];
                        double val = eScalars->GetTuple1(eIdx);
                        if (std::abs(val) > 1e-8) {
                            vtkIdType cIdx = ix + iy * ctDims[0] + iz * (vtkIdType)ctDims[0] * ctDims[1];
                            sRaw[cIdx] = (float)val;
                            nSampled++;
                        }
                    }
                }
            }

            appendLog(QString("Resample: %1 voxels mapped (CT %2x%3x%4 → stress %5x%6x%7)")
                .arg(nSampled).arg(ctDims[0]).arg(ctDims[1]).arg(ctDims[2])
                .arg(eDims[0]).arg(eDims[1]).arg(eDims[2]));

            if (nSampled > 0) {
                viewport->setEVolume(stressOverlay);
                viewport->setEOverlayVisible(true);
                viewport->setEOverlayOpacity(overlayAlpha_);
                overlayCheckbox_->setChecked(true);
                viewport->renderAll();
            } else {
                appendLog("WARNING: 0 voxels mapped — check NIfTI sform/direction");
            }
        }
    }

    // Switch to stress 3D mode
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->forceSwitch3DMode("stress");
    }
}

void StressPredictionModule::onPredictionError(const QString& err)
{
    progressBar_->setVisible(false);
    predictBtn_->setEnabled(true);
    loadNpzBtn_->setEnabled(true);

    appendLog(QString("ERROR: %1").arg(err));
    QMessageBox::critical(this, "Prediction Error", err);
}

// ============================================================
// Display
// ============================================================
void StressPredictionModule::toggleStressOverlay(bool checked)
{
    showStressOverlay_ = checked;
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setEOverlayVisible(checked);
        app_->sharedViewport()->renderAll();
    }
}

void StressPredictionModule::updateOverlayAlpha(int val)
{
    overlayAlpha_ = val / 100.0;
    alphaValLbl_->setText(QString("%1%").arg(val));
    if (app_ && app_->sharedViewport()) {
        app_->sharedViewport()->setEOverlayOpacity(overlayAlpha_);
        app_->sharedViewport()->renderAll();
    }
}

// ============================================================
// 3D Stress View — uses mesh tets (fast) + stress value sampling
// Same approach as BMD: small mesh, not 256³ voxel grid
// ============================================================
void StressPredictionModule::build3DStressView()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();
    if (viewport->current3DMode() != "stress") return;

    auto* renderer = viewport->get3DRenderer();
    if (!renderer) return;

    if (!stressVolume_ || !hasResult_) return;

    // Get mesh data from ModulusEstimation module (same tets as meshing)
    auto* modMod = dynamic_cast<ModulusEstimationModule*>(
        app_->moduleInstance("modulus_estimation"));
    if (!modMod || modMod->meshResults().isEmpty()) {
        appendLog("3D stress view: no mesh data (run meshing first).");
        return;
    }

    // Merge all label meshes and sample stress at each cell centroid
    auto mergedGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto mergedPts = vtkSmartPointer<vtkPoints>::New();
    auto stressArr = vtkSmartPointer<vtkFloatArray>::New();
    stressArr->SetName("Stress");
    stressArr->SetNumberOfComponents(1);

    double* eOrig = stressVolume_->GetOrigin();
    double* eSp = stressVolume_->GetSpacing();
    int* eDims = stressVolume_->GetDimensions();
    auto* eScalars = stressVolume_->GetPointData()->GetScalars();

    vtkIdType ptOffset = 0;
    for (auto& md : modMod->meshResults()) {
        if (!md.grid || md.grid->GetNumberOfCells() == 0) continue;
        auto* pts = md.grid->GetPoints();
        for (vtkIdType i = 0; i < pts->GetNumberOfPoints(); i++) {
            double p[3];
            pts->GetPoint(i, p);
            mergedPts->InsertNextPoint(p);
        }
        for (vtkIdType ci = 0; ci < md.grid->GetNumberOfCells(); ci++) {
            auto* cell = md.grid->GetCell(ci);
            int np = cell->GetNumberOfPoints();
            std::vector<vtkIdType> ids(np);
            for (int pi = 0; pi < np; pi++)
                ids[pi] = cell->GetPointId(pi) + ptOffset;
            mergedGrid->InsertNextCell(cell->GetCellType(), np, ids.data());

            // Sample stress at centroid
            double cx = 0, cy = 0, cz = 0;
            for (int pi = 0; pi < np; pi++) {
                double p[3];
                pts->GetPoint(cell->GetPointId(pi), p);
                cx += p[0]; cy += p[1]; cz += p[2];
            }
            cx /= np; cy /= np; cz /= np;
            int ex = (int)((cx - eOrig[0]) / eSp[0] + 0.5);
            int ey = (int)((cy - eOrig[1]) / eSp[1] + 0.5);
            int ez = (int)((cz - eOrig[2]) / eSp[2] + 0.5);
            float sVal = 0;
            if (ex >= 0 && ex < eDims[0] && ey >= 0 && ey < eDims[1] && ez >= 0 && ez < eDims[2]) {
                vtkIdType idx = ex + ey * eDims[0] + ez * (vtkIdType)eDims[0] * eDims[1];
                sVal = (float)eScalars->GetTuple1(idx);
            }
            stressArr->InsertNextValue(sVal);
        }
        ptOffset += pts->GetNumberOfPoints();
    }

    mergedGrid->SetPoints(mergedPts);
    mergedGrid->GetCellData()->SetScalars(stressArr);

    if (mergedGrid->GetNumberOfCells() == 0) {
        appendLog("3D stress view: no mesh cells.");
        return;
    }

    stressGrid_ = mergedGrid;

    // Set up LUT range
    double lo = stressMin_, hi = stressMax_;
    if (std::abs(lo) < 1e-6 && std::abs(hi) < 1e-6) { lo = -10; hi = 10; }
    jetLUT_->SetTableRange(lo, hi);

    // Clear old actors
    renderer->RemoveAllViewProps();

    // Clip plane
    if (!clipPlane_) {
        clipPlane_ = vtkSmartPointer<vtkPlane>::New();
    }

    double center[3];
    stressGrid_->GetCenter(center);
    clipPlane_->SetOrigin(center);
    clipPlane_->SetNormal(clipNormal_);
    clipOrigin_[0] = center[0];
    clipOrigin_[1] = center[1];
    clipOrigin_[2] = center[2];

    // Clip
    auto clipper = vtkSmartPointer<vtkClipDataSet>::New();
    clipper->SetInputData(stressGrid_);
    clipper->SetClipFunction(clipPlane_);
    clipper->InsideOutOn();
    clipper->Update();

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputConnection(clipper->GetOutputPort());
    mapper->SetScalarModeToUseCellData();
    mapper->SetLookupTable(jetLUT_);
    mapper->SetScalarRange(lo, hi);

    clipActor_ = vtkSmartPointer<vtkActor>::New();
    clipActor_->SetMapper(mapper);
    renderer->AddActor(clipActor_);

    // Scalar bar
    if (!scalarBar_) {
        scalarBar_ = vtkSmartPointer<vtkScalarBarActor>::New();
    }
    scalarBar_->SetLookupTable(jetLUT_);
    scalarBar_->SetTitle("S3 (MPa)");
    scalarBar_->SetNumberOfLabels(5);
    scalarBar_->SetWidth(0.08);
    scalarBar_->SetHeight(0.6);
    scalarBar_->SetPosition(0.9, 0.2);
    scalarBar_->GetTitleTextProperty()->SetFontSize(12);
    scalarBar_->GetLabelTextProperty()->SetFontSize(10);
    renderer->AddActor2D(scalarBar_);

    // Plane widget
    setupClipPlaneWidget();

    renderer->ResetCamera();
    auto* rw = viewport->get3DRenderWindow();
    if (rw) rw->Render();

    appendLog("3D stress clip view built.");
}

void StressPredictionModule::setupClipPlaneWidget()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (!rw || !renderer || !stressGrid_) return;

    if (planeWidget_) {
        planeWidget_->Off();
        planeWidget_ = nullptr;
    }

    planeWidget_ = vtkSmartPointer<vtkImplicitPlaneWidget2>::New();
    auto rep = vtkSmartPointer<vtkImplicitPlaneRepresentation>::New();

    double bounds[6];
    stressGrid_->GetBounds(bounds);
    rep->SetPlaceFactor(1.0);
    rep->PlaceWidget(bounds);
    rep->SetNormal(clipNormal_);
    rep->SetOrigin(clipOrigin_);
    rep->SetEdgeColor(0.2, 0.2, 0.2);
    rep->OutlineTranslationOff();

    planeWidget_->SetRepresentation(rep);
    planeWidget_->SetInteractor(rw->GetInteractor());

    planeWidget_->AddObserver(vtkCommand::InteractionEvent,
        this, &StressPredictionModule::onClipPlaneChanged);

    planeWidget_->On();
}

void StressPredictionModule::onClipPlaneChanged(vtkObject*, unsigned long, void*)
{
    if (!planeWidget_) return;
    auto* rep = dynamic_cast<vtkImplicitPlaneRepresentation*>(
        planeWidget_->GetRepresentation());
    if (!rep) return;

    double n[3], o[3];
    rep->GetNormal(n);
    rep->GetOrigin(o);
    doStressClip(n[0], n[1], n[2], o[0], o[1], o[2]);
}

void StressPredictionModule::doStressClip(double nx, double ny, double nz,
                                           double ox, double oy, double oz)
{
    if (!clipPlane_ || !stressGrid_ || !app_ || !app_->sharedViewport()) return;

    clipPlane_->SetNormal(nx, ny, nz);
    clipPlane_->SetOrigin(ox, oy, oz);
    clipNormal_[0] = nx; clipNormal_[1] = ny; clipNormal_[2] = nz;
    clipOrigin_[0] = ox; clipOrigin_[1] = oy; clipOrigin_[2] = oz;

    auto* renderer = app_->sharedViewport()->get3DRenderer();
    if (clipActor_) renderer->RemoveActor(clipActor_);

    double lo = stressMin_, hi = stressMax_;

    auto clipper = vtkSmartPointer<vtkClipDataSet>::New();
    clipper->SetInputData(stressGrid_);
    clipper->SetClipFunction(clipPlane_);
    clipper->InsideOutOn();
    clipper->Update();

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputConnection(clipper->GetOutputPort());
    mapper->SetScalarModeToUseCellData();
    mapper->SetLookupTable(jetLUT_);
    mapper->SetScalarRange(lo, hi);

    clipActor_ = vtkSmartPointer<vtkActor>::New();
    clipActor_->SetMapper(mapper);
    renderer->AddActor(clipActor_);

    auto* rw = app_->sharedViewport()->get3DRenderWindow();
    if (rw) rw->Render();
}

// ============================================================
// Status / Stats
// ============================================================
void StressPredictionModule::updateStatusDisplay()
{
    QString status;

    if (hasBCData_) {
        status += QString::fromUtf8("\u2705 BC: %1 elements, Load: %2 elements, u=%3 mm\n")
            .arg(nBCElements_).arg(nLoadElements_).arg(uDisp_, 0, 'f', 2);
    }

    if (hasNpzData_) {
        status += QString::fromUtf8("\u2705 Element data ready\n");
        QFileInfo fi(npzPath_);
        status += QString("   File: %1\n").arg(fi.fileName());
    } else {
        status += QString::fromUtf8("\u274C No element data\n");
    }

    if (hasResult_) {
        status += QString::fromUtf8("\u2705 Prediction complete\n");
        status += QString("   Strain: [%1, %2] MPa")
            .arg(stressMin_, 0, 'f', 4).arg(stressMax_, 0, 'f', 4);
    } else {
        status += QString::fromUtf8("\u23F3 Waiting for prediction...");
    }

    statusLabel_->setText(status);
}

void StressPredictionModule::updateStressStats()
{
    if (!hasResult_ || !stressVolume_) {
        statsText_->setText("No prediction results yet.");
        return;
    }

    auto* scalars = stressVolume_->GetPointData()->GetScalars();
    if (!scalars) return;

    int nVoxels = scalars->GetNumberOfTuples();
    int nOccupied = 0;
    double sum = 0, sumSq = 0;
    double vmin = 1e30, vmax = -1e30;

    for (vtkIdType i = 0; i < nVoxels; i++) {
        double v = scalars->GetTuple1(i);
        if (std::abs(v) < 1e-8) continue;
        nOccupied++;
        sum += v;
        sumSq += v * v;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }

    if (nOccupied == 0) {
        statsText_->setText("No valid stress voxels.");
        return;
    }

    double mean = sum / nOccupied;
    double stddev = std::sqrt(sumSq / nOccupied - mean * mean);

    QString txt;
    txt += QString("Occupied voxels: %1\n").arg(nOccupied);
    txt += QString("Min:    %1 MPa\n").arg(vmin, 0, 'f', 4);
    txt += QString("Max:    %1 MPa\n").arg(vmax, 0, 'f', 4);
    txt += QString("Mean:   %1 MPa\n").arg(mean, 0, 'f', 4);
    txt += QString("StdDev: %1 MPa").arg(stddev, 0, 'f', 4);
    statsText_->setText(txt);
}

// ============================================================
// Logging
// ============================================================
void StressPredictionModule::appendLog(const QString& msg)
{
    logText_->append(msg);
}

// ============================================================
// refreshFromProject
// ============================================================
void StressPredictionModule::refreshFromProject()
{
    if (!app_ || !app_->sharedViewport()) return;
    auto* viewport = app_->sharedViewport();

    // Connect 3D mode switch (once)
    static bool connected = false;
    if (!connected && viewport) {
        connect(viewport, &SharedViewport::mode3DChanged, this, [this](const QString& mode) {
            if (mode == "stress") {
                build3DStressView();
            }
        });
        connected = true;
    }

    // Restore stress data from project (load from .si)
    if (!hasResult_ && app_->currentProject.contains("has_stress")
        && app_->currentProject.value("has_stress").toBool()) {

        QString stressPath = app_->currentProject.value("stress_volume_path").toString();
        if (!stressPath.isEmpty() && QFile::exists(stressPath) && !stressVolume_) {
            try {
                stressVolume_ = NiftiIO::readNifti(stressPath.toStdString());
                if (stressVolume_ && stressVolume_->GetPointData()->GetScalars()) {
                    stressVolume_->GetPointData()->GetScalars()->SetName("Stress");
                    double range[2];
                    stressVolume_->GetPointData()->GetScalars()->GetRange(range);
                    stressMin_ = range[0];
                    stressMax_ = range[1];
                    hasResult_ = true;
                    overlayCheckbox_->setEnabled(true);
                    appendLog(QString("Restored stress volume: %1").arg(stressPath));
                }
            } catch (...) {
                appendLog("Failed to load stress volume from project.");
            }
        }

        // Restore element NPZ path
        QString npzP = app_->currentProject.value("element_npz_path").toString();
        if (!npzP.isEmpty() && QFile::exists(npzP)) {
            npzPath_ = npzP;
            hasNpzData_ = true;
            predictBtn_->setEnabled(true);
        }

        // Restore BC/stress params
        if (app_->currentProject.contains("bc_count")) {
            nBCElements_ = app_->currentProject.value("bc_count").toInt();
            nLoadElements_ = app_->currentProject.value("load_count").toInt();
            uDisp_ = app_->currentProject.value("u_disp").toDouble();
            hasBCData_ = true;
        }
    }

    // If has results, rebuild 3D and restore overlay
    if (hasResult_) {
        if (viewport->current3DMode() == "stress")
            build3DStressView();
    }
    updateStatusDisplay();
}
