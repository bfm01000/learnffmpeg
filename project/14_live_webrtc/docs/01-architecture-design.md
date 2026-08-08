# Step 1：WebRTC 实时直播系统 — 需求分析与架构设计

> 状态: 待评审
> 日期: 2026-08-04

---

## 目录

1. [系统概述](#1-系统概述)
2. [系统整体架构](#2-系统整体架构)
3. [模块职责定义](#3-模块职责定义)
4. [数据流向](#4-数据流向)
5. [线程模型设计](#5-线程模型设计)
6. [关键技术深度分析](#6-关键技术深度分析)
7. [技术决策对比分析](#7-技术决策对比分析)
8. [开发路线图](#8-开发路线图)
9. [面试亮点设计](#9-面试亮点设计)
10. [待决策问题](#10-待决策问题)

---

## 1. 系统概述

### 1.1 项目目标

实现一个 Linux 端实时视频直播系统，从摄像头采集到浏览器接收播放的完整链路：

```
摄像头 → V4L2采集 → 视频帧管理 → H264编码 → WebRTC PeerConnection → RTP/RTCP传输 → Chrome浏览器接收播放
```

### 1.2 项目定位

- **学习深度**：不仅调用 API，深入理解 WebRTC 内部机制
- **工程质量**：生产级代码标准（RAII、线程安全、错误处理）
- **面试亮点**：零拷贝、拥塞控制、实时系统设计

### 1.3 技术栈

| 层次 | 技术选型 |
|------|---------|
| 语言 | C++17 |
| 构建 | CMake 3.20+ |
| 编码 | FFmpeg libx264 (首版) / VAAPI (扩展) |
| WebRTC | Google libwebrtc (待确认) |
| 信令 | WebSocket + JSON |
| 日志 | spdlog |
| 测试 | Google Test |

---

## 2. 系统整体架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Application Layer                               │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                      AppController                                │   │
│  │              (生命周期管理 / 模块编排 / 状态机)                      │   │
│  └──────────────────────────┬───────────────────────────────────────┘   │
│                             │                                            │
├─────────────────────────────┼────────────────────────────────────────────┤
│                      Processing Pipeline                                 │
│                             │                                            │
│  ┌──────────┐    ┌──────────▼────┐    ┌──────────────┐                  │
│  │ V4L2     │    │  FrameBuffer  │    │  H264        │                  │
│  │ Capture  │───▶│  Pool         │───▶│  Encoder     │                  │
│  │          │    │ (引用计数/DMA) │    │ (FFmpeg/VAAPI)│                 │
│  └──────────┘    └───────────────┘    └──────┬───────┘                  │
│                                              │                          │
├──────────────────────────────────────────────┼──────────────────────────┤
│                                   Transport Layer                        │
│                                              │                          │
│  ┌──────────────┐    ┌───────────────┐       │                          │
│  │ Signaling    │    │  WebRTC       │◄──────┘                          │
│  │ Client       │◄──▶│  PeerConnection                                  │
│  │ (WebSocket)  │    │                │                                  │
│  └──────────────┘    │  ┌─────────────┴──────────┐                      │
│                      │  │ RTP/RTCP / SCTP / ICE  │                      │
│                      │  └─────────────┬──────────┘                      │
│                      └────────────────┼─────────────────┘               │
│                                       │                                  │
├───────────────────────────────────────┼──────────────────────────────────┤
│                            Monitoring Layer                              │
│                                       │                                  │
│  ┌──────────────┐    ┌────────────────▼─────────┐                       │
│  │ Stats        │◄───│  RTCP Feedback            │                       │
│  │ Collector    │    │  (REMB / TWCC / RR)       │                       │
│  └──────┬───────┘    └──────────────────────────┘                       │
│         │                                                               │
│  ┌──────▼───────┐                                                       │
│  │ Bitrate      │───▶ Encoder (码率调整)                                 │
│  │ Controller   │                                                       │
│  └──────────────┘                                                       │
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│                              External                                    │
│                                                                          │
│   ┌──────────┐     ┌──────────────┐     ┌─────────────┐                │
│   │ V4L2     │     │ Signaling    │     │ Chrome       │                │
│   │ Device   │     │ Server       │     │ Browser      │                │
│   │ /dev/video│    │ (Node/Go)    │     │ (接收端)      │                │
│   └──────────┘     └──────────────┘     └─────────────┘                │
└──────────────────────────────────────────────────────────────────────────┘
```

### 架构分层说明

| 层 | 职责 | 关键特征 |
|----|------|---------|
| **Application Layer** | 生命周期管理，模块编排，状态机 | 单线程，同步控制逻辑 |
| **Processing Pipeline** | 视频采集 → 帧管理 → 编码 | 多线程流水线，SPSC 队列 |
| **Transport Layer** | WebRTC 协议栈，信令 | libwebrtc 内部多线程 |
| **Monitoring Layer** | 统计采集，质量控制，自适应 | 独立线程，周期轮询 |
| **External** | 硬件设备，网络服务，浏览器客户端 | 跨进程/跨网络 |

---

## 3. 模块职责定义

### 3.1 Capture Layer — 采集层

| 模块 | 职责 | 关键接口 |
|------|------|----------|
| **V4L2Capture** | 打开/配置摄像头设备，管理 V4L2 缓冲区队列，出队原始帧 | `open(device)`, `start()`, `dequeue()`, `stop()`, `close()` |
| **CaptureConfig** | 分辨率、帧率、像素格式 (YUYV/MJPEG/NV12) | 配置结构体 |
| **DeviceEnumerator** | 枚举系统视频设备，查询支持的能力 | `listDevices()`, `getCapabilities(device)` |

#### 与系统交互

```
V4L2Capture ── ioctl ──▶ /dev/video{N} (V4L2 Driver)
    │
    ├── VIDIOC_QUERYCAP    查询设备能力
    ├── VIDIOC_ENUM_FMT    枚举像素格式
    ├── VIDIOC_ENUM_FRAMESIZES  枚举分辨率
    ├── VIDIOC_ENUM_FRAMEINTERVALS 枚举帧率
    ├── VIDIOC_S_FMT       设置格式
    ├── VIDIOC_REQBUFS     请求内核分配缓冲区
    ├── VIDIOC_QUERYBUF    查询缓冲区信息
    ├── VIDIOC_QBUF        将缓冲区入队（内核填充）
    ├── VIDIOC_STREAMON    开始采集
    ├── VIDIOC_DQBUF       出队（获取已填充的帧）
    ├── VIDIOC_STREAMOFF   停止采集
    └── mmap               映射内核缓冲区到用户空间
```

#### 设计考量

- V4L2 使用 `VIDIOC_REQBUFS` + `mmap` 实现内核态零拷贝
- 使用 `select/poll/epoll` 等待帧就绪，避免忙等
- 枚举设备支持的格式/分辨率/帧率，自动选择最优配置
- 竞态窗口保护：dequeue 和 queue 之间的 buffer index 不能错
- 需要处理设备断开/重连（热插拔）

### 3.2 Frame Management Layer — 帧管理层

| 模块 | 职责 | 关键接口 |
|------|------|----------|
| **VideoFrame** | 单帧数据 + 元数据 (PTS, 分辨率, 格式, stride) | 数据载体 |
| **FrameData** | 实际帧数据, 引用计数, 可选的 DMA-BUF fd | 内部实现 |
| **FrameBufferPool** | 预分配环形缓冲区池, 引用计数管理, 支持 DMA-BUF 导出 | `acquire()`, `release()`, `getDmaFd()` |

#### VideoFrame 生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                     VideoFrame 生命周期                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  FrameBufferPool (预分配 N 个 FrameData slot)                │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐          │
│  │slot 0│ │slot 1│ │slot 2│ │slot 3│ ... │slot N│          │
│  │ free │ │ free │ │ free │ │ free │     │ free │          │
│  └──────┘ └──────┘ └──────┘ └──────┘     └──────┘          │
│      │                                                       │
│      │ acquire() → shared_ptr<FrameData> (ref_count = 1)     │
│      ▼                                                       │
│  ┌──────┐ ← Capture 引用 (ref_count >= 1)                    │
│  │in use│ ← Encoder 引用 (ref_count >= 2, 如果还在编码)       │
│  └──────┘ ← preview 引用 (ref_count >= 3, 如果有预览)        │
│      │                                                       │
│      │ 所有引用释放 (ref_count → 0)                           │
│      ▼                                                       │
│  自定义 deleter: 标记 slot 为 free, 通知池可复用              │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 设计考量

- **预分配**：避免运行时分配开销和内存碎片
- **引用计数**：使用 `shared_ptr` + 自定义 `deleter` 实现自动回收
- **DMA-BUF 预留**：`FrameData` 内预留 `dma_buf_fd` 字段，为硬件零拷贝做准备
- **池满处理**：当所有 slot 都在使用中时，策略选择：
  - 阻塞等待（保证不丢帧，但可能阻塞采集线程）
  - 丢弃最旧帧（实时优先）
  - 动态扩容（临时扩展，但避免无限增长）

### 3.3 Encoding Layer — 编码层

| 模块 | 职责 | 关键接口 |
|------|------|----------|
| **IVideoEncoder** | 编码器抽象接口 | `encode(frame) -> Result<Packet>`, `setBitrate(bps)`, `requestKeyFrame()`, `flush()` |
| **FFmpegH264Encoder** | 基于 FFmpeg 的 H264 软件编码实现 | 实现 IVideoEncoder |
| **VAAPIH264Encoder** | 基于 VAAPI 的 H264 硬件编码实现 | 实现 IVideoEncoder |
| **EncoderConfig** | 码率、GOP 大小、preset、profile、tune 等 | 配置结构体 |
| **EncodedPacket** | 编码后的数据包（NALU 级别） | data, size, pts, dts, is_keyframe |

#### FFmpeg 编码管线

```
VideoFrame (YUV/NV12)
    │
    ▼
avcodec_send_frame(avctx, avframe)
    │  avframe->data[0..2] → planes
    │  avframe->pts        → 时间戳
    │
    ▼
[FFmpeg 内部编码器]
    │
    ▼
avcodec_receive_packet(avctx, avpkt)
    │  avpkt->data → H264 NAL units
    │  avpkt->flags & AV_PKT_FLAG_KEY
    │
    ▼
EncodedPacket {data, size, pts, keyframe}
```

#### 设计考量

- **策略模式**：通过 `IVideoEncoder` 接口实现 encoder 可插拔
- **动态码率**：运行时调用 `avctx->bit_rate = new_bitrate` 或通过 `av_opt_set`
- **强制关键帧**：`avframe->pict_type = AV_PICTURE_TYPE_I` + `avframe->key_frame = 1`
- **内部队列**：编码器有独立的输入队列和编码线程
- **编码延迟**：测量 `send_frame` 到 `receive_packet` 的时间，用于拥塞控制水位线

### 3.4 Transport Layer — 传输层

| 模块 | 职责 | 关键接口 |
|------|------|----------|
| **WebRTCManager** | PeerConnection 生命周期管理, 创建 VideoTrack, 发送编码帧 | `init()`, `createOffer()`, `setRemoteSdp()`, `addIceCandidate()`, `sendFrame()` |
| **CustomVideoSource** | 自定义 RTC 视频源, 从编码器输出获取帧 | 继承 `rtc::VideoTrackSourceInterface` (待确认 API) |
| **SignalingClient** | WebSocket 连接到 Signaling Server, 收发 SDP/ICE 消息 | `connect(url)`, `sendSdp(sdp)`, `sendIce(candidate)`, `setCallbacks()` |
| **SignalingMessage** | SDP/ICE 消息的序列化/反序列化 | `toJson()`, `fromJson()` |

#### WebRTC 内部结构

```
WebRTCManager
    │
    ├── PeerConnectionFactory
    │   ├── Signaling Thread  (信令状态机)
    │   ├── Worker Thread     (编解码/媒体处理)
    │   └── Network Thread    (网络 IO)
    │
    ├── PeerConnection
    │   ├── ICE Agent      (连接建立/维护)
    │   ├── DTLS Transport (加密握手)
    │   └── RTP Sender     (媒体发送)
    │
    └── VideoTrackSource (我们实现)
        └── OnFrame(frame) → libwebrtc 内部处理
```

### 3.5 Signaling Layer — 信令层

| 实体 | 职责 |
|------|------|
| **SignalingServer** (独立进程) | 房间管理, SDP/ICE 消息转发, 连接状态维护 |
| **SignalingClient** (C++ 客户端内) | WebSocket 客户端, 消息收发, 回调分发 |

#### 信令协议设计

```jsonc
// 客户端 → 服务器
{
  "type": "join",        // 加入房间
  "room": "live001",
  "client": "broadcaster"
}

{
  "type": "sdp",          // SDP 交换
  "sdp": "v=0\r\no=..."
}

{
  "type": "ice",          // ICE 候选
  "candidate": "candidate:...",
  "sdp_mline_index": 0
}

// 服务器 → 客户端
{
  "type": "joined",       // 加入成功
  "room": "live001",
  "clients": ["viewer1"]
}

{
  "type": "peer_joined",  // 新对端加入
  "client": "viewer1"
}

{
  "type": "sdp",
  "from": "viewer1",
  "sdp": "v=0\r\no=..."
}

{
  "type": "ice",
  "from": "viewer1",
  "candidate": "candidate:...",
  "sdp_mline_index": 0
}
```

### 3.6 Monitoring Layer — 监控层

| 模块 | 职责 | 关键接口 |
|------|------|----------|
| **StatsCollector** | 周期性采集 WebRTC 内部统计 (getStats API) | `poll()`, `getSnapshot()` |
| **StatsSnapshot** | 统计快照：RTT, 丢包率, 抖动, 发送/接收码率, 帧率 | 数据结构 |
| **BitrateController** | 基于网络状态 + 队列水位线动态调整编码码率 | `update(stats, queueDepth) -> targetBitrate` |
| **NetworkQuality** | 网络质量评级 (excellent/good/poor/bad) | `assess(stats) -> Quality` |

#### 混合拥塞控制算法

```
┌──────────────────────────────────────────────────────┐
│              BitrateController 双信号输入              │
├──────────────────────────────────────────────────────┤
│                                                       │
│  信号 1: 队列水位线 (QueueDepth)                      │
│  ┌─────────────────────────────────────────┐         │
│  │ Encoder Output Queue                   │         │
│  │ [████████░░░░░░░░░░░░░░░░░░] 30% full  │         │
│  │                                       │         │
│  │ High watermark (>80%):  ↓ bitrate     │         │
│  │ Low watermark  (<20%):  ↑ bitrate     │         │
│  │ 说明: 编码快但发送慢 → 网络瓶颈         │         │
│  └─────────────────────────────────────────┘         │
│                                                       │
│  信号 2: RTCP 丢包率 (FractionLost)                   │
│  ┌─────────────────────────────────────────┐         │
│  │ loss_rate < 2%:    Additive Increase    │         │
│  │ 2% < loss < 10%:   Hold                 │         │
│  │ loss >= 10%:       Multiplicative Decrease│       │
│  └─────────────────────────────────────────┘         │
│                                                       │
│  最终决策:                                            │
│  target = min(watermark_target, loss_target)          │
│  target = clamp(target, min_bitrate, max_bitrate)    │
│                                                       │
└──────────────────────────────────────────────────────┘
```

---

## 4. 数据流向

### 4.1 主数据流 — 视频帧链路（热路径）

```
Camera Sensor
     │
     ▼
[V4L2 Driver] ─── DMA ───▶ [Kernel Buffer (mmap)]
     │                            │
     ▼                            ▼
[V4L2Capture::dequeue()]   获得 v4l2_buffer {index, bytesused, timestamp}
     │
     ▼
FrameBufferPool::acquire()
  ├─ 从池中获取空闲 slot
  ├─ 将 mmap 数据拷贝/引用到 FrameData
  └─ 返回 shared_ptr<FrameData>
     │
     ▼
VideoFrame(shared_ptr<FrameData>, format, width, height, stride, pts)
     │
     ├── (0次拷贝) shared_ptr 传递 ──────┐
     ▼                                   ▼
[Encoder Input Queue]              [Preview/Stats (可选)]
     │
     ▼
[Encoder Thread]
avcodec_send_frame() → [FFmpeg] → avcodec_receive_packet()
     │
     ▼
EncodedPacket {data*, size, pts, is_keyframe}
     │
     ▼
[WebRTC Send Queue]
     │
     ▼
[Network Thread]
CustomVideoSource::OnFrame()
  → libwebrtc 内部: RTP 打包 → SRTP → ICE send
     │
     ▼
[Network] ──── UDP/RTP ────▶
     │
     ▼
[Chrome Browser]
  libwebrtc → SRTP 解密 → RTP 解包 → JitterBuffer → H264 Decoder → Render
```

### 4.2 信令数据流

```
   [C++ App]              [Signaling Server]           [Chrome Browser]
       │                        │                           │
       │── WebSocket Connect ──▶│                           │
       │── Join Room ──────────▶│                           │
       │── Create PeerConnection│                           │
       │── CreateOffer() ──────▶│                           │
       │── SetLocalDescription()│                           │
       │                        │                           │
       │── SDP Offer ──────────▶│─── SDP Offer ───────────▶│
       │                        │◄── SDP Answer ──────────│
       │◄─ SDP Answer ────────│                           │
       │── SetRemoteDescription()                          │
       │                        │                           │
       │── ICE Candidate ──────▶│─── ICE Candidate ───────▶│
       │◄─ ICE Candidate ──────│◄── ICE Candidate ─────────│
       │── AddIceCandidate()    │                           │
       │                        │◄══ DTLS Handshake ═══════▶│
       │                        │◄═══ SRTP Media ═══════════▶│
```

### 4.3 控制/反馈数据流

```
[Network] ──── RTCP RR/REMB/TCC ────▶ [libwebrtc 内部处理]
                                              │
                                              │ (RTCPCallback / getStats)
                                              ▼
                                       [StatsCollector::poll()]
                                       提取: RTT, loss_rate, jitter,
                                             target_bitrate, encode_bitrate,
                                             packets_sent, nack_count
                                              │
                                              ▼
                                       [BitrateController::update()]
                                       输入: stats + encoder_queue_depth
                                       算法: 水位线 + AIMD
                                       输出: target_bitrate
                                              │
                                              ▼
                                       [IVideoEncoder::setBitrate(target)]
                                       修改 avctx->bit_rate
                                       影响后续编码输出
```

---

## 5. 线程模型设计

### 5.1 线程全景

```
┌──────────────────────────────────────────────────────────────┐
│                       线程架构                                 │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  Main Thread (主线程)                                          │
│  ├─ 所有者: 本应用                                              │
│  ├─ AppController 状态机                                       │
│  ├─ 初始化各模块                                                │
│  ├─ 信号处理 (SIGINT/SIGTERM)                                   │
│  └─ 优雅关闭协调 (各模块 join/stop)                              │
│                                                               │
│  Capture Thread (采集线程) ← 自己管理                            │
│  ├─ V4L2 dequeue 循环 (epoll 等待)                              │
│  ├─ FrameBufferPool::acquire()                                 │
│  ├─ 封装 VideoFrame                                             │
│  └─ 投递到 Encoder Input Queue (SPSC)                           │
│                                                               │
│  Encoder Thread (编码线程) ← 自己管理                            │
│  ├─ 从 Encoder Input Queue 取帧                                 │
│  ├─ avcodec_send_frame / avcodec_receive_packet                │
│  ├─ 生成 EncodedPacket                                          │
│  └─ 投递到 WebRTC Send Queue (SPSC)                             │
│                                                               │
│  Signaling Thread (libwebrtc 内部)                              │
│  ├─ PeerConnection 信令状态机                                   │
│  ├─ ICE 收集/连接检查/保活                                      │
│  └─ DTLS 握手                                                  │
│                                                               │
│  Network Thread (libwebrtc 内部)                                │
│  ├─ RTP 打包/发送                                              │
│  ├─ RTCP 接收/处理                                             │
│  ├─ SRTP 加解密                                                │
│  └─ SCTP 数据通道                                              │
│                                                               │
│  Worker Thread (libwebrtc 内部)                                 │
│  └─ 媒体处理（如果需要重编码等）                                  │
│                                                               │
│  Stats Thread (统计线程) ← 自己管理                              │
│  ├─ 周期性 (1s) 调用 pc->GetStats()                             │
│  ├─ 计算丢包率/抖动/RTT                                         │
│  ├─ 运行 BitrateController 算法                                 │
│  └─ 触发 Encoder::setBitrate() (通过原子变量或回调)              │
│                                                               │
│  SignalingClient Thread (WebSocket IO) ← 自己管理               │
│  ├─ WebSocket 收发 (使用 libwebsockets 或 IXWebSocket)          │
│  ├─ JSON 序列化/反序列化                                       │
│  └─ 回调主线程 (通过线程安全回调队列)                             │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 线程间通信拓扑

```
                        SignalingClient Thread
                              │
                    (Callback Queue)
                              │
                              ▼
  Capture ──(SPSC)──▶ Encoder ──(SPSC)──▶ WebRTC(libwebrtc)
  Thread             Thread              Network Thread
    │                   │                     │
    │                   │              (getStats API poll)
    │                   │                     │
    │                   ▼                     ▼
    │              EncoderQueueDepth    StatsCollector
    │                   │               (Stats Thread)
    │                   │                     │
    │                   ▼                     │
    │              BitrateController ◀────────┘
    │                   │
    │                   │ (atomic target_bitrate)
    │                   ▼
    └──────────── Encoder::setBitrate()
```

### 5.3 线程安全策略

| 数据结构 | 访问模式 | 同步机制 |
|---------|---------|---------|
| FrameBufferPool | Capture 写, Encoder 读 (引用计数) | `shared_ptr` 原子引用计数 + mutex 保护池状态 |
| Encoder Input Queue | Capture 推, Encoder 取 | Lock-free SPSC Queue |
| WebRTC Send Queue | Encoder 推, Network Thread 取 | Lock-free SPSC Queue |
| StatsSnapshot | Stats Thread 写, BitrateController 读 | `std::atomic<shared_ptr<StatsSnapshot>>` (RCU) |
| BitrateController state | Stats Thread 写, Encoder Thread 读 (target) | `std::atomic<int64_t>` |
| Signaling callbacks | SignalingClient Thread → Main Thread | 回调队列 + mutex |
| AppController state | Main Thread 独占 | 无竞争 |

---

## 6. 关键技术深度分析

### 6.1 VideoFrame 生命周期 — 四种方案对比

#### 方案 A: Copy

```
Camera Buffer → memcpy → New Buffer → Encoder → memcpy → AVFrame
拷贝次数: 至少 2 次
CPU 占用: 1080p@30fps ≈ 93MB/s 内存带宽
延迟: 每次拷贝增加 ~0.5-1ms
优点: 实现简单，生命周期清晰
缺点: 内存带宽浪费
```

#### 方案 B: shared_ptr

```
V4L2 mmap Buffer → shared_ptr<FrameData> ──零拷贝──▶ Encoder
                                                ──零拷贝──▶ Preview
拷贝次数: 0 (在同一地址空间内)
延迟: 无额外延迟
优点: 零拷贝，生命周期自动管理，可实现多消费者
缺点: 所有消费者必须在同一进程内
```

#### 方案 C: Buffer Pool

```
FrameBufferPool (N 个预分配 slot)
    │
    ├── acquire() → slot_i → ... → release() → 回池
    ├── acquire() → slot_j → ... → release() → 回池
    │                           (引用计数归零时)
拷贝次数: 0
优点: 预分配，无运行时内存分配，固定内存占用
缺点: 需要管理池大小，池满需要策略
```

#### 方案 D: DMA-BUF fd

```
V4L2 → export dma_buf fd → VAAPI Encoder (通过 kms/gbm 直接读取)
                                    │
                            零 CPU 拷贝, GPU 直接访问
拷贝次数: 0 (连 CPU 一侧的数据访问都没有)
优点: 极低延迟，极低 CPU，硬件通路
缺点: API 复杂，设备兼容性，调试困难
```

#### 本项目推荐方案

```
Phase 1 (当前): Buffer Pool + shared_ptr
  路径: V4L2 mmap → FrameBufferPool → shared_ptr<FrameData> → Encoder
  优点: 生产和项目级别的鲁棒性
  拷贝: 1 次 (mmap buffer → pool buffer，如果需要格式转换)

Phase 2 (优化): DMA-BUF fd
  路径: V4L2 dmabuf_export → FrameBufferPool (fd) → VAAPI Encoder
  优点: 零 CPU 拷贝
```

### 6.2 WebRTC PeerConnection 建立全流程

```
═══════════════════════════════════════════════════════════════
Phase 1: Factory 创建
═══════════════════════════════════════════════════════════════
webrtc::CreatePeerConnectionFactory(
    network_thread,       // 网络 IO
    worker_thread,        // 媒体处理
    signaling_thread,     // 信令
    ...
)
└─ 创建成功返回 factory (线程安全的 ref-counted 对象)

═══════════════════════════════════════════════════════════════
Phase 2: PeerConnection 创建
═══════════════════════════════════════════════════════════════
webrtc::PeerConnectionInterface::RTCConfiguration config;
config.servers = {
    { "stun:stun.l.google.com:19302" },  // STUN
    // { "turn:...", "user", "pass" },    // TURN (生产环境)
};
config.type = webrtc::PeerConnectionInterface::IceTransportsType::kAll;
config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyBalanced;

pc = factory->CreatePeerConnection(config, ..., ..., observer);
// observer 实现 PeerConnectionObserver 接口

═══════════════════════════════════════════════════════════════
Phase 3: Media Track 添加
═══════════════════════════════════════════════════════════════
// 创建自定义视频源
rtc::scoped_refptr<CustomVideoSource> video_source =
    CustomVideoSource::Create();

// 创建 VideoTrack
rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track =
    factory->CreateVideoTrack("video_label", video_source);

// 添加到 PeerConnection (或用 AddTrack)
auto result = pc->AddTrack(video_track, {"stream_id"});
if (!result.ok()) { /* error handling */ }

═══════════════════════════════════════════════════════════════
Phase 4: SDP 协商 (Offer/Answer)
═══════════════════════════════════════════════════════════════
// 1. 创建 Offer
pc->CreateOffer(observer, options);
// → observer->OnSuccess(SessionDescriptionInterface* sdp)
// sdp 包含:
//   - 媒体行 (m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 ...)
//   - codec 信息 (a=rtpmap:96 H264/90000)
//   - ICE 凭证 (a=ice-ufrag:... a=ice-pwd:...)
//   - DTLS 指纹 (a=fingerprint:sha-256 ...)
//   - SSRC (a=ssrc:... cname:...)

// 2. 设置本地描述
pc->SetLocalDescription(observer, sdp);

// 3. 发送 Offer 到 Signaling Server
signaling->sendSdp(sdp->ToString());  // JSON over WebSocket

// 4. 收到远程 Answer (来自 signaling 回调)
signaling->onSdpAnswer = [&](std::string sdp_str) {
    auto sdp = CreateSessionDescription("answer", sdp_str);
    pc->SetRemoteDescription(observer, sdp);
};

═══════════════════════════════════════════════════════════════
Phase 5: ICE 流程
═══════════════════════════════════════════════════════════════
// ICE Agent 自动开始收集候选地址
// observer->OnIceCandidate(IceCandidateInterface* candidate)
// 候选类型:
//   host     (192.168.1.x:random_port)    ← 本地网卡
//   srflx    (public_ip:port from STUN)   ← NAT 穿透
//   relay    (turn_server:port)            ← 中继备选
//   prflx    (peer reflexive)              ← 对端发现的

// 每个候选通过 signaling 发送给对端
signaling->sendIce(candidate);

// 收到对端候选
signaling->onIceCandidate = [&](IceCandidate candidate) {
    pc->AddIceCandidate(candidate);
    // ICE Agent 开始对此候选进行连通性检查
};

// ICE 连通性检查 (STUN Binding):
//   优先级: host(126) > srflx(100) > relay(0)  + local preference
//   Nomination: 选通后标记为 nominated pair
//   连接建立: 选定 candidate pair 后开始 DTLS

═══════════════════════════════════════════════════════════════
Phase 6: DTLS 握手
═══════════════════════════════════════════════════════════════
// 在选定的 ICE candidate pair 上
// 使用 DTLS 1.2 + DTLS-SRTP extension
//
// 握手过程:
//   ClientHello  ──────────────▶
//                 (加密套件, 随机数)
//   ServerHello,
//   Certificate,                ◀──────────────
//   ServerKeyExchange,          ServerHelloDone
//   (ECDHE 密钥交换)
//   ClientKeyExchange,
//   ChangeCipherSpec,           ──────────────▶
//   Finished
//                      ◀──────────────  ChangeCipherSpec, Finished
//
// 从 DTLS 握手导出 SRTP 密钥对:
//   - SRTP master key (用于 RTP 加密)
//   - SRTCP master key (用于 RTCP 认证)
//   - 密钥派生: TLS Extractors (RFC 5705, use_srtp extension)

═══════════════════════════════════════════════════════════════
Phase 7: SRTP 媒体流
═══════════════════════════════════════════════════════════════
// SRTP (RFC 3711): Secure RTP
// 加密:  AES-CTR (默认) 或 AES-GCM
// 认证:  HMAC-SHA1 (80-bit tag)
//
// 发送:
// RTP Payload → AES 加密 → HMAC → [RTP Header(12B)] [Encrypted Payload] [Auth Tag(10B)]
//
// RTCP:
// Sender Report (SR):   发送端统计 (NTP timestamp, RTP timestamp, packet count, octet count)
// Receiver Report (RR): 接收端统计 (fraction lost, cumulative lost, jitter, LSR, DLSR)
// REMB:                 接收端最大码率估计
// TCC (Transport-CC):   传输层拥塞控制反馈

═══════════════════════════════════════════════════════════════
Phase 8: 动态性
═══════════════════════════════════════════════════════════════
// ICE Restart: 网络切换时重新收集候选
// ICE Consent Freshness: 每 30s 发送 STUN Binding 确认对端还在
// DTLS renegotiation: 密钥过期后重新协商
// SSRC 变更: 编码器重置后可能需要新的 SSRC
```

### 6.3 GCC 拥塞控制深度分析

```
═══════════════════════════════════════════════════════════════
Google Congestion Control (GCC) — 完整算法
═══════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────┐
│                 Send-side (RTCP-based)                   │
│                                                          │
│  输入: RTCP RR (fraction_lost)                          │
│        RTCP REMB/TCC (receiver estimated max bitrate)    │
│                                                          │
│  Loss-based controller:                                  │
│  ┌────────────────────────────────────────────────┐     │
│  │ if fraction_lost < 2%:                         │     │
│  │     A_loss = A_loss * 1.08      (乘性增加)      │     │
│  │ elif fraction_lost < 10%:                      │     │
│  │     A_loss = A_loss            (保持不变)       │     │
│  │ else:                                           │     │
│  │     A_loss = A_loss * (1 - 0.5 * fraction_lost) │     │
│  │                    (乘性减少)                    │     │
│  └────────────────────────────────────────────────┘     │
│                                                          │
│  A_target = min(A_loss, A_remb)                          │
│  A_target = clamp(A_target, min_bps, max_bps)           │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│              Receive-side (delay-based)                  │
│                                                          │
│  输入: RTP 到达时间, RTP 时间戳                           │
│                                                          │
│  1. Arrival-time filter (卡尔曼滤波):                    │
│     d(i) = t_arrive(i) - t_arrive(i-1)                  │
│           - (t_send(i) - t_send(i-1))                   │
│     m(i) = 估计的 one-way delay gradient                 │
│                                                          │
│  2. Overuse detector:                                    │
│     m(i) 通过自适应阈值比较                               │
│     → overuse / normal / underuse                       │
│                                                          │
│  3. Adaptive threshold:                                  │
│     threshold = gamma * RTT + delta                      │
│     动态调整以控制灵敏度                                  │
│                                                          │
│  4. Rate controller:                                     │
│     overuse:  A_delay = A_delay * alpha * RTT           │
│     normal:   A_delay = A_delay + beta                  │
│     underuse: A_delay = A_delay * eta                   │
└─────────────────────────────────────────────────────────┘
```

#### 我们的简化实现 — 混合控制器

```cpp
// 为什么用"队列水位线"补充纯 GCC：
//
// 1. 编码器输出队列直接反映"编码速度 vs 网络发送速度"的差值
// 2. 比 RTCP 反馈的延迟更低（本地信号 vs 网络往返）
// 3. 可以作为早期预警：队列开始增长 → 网络变慢，不等丢包
// 4. 这是你已有的优化经验，有实战价值
//
// 混合策略:
//   信号1 (快速): 队列水位线 → 快速响应 (本地, ~10ms)
//   信号2 (慢速): RTCP 丢包率 → 精确值 (网络, ~100ms-1s)
//   决策: target = min(queue_safe_rate, loss_safe_rate)
```

---

## 7. 技术决策对比分析

### 7.1 编码器方案

| 维度 | FFmpeg libx264 | VAAPI (Intel) | NVENC (NVIDIA) |
|------|---------------|---------------|----------------|
| 兼容性 | 任何 CPU | Intel GPU | NVIDIA GPU |
| 延迟 | ~10-30ms | ~3-5ms | ~2-4ms |
| CPU 占用 | 高 (20-40% 一核) | 极低 (~1%) | 极低 (~1%) |
| 码率控制 | 丰富 (CRF/CBR/VBR) | 有限 | 较好 |
| 开发难度 | 低 (成熟 API) | 中 (需要 libva + libva-drm) | 中 (NvCodec) |
| 本项目推荐 | Phase 1 ✅ | Phase 2 | 可选 |

### 7.2 帧传递方案

| 维度 | Copy | shared_ptr | Buffer Pool | DMA-BUF fd |
|------|------|-----------|-------------|------------|
| 拷贝次数 | N 次 | 0 | 0 | 0 |
| CPU 开销 | 高 | 极低 | 极低 | 极低 |
| 内存占用 | 2x | 1x + refcnt | 固定预分配 | 固定预分配 |
| 跨硬件共享 | ❌ | ❌ (CPU only) | ❌ | ✅ |
| 复杂度 | ★☆☆☆☆ | ★★☆☆☆ | ★★★☆☆ | ★★★★★ |
| 调试难度 | 低 | 中 | 中 | 高 |
| Phase 1 | — | ✅ | ✅ | — |
| Phase 2 | — | — | ✅ | ✅ |

### 7.3 信令传输方案

| 维度 | WebSocket | HTTP Long Poll | SSE | gRPC |
|------|-----------|---------------|-----|------|
| 双向通信 | ✅ | ❌ (fake) | ❌ (server→client) | ✅ |
| 协议开销 | 低 (per-frame) | 高 (per-request) | 低 | 低 |
| 浏览器兼容 | ✅ | ✅ | ✅ | ❌ (需要 grpc-web) |
| C++ 客户端库 | IXWebSocket, libwebsockets | libcurl | ❌ | gRPC C++ |
| 本项目推荐 | ✅ | — | — | — |

---

## 8. 开发路线图

```
Phase 0: 基础框架 ████████████░░░░░░░░░░░░ (1-2 天)
├─ Task 0.1: 项目骨架, CMake 构建系统
├─ Task 0.2: 日志系统 (spdlog), 编译选项
├─ Task 0.3: AppController 状态机骨架
├─ Task 0.4: Result<T> 错误处理类型
└─ Task 0.5: 编码规范, .clang-format

Phase 1: V4L2 采集 ░░░░░░░░░░░░░░░░░░░░░░ (2-3 天)
├─ Task 1.1: DeviceEnumerator — 枚举摄像头
├─ Task 1.2: CaptureConfig — 分辨率/帧率/格式选择
├─ Task 1.3: V4L2Capture — 设备打开/配置
├─ Task 1.4: V4L2Capture — mmap buffer 管理
├─ Task 1.5: V4L2Capture — 采集循环 (epoll)
└─ Task 1.6: 单元测试 (v4l2loopback mock)

Phase 2: 帧管理 ░░░░░░░░░░░░░░░░░░░░░░░░░░ (1-2 天)
├─ Task 2.1: VideoFrame + FrameData 数据结构
├─ Task 2.2: FrameBufferPool (引用计数 + 池管理)
├─ Task 2.3: SPSC 线程安全队列
└─ Task 2.4: 帧传递测试 (多线程引用计数)

Phase 3: H264 编码 ░░░░░░░░░░░░░░░░░░░░░░ (2-3 天)
├─ Task 3.1: IVideoEncoder 抽象接口
├─ Task 3.2: EncoderConfig 配置结构体
├─ Task 3.3: FFmpegH264Encoder — FFmpeg 初始化
├─ Task 3.4: FFmpegH264Encoder — encode 循环
├─ Task 3.5: 动态码率调整接口
├─ Task 3.6: 关键帧请求 + flush
└─ Task 3.7: 编码测试 (输入 YUV 文件验证输出)

Phase 4: Signaling ░░░░░░░░░░░░░░░░░░░░ (2 天)
├─ Task 4.1: 信令协议设计 + JSON 格式
├─ Task 4.2: SignalingClient — WebSocket 封装
├─ Task 4.3: SignalingClient — 消息收发 + 回调
├─ Task 4.4: SignalingServer (Node.js) — 房间 + 转发
└─ Task 4.5: 信令集成测试

Phase 5: WebRTC ░░░░░░░░░░░░░░░░░░░░░░░░ (5-7 天) ★ 核心
├─ Task 5.1: libwebrtc 编译/集成到 CMake
├─ Task 5.2: WebRTCManager — Factory + PC 创建
├─ Task 5.3: CustomVideoTrackSource 实现
├─ Task 5.4: PeerConnectionObserver 回调
├─ Task 5.5: SDP Offer/Answer 流程
├─ Task 5.6: ICE 候选收集 + 交换
├─ Task 5.7: DTLS 握手状态监控
├─ Task 5.8: 编码帧 → WebRTC 发送通路
├─ Task 5.9: 端到端测试 (Chrome 接收视频)
└─ Task 5.10: 重新协商 (网络切换等)

Phase 6: 监控与自适应 ░░░░░░░░░░░░░░░░ (2-3 天)
├─ Task 6.1: StatsCollector — getStats 集成
├─ Task 6.2: StatsSnapshot 数据结构
├─ Task 6.3: BitrateController — 队列水位线算法
├─ Task 6.4: BitrateController — 丢包率反馈
├─ Task 6.5: NetworkQuality 评估
└─ Task 6.6: 实时日志输出 + 调试

Phase 7: 优化与测试 ░░░░░░░░░░░░░░░░░░ (3-5 天)
├─ Task 7.1: 端到端延迟测量 + profiling
├─ Task 7.2: 内存/CPU profiling
├─ Task 7.3: 异常恢复 (断连重连, 热插拔)
├─ Task 7.4: 长时间稳定性测试 (24h+)
├─ Task 7.5: 性能基准数据收集
└─ Task 7.6: 文档 + 面试准备
```

---

## 9. 面试亮点设计

### 9.1 可以深入讨论的技术点

#### 亮点 1: VideoFrame 零拷贝路径

> "我设计了一个 FrameBufferPool，结合 shared_ptr 引用计数和 DMA-BUF 文件描述符导出，实现了从 V4L2 内核缓冲区到硬件编码器的全零拷贝路径。
>
> 第一阶段使用 mmap + shared_ptr 实现 CPU 可见的零拷贝共享；
> 第二阶段预留了 DMA-BUF fd 接口，可以绕过 CPU 直接将帧数据传递给 GPU 编码器。
>
> 我分析了四种帧传递方案（Copy / shared_ptr / Buffer Pool / DMA-BUF fd），对比了它们在延迟、CPU 占用、内存带宽和实现复杂度上的 trade-off，选择了适合我们项目阶段的方案。"

#### 亮点 2: 三级流水线线程模型

> "我设计了 Capture → Encode → WebRTC 的三级异步流水线，使用 lock-free SPSC 队列解耦每级处理。
>
> 关键设计决策：
> - 为什么不用合并 Capture+Encode 线程？—— V4L2 出队有实时性要求（帧不能被 V4L2 driver 覆盖），编码延迟可能抖动。
> - 为什么不直接用 WebRTC 内部 encoder？—— 我们需要在编码器输出后做自适应码率控制，需要独立控制编码参数。
> - SPSC 队列保证了每级之间的低延迟（< 1μs 的 push/pop）和无锁操作。"

#### 亮点 3: 混合拥塞控制

> "我实现了一个双信号混合拥塞控制算法：
> - 快速信号：编码器输出队列水位线（本地，~10ms 响应），直接反映编码和网络的速度差
> - 慢速信号：RTCP 丢包率反馈（网络，~100ms-1s 响应），反映真实网络拥塞
>
> 对比纯 GCC：
> - 队列水位线比 RTCP 反馈提前 1-2 个 RTT 检测到拥塞
> - 在丢包发生前就可以降低码率（队列开始积累时）
> - 这来自于我在直播优化中的实际经验"

#### 亮点 4: WebRTC 内部协议栈理解

> "我不仅调用了 libwebrtc 的 API，还深入理解了每一层的协议细节：
> - ICE: 候选收集优先级算法 (host > srflx > relay)，连通性检查的 STUN Binding 流程，Nomination 策略
> - DTLS: 自签名证书生成，fingerprint 通过 SDP 带外验证，DTLS-SRTP extension 如何导出 SRTP 密钥
> - SRTP: AES-CTR 加密和 HMAC-SHA1 认证的 per-packet 开销
> - RTP/RTCP: 序列号和时间戳的 NTP 转换，RTCP SR/RR/REMB/TCC 各自的作用
> - GCC: delay-based (卡尔曼滤波 + 过载检测器) 和 loss-based 两大控制器的协同"

### 9.2 可能的面试追问及回答方向

| 面试问题 | 回答方向 |
|---------|---------|
| **为什么要自己实现，不用 GStreamer/FFmpeg 的推流？** | 学习目标是理解 WebRTC 协议栈，GStreamer 封装了太多细节。而且很多实时通信公司需要定制协议栈。 |
| **端到端延迟是多少？** | Glass-to-glass < 200ms。分解：V4L2 采集 10-30ms, 编码 10-30ms, 网络 RTT 10-40ms, JitterBuffer 30-80ms, 解码+渲染 5-15ms。 |
| **如何处理网络抖动？** | WebRTC 内置 JitterBuffer + NACK 重传 + FEC 前向纠错。加上我们的动态码率调整在应用层的主动适应。 |
| **为什么选 H264 而不是 H265/VP9/AV1？** | 浏览器兼容性最广（Chrome/Firefox/Safari 均支持），硬件编码器支持最好，延迟最低。 |
| **多路流如何扩展？** | SFU 架构：每路流独立的编码器 + PeerConnection，通过 Selective Forwarding Unit 路由。PeerConnection 可以复用 DTLS 连接（bundle）。 |
| **编码器输出帧率不稳怎么办？** | 我们的 FrameBufferPool + 队列水位线会检测积压，触发丢帧或降低采集帧率。 |
| **如果 V4L2 设备断开怎么办？** | 实现了热插拔检测（udev monitor 或 poll 错误处理），自动重连、重新配置设备。 |

---

## 10. 待决策问题

以下是进入 Step 2 之前需要确认的架构决策：

### Q1: WebRTC 库选型

| 选项 | 说明 |
|------|------|
| **A. Google libwebrtc (源码编译)** ★ 推荐 | 最完整, 学习价值最高, 但源码 ~30GB, 编译复杂, 需要 depot_tools |
| **B. libdatachannel** | 轻量, 易集成, 但只提供 DataChannel, 媒体需要额外处理 |
| **C. GStreamer + webrtcbin** | 生产力高, 但封装太深, 失去学习目标 |

### Q2: 信令服务器语言

| 选项 | 说明 |
|------|------|
| **A. Node.js** ★ 推荐 | WebSocket 库成熟 (ws), 快速开发, 生态丰富 |
| **B. Go** | 高性能, 与 C++ 风格接近, 但增加学习成本 |
| **C. Python** | 最简单快速, 但性能和并发模型不如前两者 |

### Q3: 编码器首版方案

| 选项 | 说明 |
|------|------|
| **A. FFmpeg libx264** ★ 推荐 | 软件编码, 兼容性最好, 先验证端到端流程 |
| **B. VAAPI 硬件编码** | 低延迟低 CPU, 但需要 Intel GPU, 调试复杂 |

### Q4: C++ 标准版本

| 选项 | 说明 |
|------|------|
| **A. C++17** | 稳定, 编译器支持好, 够用 |
| **B. C++20** | `std::span`, coroutine, `std::format`, 更现代 |

### Q5: 构建系统细节

| 选项 | 说明 |
|------|------|
| **A. CMake + vcpkg** | 跨平台包管理, 适合生产项目 |
| **B. CMake + FetchContent** | 纯 CMake, 自动下载依赖, 更自包含 |
| **C. CMake + system packages** | 最小依赖, 使用系统已安装的库 |

---

## 附录

### A. 参考资料

- [WebRTC for the Curious](https://webrtcforthecurious.com/) — WebRTC 协议详解
- [RFC 8825 - WebRTC 概述](https://datatracker.ietf.org/doc/rfc8825/)
- [RFC 8831 - WebRTC Data Channels](https://datatracker.ietf.org/doc/rfc8831/)
- [RFC 8832 - WebRTC Data Channel Establishment Protocol](https://datatracker.ietf.org/doc/rfc8832/)
- [RFC 8835 - Transports for WebRTC](https://datatracker.ietf.org/doc/rfc8835/)
- [GCC Algorithm (Google WebRTC)](https://datatracker.ietf.org/doc/html/draft-ietf-rmcat-gcc-02)
- [V4L2 API Documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)
- [FFmpeg Encoding Guide](https://trac.ffmpeg.org/wiki/Encode/H.264)

### B. 术语表

| 术语 | 全称 | 说明 |
|------|------|------|
| V4L2 | Video4Linux2 | Linux 视频采集 API |
| NALU | Network Abstraction Layer Unit | H264 编码数据单元 |
| SDP | Session Description Protocol | 会话描述协议 |
| ICE | Interactive Connectivity Establishment | 交互式连接建立 |
| STUN | Session Traversal Utilities for NAT | NAT 穿透工具 |
| TURN | Traversal Using Relays around NAT | 中继穿透 |
| DTLS | Datagram Transport Layer Security | 数据报 TLS |
| SRTP | Secure Real-time Transport Protocol | 安全 RTP |
| RTCP | RTP Control Protocol | RTP 控制协议 |
| GCC | Google Congestion Control | Google 拥塞控制算法 |
| REMB | Receiver Estimated Maximum Bitrate | 接收端最大码率估计 |
| TWCC | Transport-Wide Congestion Control | 传输层拥塞控制 |
| NACK | Negative Acknowledgment | 否定确认（丢包重传请求） |
| PLI | Picture Loss Indication | 图像丢失指示（请求关键帧） |
| FIR | Full Intra Request | 完整帧内刷新请求 |
| DMA-BUF | DMA Buffer Sharing | DMA 缓冲区跨设备共享 |
| VAAPI | Video Acceleration API | Intel 视频硬件加速 API |
| MPMC | Multi-Producer Multi-Consumer | 多生产者多消费者队列 |
| SPSC | Single-Producer Single-Consumer | 单生产者单消费者队列 |
