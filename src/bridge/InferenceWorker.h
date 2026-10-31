/**
 * ============================================================================
 * InferenceWorker - 异步 Python AI 推理线程
 * ============================================================================
 * 为什么需要线程？
 *   AI 推理（分割、应力预测）可能需要 30-60 秒。如果在主线程中运行，
 *   整个界面会卡住（"未响应"）。所以把 Python 调用放到单独的线程中。
 *
 * Qt 线程和信号槽：
 *   - QThread：Qt 的线程类
 *   - signals：线程发出的"通知"（类似 Python 的 pyqtSignal）
 *   - slots：接收通知的函数
 *   - 信号可以跨线程安全传递数据
 *
 * GIL (Global Interpreter Lock)：
 *   Python 的全局解释器锁。同一时间只有一个线程能执行 Python 代码。
 *   在调用 Python 前获取 GIL，调用完后释放。
 * ============================================================================
 */

#ifndef SPINEX_INFERENCE_WORKER_H
#define SPINEX_INFERENCE_WORKER_H

// pybind11 MUST come before Qt headers (Python's "slots" macro conflicts with Qt)
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

#ifdef slots
#undef slots
#endif

#include <QThread>                         // Qt 线程基类
#include <QString>                         // Qt 字符串
#include <QVariant>                        // Qt 通用数据类型
#include <functional>                      // std::function（函数对象）

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkPolyData.h>

/**
 * InferenceWorker - 在后台线程中执行 Python AI 推理
 *
 * Q_OBJECT 宏：
 *   告诉 Qt 的 MOC 工具这个类使用了信号/槽机制。
 *   任何使用 signals/slots 的类都必须包含这个宏。
 */
class InferenceWorker : public QThread
{
    Q_OBJECT

public:
    // 推理任务类型枚举
    // enum class 是 C++ 的枚举类型，类似 Python 的 Enum
    enum class TaskType {
        Segmentation,          // nnUNet 脊柱分割
        HU2E,                  // HU → 弹性模量转换
        StressPrediction,      // DL-FE 应力预测
        CustomPython           // 自定义 Python 调用
    };

    // 构造函数
    // explicit 防止隐式类型转换（C++ 安全特性）
    // parent 是 Qt 的父对象（用于自动内存管理）
    explicit InferenceWorker(QObject* parent = nullptr);

    // 设置任务参数
    void setTask(TaskType type, const QVariantMap& params);

    // 设置自定义 Python 函数
    // std::function 类似 Python 的可调用对象（函数/lambda）
    void setCustomFunction(std::function<py::object()> func);

// ---- Qt 信号 ----
// signals 是线程发出的事件通知，其他对象可以"连接"到这些信号
// 信号只需要声明，不需要实现（Qt MOC 自动生成）
Q_SIGNALS:
    // 进度更新（0-100 的百分比 + 描述信息）
    void progressUpdated(int percent, QString message);

    // 分割完成：返回掩膜数据
    void segmentationFinished(vtkSmartPointer<vtkImageData> mask);

    // HU2E 完成：返回点云数据 [x, y, z, E]
    void hu2eFinished(vtkSmartPointer<vtkPolyData> pointCloud);

    // 应力预测完成：返回应力场体数据
    void stressFinished(vtkSmartPointer<vtkImageData> stressField);

    // 自定义任务完成
    void customFinished(QVariant result);

    // 发生错误
    void errorOccurred(QString errorMessage);

protected:
    // run() 是线程的入口函数（在新线程中执行）
    // override 表示这个函数重写了基类的虚函数
    void run() override;

private:
    // 各类型任务的执行函数
    void runSegmentation();
    void runHU2E();
    void runStressPrediction();
    void runCustomPython();

    TaskType taskType_;
    QVariantMap params_;                   // 任务参数（键值对）
    std::function<py::object()> customFunc_;
};

#endif // SPINEX_INFERENCE_WORKER_H
