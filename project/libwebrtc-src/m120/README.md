# libwebrtc m120 源码（阅读用）

> **来源**：Chromium WebRTC, `branch-heads/6099`（m120 分支）
> **下载方式**：`googlesource.com` raw file（`format=TEXT` + base64 decode）
> **用途**：配合 [06-M4-RTP传输模块.md](../../project/WebRTC/06-M4-RTP传输模块.md) §5 和 [07-M6-JitterBuffer模块.md](../../project/WebRTC/07-M6-JitterBuffer模块.md) §5 阅读

## 概况

| 统计 | 数值 |
|------|------|
| 文件总数 | **79** |
| 代码总行数 | **~20,000** |
| 覆盖模块 | M4 RTP 传输 + M6 JitterBuffer + 依赖 |

## 按模块分布

| 目录 | 文件数 | 说明 |
|------|--------|------|
| `modules/rtp_rtcp/source/` + `rtp_rtcp/source/` | 17 | **M4 核心**：RTP 打包/解包/发送/历史/字节序 |
| `common_video/h264/` | 4 | H264 NALU 扫描/SPS 解析 |
| `modules/video_coding/` | 30 | **M6 核心**：PacketBuffer/FrameBuffer/JitterEstimator/参考帧/丢包 |
| `modules/video_coding/timing/` | 10 | 渲染调度/时间戳外推/RTT 过滤 |
| `video/` | 2 | RtpVideoStreamReceiver2（M6 总入口） |
| `api/` + `api/video/` | 6 | 公共类型：EncodedFrame/RtpHeaders/ArrayView |
| `rtc_base/` | 5 | CopyOnWriteBuffer/字节序/百分位过滤器 |
| `media/engine/` | 1 | kMaxPayloadSize 参考 |
| `system_wrappers/` | 2 | Clock/NtpTime |

## ⚠️ m120 路径变化（文档 vs 实际源码）

文档（06/07-Mx）中引用的路径是**概念路径**，m120 实际文件结构有以下差异：

| 文档引用 | m120 实际路径 | 说明 |
|----------|--------------|------|
| `modules/video_coding/frame_buffer.cc` | `video_receiver2.cc` + `generic_decoder.cc` + `frame_helpers.cc` | FrameBuffer 在 m120 被拆成多个文件 |
| `modules/rtp_rtcp/source/nack_tracker.cc` | `modules/video_coding/nack_requester.cc` | NackTracker 重命名为 NackRequester |
| `modules/video_coding/rtp_video_stream_receiver2.cc` | `video/rtp_video_stream_receiver2.cc` | 移到 video/ 目录 |
| `modules/video_coding/jitter_estimator.cc` | `modules/video_coding/timing/jitter_estimator.cc` | 移到 timing/ 子目录 |
| `modules/video_coding/timing/inter_frame_delay.h` | `timing/inter_frame_delay_variation_calculator.h` | 重命名 |
| `modules/video_coding/packet.h` | `api/video/encoded_frame.h` | Packet 类型统一到 api/video |

> **注意**：阅读源码时用 m120 实际路径（本目录），面试讲述时用文档中的概念路径（面试官更熟悉旧命名）。

## 文件清单（完整 79 个）

```
m120/
├── README.md
├── api/
│   ├── array_view.h
│   ├── rtp_headers.h
│   ├── rtp_parameters.h
│   └── transport/rtp/dependency_descriptor.h
├── api/video/
│   ├── encoded_frame.h
│   ├── encoded_image.h
│   └── video_timing.h
├── common_video/h264/
│   ├── h264_common.cc / .h
│   ├── sps_parser.h
│   └── sps_vui_rewriter.h
├── media/engine/
│   └── webrtc_video_engine.cc
├── modules/rtp_rtcp/source/
│   ├── byte_io.h
│   ├── rtp_format.h
│   ├── rtp_format_h264.cc / .h
│   ├── rtp_packet.cc / .h
│   ├── rtp_packet_history.cc / .h
│   ├── rtp_packet_received.h
│   ├── rtp_packet_to_send.h
│   ├── rtp_rtcp_config.h
│   ├── rtp_rtcp_interface.h
│   ├── rtp_sender.cc / .h
│   └── video_rtp_depacketizer_h264.cc / .h
├── modules/video_coding/
│   ├── encoded_frame.h
│   ├── fec_controller_default.cc / .h
│   ├── frame_helpers.cc / .h
│   ├── generic_decoder.cc / .h
│   ├── h264_sps_pps_tracker.cc / .h
│   ├── histogram.cc / .h
│   ├── loss_notification_controller.cc / .h
│   ├── nack_requester.cc / .h
│   ├── packet_buffer.cc / .h
│   ├── rtp_frame_id_only_ref_finder.cc
│   ├── rtp_frame_reference_finder.cc / .h
│   ├── rtp_generic_ref_finder.cc / .h
│   ├── rtp_seq_num_only_ref_finder.cc / .h
│   ├── rtp_vp8_ref_finder.cc / .h
│   ├── rtp_vp9_ref_finder.cc / .h
│   ├── video_receiver2.cc / .h
│   └── timing/
│       ├── decode_time_percentile_filter.h
│       ├── inter_frame_delay_variation_calculator.h
│       ├── jitter_estimator.cc / .h
│       ├── rtt_filter.cc / .h
│       ├── timing.cc / .h
│       └── timestamp_extrapolator.cc / .h
├── video/
│   └── rtp_video_stream_receiver2.cc / .h
├── rtc_base/
│   ├── bitstream_reader.h
│   ├── byte_order.h
│   ├── copy_on_write_buffer.h
│   └── numerics/
│       ├── histogram_percentile_counter.h
│       └── moving_percentile_filter.h
├── rtp_rtcp/source/
│   └── (此前单独下载的 8 个文件，与 modules/rtp_rtcp/source/ 有重复）
└── system_wrappers/include/
    ├── clock.h
    └── ntp_time.h
```

## 和本项目 B 层的关系

B 层代码（`project/WebRTC/06-M4-RTP传输模块/code/` + `07-M6-JitterBuffer模块/code/`）是这些 libwebrtc 源码的**自己重写版本**——核心算法对齐，但裁剪掉了本项目不需要的部分（RTX 重传、VP8/VP9 支持、Pacer 协作等）。对照阅读能看清楚"WebRTC 生产代码 vs 学习重写版本"的层差。

## 配合 Cursor 跳转

本目录配置了 `.clangd` 文件，在 Cursor 中打开本目录即可使用 go-to-definition / find-references。

依赖：Cursor 需安装 clangd 扩展（或使用项目根目录的 clangd 18）。
