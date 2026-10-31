#!/usr/bin/env bash
# ============================================================================
# SpineX C++ 构建脚本 (Linux)
# ============================================================================
# 用法:
#   ./build_linux.sh                # Release (默认)
#   ./build_linux.sh Debug          # Debug
#   ./build_linux.sh Release clean  # 删除 build_linux/ 重新配置
# ============================================================================
set -euo pipefail

BUILD_TYPE="${1:-Release}"
CLEAN="${2:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_linux"

# BoneMed conda env 位置（用户可通过环境变量覆盖）
BONEMED="${SPINEX_BONEMED:-${HOME}/miniforge3/envs/BoneMed}"

if [[ ! -x "${BONEMED}/bin/python3.10" ]]; then
    echo "[错误] 没找到 BoneMed conda env: ${BONEMED}"
    echo "       用 SPINEX_BONEMED=<path> 覆盖，或先创建环境："
    echo "       conda create -n BoneMed python=3.10"
    exit 1
fi

if [[ "${CLEAN}" == "clean" ]]; then
    echo "===== 清理旧构建 ====="
    rm -rf "${BUILD_DIR}"
fi

echo "===== SpineX Linux 构建 (${BUILD_TYPE}) ====="
echo "BoneMed env: ${BONEMED}"

echo "[1/2] CMake 配置..."
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -D_BONEMED="${BONEMED}"

echo "[2/2] 编译..."
cmake --build "${BUILD_DIR}" --parallel

echo
echo "===== 构建成功 ====="
echo "可执行文件: ${BUILD_DIR}/SpineX"
echo "运行（需要 BoneMed env 中的 libs 在 LD_LIBRARY_PATH 中）："
echo "  LD_LIBRARY_PATH=\"${BONEMED}/lib:\${LD_LIBRARY_PATH:-}\" ${BUILD_DIR}/SpineX"
