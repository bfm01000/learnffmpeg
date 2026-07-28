#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

# ── Parse args ──────────────────────────────────────────────────────────────
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(nproc)}"
CLEAN=0
SANITIZERS=OFF

while [[ $# -gt 0 ]]; do
  case $1 in
    --debug)   BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
    --clean)   CLEAN=1 ;;
    --san)     SANITIZERS=ON ;;
    -j) JOBS="$2"; shift ;;
    -h|--help)
      echo "Usage: $0 [--debug|--release] [--clean] [--san] [-j N]"
      exit 0 ;;
  esac
  shift
done

# ── Configure ───────────────────────────────────────────────────────────────
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [[ ${CLEAN} -eq 1 ]]; then
  rm -rf "${BUILD_DIR:?}"/*
fi

cmake "${ROOT_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DENABLE_SANITIZERS="${SANITIZERS}" \
  -DBUILD_TESTS=ON \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# ── Build ───────────────────────────────────────────────────────────────────
cmake --build . -j "${JOBS}"

echo ""
echo "Build complete: ${BUILD_TYPE}"
echo "Output: ${BUILD_DIR}"
