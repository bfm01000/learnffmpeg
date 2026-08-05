#!/usr/bin/env bash
# 一键清理旧编译 → 重新 cmake → 构建 → 运行
# 用法: ./rebuild.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== 1/3 清理旧编译产物 ==="
rm -rf "$BUILD_DIR"
echo "   ✓ 已删除 $BUILD_DIR"

echo "=== 2/3 cmake 配置 ==="
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"
echo "   ✓ cmake 配置完成"

echo "=== 3/3 编译 ==="
cmake --build "$BUILD_DIR" -j "$(nproc)"
echo "   ✓ 编译完成"

echo ""
echo "=== ✅ 构建成功 ==="
echo "二进制: $BUILD_DIR/epoll_poll_select_demo"
echo ""
echo "=== ▶ 运行 ==="
exec "$BUILD_DIR/epoll_poll_select_demo"
