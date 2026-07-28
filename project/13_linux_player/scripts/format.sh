#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# ── clang-format ────────────────────────────────────────────────────────────
if command -v clang-format &>/dev/null; then
  echo "Running clang-format..."
  find "${ROOT_DIR}" \
    \( -name '*.h' -o -name '*.cpp' \) \
    ! -path '*/build/*' \
    ! -path '*/external/*' \
    -exec clang-format -i -style=file {} \;
else
  echo "clang-format not found, skipping."
fi

# ── cmake-format ────────────────────────────────────────────────────────────
if command -v cmake-format &>/dev/null; then
  echo "Running cmake-format..."
  find "${ROOT_DIR}" -name 'CMakeLists.txt' \
    ! -path '*/build/*' \
    -exec cmake-format -i {} \;
else
  echo "cmake-format not found, skipping."
fi

echo "Done."
