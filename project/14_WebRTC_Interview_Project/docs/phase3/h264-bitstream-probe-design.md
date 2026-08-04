# H264 Bitstream Probe 设计说明

## 目标

在 `ffmpeg_probe` 中扩展 H.264 bitstream 分析能力：识别 MP4 中的 H.264 extradata 是否为 AVCC，使用 `h264_mp4toannexb` bitstream filter 导出 Annex-B，并统计 NALU type。

## 为什么做这个

WebRTC 传输 H.264 时，最终要面对 NALU、RTP packetization、关键帧、SPS/PPS、IDR、非 IDR slice 等概念。MP4 中的 H.264 通常是 AVCC 格式，而 RTP/H264 和裸流分析更常见 Annex-B 或 NALU 级模型。

这一步用于打通：

```text
MP4 / AVCC H264
  -> h264_mp4toannexb
  -> Annex-B start code
  -> NALU type statistics
  -> H264 over RTP packetization concept
```

## 当前实现

`ffmpeg_probe` 新增参数：

```bash
--annexb-output output.h264
```

当输入视频是 H.264 且指定该参数时：

1. 创建 `h264_mp4toannexb` bitstream filter。
2. 将 demux 后的 video packet 送入 filter。
3. 输出 Annex-B H.264 到文件。
4. 扫描 start code。
5. 统计 NALU type。

## NALU 类型

当前重点识别：

1. type 1：non-IDR slice。
2. type 5：IDR slice。
3. type 6：SEI。
4. type 7：SPS。
5. type 8：PPS。
6. type 9：AUD。

## 统一素材验证结果

使用：

```bash
/home/bfm01000/workspace/video_downloads/FRXXZ.mp4
```

命令：

```bash
./build/ffmpeg_probe/ffmpeg_probe \
  --input /home/bfm01000/workspace/video_downloads/FRXXZ.mp4 \
  --packets 80 \
  --annexb-output ../captures/frxxz-first80.h264
```

输出：

```text
extradata : 49 bytes (looks like AVCC)
total_nalus : 163
type 1 (non-IDR slice) : 79
type 5 (IDR slice) : 1
type 6 (SEI) : 1
type 7 (SPS) : 1
type 8 (PPS) : 1
type 9 (AUD) : 80
```

## 和 WebRTC 的关系

H.264 over RTP 需要把 NALU 放入 RTP payload。小 NALU 可以单包发送，大 NALU 需要 FU-A 分片。关键帧恢复依赖 IDR，解码初始化依赖 SPS/PPS。WebRTC 中 PLI/FIR 请求关键帧，本质上就是要求发送端尽快发出可解码恢复点。

## 后续扩展

1. 解析 NALU payload header。
2. 统计每个 packet 包含几个 NALU。
3. 实现 H264 FU-A packetization 实验。
4. 输出 RTP timestamp / sequence 模拟结果。
