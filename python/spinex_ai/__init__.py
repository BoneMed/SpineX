"""
SpineX AI 模块 - C++ 桥接层

这些 Python 模块被 C++ 主程序通过 pybind11 嵌入式解释器调用。
每个模块是对原始 Function/ 和 Lib/ 中 AI 代码的薄封装。

调用流程：
    C++ InferenceWorker (QThread)
        → py::gil_scoped_acquire (获取 GIL)
        → py::module_::import("spinex_ai.segmentation")
        → result = module.attr("run_segmentation")(input_path, output_dir)
        → py::gil_scoped_release (释放 GIL)
"""
