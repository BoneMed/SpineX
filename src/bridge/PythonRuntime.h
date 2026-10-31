/**
 * ============================================================================
 * PythonRuntime - Python 解释器生命周期管理（头文件）
 * ============================================================================
 * 头文件 (.h) 的作用：
 *   - 声明类的结构（有哪些成员变量和函数），但不写具体实现
 *   - 类似 Python 的 class 定义，但把函数体放到 .cpp 文件里
 *   - 其他文件通过 #include 这个 .h 来使用这个类
 *
 * 单例模式（Singleton）：
 *   - 整个程序只有一个 PythonRuntime 实例
 *   - 通过 PythonRuntime::instance() 获取
 *   - 类似 Python 中的全局变量，但更安全
 * ============================================================================
 */

#ifndef SPINEX_PYTHON_RUNTIME_H           // 头文件保护：防止重复包含
#define SPINEX_PYTHON_RUNTIME_H           // 如果已经包含过，就跳过

#include <string>                          // C++ 字符串类型
#include <memory>                          // 智能指针（自动管理内存）
#include <pybind11/embed.h>                // pybind11 嵌入式 Python
#include <pybind11/numpy.h>                // pybind11 的 numpy 支持

// Python's object.h defines a "slots" macro that conflicts with Qt's "slots" keyword.
#ifdef slots
#undef slots
#endif

namespace py = pybind11;

/**
 * PythonRuntime 类 - 管理嵌入式 Python 解释器
 *
 * C++ 类的访问控制：
 *   public:   任何代码都能调用（类似 Python 的普通方法）
 *   private:  只有类自己能访问（类似 Python 的 __xxx 双下划线方法）
 */
class PythonRuntime
{
public:
    // ---- 单例模式 ----
    // static 函数属于类本身，不属于某个实例
    // 返回类型后面的 & 表示"引用"——直接操作原始对象，不复制
    static PythonRuntime& instance();

    // 初始化 Python 解释器
    // const std::string& 表示"常量字符串引用"——传入字符串但不允许修改
    void initialize(const std::string& pythonHome = "");

    // 关闭 Python 解释器
    void shutdown();

    // 检查 Python 是否已初始化
    bool isInitialized() const;            // const 表示这个函数不修改对象状态

    // 导入 Python 模块（类似 Python 的 import xxx）
    // py::module_ 是 pybind11 对 Python 模块对象的封装
    py::module_ importModule(const std::string& moduleName);

    // 执行 Python 代码字符串（类似 Python 的 exec()）
    void exec(const std::string& code);

    // 获取 Python 模块的搜索路径（用于调试）
    std::string getPythonPath() const;

    // 获取 Python Home 路径（BoneMed 环境路径）
    std::string pythonHome() const { return pythonHome_; }

    // 跨平台获取 Python 可执行文件路径
    // Windows: <home>/python.exe   Linux: <home>/bin/python3.10
    std::string pythonExecutable() const {
        return pythonHome_ + "/bin/python3.10";
    }

    // 跨平台获取项目源码目录（用于开发期定位 python/ 脚本）
    static std::string projectSourceDir() {
#ifdef SPINEX_SOURCE_DIR
        return SPINEX_SOURCE_DIR;
#else
        return "";
#endif
    }

    // ---- 禁止拷贝和赋值 ----
    // 单例模式下不允许复制对象
    // = delete 表示"禁用这个函数"
    PythonRuntime(const PythonRuntime&) = delete;             // 禁止拷贝构造
    PythonRuntime& operator=(const PythonRuntime&) = delete;  // 禁止赋值

private:
    // ---- 私有构造函数 ----
    // 构造函数和析构函数是特殊函数：
    //   构造函数：创建对象时自动调用（类似 Python 的 __init__）
    //   析构函数（~前缀）：销毁对象时自动调用（类似 Python 的 __del__）
    PythonRuntime();
    ~PythonRuntime();

    // ---- 成员变量 ----
    bool initialized_;
    std::string pythonHome_;               // BoneMed 环境路径

    // unique_ptr 是智能指针：自动管理内存，不需要手动 delete
    // 当 PythonRuntime 被销毁时，interpreter_ 会自动释放
    std::unique_ptr<py::scoped_interpreter> interpreter_;
};

#endif // SPINEX_PYTHON_RUNTIME_H
