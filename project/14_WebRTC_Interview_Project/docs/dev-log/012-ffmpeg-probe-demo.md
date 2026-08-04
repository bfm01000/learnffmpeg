# 012 FFmpeg Probe Demo

## 本次目标

实现 Phase 3 的第二个 native 小闭环：C++/FFmpeg 媒体探测工具，用于分析封装、stream、PTS/DTS/time_base、keyframe 和 GOP。

## 做了什么

新增 `native/ffmpeg_probe`：

1. `CMakeLists.txt`
2. `main.cpp`
3. `media_probe.h`
4. `media_probe.cpp`

更新 `native/CMakeLists.txt`，加入 `add_subdirectory(ffmpeg_probe)`。

新增文档：

1. `docs/phase3/ffmpeg-timestamp-design.md`
2. `docs/interview-notes/10-ffmpeg-timestamp-gop.md`

## 遇到的问题

Phase 3 不能只做摄像头采集，还需要把已有 FFmpeg/编解码知识迁移进 WebRTC 项目。时间戳和 GOP 是最适合切入的点，因为它们连接了封装、解码、Seek、RTP timestamp、jitter buffer 和关键帧恢复。

## 怎么排查

先检查 `pkg-config` 和 FFmpeg dev headers，确认 `libavformat`、`libavcodec`、`libavutil` 已安装。然后用 libavformat 做 demux 层分析，不进入解码，保持第一版简单。

## 怎么解决

实现 `ffmpeg_probe --input file --packets N`：打开文件、打印 stream 信息、遍历 video packet、输出 PTS/DTS/duration/keyframe，并统计 keyframe interval。

## 技术取舍

第一版不做 decode，不解析 NALU。这样能聚焦时间模型和 GOP，而不是把复杂度扩散到解码和 bitstream parsing。

## 涉及的关键知识点

1. AVFormatContext。
2. AVStream / AVCodecParameters。
3. AVPacket。
4. PTS / DTS / duration。
5. stream time_base。
6. keyframe / GOP。

## 和我过往经验的连接

这一步直接连接精准 Seek、frame index、int64 pts、Smart Seek。后续也能连接 WebRTC RTP timestamp、jitter buffer、PLI/FIR 和关键帧恢复。

## 面试讲法

我在 V4L2 采集之后实现了 FFmpeg Probe，用来分析媒体文件的时间模型。工具会打印 stream time_base，并逐个 packet 输出 PTS、DTS、duration、keyframe 和 GOP 间隔。我可以借这个工具讲清楚 PTS/DTS 的区别、B 帧重排为什么会引入延迟、GOP 对错误恢复和首帧等待的影响，以及这些概念如何迁移到 WebRTC 的 RTP timestamp 和 jitter buffer。

## 后续可扩展

1. 解析 H264 Annex-B / AVCC。
2. 输出 JSON/CSV。
3. 统计 B 帧和 reorder 深度。
4. 接入 RTP packetization 实验。

## 验证结果

已完成编译：

```bash
cd native
cmake -S . -B build
cmake --build build -j2
```

已使用已有测试视频验证：

```bash
./build/ffmpeg_probe/ffmpeg_probe --input ../../11_FrameIndex_Extraction_Demo/test_short_gop.mp4 --packets 40
```

关键输出：

```text
format    : QuickTime / MOV
duration  : 5.000s
codec     : h264
time_base : 1/15360
size      : 640x360
fps       : 30.000
```

Packet 分析显示每帧 duration 为 `512`，在 `1/15360` time_base 下等于 `0.033333s`，即 30fps。关键帧出现在 packet 0、15、30，统计 GOP 间隔为 15 个 video packet。

```text
gop_min_packets : 15
gop_max_packets : 15
gop_avg_packets : 15.00
```

这个验证结果可以直接用于面试讲解：time_base 决定时间戳单位，PTS/DTS 转秒需要乘以 time_base；GOP=15 表示约 0.5 秒一个关键帧，在实时传输中会影响首帧等待、错误恢复和关键帧请求后的恢复速度。
