#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
TARGET="${TARGET:-smart_grid_server}"

echo "Building ${TARGET} (${CMAKE_BUILD_TYPE}) into ${BUILD_DIR}..."

cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --target "${TARGET}" -j"$(nproc)"

# Keep bench_native.sh defaults working by copying the binary to repo root.
if [ -x "${BUILD_DIR}/${TARGET}" ]; then
  cp -f "${BUILD_DIR}/${TARGET}" "./${TARGET}"
else
  # Fallback: some CMake setups place the binary elsewhere; try to locate it.
  BIN_PATH="$(find "${BUILD_DIR}" -maxdepth 2 -type f -name "${TARGET}" -perm -111 | head -n1 || true)"
  if [ -n "${BIN_PATH}" ]; then
    cp -f "${BIN_PATH}" "./${TARGET}"
  else
    echo "Error: built binary ${TARGET} not found in ${BUILD_DIR}"
    exit 1
  fi
fi

echo "Build complete."
echo "Binary: $(realpath ./${TARGET})"
