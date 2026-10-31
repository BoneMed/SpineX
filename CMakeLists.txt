# ============================================================================
# SpineX C++ 项目 - 顶层 CMake 构建文件 (跨平台: Windows / Linux)
# ============================================================================
# Windows 构建（vcpkg + MSVC）：
#   1. cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=H:/vcpkg/scripts/buildsystems/vcpkg.cmake
#   2. cmake --build build --config Release
#   3. ./build/Release/SpineX.exe
#
# Linux 构建（apt + GCC）：
#   1. ./build_linux.sh
#   或者：
#   1. cmake -B build -S . -GNinja -DCMAKE_BUILD_TYPE=Release
#   2. cmake --build build
#   3. ./build/SpineX
# ============================================================================

cmake_minimum_required(VERSION 3.24)

project(SpineX
    VERSION 2.0.0
    LANGUAGES CXX
    DESCRIPTION "SpineX - 脊柱手术规划平台"
)

# ---- C++ 标准设置 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 默认 Release（Linux 单配置生成器需要显式设）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Linux: 让 CMake 优先在 BoneMed conda env 中找 Qt6/VTK ----
# Ubuntu 自带的 libvtk9-qt-dev 是用 Qt5 编译的，跟 Qt6 不兼容；
# conda-forge 的 vtk (qt_* build) 用 Qt6，所以 Linux 上统一用 conda-forge 版本。
if(UNIX AND NOT APPLE)
    if(NOT DEFINED _BONEMED OR _BONEMED STREQUAL "")
        set(_BONEMED "$ENV{HOME}/miniforge3/envs/BoneMed" CACHE PATH "BoneMed conda env root")
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${_BONEMED}")
endif()

# ---- 查找依赖库 ----
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)

find_package(VTK REQUIRED COMPONENTS
    CommonCore CommonDataModel
    FiltersCore FiltersGeneral FiltersSources
    IOImage IOXML IOInfovis
    ImagingCore ImagingGeneral ImagingColor
    InteractionStyle InteractionWidgets
    RenderingCore RenderingOpenGL2 RenderingVolumeOpenGL2
    RenderingAnnotation RenderingFreeType
    GUISupportQt
    CommonComputationalGeometry
    FiltersFlowPaths
)

# ---- Python: 使用 BoneMed conda 环境 ----
if(WIN32)
    set(_BONEMED "C:/Users/Administrator/anaconda3/envs/BoneMed" CACHE PATH "BoneMed conda env root")
    set(Python3_ROOT_DIR "${_BONEMED}" CACHE PATH "" FORCE)
    set(Python3_EXECUTABLE "${_BONEMED}/python.exe" CACHE FILEPATH "" FORCE)
    set(_Python3_INCLUDE_DIR "${_BONEMED}/include" CACHE PATH "" FORCE)
    set(_Python3_LIBRARY_RELEASE "${_BONEMED}/libs/python310.lib" CACHE FILEPATH "" FORCE)
    set(Python3_FIND_REGISTRY NEVER)
    set(Python3_FIND_STRATEGY LOCATION)
else()
    # Linux: 默认 ~/miniforge3/envs/BoneMed，用户可通过 -D_BONEMED=/path 覆盖
    if(NOT DEFINED _BONEMED OR _BONEMED STREQUAL "")
        set(_BONEMED "$ENV{HOME}/miniforge3/envs/BoneMed" CACHE PATH "BoneMed conda env root")
    endif()
    set(Python3_ROOT_DIR "${_BONEMED}" CACHE PATH "" FORCE)
    set(Python3_EXECUTABLE "${_BONEMED}/bin/python3.10" CACHE FILEPATH "" FORCE)
    set(_Python3_INCLUDE_DIR "${_BONEMED}/include/python3.10" CACHE PATH "" FORCE)
    set(_Python3_LIBRARY_RELEASE "${_BONEMED}/lib/libpython3.10.so" CACHE FILEPATH "" FORCE)
    set(Python3_FIND_STRATEGY LOCATION)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)

