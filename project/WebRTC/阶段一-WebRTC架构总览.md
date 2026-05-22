# 阶段一：WebRTC 整体架构总览

> 自顶向下梳理 WebRTC 的核心架构，目标是把"分层、对象、线程、数据流、能力地图"五块讲透，
> 为后续阶段二的项目设计和阶段三的模块下钻打地基。

---

## 目录

1. [总览框图（Big Picture）](#0-总览框图big-picture)
2. [核心分层与职责边界](#1-核心分层与职责边界)
3. [关键对象与生命周期](#2-关键对象与生命周期)
4. [线程模型（面试重灾区）](#3-线程模型面试重灾区)
5. [核心数据流：一帧视频的旅程](#4-核心数据流一帧视频的旅程)
6. [关键能力地图](#5-关键能力地图)
7. [整体设计哲学](#6-整体设计哲学约-350-字)
8. [面试视角问题清单](#7-本阶段对应的面试视角问题后续会逐一深挖)

---

## 0. 总览框图（Big Picture）

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          应用层 (App / JS / Native UI)                    │
│           getUserMedia · createOffer · addTrack · ondatachannel           │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
┌──────────────────────────────────────────────────────────────────────────┐
│                 API 层 (PeerConnection / MediaStream API)                 │
│   PeerConnectionFactory ─▶ PeerConnection ─▶ RtpSender/RtpReceiver/DC    │
│                  SessionDescription · IceCandidate · Observer            │
└──────────────────────────────────────────────────────────────────────────┘
        │                                                       │
        ▼                                                       ▼
┌──────────────────────────┐                  ┌─────────────────────────────┐
│   媒体引擎层 MediaEngine  │                  │     网络传输层 Transport     │
│ ┌──────────┐ ┌─────────┐ │                  │ ┌─────┐ ┌──────┐ ┌────────┐ │
│ │VideoEngine│ │AudioEng.│ │                  │ │ ICE │ │ DTLS │ │ SRTP   │ │
│ │ Capture   │ │ Capture │ │                  │ └─────┘ └──────┘ └────────┘ │
│ │ Encoder   │ │ Encoder │ │   RTP/RTCP       │ ┌────────────────────────┐  │
│ │ Pacer     │ │ NetEQ   │ │ ───────────────▶ │ │  RtpTransport / BWE    │  │
│ │ JitterBuf │ │ AEC/AGC │ │                  │ │  GCC / BBR · CC-FB     │  │
│ │ Decoder   │ │ Decoder │ │                  │ └────────────────────────┘  │
│ │ Renderer  │ │ Playout │ │                  │       UDP / TCP / TURN      │
│ └──────────┘ └─────────┘  │                  └─────────────────────────────┘
└──────────────────────────┘
        │                                                       │
        ▼                                                       ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                平台适配层 (PAL: OS / HW / Codec / NIC)                    │
│   Camera/Mic 驱动 · 硬件编解码(H.264/H.265) · 网卡 · 系统线程 · 时钟      │
└──────────────────────────────────────────────────────────────────────────┘

        ┌────────── 信令通道 (业务自实现, WebSocket / HTTPS) ──────────┐
        │       SDP Offer/Answer · ICE Candidate 交换 · 房间路由       │
        └──────────────────────────────────────────────────────────────┘
```

**一句话总结**：WebRTC 的核心是**控制面与数据面分离**——左侧是"业务+媒体处理"，右侧是"网络传输+加密+拥塞控制"，二者通过 RTP/RTCP 这条窄腰耦合。

---

## 1. 核心分层与职责边界


| 层          | 主要职责                                     | 不能做的事                 |
| ---------- | ---------------------------------------- | --------------------- |
| **应用层**    | 业务逻辑、UI、信令服务接入                           | 不直接操作 RTP/编码器         |
| **API 层**  | 暴露 `PeerConnection` 等高层抽象，封装协商/轨道管理      | 不直接做编码和网络收发           |
| **媒体引擎层**  | 采集、编码、Pacing、抖动缓冲、解码、渲染、3A               | 不直接 send/recv socket  |
| **网络传输层**  | ICE 打洞、DTLS 握手、SRTP 加解密、RTP/RTCP 收发、拥塞控制 | 不感知"这帧是 I 帧还是 P 帧"的语义 |
| **平台适配层**  | 摄像头/麦克风驱动、HW Codec、Socket、时钟、线程          | 不参与任何业务逻辑             |
| **信令(外部)** | SDP/Candidate 交换、房间管理                    | 不归 WebRTC 标准管，业务自定    |


**依赖方向**：应用 → API → (媒体引擎 ‖ 网络传输) → PAL。

**关键约束**：

- 媒体引擎和网络传输是**平级**的，二者通过 RTP/RTCP 这条窄腰耦合
- **不允许反向依赖**（网络层不能调 Encoder，媒体层不能直接碰 socket）
- 信令通道是 WebRTC 标准之外的部分，业务自己实现

这是 WebRTC 能把"媒体处理"和"网络抗丢包"独立演进的关键。

---

## 2. 关键对象与生命周期

### 对象关系图

```
PeerConnectionFactory  (进程级, 单例, 持有全局线程池/编解码工厂)
        │ CreatePeerConnection(config, observer)
        ▼
PeerConnection         (连接级, 一次会话一个)
        │ AddTrack / AddTransceiver
        ├──▶ RtpSender ──▶ MediaStreamTrack(Source) ──▶ Encoder ──▶ RtpTransport
        ├──▶ RtpReceiver ◀── Decoder ◀── JitterBuffer ◀── RtpTransport
        ├──▶ DataChannel  (基于 SCTP over DTLS)
        └──▶ Call / Channel (媒体调度核心, 把 Track 绑到 RTP 流)

Transport 子系统:
   IceTransport ─▶ DtlsTransport ─▶ SrtpTransport ─▶ RtpTransport
```

### 创建顺序

1. `PeerConnectionFactory::Create()` —— **进程内一次**，持有 Signaling/Worker/Network 三条线程
2. `factory->CreatePeerConnection(config, observer)` —— **每会话一次**
3. `peerConnection->AddTrack(audioTrack/videoTrack)` —— 把媒体源挂进来
4. `peerConnection->CreateOffer(...)` → `SetLocalDescription` → 通过信令发给对端
5. 收到对端 Answer → `SetRemoteDescription`
6. ICE 候选交换完成 → `OnIceConnectionStateChange = Connected` → 媒体真正流动

### 销毁顺序

与创建相反：

1. `peerConnection->Close()` 触发 PC 停止收发
2. 释放 Track / Sender / Receiver
3. 最后让 Factory 自然回收线程

### 生命周期陷阱

`PeerConnection::Close()` 之后线程上可能还有未执行的 `PostTask`，所以 WebRTC 内部大量用：

- `rtc::scoped_refptr<T>`（引用计数智能指针）
- `WeakPtr<T>`（解决跨线程回调时对象已销毁的问题）

**铁律**：禁止裸指针跨线程传递。

---

## 3. 线程模型（面试重灾区）

WebRTC 内部固定三条核心线程：


| 线程                   | 主要工作                                 | 为什么独立                                |
| -------------------- | ------------------------------------ | ------------------------------------ |
| **Signaling Thread** | 跑 `PeerConnection` API、SDP 协商、状态机    | API 是用户调的，要快速返回，不能被网络/编码阻塞           |
| **Worker Thread**    | 媒体管线调度、`Call`/`Channel`、统计上报         | 媒体处理是 CPU 密集，独占一条避免抖动                |
| **Network Thread**   | 真正的 socket recv/send、ICE 打洞、SRTP 加解密 | I/O 必须低延迟，**禁止任何耗时操作**，否则 RTP 抖动直接放大 |


外加一组**编解码线程**（每路流单独）和**采集/渲染线程**（OS 给的回调线程）。

### 跨线程投递的三种方式


| 方式                             | 语义      | 使用场景              |
| ------------------------------ | ------- | ----------------- |
| `PostTask(loc, std::function)` | 异步、无返回值 | **最常用**，绝大多数跨线程通信 |
| `BlockingCall / Invoke`        | 同步等返回   | **谨慎使用**，容易死锁     |
| 无锁队列 + 时间戳                     | 高频数据流   | 音视频帧、RTP 包        |


### 为什么不全做成无锁？

1. **状态机天然串行**：SDP 协商、ICE 状态转换强行无锁会让逻辑爆炸
2. **线程隔离给了清晰的边界**：你看到某段代码在 Network Thread，就立刻知道"这里不能调用编码器"
3. **调试和回放更容易**：每条线程的任务队列可被序列化记录

### 死锁的典型陷阱

```
Signaling Thread:  Invoke(NetworkThread, doSomething)  ── 等待中 ──┐
Network Thread:    Invoke(SignalingThread, queryState) ── 等待中 ──┘
                                                                    └──▶ 互相等待，死锁
```

**规避手段**：除非有强同步语义需求，否则永远用 `PostTask` 而不是 `Invoke`。

> **面试提问形式**："为什么 Network Thread 不能直接调用 Encoder？"
> **标准答案**：Encoder 耗时不可控（特别是软编/关键帧时），会阻塞 socket 读，导致 RTP 抖动和 ACK 不及时，连带 GCC 误判拥塞。

---

## 4. 核心数据流：一帧视频的旅程

```
[发送端]
 Camera ──▶ VideoFrame(I420/NV12, 时间戳, 旋转)
            │
            ▼
       VideoStreamEncoder (码率/分辨率/帧率自适应, Simulcast 分层)
            │  EncodedImage (码流 + 时间戳 + FrameType)
            ▼
       RtpPacketizer (按 H.264/VP8/AV1 规则切片, 加 RTP 扩展头)
            │  RtpPacketToSend
            ▼
       Pacer (匀速发送, 避免突发)
            │
            ▼
       SrtpTransport (加密) ──▶ IceTransport ──▶ UDP socket
                                                  │
                       公网/中继 TURN              │
                                                  ▼
[接收端]
       UDP socket ──▶ IceTransport ──▶ SrtpTransport (解密)
            │  RtpPacketReceived
            ▼
       RtpDemuxer (按 SSRC 路由到对应 RtpReceiver)
            │
            ▼
       RtpDepacketizer + PacketBuffer (按 seq 重组成帧)
            │
            ▼
       JitterBuffer (FrameBuffer3, 决定哪一帧可以解码: 完整 + 可解 + 不抖)
            │  EncodedFrame
            ▼
       VideoDecoder ──▶ VideoFrame ──▶ Renderer

  ◀── 反馈链路: Receiver 每隔一段时间发 RTCP RR / TWCC-FB / NACK / PLI
                Sender 据此调整码率(GCC)、补包(RTX)、关键帧(PLI)
```

### 关键数据结构


| 结构             | 阶段      | 内容                          |
| -------------- | ------- | --------------------------- |
| `VideoFrame`   | 采集/渲染   | 原始 YUV 数据 + 时间戳 + 旋转信息      |
| `EncodedImage` | 编码后/解码前 | 压缩码流 + 时间戳 + 帧类型(I/P/B)     |
| `RtpPacket`    | 网络上     | RTP header + 扩展 + 载荷        |
| `RtcpPacket`   | 反馈链路    | SR/RR/NACK/PLI/REMB/TWCC-FB |


### 音频链路差异

音频流程类似，但有几个关键差别：

- 没有 Pacer（音频码率本来就匀速）
- JitterBuffer 换成 **NetEQ**（自带变速播放、丢包隐藏 PLC、自适应缓冲）
- 编码器更轻量（Opus 通常 40-128 kbps）
- 3A 处理（AEC/AGC/NS）在采集后立即做

---

## 5. 关键能力地图


| 能力                        | 属于层       | 解决什么问题                          | 面试频率  |
| ------------------------- | --------- | ------------------------------- | ----- |
| **ICE**                   | 网络传输      | NAT 穿透、找出最佳通路（host/srflx/relay） | ★★★★★ |
| **DTLS**                  | 网络传输      | 端到端密钥协商（用于 SRTP keying）         | ★★★   |
| **SRTP**                  | 网络传输      | RTP 包的加密与完整性                    | ★★★   |
| **SDP Offer/Answer**      | API 层     | 媒体能力协商（编码、分辨率、扩展）               | ★★★★★ |
| **NACK / RTX**            | 媒体引擎 ↔ 传输 | 丢包后请求重传，RTX 用独立 SSRC 重发         | ★★★★  |
| **FEC (UlpFEC/FlexFEC)**  | 媒体引擎      | 前向纠错，时延敏感场景替代部分 NACK            | ★★★   |
| **Jitter Buffer / NetEQ** | 媒体引擎      | 乱序、抖动、丢包隐藏                      | ★★★★★ |
| **GCC (Google CC)**       | 媒体引擎 ↔ 传输 | 基于延迟梯度 + 丢包率联合估计带宽              | ★★★★★ |
| **BBR / Transport-CC**    | 传输        | 更现代的带宽估计与反馈                     | ★★★   |
| **Pacer**                 | 媒体引擎      | 把突发码流匀速发出，配合 BWE                | ★★★★  |
| **Simulcast / SVC**       | 媒体引擎      | 一路源多路流，SFU 按订阅下发                | ★★★★  |
| **AEC / AGC / NS (3A)**   | 媒体引擎      | 回声消除、增益、降噪                      | ★★★   |


### 能力分组速记

- **网络层四件套**：ICE（找路）+ DTLS（握手）+ SRTP（加密）+ RTP/RTCP（传输+反馈）
- **抗丢包三招**：NACK（重传）+ FEC（前向纠错）+ RED（包级冗余）
- **抗抖动两件**：Pacer（发端匀速）+ JitterBuffer/NetEQ（收端缓冲）
- **带宽自适应**：GCC（延迟梯度+丢包率）→ 码率调整 → Encoder 重配 → Simulcast 切层

---

## 6. 整体设计哲学（约 350 字）

WebRTC 的整体设计可以用三句话概括：**控制面与数据面分离、线程边界即模块边界、能力分层而非功能堆叠**。

控制面（SDP/ICE/PeerConnection 状态机）跑在 Signaling Thread，强调"逻辑正确、不卡 UI"；数据面（RTP/RTCP/编解码）跑在 Worker 和 Network Thread，强调"低延迟、零抖动"。两者通过明确的 API 边界耦合，而不是共享状态，这让任何一侧的演进（比如换编码器、换拥塞算法）都不会震荡到对方。

线程模型本身就是架构。Network Thread 的"不准做耗时操作"不是建议而是铁律——它把 I/O 时序从复杂业务里隔离出来，使得 GCC 的延迟梯度估计是可信的。媒体引擎和传输层用 RTP 这条"窄腰"对接，传输层不关心帧类型、媒体引擎不关心 socket，正是这种**最小耦合 + 标准协议**的接口设计让 WebRTC 能在浏览器、Native、SFU 三种形态里都活下来。

最后是分层而非堆叠：ICE/DTLS/SRTP/RTP 是四层独立可替换的"网络能力"，JitterBuffer/Pacer/Encoder 是三段独立可替换的"媒体能力"，每一块都有清晰契约。面试时讲设计，重点不在"我用了什么"，而在"我把什么挡在了哪一层之外"。

---

## 7. 本阶段对应的面试视角问题（后续会逐一深挖）

### 架构层

1. WebRTC 为什么要把 Signaling / Worker / Network 三条线程分开？合并行不行？
2. `PeerConnectionFactory` 为什么是进程级单例？它握住了什么资源？
3. 媒体引擎和网络传输为什么是平级而不是上下层？

### 协商层

1. SDP 协商为什么要分 Offer/Answer？为什么不一次性互换能力？
2. ICE 的 host / srflx / prflx / relay 四类候选优先级怎么排？为什么 relay 是兜底？
3. DTLS 既然能加密，为什么还需要 SRTP？两者各自防什么攻击？

### 媒体层

1. JitterBuffer 是按时间排还是按序号排？乱序和延迟它怎么权衡？
2. NetEQ 和 JitterBuffer 的核心区别是什么？为什么音频不能复用视频的 JitterBuffer？
3. Pacer 存在的意义是什么？没有它会怎样？

### 传输层

1. GCC 的"延迟梯度"和"丢包率"两路信号为什么要联合而不是单用一路？
2. NACK 和 FEC 各自的代价是什么？为什么要做"NACK + FEC"混合策略？
3. Simulcast 和 SVC 在 SFU 场景里各自的优劣？为什么大多数生产环境选 Simulcast？

### 工程层

1. WebRTC 的端到端延迟一般拆成哪几段？每段你能怎么压？
2. 跨线程对象生命周期 WebRTC 是怎么管理的？`scoped_refptr` 和 `WeakPtr` 各自解决什么？
3. 如果让你设计一个 SFU，你会复用 WebRTC 哪些模块？哪些必须自己写？

---

## 阶段进度

- ✅ 阶段一：架构总览（本文档）
- ⏳ 阶段二：项目设计方案（待启动）
- ⏳ 阶段三：模块逐个下钻
- ⏳ 阶段四：项目讲述与面试包装

