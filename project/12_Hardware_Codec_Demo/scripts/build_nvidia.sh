#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# build_nvidia.sh — Build and run NVIDIA CUDA hardware codec demo
#
# Prerequisites:
#   - NVIDIA GPU with CUDA driver installed
#   - FFmpeg built with --enable-cuda --enable-cuvid --enable-nvenc
#     (check: ffmpeg -hwaccels | grep cuda)
#   - CMake 3.16+, pkg-config, C++17 compiler
#
# Usage:
#   ./scripts/build_nvidia.sh              # build only
#   ./scripts/build_nvidia.sh run          # build and run with test input
#   ./scripts/build_nvidia.sh run <input>  # build and run with custom input
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# ---- Preflight checks ----
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found"; exit 1; }
command -v pkg-config >/dev/null 2>&1 || { echo "ERROR: pkg-config not found"; exit 1; }

if ! pkg-config --exists libavcodec libavformat libavutil libswscale libavfilter; then
    echo "ERROR: FFmpeg libraries not found via pkg-config"
    echo "  Install FFmpeg dev packages or build from source"
    echo "  Arch: sudo pacman -S ffmpeg"
    echo "  Ubuntu: sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev"
    exit 1
fi

echo "[1/3] Checking CUDA availability via FFmpeg..."
if ! ffmpeg -hwaccels 2>/dev/null | grep -q cuda; then
    echo "WARNING: 'cuda' not found in ffmpeg -hwaccels"
    echo "  The demo will still compile, but hardware decode may fall back to CPU."
    echo "  To enable CUDA: rebuild FFmpeg with --enable-cuda --enable-cuvid --enable-nvenc"
fi

# ---- Build ----
echo "[2/3] Configuring CMake..."
rm -rf "$BUILD_DIR"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR"

echo "[3/3] Building..."
cmake --build "$BUILD_DIR" -j

echo ""
echo "Build successful!"
echo "  Binary: $BUILD_DIR/nvidia_hw_codec"
echo ""

# ---- Run ----
MODE="${1:-}"
INPUT="${2:-}"

if [ "$MODE" = "run" ]; then
    if [ -z "$INPUT" ]; then
        # Generate a test file if none provided
        INPUT="$BUILD_DIR/test_input.mp4"
        if [ ! -f "$INPUT" ]; then
            echo "Generating 5-second test video (libx264, 1080p, 30fps)..."
            ffmpeg -y -f lavfi -i testsrc=duration=5:size=1920x1080:rate=30 \
                   -f lavfi -i sine=frequency=440:duration=5 \
                   -c:v libx264 -preset fast -crf 23 \
                   -c:a aac -b:a 128k \
                   -shortest "$INPUT" 2>/dev/null
        fi
    fi

    if [ ! -f "$INPUT" ]; then
        echo "ERROR: Input file not found: $INPUT"
        exit 1
    fi

    echo "============================================"
    echo " Running NVIDIA hardware codec demo"
    echo " Input: $INPUT"
    echo "============================================"
    echo ""

    "$BUILD_DIR/nvidia_hw_codec" "$INPUT"
fi
