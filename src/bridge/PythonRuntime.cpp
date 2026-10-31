/**
 * PythonRuntime - Python interpreter lifecycle management
 */

#include "PythonRuntime.h"
#include <iostream>
#include <filesystem>

PythonRuntime& PythonRuntime::instance()
{
    static PythonRuntime inst;
    return inst;
}

PythonRuntime::PythonRuntime()
    : initialized_(false)
    , interpreter_(nullptr)
{
}

PythonRuntime::~PythonRuntime()
{
    shutdown();
}

void PythonRuntime::initialize(const std::string& pythonHome)
{
    if (initialized_) {
        return;
    }

    pythonHome_ = pythonHome;

    try {
        if (!pythonHome.empty()) {
                setenv("PYTHONHOME", pythonHome.c_str(), 1);
        }

        interpreter_ = std::make_unique<py::scoped_interpreter>();
        initialized_ = true;

        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        path.append("./python");
#ifdef SPINEX_SOURCE_DIR
        path.append(std::string(SPINEX_SOURCE_DIR) + "/python");
        path.append(std::string(SPINEX_SOURCE_DIR) + "/python/nnUNet");
#endif

        py::module_ platform = py::module_::import("platform");
        std::string pyVersion = platform.attr("python_version")().cast<std::string>();
        // Prevent multiprocessing from re-launching the C++ exe on Windows
        try {
            py::exec(R"(
import multiprocessing
multiprocessing.freeze_support()
try:
    multiprocessing.set_start_method('spawn', force=True)
except RuntimeError:
    pass
# Disable nnUNet multiprocessing to prevent child process spawning
import os
os.environ['nnUNet_n_proc_DA'] = '0'
os.environ['OMP_NUM_THREADS'] = '1'
os.environ['nnUNet_def_n_proc'] = '1'
os.environ['MKL_NUM_THREADS'] = '1'
)");
        } catch (...) {}

        // Add DLL search directories for _ctypes / PyTorch (Windows only)

        // Verify key modules
        try {
            py::module_::import("numpy");
        } catch (const py::error_already_set& e) {
            std::cerr << "[PythonRuntime] NumPy not available: " << e.what() << std::endl;
        }

        try {
            py::module_ torch = py::module_::import("torch");
            bool cudaAvailable = torch.attr("cuda").attr("is_available")().cast<bool>();
        } catch (const py::error_already_set& e) {
            std::cerr << "[PythonRuntime] PyTorch not available: " << e.what() << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] Init failed: " << e.what() << std::endl;
        initialized_ = false;
        interpreter_.reset();
        throw;
    }
}

void PythonRuntime::shutdown()
{
    if (initialized_) {
        interpreter_.reset();
        initialized_ = false;
    }
}

bool PythonRuntime::isInitialized() const
{
    return initialized_;
}

py::module_ PythonRuntime::importModule(const std::string& moduleName)
{
    if (!initialized_) {
        throw std::runtime_error("Python not initialized, cannot import: " + moduleName);
    }
    return py::module_::import(moduleName.c_str());
}

void PythonRuntime::exec(const std::string& code)
{
    if (!initialized_) {
        throw std::runtime_error("Python not initialized, cannot exec code");
    }
    py::exec(code);
}

std::string PythonRuntime::getPythonPath() const
{
    if (!initialized_) return "(not initialized)";

    py::module_ sys = py::module_::import("sys");
    py::list path = sys.attr("path");
    std::string result;
    for (auto& p : path) {
        result += p.cast<std::string>() + "\n";
    }
    return result;
}
