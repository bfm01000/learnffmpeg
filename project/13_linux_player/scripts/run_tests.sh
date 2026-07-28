#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "No build directory. Run scripts/build.sh first."
  exit 1
fi

cd "${BUILD_DIR}"
ctest --output-on-failure -j "$(nproc)" "$@"
