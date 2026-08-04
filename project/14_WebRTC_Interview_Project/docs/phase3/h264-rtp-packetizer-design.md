# H.264 RTP Packetizer 设计说明

## 目标

这一步的目标不是马上接入网络发送，而是把 H.264 裸流到 RTP 包之间的关键转换做成一个可观察、可验证的最小模块。

输入是 `ffmpeg_probe` 生成的 Annex-B H.264 文件，例如：

```bash
captures/frxxz-first80.h264
```

输出是 RTP 包元数据 CSV，例如：

```bash
captures/frxxz-first80-rtp.csv
```

这样面试时可以直接展示：同一帧里的 SPS、PPS、SEI、IDR、普通 slice 是如何映射到 RTP sequence number、timestamp、marker bit 和 FU-A 分片上的。

## 模块边界

```mermaid
flowchart LR
  A["MP4: FRXXZ.mp4"] --> B["ffmpeg_probe: AVCC -> Annex-B"]
  B --> C["Annex-B .h264"]
  C --> D["annexb_reader: start code / NALU parser"]
  D --> E["rtp_packetizer: single NALU / FU-A"]
  E --> F["CSV: RTP packet metadata"]
```

## 当前实现

新增 native 子模块：

```text
native/h264_rtp_packetizer/
  main.cpp
  annexb_reader.h
  annexb_reader.cpp
  rtp_packetizer.h
  rtp_packetizer.cpp
```

职责划分：

- `annexb_reader`：读取 `.h264` 文件，识别 `00 00 01` / `00 00 00 01` start code，切出 NALU payload。
- `rtp_packetizer`：按照最大 RTP payload size 判断使用 single NALU 还是 FU-A。
- `main`：解析命令行参数，输出统计信息和 CSV。

## RTP/H.264 规则

当前实现覆盖两个最核心的 packetization 模式：

- Single NALU：当一个 NALU 小于等于 `max_payload_size` 时，一个 NALU 对应一个 RTP 包。
- FU-A：当一个 NALU 超过 `max_payload_size` 时，跳过原始 NALU header，把剩余 payload 分片；每个 RTP payload 前面加 2 字节 FU indicator / FU header。

CSV 中重点字段：

- `sequence_number`：每个 RTP 包递增。
- `timestamp`：同一帧内的所有 RTP 包保持一致。
- `marker`：一帧最后一个 RTP 包置 1。
- `packetization`：`single-nalu` 或 `fu-a`。
- `fu_start` / `fu_end`：标记 FU-A 分片的起始和结束。

## 为什么跳过 AUD

Annex-B 文件中包含 AUD，即 Access Unit Delimiter。它可以帮助离线解析器识别帧边界，但实际 WebRTC RTP 发送时通常不需要把 AUD 发给接收端。

因此当前默认 `skip_aud=true`，用 AUD 做 frame grouping，但不输出 AUD RTP 包。可以通过 `--keep-aud` 观察 AUD 也被 packetize 的情况。

## 已验证结果

命令：

```bash
./build/h264_rtp_packetizer/h264_rtp_packetizer \
  --input ../captures/frxxz-first80.h264 \
  --output ../captures/frxxz-first80-rtp.csv \
  --max-payload 1200 \
  --fps 25
```

结果：

```text
frames       : 80
total_nalus  : 163
total_packets: 159
fu-a         : 92
single-nalu  : 67
```

这说明样例中既有可以直接放入单个 RTP 包的小 NALU，也有需要 FU-A 分片的大 NALU。第一帧 IDR 较大，被切成多包，这是解释 RTP 分片最好的样例。

## 当前限制

- 这是离线 packetizer，不创建 socket，也不发送 UDP。
- RTP header 目前只输出元数据，没有序列化为二进制 12 字节 RTP header。
- 帧边界主要依赖 AUD；如果输入裸流没有 AUD，需要进一步解析 slice header 或从解封装阶段传入 packet/frame 边界。
- 暂未实现 STAP-A 聚合包；面试项目里先讲清楚 single NALU 和 FU-A 更重要。

## 下一步

下一步可以做两个方向：

1. 增加 RTP 二进制包输出，把 header + payload 写成 `.rtp` 文件。
2. 进入 native libwebrtc sender 调研，确认 libwebrtc 自身的 H.264 frame 输入接口、编码帧时间戳和 packetizer 的职责边界。
