#!/bin/bash
# 生成测试用的 MP4 视频文件
# 用法: bash generate_test_video.sh

set -e

echo "========================================="
echo "  生成测试视频"
echo "========================================="

# 测试视频1: 标准 CFR H.264 MP4 (每30帧一个I帧, 5秒)
echo ""
echo "[1/3] 生成 test.mp4 (5秒, 30fps, GOP=30, H.264)..."
ffmpeg -y \
    -f lavfi -i testsrc=duration=5:size=640x360:rate=30 \
    -f lavfi -i sine=frequency=440:duration=5 \
    -c:v libx264 \
    -preset ultrafast \
    -g 30 \
    -keyint_min 30 \
    -sc_threshold 0 \
    -c:a aac \
    -shortest \
    test.mp4 2>/dev/null
echo "  ✅ test.mp4 生成完成 ($(du -sh test.mp4 | cut -f1))"

# 测试视频2: 短 GOP 视频 (每15帧一个I帧, 演示高频Smart Seek)
echo ""
echo "[2/3] 生成 test_short_gop.mp4 (5秒, 30fps, GOP=15)..."
ffmpeg -y \
    -f lavfi -i testsrc=duration=5:size=640x360:rate=30 \
    -c:v libx264 \
    -preset ultrafast \
    -g 15 \
    -keyint_min 15 \
    -sc_threshold 0 \
    -an \
    test_short_gop.mp4 2>/dev/null
echo "  ✅ test_short_gop.mp4 生成完成 ($(du -sh test_short_gop.mp4 | cut -f1))"

# 测试视频3: 长 GOP 视频 (每60帧一个I帧, 演示Smart Seek高命中率)
echo ""
echo "[3/3] 生成 test_long_gop.mp4 (5秒, 30fps, GOP=60)..."
ffmpeg -y \
    -f lavfi -i testsrc=duration=5:size=640x360:rate=30 \
    -c:v libx264 \
    -preset ultrafast \
    -g 60 \
    -keyint_min 60 \
    -sc_threshold 0 \
    -an \
    test_long_gop.mp4 2>/dev/null
echo "  ✅ test_long_gop.mp4 生成完成 ($(du -sh test_long_gop.mp4 | cut -f1))"

echo ""
echo "========================================="
echo "  All done! 现在可以运行:"
echo "    cmake -S . -B build && cmake --build build"
echo "    ./build/frame_index_demo test.mp4"
echo "    ./build/frame_index_demo test.mp4 --extract 42"
echo "========================================="
