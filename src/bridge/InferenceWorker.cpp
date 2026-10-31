/**
 * ============================================================================
 * InferenceWorker - 异步 Python AI 推理线程（实现）
 * ============================================================================
 */

#include "InferenceWorker.h"
#include "PythonRuntime.h"
#include "VolumeExchange.h"
#include <iostream>

// ---- 构造函数 ----
InferenceWorker::InferenceWorker(QObject* parent)
    : QThread(parent)                      // 调用基类构造函数
    , taskType_(TaskType::CustomPython)
{
}

// ---- 设置任务 ----
void InferenceWorker::setTask(TaskType type, const QVariantMap& params)
{
    taskType_ = type;
    params_ = params;
}

// ---- 设置自定义函数 ----
void InferenceWorker::setCustomFunction(std::function<py::object()> func)
{
    customFunc_ = std::move(func);         // std::move 转移所有权（避免复制）
    taskType_ = TaskType::CustomPython;
}

// ---- 线程入口函数 ----
// 这个函数在新线程中运行！不能直接操作 UI
void InferenceWorker::run()
{
    try {
        // 检查 Python 是否可用
        if (!PythonRuntime::instance().isInitialized()) {
            Q_EMIT errorOccurred("Python 未初始化，无法执行 AI 推理");
            return;
        }

        // 根据任务类型执行对应的推理
        // switch 类似 Python 的 match/case 或 if/elif 链
        switch (taskType_) {
            case TaskType::Segmentation:
                runSegmentation();
                break;
            case TaskType::HU2E:
                runHU2E();
                break;
            case TaskType::StressPrediction:
                runStressPrediction();
                break;
            case TaskType::CustomPython:
                runCustomPython();
                break;
        }
    }
    catch (const py::error_already_set& e) {
        // Python 异常被 pybind11 转换为 C++ 异常
        Q_EMIT errorOccurred(QString("Python 错误: %1").arg(e.what()));
    }
    catch (const std::exception& e) {
        Q_EMIT errorOccurred(QString("C++ 错误: %1").arg(e.what()));
    }
}

// ---- 分割推理 ----
void InferenceWorker::runSegmentation()
{
    // 获取 Python GIL（全局解释器锁）
    // 这是一个 RAII 对象：构造时获取锁，析构时释放锁
    // RAII = Resource Acquisition Is Initialization（资源获取即初始化）
    py::gil_scoped_acquire acquire;

    Q_EMIT progressUpdated(0, "正在加载分割模型...");

    // 调用 Python 的 spinex_ai.segmentation 模块
    py::module_ seg_module = py::module_::import("spinex_ai.segmentation");

    // 获取参数
    QString inputPath = params_.value("input_path").toString();
    QString outputDir = params_.value("output_dir").toString();

    Q_EMIT progressUpdated(20, "正在运行第一阶段分割...");

    // 调用 Python 函数
    // py::object 类似 Python 的通用对象
    py::object result = seg_module.attr("run_segmentation")(
        inputPath.toStdString(),
        outputDir.toStdString()
    );

    Q_EMIT progressUpdated(80, "正在处理分割结果...");

    // 获取结果（numpy 数组）
    py::array_t<uint8_t> maskArray = result.cast<py::array_t<uint8_t>>();

    // 将 numpy 转换为 VTK 数据
    double spacing[3] = {1.0, 1.0, 1.0};  // 默认间距，实际从 NIfTI 头读取
    double origin[3] = {0.0, 0.0, 0.0};

    // 从参数获取实际的 spacing 和 origin（如果提供了）
    if (params_.contains("spacing_x")) {
        spacing[0] = params_.value("spacing_x").toDouble();
        spacing[1] = params_.value("spacing_y").toDouble();
        spacing[2] = params_.value("spacing_z").toDouble();
    }

    auto vtkMask = VolumeExchange::numpyMaskToVtkImage(maskArray, spacing, origin);

    Q_EMIT progressUpdated(100, "分割完成！");

    // Q_EMIT 发出信号（通知主线程更新 UI）
    Q_EMIT segmentationFinished(vtkMask);
}

