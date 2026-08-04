# Dev Log 015 - H.264 RTP Packetizer

## 本次目标

在 Phase 3 native 链路中新增一个 H.264 RTP packetizer 最小 demo，把前一步生成的 Annex-B H.264 裸流转换成 RTP 包级别的 CSV 元数据。

## 做了什么

1. 新增 `native/h264_rtp_packetizer` 子模块。
2. 实现 Annex-B start code 扫描，支持 `00 00 01` 和 `00 00 00 01`。
3. 基于 NALU type 识别 SPS、PPS、SEI、IDR、AUD 和普通 slice。
4. 用 AUD 作为 access unit/frame 边界，默认跳过 AUD 发送。
5. 实现 single NALU packetization。
6. 实现 FU-A fragmentation。
7. 输出 CSV 字段：packet index、frame index、sequence number、timestamp、marker、payload type、NALU type、packetization、payload size、FU start/end。
8. 更新 `native/README.md`、Phase 3 设计文档和面试讲解文档。

## 验证命令

```bash
cd /home/bfm01000/workspace/learnffmpeg/project/14_WebRTC_Interview_Project/native
cmake -S . -B build
cmake --build build -j2
./build/h264_rtp_packetizer/h264_rtp_packetizer \
  --input ../captures/frxxz-first80.h264 \
  --output ../captures/frxxz-first80-rtp.csv \
  --max-payload 1200 \
  --fps 25
```

## 验证结果

```text
frames       : 80
total_nalus  : 163
total_packets: 159
fu-a         : 92
single-nalu  : 67
```

CSV 前几行显示第一帧中的 SPS、PPS、SEI 走 single NALU，IDR 因为较大走 FU-A 分片。

## 遇到的问题

### apply_patch 无法写入 WSL UNC 路径

在当前桌面环境中，`apply_patch` 读取 `\\wsl.localhost\Ubuntu\...` 路径时报 `windows sandbox helper_unknown_error`。

解决方式：改用 PowerShell `Set-Content` 在同一 workspace writable root 下写入文件。改动范围仍然限制在本项目目录内。

### 裸流帧边界如何确定

Annex-B NALU 本身不总是显式告诉应用层完整帧边界。当前统一素材经 FFmpeg 转换后包含 AUD，所以本 demo 使用 AUD 作为 frame boundary。

后续如果输入流没有 AUD，需要从解封装 packet 边界传递 frame 信息，或者进一步解析 H.264 slice header。

## 面试可讲点

这一步可以说明我不是只会调 WebRTC API，而是理解编码数据进入 RTP 前的结构：NALU、SPS/PPS、IDR、RTP timestamp、sequence number、marker bit，以及大 NALU 的 FU-A 分片。

## 下一步

可以继续做 RTP binary writer，或者开始进入 native libwebrtc sender 调研，确认 libwebrtc 对 encoded frame 输入、RTP packetizer、pacing 和 RTCP 反馈的职责划分。