# pybind11: C++ 调用 Python 的桥接库
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 REQUIRED)

# nlohmann_json
find_package(nlohmann_json REQUIRED)

# ---- 收集源文件 ----
set(SPINEX_SOURCES
    src/main.cpp

    src/app/SpineXApp.h
    src/app/SpineXApp.cpp
    src/app/AppConfig.h
    src/app/AppConfig.cpp
    src/app/LicenseManager.h
    src/app/LicenseManager.cpp
    src/app/UpdateChecker.h
    src/app/UpdateChecker.cpp

    src/bridge/PythonRuntime.h
    src/bridge/PythonRuntime.cpp
    src/bridge/VolumeExchange.h
    src/bridge/VolumeExchange.cpp
    src/bridge/InferenceWorker.h
    src/bridge/InferenceWorker.cpp

    src/io/NiftiIO.h
    src/io/NiftiIO.cpp
    src/io/ProjectIO.h
    src/io/ProjectIO.cpp

    src/rendering/SliceRenderer.h
    src/rendering/SliceRenderer.cpp
    src/rendering/MaskOverlay.h
    src/rendering/MaskOverlay.cpp
    src/rendering/MeshGenerator.h
    src/rendering/MeshGenerator.cpp

    src/ui/SharedViewport.h
    src/ui/SharedViewport.cpp
    src/ui/CTSliceViewer.h
    src/ui/CTSliceViewer.cpp
    src/ui/MenuBar.h
    src/ui/MenuBar.cpp
    src/ui/WelcomeScreen.h
    src/ui/WelcomeScreen.cpp
    src/ui/BrushCanvas.h
    src/ui/BrushCanvas.cpp
    src/ui/LicenseDialog.h
    src/ui/LicenseDialog.cpp
    src/ui/BCSelectionDialog.h
    src/ui/BCSelectionDialog.cpp

    src/modules/ModuleBase.h
    src/modules/ModuleBase.cpp
    src/modules/DataModule.h
    src/modules/DataModule.cpp
    src/modules/CTSegmentationModule.h
    src/modules/CTSegmentationModule.cpp
    src/modules/ModulusEstimationModule.h
    src/modules/ModulusEstimationModule.cpp
    src/modules/StressPredictionModule.h
    src/modules/StressPredictionModule.cpp
    src/modules/CobbAngleAddon.h
    src/modules/CobbAngleAddon.cpp
)

# Windows 资源（嵌入 exe 图标）— 只在 Windows 编译
if(WIN32)
    list(APPEND SPINEX_SOURCES src/SpineX.rc)
endif()

# ---- 创建可执行文件 ----
qt6_standard_project_setup()
add_executable(SpineX ${SPINEX_SOURCES})

# ---- 链接依赖库 ----
target_link_libraries(SpineX PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network

    ${VTK_LIBRARIES}

    pybind11::embed

    Python3::Python
    Python3::NumPy

    nlohmann_json::nlohmann_json
)

# Gmsh SDK：Windows 用 .lib，Linux 用 libgmsh.so
if(WIN32)
    target_link_libraries(SpineX PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/gmsh/lib/gmsh.lib
    )
    target_include_directories(SpineX PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/gmsh/include
    )
else()
    target_link_libraries(SpineX PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/gmsh_linux/lib/libgmsh.so
    )
    target_include_directories(SpineX PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/gmsh_linux/include
    )
    # Linux 需要 RPATH 在运行时找到 libgmsh.so
    set_target_properties(SpineX PROPERTIES
        BUILD_RPATH "${CMAKE_SOURCE_DIR}/third_party/gmsh_linux/lib;${_BONEMED}/lib"
        INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN"
    )
endif()

# ---- 避免 Python/Qt slots 宏冲突 ----
target_compile_definitions(SpineX PRIVATE QT_NO_KEYWORDS)