// ---- HU2E 推理 ----
void InferenceWorker::runHU2E()
{
    py::gil_scoped_acquire acquire;

    Q_EMIT progressUpdated(0, "正在运行 HU → 弹性模量转换...");

    py::module_ hu2e_module = py::module_::import("spinex_ai.hu2e");

    QString ctPath = params_.value("ct_path").toString();
    QString maskPath = params_.value("mask_path").toString();

    Q_EMIT progressUpdated(30, "GMM 标定中...");

    py::object result = hu2e_module.attr("run_hu2e")(
        ctPath.toStdString(),
        maskPath.toStdString()
    );

    Q_EMIT progressUpdated(80, "正在处理结果...");

    // 结果是 (N, 4) 的 numpy 数组 [x, y, z, E]
    py::array_t<float> xyzE = result.cast<py::array_t<float>>();
    auto pointCloud = VolumeExchange::numpyToPointCloud(xyzE);

    Q_EMIT progressUpdated(100, "HU2E 转换完成！");
    Q_EMIT hu2eFinished(pointCloud);
}

// ---- 应力预测 (Hybrid DualEncoder) ----
void InferenceWorker::runStressPrediction()
{
    py::gil_scoped_acquire acquire;

    Q_EMIT progressUpdated(0, "Loading Hybrid DualEncoder model...");

    py::module_ stress_module = py::module_::import("spinex_ai.stress_inference");

    // 获取参数
    QString npzPath = params_.value("npz_path").toString();
    int gridRes = params_.value("grid_res", 256).toInt();
    int patchSize = params_.value("patch_size", 128).toInt();

    Q_EMIT progressUpdated(10, "Voxelizing element data...");

    // 调用新的 Hybrid 推理接口
    py::object result = stress_module.attr("run_inference")(
        npzPath.toStdString(),
        py::arg("grid_res") = gridRes,
        py::arg("patch_size") = patchSize
    );

    Q_EMIT progressUpdated(90, "Converting results...");

    // result is a dict: {stress_mpa, mask, bbox_min, bbox_max}
    py::dict resultDict = result.cast<py::dict>();
    py::array_t<float> stressArray = resultDict["stress_mpa"].cast<py::array_t<float>>();
    py::array_t<float> bboxMinArr = resultDict["bbox_min"].cast<py::array_t<float>>();
    py::array_t<float> bboxMaxArr = resultDict["bbox_max"].cast<py::array_t<float>>();

    auto bboxMinBuf = bboxMinArr.unchecked<1>();
    auto bboxMaxBuf = bboxMaxArr.unchecked<1>();

    // Compute spacing from bbox and grid_res
    double spacing[3], origin[3];
    for (int i = 0; i < 3; i++) {
        origin[i] = static_cast<double>(bboxMinBuf(i));
        double extent = static_cast<double>(bboxMaxBuf(i) - bboxMinBuf(i));
        spacing[i] = extent / gridRes;
        if (spacing[i] < 1e-8) spacing[i] = 1.0;
    }

    auto stressField = VolumeExchange::numpyToVtkImage(stressArray, spacing, origin);

    // Name the scalar array
    if (stressField && stressField->GetPointData()->GetScalars())
        stressField->GetPointData()->GetScalars()->SetName("Stress");

    Q_EMIT progressUpdated(100, "Stress prediction complete!");
    Q_EMIT stressFinished(stressField);
}

// ---- 自定义 Python 调用 ----
void InferenceWorker::runCustomPython()
{
    if (!customFunc_) {
        Q_EMIT errorOccurred("未设置自定义 Python 函数");
        return;
    }

    py::gil_scoped_acquire acquire;

    Q_EMIT progressUpdated(0, "正在执行自定义 Python 任务...");

    py::object result = customFunc_();

    Q_EMIT progressUpdated(100, "完成！");

    // 将 Python 结果转换为 QVariant
    try {
        std::string resultStr = py::str(result).cast<std::string>();
        Q_EMIT customFinished(QVariant(QString::fromStdString(resultStr)));
    } catch (...) {
        Q_EMIT customFinished(QVariant());
    }
}