# 注入源码目录路径，让 PythonRuntime 能在开发期找到 python/ 模块
target_compile_definitions(SpineX PRIVATE
    SPINEX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# ---- 变体：机构版(Institute Edition) ----
option(SPINEX_INSTITUTE "Build institute (internal) edition" OFF)
if(SPINEX_INSTITUTE)
    target_compile_definitions(SpineX PRIVATE SPINEX_INSTITUTE)
    message(STATUS "*** Variant: INSTITUTE EDITION (no auto-update) ***")
else()
    message(STATUS "*** Variant: PUBLIC EDITION ***")
endif()

# ---- 包含目录 ----
target_include_directories(SpineX PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${Python3_NumPy_INCLUDE_DIRS}
)

# ---- VTK 模块初始化 ----
vtk_module_autoinit(
    TARGETS SpineX
    MODULES ${VTK_LIBRARIES}
)

# ============================================================================
# 部署：把运行时需要的资源/插件/DLL 复制到构建目录
# ============================================================================

if(WIN32)
    # ---- 复制 Qt 平台插件（qwindows.dll）----
    add_custom_command(TARGET SpineX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SpineX>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/platforms/qwindows.dll"
            $<TARGET_FILE_DIR:SpineX>/platforms/
        COMMENT "复制 Qt 平台插件..."
    )

    # ---- 复制 Qt imageformats 插件 ----
    add_custom_command(TARGET SpineX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SpineX>/imageformats
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/imageformats"
            $<TARGET_FILE_DIR:SpineX>/imageformats/
        COMMENT "复制 Qt imageformats 插件..."
    )

    # ---- 复制 Python DLL ----
    add_custom_command(TARGET SpineX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_BONEMED}/python310.dll"
            $<TARGET_FILE_DIR:SpineX>/
        COMMENT "复制 python310.dll 到构建目录..."
    )

    # ---- 复制 Gmsh DLL ----
    add_custom_command(TARGET SpineX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/gmsh/lib/gmsh-4.13.dll
            $<TARGET_FILE_DIR:SpineX>/
        COMMENT "复制 gmsh-4.13.dll 到构建目录..."
    )
endif()

# 跨平台：复制资源、许可证、Python AI 代码
add_custom_command(TARGET SpineX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:SpineX>/resources
    COMMENT "复制资源文件到构建目录..."
)

add_custom_command(TARGET SpineX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt
        $<TARGET_FILE_DIR:SpineX>/THIRD_PARTY_LICENSES.txt
    COMMENT "复制第三方许可证文件..."
)

add_custom_command(TARGET SpineX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/python
        $<TARGET_FILE_DIR:SpineX>/python
    COMMENT "复制 Python AI 代码到构建目录..."
)

# ============================================================================
# Cobb 角分析独立测试工具
# ============================================================================
add_executable(cobb_test tools/cobb_test/main.cpp)
target_link_libraries(cobb_test PRIVATE
    ${VTK_LIBRARIES}
    nlohmann_json::nlohmann_json
)
vtk_module_autoinit(TARGETS cobb_test MODULES ${VTK_LIBRARIES})

if(WIN32)
    add_custom_command(TARGET cobb_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cobb_test>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/platforms/qwindows.dll"
            $<TARGET_FILE_DIR:cobb_test>/platforms/
        COMMENT "Copy Qt platforms for cobb_test..."
    )
endif()

# ---- 输出构建信息 ----
message(STATUS "===== SpineX 构建配置 =====")
message(STATUS "Platform:      ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "Qt6 版本:      ${Qt6_VERSION}")
message(STATUS "VTK 版本:      ${VTK_VERSION}")
message(STATUS "Python 版本:   ${Python3_VERSION}")
message(STATUS "Python 路径:   ${Python3_EXECUTABLE}")
message(STATUS "BoneMed env:   ${_BONEMED}")
message(STATUS "pybind11 版本: ${pybind11_VERSION}")
message(STATUS "==========================")
