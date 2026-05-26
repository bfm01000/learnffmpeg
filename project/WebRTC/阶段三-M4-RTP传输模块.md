# 阶段三 · M4：RTP 传输模块（B 层重写核心模块 1/2）

> 自顶向下下钻 RTP 传输模块——这是 WebRTC 数据面的最底层，所有视频帧/音频帧最终都要变成 RTP 包发出去。
> 模块归属：**B 层（自己重写）**——和 libwebrtc 的 `modules/rtp_rtcp/source/rtp_format_h264.cc` 对位。
> 本文档覆盖：原理详解 → 类图与协作 → 设计取舍 → libwebrtc 源码笔记（13 个关键函数）→ 接口骨架 → 面试问答。
> **B 层完整代码留到环境就绪后单独提交**，本文档先把"为什么、是什么、怎么设计"讲透。
>
> **建议投入**：阅读 4-6 小时（含对照 libwebrtc 源码），实操编码 5-7 天。

---

## 目录

1. [职责再陈述](#1-职责再陈述)
2. [原理详解（RFC 3550 + RFC 6184）](#2-原理详解rfc-3550--rfc-6184)
3. [类图与协作图](#3-类图与协作图)
4. [设计取舍（面试追问点）](#4-设计取舍面试追问点)
5. [libwebrtc 源码阅读笔记（13 个关键函数）](#5-libwebrtc-源码阅读笔记13-个关键函数)
6. [接口与关键代码骨架](#6-接口与关键代码骨架)
7. [B 层完整代码（待补）](#7-b-层完整代码待补)
8. [面试问答映射（8 道高频题）](#8-面试问答映射8-道高频题)

---

## 1. 职责再陈述

### 阶段二里 M4 是什么

| 项 | 内容 |
|----|------|
| **模块名** | RtpTransport |
| **归属** | **B 层（自己重写）** |
| **输入** | 编码器产出的 `EncodedImage`（一帧 H264 NALU 流，含 SPS/PPS/IDR/P 帧）/ Opus 编码帧 |
| **输出** | 符合 RFC 3550 + RFC 6184 的 RTP 包字节流 |
| **反向能力** | 接收端把 RTP 包还原成 NALU 流，交给 Jitter Buffer |
| **不负责** | ① 加密（SRTP，下游 ICE 层做）；② NACK 重传（独立模块）；③ FEC 前向纠错；④ 拥塞控制；⑤ 渲染 |

### 边界（一句话）

> **"把帧切成 RTP 包"和"把 RTP 包还原成帧"**——只做格式翻译，不管可靠性、不管加密、不管时序。

### 在系统中的位置

```
[Encoder] ──EncodedImage──▶ [★ RtpPacketizer] ──RtpPacket[]──▶ [ICE/DTLS/SRTP] ──UDP──▶ 网络
                                                                                          │
                                                                                          ▼
                                                                       ◀──[ICE/DTLS/SRTP] ──
[Decoder] ◀──NALU──── [JitterBuffer] ◀──Frame─── [★ RtpDepacketizer] ◀──RtpPacket[]────────┘
```

**★ 是本模块。**

---

## 2. 原理详解（RFC 3550 + RFC 6184）

### 2.1 RTP 协议的设计哲学

**RTP（Real-time Transport Protocol，RFC 3550）** 解决的核心问题是：**UDP 之上跑实时媒体流时，接收端怎么知道包的顺序、时机、归属、丢失情况？**

| 问题 | RTP 的解法 | 对应字段 |
|------|-----------|---------|
| 包乱序怎么排回去？ | 16 位序号 | Sequence Number |
| 什么时候播放这一帧？ | 媒体时钟时间戳 | Timestamp |
| 这是谁发的？ | 32 位同步源 ID | SSRC |
| 这是音频还是视频？哪种编码？ | 7 位载荷类型 | Payload Type |
| 这一帧的最后一个包是哪个？ | 1 位标记位 | Marker |

**关键观察**：RTP **不提供可靠性、不提供拥塞控制**——这两件事分别由应用层 NACK 和 RTCP 反馈处理。这是"UNIX 哲学"在网络协议上的体现：每个协议只做一件事。

### 2.2 RTP 头格式（12 字节固定头 + 可变扩展）

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           synchronization source (SSRC) identifier            |
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|            contributing source (CSRC) identifiers             |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|              (optional) header extension                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         payload data                          |
|                             ....                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

#### 字段语义详解

| 字段 | 位宽 | 含义 |
|------|------|------|
| **V** | 2 | 版本号，固定 2 |
| **P** | 1 | Padding 标志，为 1 时载荷末尾有填充字节（最后一个字节标识填充长度）|
| **X** | 1 | Extension 标志，为 1 时固定头后接扩展头（RFC 8285 定义 one-byte / two-byte 扩展） |
| **CC** | 4 | CSRC 数量（0-15）。混音/合流场景列出原始 SSRC |
| **M** | 1 | Marker 位。**视频中标记一帧的最后一个包；音频中标记静音结束后的第一个包** |
| **PT** | 7 | Payload Type。静态范围 0-95 预分配（如 PCMU=0、PCMA=8），96-127 动态范围由 SDP 协商 |
| **SequenceNumber** | 16 | 单调递增，每发一个 RTP 包 +1。**初始值随机**（防攻击）、溢出 wraparound 到 0 |
| **Timestamp** | 32 | 媒体时钟时间戳。视频常用 90kHz 基（一秒钟时间戳走 90000）、音频常用采样率（Opus 48kHz）。**初始值随机** |
| **SSRC** | 32 | 同步源 ID。同一会话内不同流必须不同；冲突时自动换并发 BYE |
| **CSRC** | N×32 | 贡献源列表，仅 CC>0 时存在（SFU 合流场景）|

#### Marker 位的视频用法（重点）

一个 H264 关键帧可能 200KB+，必须拆成多个 RTP 包。接收端怎么知道一帧结束了？**Marker=1 的包是这一帧的最后一个**：

```
帧 1 (IDR 200KB):  RTP[seq=100, M=0] RTP[seq=101, M=0] ... RTP[seq=205, M=1]  ← 帧结束
帧 2 (P 30KB):     RTP[seq=206, M=0] RTP[seq=207, M=0] ... RTP[seq=220, M=1]  ← 帧结束
```

**Jitter Buffer 用这个标志判定帧完整性**。

### 2.3 H264 over RTP（RFC 6184）的 3 种打包模式

H264 编码器输出的是 **NALU 流**——每个 NALU 是一个独立的编码单元（IDR、P、SPS、PPS、SEI 都是不同类型的 NALU）。NALU 大小从几十字节（PPS）到几百 KB（4K 关键帧）不等。

**核心矛盾**：UDP 的 MTU 限制单包载荷不能超过约 1400 字节（典型以太网 1500 - IP/UDP 头），所以 RFC 6184 定义了 3 种打包策略：

#### 模式 1：Single NALU（单包模式）

**适用**：NALU 大小 < MTU（典型小 NALU：SPS、PPS、P 帧的某些 slice）

```
[RTP Header 12B] [NALU Header 1B] [NALU RBSP 数据]
                 │
                 └── 直接放原始 NALU 头（type 字段 1-23）
```

接收端看 RTP 载荷第一字节 `& 0x1F`，type ∈ [1,23] 就是 Single NALU 模式。

#### 模式 2：STAP-A（Single-Time Aggregation Packet，聚合模式）

**适用**：多个小 NALU 拼一个 RTP 包发（典型场景：SPS + PPS + IDR 起始包，合并发提高效率）

```
[RTP Header 12B] [STAP-A NAL Header 1B = 0x18] [Size1 2B][NALU1] [Size2 2B][NALU2] ...
                 │
                 └── type=24 标识这是 STAP-A
```

每个 NALU 前加 2 字节大端长度前缀。**典型用法**：SPS（30B）+ PPS（8B）放一个包里发。

#### 模式 3：FU-A（Fragmentation Unit，分片模式，最常用）

**适用**：NALU 大小 > MTU（关键帧的主要载荷必走这里）

```
[RTP Header 12B] [FU Indicator 1B = 0x7C] [FU Header 1B] [NALU 数据分片]
                 │                        │
                 │                        ├─ S (1 bit): Start，首片为 1
                 │                        ├─ E (1 bit): End，尾片为 1
                 │                        ├─ R (1 bit): 保留位，固定 0
                 │                        └─ Type (5 bit): 原 NALU 类型
                 │
                 └── type=28 标识这是 FU-A 分片
                     forbidden_zero_bit / nal_ref_idc 从原 NALU 头继承
```

##### FU-A 分片的关键约束

1. **同一个 NALU 的所有分片必须有相同的 RTP timestamp**——接收端用 timestamp 识别"属于同一帧"。
2. **序号必须连续**——丢任何一片，整个 NALU 都不能还原。
3. **首片 S=1 E=0、中间片 S=0 E=0、尾片 S=0 E=1**——三态状态机。
4. **Marker 位只在该 NALU 是帧的最后一个 NALU 时才设 1**（即 FU-A 尾片 + 帧最后一个 NALU = Marker=1）。

##### 一个 100KB IDR 帧的打包示例（MTU=1400）

```
原始 NALU: [0x65][100KB RBSP]    (0x65 = nal_ref_idc=3, type=5 IDR)
拆分后每片载荷可用 = 1400 - 12 (RTP) - 1 (FU Indicator) - 1 (FU Header) = 1386 字节
分片数 = ⌈100000 / 1386⌉ = 73 片

包 1 (S=1, E=0):  RTP[seq=N+0, M=0, ts=T] [0x7C][0x85][数据 1386B]   # FU Indicator=0x7C, FU Header: S=1, type=5 (IDR)
包 2 (S=0, E=0):  RTP[seq=N+1, M=0, ts=T] [0x7C][0x05][数据 1386B]
...
包 72 (S=0, E=0): RTP[seq=N+71, M=0, ts=T] [0x7C][0x05][数据 1386B]
包 73 (S=0, E=1): RTP[seq=N+72, M=1, ts=T] [0x7C][0x45][数据 <1386B]  # FU Header: E=1, M=1（假设是帧最后 NALU）
```

**FU Indicator** 第一字节 `0x7C = 01111100`：F=0, NRI=11, type=28。NRI（nal_ref_idc）从原 NALU 头复制——表示参考重要性，丢了影响后续解码。

### 2.4 Opus over RTP（RFC 7587）简版

Opus 比 H264 简单得多：**一帧 Opus（20ms / 960 采样 @ 48kHz）通常 < 200 字节，永远不需要分片**。直接：

```
[RTP Header 12B] [Opus 编码数据]
```

注意：
- **采样率 90kHz 不适用 Opus**——Opus RTP 时间戳基是 48kHz（每包 timestamp 增量 = 960）
- **Marker 位**：连续语音段中所有包 M=0；静音 DTX 之后的第一个语音包 M=1（告诉接收端"语音重新开始，丢弃旧的舒适噪声"）

### 2.5 时间戳计算（最容易踩坑的部分）

#### 视频时间戳

```
RTP timestamp = 任意初始值 + 累计帧间隔（90kHz 基）

例：30fps 视频，帧间隔 = 90000 / 30 = 3000 ticks
frame_0: ts = 1000000
frame_1: ts = 1003000
frame_2: ts = 1006000
...
```

**关键点**：同一帧的所有 FU-A 分片 ts 相同；同一帧的所有 RTP 包 ts 都相同。

#### 音频时间戳

```
RTP timestamp = 任意初始值 + 累计采样数

例：Opus 20ms @ 48kHz，每包 960 采样
packet_0: ts = 2000000
packet_1: ts = 2000960
packet_2: ts = 2001920
...
```

#### 跨流时间戳对齐

视频和音频的 RTP 时间戳是**两个独立的时间轴**（基不同、初始值不同），靠 **RTCP SR（Sender Report）** 把"RTP 时间戳"和"NTP 墙钟"绑起来，接收端用这个映射做音画同步。

```
RTCP SR 报文携带：
- NTP 时间戳 (64 bit 墙钟)
- RTP 时间戳 (32 bit 当前媒体时钟)

接收端推算：任意 RTP 时间戳 → NTP 时间 → 跨流对齐
```

---

## 3. 类图与协作图

### 3.1 类组织（ASCII 类图）

```
┌────────────────────────────────────────────────────────────────┐
│                       IRtpPacketizer                            │
│  + NextPacket(out RtpPacket) -> bool                            │
│  + RemainingNalus() -> size_t                                   │
└──────────────────────────┬─────────────────────────────────────┘
                           │ (实现)
        ┌──────────────────┼──────────────────────────┐
        │                  │                          │
        ▼                  ▼                          ▼
┌──────────────────┐  ┌─────────────────┐  ┌─────────────────────┐
│ H264Packetizer   │  │ OpusPacketizer  │  │ Vp8Packetizer       │
│  (Single+STAP-A  │  │ (单包，无分片)    │  │ (此项目不实现)        │
│   +FU-A 分片)    │  │                 │  │                     │
└──────────────────┘  └─────────────────┘  └─────────────────────┘
        │
        │ 依赖
        ▼
┌────────────────────────────────────────────────────────────────┐
│                          RtpPacket                              │
│  - rtpHeader: RtpHeader (12B 固定头 + 可选扩展)                  │
│  - payload: vector<uint8_t>                                     │
│  + Serialize(out buffer) -> size_t                              │
│  + Parse(in buffer, size) -> bool                               │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                       IRtpDepacketizer                          │
│  + InsertPacket(packet) -> InsertResult                         │
│  + PopReassembledNalu(out nalUnit) -> bool                      │
└──────────────────────────┬─────────────────────────────────────┘
                           │ (实现)
                           ▼
┌────────────────────────────────────────────────────────────────┐
│                    H264Depacketizer                             │
│  - fuaReassemblyBuffer: vector<uint8_t>                         │
│  - currentTimestamp: uint32_t                                   │
│  - lastReceivedSequenceNumber: uint16_t                         │
│  + InsertPacket(packet)                                         │
│  + ReassembleFua()                                              │
└────────────────────────────────────────────────────────────────┘
```

### 3.2 典型场景的时序图

#### 场景：一个 IDR 帧从编码到 RTP 发送

```
Encoder           H264Packetizer        RtpPacketSender    Network
  │                     │                     │              │
  │ EncodedImage        │                     │              │
  │ (NALU stream)       │                     │              │
  ├────────────────────►│                     │              │
  │                     │                     │              │
  │                     │ Parse NALU stream   │              │
  │                     │ 拆出 [SPS, PPS, IDR]│              │
  │                     │                     │              │
  │                     │ 决策打包模式:        │              │
  │                     │ - SPS(30B)+PPS(8B)  │              │
  │                     │   → STAP-A 合并     │              │
  │                     │ - IDR(100KB)        │              │
  │                     │   → FU-A 分 73 片   │              │
  │                     │                     │              │
  │                     │ NextPacket()        │              │
  │                     ├────────────────────►│              │
  │                     │ RtpPacket #1 (STAP-A SPS+PPS)      │
  │                     │                     │              │
  │                     │                     │   send()     │
  │                     │                     ├─────────────►│
  │                     │                     │              │
  │                     │ NextPacket() × 73   │              │
  │                     ├────────────────────►│              │
  │                     │ RtpPacket #2..#74 (FU-A 分片)      │
  │                     │                     ├─────────────►│
  │                     │                     │              │
  │                     │ RemainingNalus()=0  │              │
  │                     │                     │              │
```

#### 场景：接收端把 73 个 FU-A 分片还原成 IDR

```
Network    H264Depacketizer       FuaBuffer         JitterBuffer
  │              │                    │                  │
  │  RTP #2      │                    │                  │
  ├─────────────►│                    │                  │
  │              │ Parse FU Indicator │                  │
  │              │ S=1, E=0, type=5   │                  │
  │              │ → 新建分片重组      │                  │
  │              ├───────────────────►│                  │
  │              │ Init(ts=T, seq=N+1)│                  │
  │              │ Append(data 1386B) │                  │
  │              │                    │                  │
  │  RTP #3..#73 │                    │                  │
  ├─────────────►│                    │                  │
  │              │ S=0, E=0           │                  │
  │              ├───────────────────►│                  │
  │              │ Append × 71        │                  │
  │              │                    │                  │
  │  RTP #74     │                    │                  │
  ├─────────────►│                    │                  │
  │              │ S=0, E=1           │                  │
  │              ├───────────────────►│                  │
  │              │ Append + Finalize  │                  │
  │              │◄───────────────────┤                  │
  │              │ 完整 IDR NALU       │                  │
  │              │                    │                  │
  │              │ PopReassembledNalu │                  │
  │              ├───────────────────────────────────────►│
  │              │ NALU + ts + isKeyFrame=true            │
  │              │                                       │
```

---

## 4. 设计取舍（面试追问点）

每个取舍都按"**WebRTC 怎么做 → 其他方案是什么 → 代价对比 → 面试一句话答**"的格式。

### 4.1 取舍 1：MTU 大小怎么选？

**WebRTC 怎么做**：默认 RTP 载荷最大 **1200 字节**（不是 1400！），保留余量给 IPv6 头 + 可能的隧道封装 + SRTP 加密扩展。源码在 `media/engine/webrtc_video_engine.cc` 的 `kMaxPayloadSize`。

**其他方案**：
- 1400 字节（PMTU = 1500 - IP/UDP）—— 风险：路径上经 VPN/隧道时分片，UDP 分片丢一个就整包重传
- 600 字节（小包模式）—— 优点：丢包恢复粒度细；缺点：RTP 头开销占比从 1% 升到 2%

**面试一句话答**：1200 字节是"足够大用满 PMTU + 留 SRTP 扩展余量 + 容忍隧道封装"的折中值。

### 4.2 取舍 2：FU-A 优先还是 STAP-A 优先？

**WebRTC 怎么做**：**FU-A 是默认路径**——只要 NALU > MTU 就用 FU-A，几乎不用 STAP-A。`rtp_format_h264.cc:RtpPacketizerH264::Packetize` 里 STAP-A 仅用于"SPS + PPS + IDR 起始一组小 NALU"的特殊场景。

**其他方案**：积极 STAP-A（多个小 NALU 都聚合）—— 优点：减少 RTP 头开销；缺点：聚合后丢一个包损失多个 NALU，且 STAP-A 模式接收端实现复杂（要解析多层头）。

**面试一句话答**：FU-A 是"丢包损失局部化"的设计；STAP-A 节省的头开销不值得"丢一个包损失多 NALU"的风险。

### 4.3 取舍 3：RTP 头用 struct 还是 manual 序列化？

**WebRTC 怎么做**：**manual 序列化**（手动按位移位拼字节），不用 struct。源码在 `modules/rtp_rtcp/source/rtp_packet.cc:RtpPacket::ToBuffer`。

**其他方案**：定义 `struct RtpHeader { uint8_t v_p_x_cc; uint8_t m_pt; ... };` 直接 memcpy。

**为什么不用 struct**：
1. **字节序问题**：RTP 头是**大端网络字节序**，x86/ARM 是小端，struct memcpy 后字段全反；
2. **位域不可移植**：C++ 标准不规定 `unsigned int v : 2` 的字节内布局，不同编译器结果不同；
3. **对齐填充**：struct 编译器可能在字段间插 padding，破坏 12 字节固定长度；
4. **扩展头变长**：固定 struct 装不下可变长扩展。

**面试一句话答**：struct + memcpy 看起来快，但在跨字节序、跨编译器、变长协议场景全是雷——RTP 用 manual 序列化是"显式控制比编译器隐式行为更可靠"的工程选择。

### 4.4 取舍 4：发送侧 Packetizer 用一次性产出全部包，还是迭代器模式？

**WebRTC 怎么做**：**迭代器模式**（`NextPacket(RtpPacketToSend*)` 一次产出一个）。源码 `modules/rtp_rtcp/source/rtp_format.h:RtpPacketizer::NextPacket`。

**其他方案**：`std::vector<RtpPacket> PacketizeAll(EncodedImage)` 一次返回全部。

**为什么用迭代器**：
1. **配合 Pacer**：发送端要按拥塞控制反馈的码率**节流**发送（不能瞬间灌满网卡），迭代器允许 Pacer 一次 pull 一个、按节奏发；
2. **内存峰值小**：100KB IDR 一次性产 73 个 RtpPacket（每个含头 + 载荷），峰值内存大；迭代式每次只持有一个；
3. **错误恢复方便**：发送中途出错（网卡满、内存不足），可以停在当前迭代位置，恢复后继续。

**面试一句话答**：迭代器模式让"打包"和"发送节奏"解耦，是配合 Pacer 拥塞控制的必要设计。

### 4.5 取舍 5：接收侧 FU-A 重组中途丢包怎么办？

**WebRTC 怎么做**：**整 NALU 丢弃**——一旦发现 FU-A 序列中断（缺中间分片），整个 NALU 重组失败，全部丢弃；同时通过 NACK 反馈请求重传所有缺失 SeqNum。源码 `modules/rtp_rtcp/source/video_rtp_depacketizer_h264.cc:ParseFuaNalu`。

**其他方案**：
- 部分提交：把已收到的前 N 个分片拼起来交给解码器，让解码器"尽力而为"——**会让解码器崩溃**，H264 NALU 不允许截断；
- 等待 NACK 重传后再重组——**延迟爆炸**，重传 RTT 内整帧卡住。

**为什么整丢**：H264 NALU 是密码本式编码，截断的 NALU 喂解码器**直接产生雪花/绿屏**甚至 crash，宁可丢一帧也不能传残帧。

**面试一句话答**：FU-A 中途丢包等于这个 NALU 报废，Jitter Buffer 决定是 NACK 重传还是直接请求 IDR 刷新。

---

## 5. libwebrtc 源码阅读笔记（13 个关键函数）

> 源码版本：**m120（branch-heads/6099）**。每个条目格式：路径 / 签名 / 注释 / 面试可能问的点。
> 阅读建议：先看接口 `.h`、再看实现 `.cc`、最后看单测 `_unittest.cc`。

### 5.1 RtpPacket 数据结构

#### F1. `RtpPacket::RtpPacket()` 构造函数

- **路径**：`modules/rtp_rtcp/source/rtp_packet.cc`（约第 60-100 行）
- **签名**：`RtpPacket::RtpPacket(const ExtensionManager* extensions, size_t capacity)`
- **注释**：构造一个空的 RTP 包，预分配 `capacity` 字节的内部 buffer。`extensions` 是扩展头管理器（RFC 8285 one-byte/two-byte 扩展的 ID → 类型映射）。**WebRTC 的 RtpPacket 是"buffer 拥有者"模式**——内部持有 `CopyOnWriteBuffer`，所有字段读写都是直接操作这块 buffer 的对应字节偏移，没有中间 struct。
- **面试可能问的点**：
  - "为什么 RtpPacket 内部用 buffer 而不是 fields？" → 见 4.3 取舍
  - "CopyOnWriteBuffer 是什么？什么时候触发拷贝？" → 多线程引用计数共享，写时分裂

#### F2. `RtpPacket::SetMarker()` / `SetTimestamp()` / `SetSequenceNumber()`

- **路径**：`modules/rtp_rtcp/source/rtp_packet.cc`（约第 150-220 行）
- **签名**：`void RtpPacket::SetMarker(bool marker)` 等
- **注释**：每个 Setter 直接按字段偏移写 buffer：Marker 是第 1 字节的最高位、Timestamp 是第 4-7 字节大端、SequenceNumber 是第 2-3 字节大端。用宏 `ByteWriter<uint16_t>::WriteBigEndian(buffer + offset, value)` 处理字节序——这是 WebRTC 自封装的字节序工具，跨平台稳定。
- **面试可能问的点**：
  - "如果让你写一个 SetSequenceNumber，你怎么处理字节序？" → htons / 手动位移
  - "Setter 调用顺序有讲究吗？" → SetPayloadType 必须先调，因为它会修改头长度（M+PT 字节共享一个 byte）

#### F3. `RtpPacket::Parse(const uint8_t* buffer, size_t size)`

- **路径**：`modules/rtp_rtcp/source/rtp_packet.cc`（约第 350-450 行）
- **签名**：`bool RtpPacket::Parse(rtc::CopyOnWriteBuffer buffer)`
- **注释**：反向：从字节流解析出 RTP 头各字段，存到内部 buffer。返回 `false` 时表示包格式非法（版本号错、CSRC 数量超长、长度不足等），调用方应丢弃。**注意**：Parse 不验证扩展头内部格式（那是上层 ExtensionManager 的责任）。
- **面试可能问的点**：
  - "Parse 失败的常见原因？" → 长度不足 12 字节、版本号 != 2、CSRC*4 + 12 > size
  - "为什么 Parse 不直接抛异常？" → 热路径不抛异常，返回 bool 更可控

### 5.2 H264 打包逻辑

#### F4. `RtpPacketizerH264::RtpPacketizerH264(...)` 构造函数

- **路径**：`modules/rtp_rtcp/source/rtp_format_h264.cc`（约第 50-120 行）
- **签名**：`RtpPacketizerH264(rtc::ArrayView<const uint8_t> payload, PayloadSizeLimits limits, H264PacketizationMode packetization_mode)`
- **注释**：构造时**一次性扫描**整个 EncodedImage（输入是连续的 NALU 字节流，用起始码 `0x000001` / `0x00000001` 分隔），把每个 NALU 切出来存到内部 `input_fragments_` 队列里。`packetization_mode` 是 SDP 协商出来的（`packetization-mode=1` 支持 FU-A，`=0` 只支持 Single NALU）。**WebRTC 在构造期就决定了每个 NALU 用哪种模式**（Single / STAP-A / FU-A），后续 NextPacket 只是按计划吐包。
- **面试可能问的点**：
  - "为什么构造时一次性扫描，不能边发边扫？" → 因为要先算总包数，配合 Pacer 估算时长
  - "packetization-mode=0 和 =1 的区别？" → mode 0 只允许 Single NALU（兼容老设备），mode 1 允许全部三种

#### F5. `RtpPacketizerH264::PacketizeFuA(size_t fragment_index)`

- **路径**：`modules/rtp_rtcp/source/rtp_format_h264.cc`（约第 200-280 行）
- **签名**：`void RtpPacketizerH264::PacketizeFuA(size_t fragment_index)`
- **注释**：把第 `fragment_index` 个 NALU 拆成 FU-A 分片。**关键算法**：① 算每包最大载荷 = `max_payload - 2 (FU 双头)`；② 总片数 = `⌈(NALU 大小 - 1) / 每包载荷⌉`（减 1 是因为 NALU 头被 FU Header 替代）；③ 平均分配字节到各片（不是简单除法，要让最后一片尽量满，避免"最后一片只有 100 字节"的浪费）；④ 首片标 S=1、尾片标 E=1。
- **面试可能问的点**：
  - "为什么不简单除法切？" → 简单除法会让最后一片极小，包数还是一样但带宽利用率低
  - "FU Indicator 第一字节怎么算？" → 从原 NALU 头复制 F(0) + NRI(2bit) + type(5bit=28)

#### F6. `RtpPacketizerH264::PacketizeStapA(size_t fragment_index)`

- **路径**：`modules/rtp_rtcp/source/rtp_format_h264.cc`（约第 280-340 行）
- **签名**：`bool RtpPacketizerH264::PacketizeStapA(size_t fragment_index)`
- **注释**：尝试把从 `fragment_index` 开始的多个小 NALU 聚合成一个 STAP-A 包。**判定条件**：累计大小 + STAP-A 头 + 每个 NALU 的 2 字节长度前缀 < max_payload。返回 `true` 表示成功聚合了至少 2 个 NALU；`false` 表示放弃（聚合不划算或单 NALU 都不够）。**实际触发场景极少**：只在 IDR 帧前的 SPS+PPS 组合。
- **面试可能问的点**：
  - "为什么 STAP-A 用得少？" → 见 4.2
  - "STAP-A 内每个 NALU 的 timestamp 是同一个吗？" → 是，整个 RTP 包共享一个 timestamp

#### F7. `RtpPacketizerH264::NextPacket(RtpPacketToSend* rtp_packet)`

- **路径**：`modules/rtp_rtcp/source/rtp_format_h264.cc`（约第 380-450 行）
- **签名**：`bool RtpPacketizerH264::NextPacket(RtpPacketToSend* rtp_packet)`
- **注释**：迭代器接口——每次调用产出一个 RTP 包到 `rtp_packet`，返回 `false` 表示已经全部发完。内部维护 `current_packet_` 索引指向构造期算好的"包计划"。**最关键的副作用**：设置 Marker 位——只有"这是某一帧的最后一个 RTP 包"时 marker=1，等价于"current_packet_ == 总包数 - 1"。
- **面试可能问的点**：
  - "NextPacket 是线程安全的吗？" → 不是，Packetizer 本身不持锁，调用方（RtpSender）保证串行
  - "如果调用方丢失了一个 RtpPacket，怎么重传？" → Packetizer 不负责，由 RtpPacketHistory 缓存历史包配合 NACK 重传

### 5.3 H264 解包逻辑

#### F8. `VideoRtpDepacketizerH264::Parse(rtc::CopyOnWriteBuffer rtp_payload)`

- **路径**：`modules/rtp_rtcp/source/video_rtp_depacketizer_h264.cc`（约第 80-150 行）
- **签名**：`absl::optional<ParsedRtpPayload> VideoRtpDepacketizerH264::Parse(...)`
- **注释**：接收端的入口。看 RTP 载荷第一字节 `& 0x1F` 取 NALU type：① type ∈ [1,23] → Single NALU 模式，整个载荷就是一个完整 NALU；② type = 24 → STAP-A 模式，调 `ProcessStapAOrSingleNalu` 拆出多个 NALU；③ type = 28 → FU-A 模式，调 `ParseFuaNalu` 处理分片。**返回值**：`ParsedRtpPayload` 含视频帧元信息（is_first_packet_in_frame、is_keyframe、video_header）和载荷。
- **面试可能问的点**：
  - "如果 type 是其他保留值会怎样？" → 返回 absl::nullopt，上层丢弃
  - "为什么 Parse 不直接给完整 NALU 给 JitterBuffer？" → 因为 FU-A 分片本身就不是完整 NALU，需要 JitterBuffer 配合重组

#### F9. `VideoRtpDepacketizerH264::ParseFuaNalu(...)`

- **路径**：`modules/rtp_rtcp/source/video_rtp_depacketizer_h264.cc`（约第 200-260 行）
- **签名**：`absl::optional<ParsedRtpPayload> ParseFuaNalu(rtc::CopyOnWriteBuffer rtp_payload, ...)`
- **注释**：解析单个 FU-A 分片（**不做重组**——重组是 PacketBuffer/JitterBuffer 的责任，本函数只解析当前包的 S/E/Type 位）。读 FU Indicator 取 NRI、读 FU Header 取 S/E + 原 NALU type。**关键判断**：S=1 时**重建原 NALU 头**（拼回 F+NRI+type），放到 payload 开头返回——这样 JitterBuffer 重组时第一个分片的 payload 已经带回了原 NALU 头，后续分片直接拼数据即可。
- **面试可能问的点**：
  - "为什么是 JitterBuffer 做重组，不是 Depacketizer 做？" → Depacketizer 是无状态的（单包解析），重组需要跨包缓存状态
  - "S=1 时重建 NALU 头的字节怎么算？" → `nalu_header = (fu_indicator & 0xE0) | (fu_header & 0x1F)`，高 3 位（F+NRI）来自 FU Indicator、低 5 位（type）来自 FU Header

### 5.4 RtpSender 主控

#### F10. `RTPSender::SendOutgoingData(...)`

- **路径**：`modules/rtp_rtcp/source/rtp_sender.cc`（约第 300-400 行）
- **签名**：`bool RTPSender::SendOutgoingData(FrameType frame_type, int8_t payload_type, uint32_t capture_timestamp, ...)`
- **注释**：发送端的"门面"——从编码器接收一帧数据，**协调 Packetizer + Pacer + History**：① 调 Packetizer 把帧切成多个 RtpPacket；② 把每个包加入 RtpPacketHistory（用于 NACK 重传）；③ 把包入队 PacedSender 等待拥塞控制放行；④ Pacer 按节奏取包调 `SendToNetwork`。
- **面试可能问的点**：
  - "为什么不直接 socket->send 而要走 Pacer？" → Pacer 平滑发送，避免突发流量打爆 BWE 估算
  - "RtpPacketHistory 存多久？" → 默认 1 秒，过期淘汰

#### F11. `RtpPacketHistory::PutRtpPacket(...)` / `GetPacketAndMarkAsPending(seq)`

- **路径**：`modules/rtp_rtcp/source/rtp_packet_history.cc`（约第 100-180 行）
- **签名**：`void RtpPacketHistory::PutRtpPacket(std::unique_ptr<RtpPacketToSend> packet, ...)`
- **注释**：环形缓冲，按 SeqNum 索引存最近发过的包，供 NACK 重传查找。**容量动态调整**：根据当前码率和 RTT 估算。**关键细节**：NACK 重传时不能直接复用原包，要重新打 SSRC（用 RTX SSRC）和重新分配 SeqNum——所以 GetPacket 返回的是**深拷贝**。
- **面试可能问的点**：
  - "为什么重传不能用原 SeqNum？" → 接收端按 SeqNum 排序，重传包给原 SeqNum 会被认为重复丢弃
  - "RTX 是什么？" → RFC 4588 定义的重传机制，用独立 SSRC 走另一条 RTP 流

### 5.5 共享工具

#### F12. `RtpFormatHelper::FindNaluIndices()` / NALU 起始码扫描

- **路径**：`common_video/h264/h264_common.cc`（约第 30-80 行）
- **签名**：`std::vector<NaluIndex> H264::FindNaluIndices(const uint8_t* buffer, size_t buffer_size)`
- **注释**：扫描 NALU 起始码（`0x000001` 或 `0x00000001`），返回每个 NALU 在 buffer 中的偏移、起始码长度、payload 长度。**Boyer-Moore 类似的快速扫描**，跳跃式比较优化。**坑**：起始码可能在 NALU 数据内部出现，要用 emulation prevention 字节（0x03）规避——但 FindNaluIndices 假设输入已经 escape 过了。
- **面试可能问的点**：
  - "emulation prevention 是什么？" → RBSP 数据里如果出现 `0x000000` / `0x000001` / `0x000002` / `0x000003` 会被误判为起始码，编码时在中间插 0x03 转义
  - "怎么区分 3 字节起始码和 4 字节起始码？" → 4 字节前面多一个 0x00，扫描时先尝试 4 字节再回退 3 字节

#### F13. `ByteReader<uint16_t>::ReadBigEndian(buf)` / `ByteWriter<uint16_t>::WriteBigEndian(buf, val)`

- **路径**：`rtc_base/byte_order.h`
- **签名**：`template<typename T> static T ByteReader<T>::ReadBigEndian(const uint8_t* buffer)`
- **注释**：WebRTC 自封装的字节序读写工具，避免直接用 `htons/ntohs`（在 Windows 上要 link winsock）。模板特化处理 uint16/uint32/uint64/int24（24 位用 3 字节存的特殊类型，RTP 时间戳的部分扩展用到）。
- **面试可能问的点**：
  - "为什么不直接用 ntohs？" → 跨平台依赖问题、Windows 要额外 link
  - "uint24 怎么用 3 字节表示？" → 高位补 0 当 uint32 处理，但写入时只写低 3 字节

---

## 6. 接口与关键代码骨架

> 只给**接口签名 + 最核心 20-40 行算法**，完整实现见第 7 节（待补）。
> 命名严格遵循"自解释、禁缩写"原则（无 `lo` / `hi` / `i` / `j` / `tmp` / `buf`）。

### 6.1 RtpPacket 数据结构（接口）

```cpp
// include/rtp_packet.h
#pragma once
#include <cstdint>
#include <vector>
#include <span>

namespace my_webrtc {

class RtpPacket {
public:
    static constexpr size_t kFixedHeaderSize = 12;
    static constexpr size_t kMaxPayloadSize = 1200;

    RtpPacket();

    // 字段访问（getter / setter）
    void SetVersion(uint8_t version);
    void SetMarker(bool isMarker);
    void SetPayloadType(uint8_t payloadType);
    void SetSequenceNumber(uint16_t sequenceNumber);
    void SetTimestamp(uint32_t timestamp);
    void SetSynchronizationSource(uint32_t synchronizationSource);
    void SetPayload(std::span<const uint8_t> payloadBytes);

    bool IsMarker() const;
    uint8_t PayloadType() const;
    uint16_t SequenceNumber() const;
    uint32_t Timestamp() const;
    uint32_t SynchronizationSource() const;
    std::span<const uint8_t> Payload() const;

    // 序列化 / 反序列化
    size_t Serialize(uint8_t* outputBuffer, size_t outputBufferCapacity) const;
    bool Parse(const uint8_t* inputBuffer, size_t inputBufferSize);

private:
    std::vector<uint8_t> internalBuffer_;  // 固定头 + 扩展 + 载荷
    size_t payloadOffset_;                 // 载荷起始偏移（含扩展头）
    size_t payloadSize_;
};

}  // namespace my_webrtc
```

### 6.2 IRtpPacketizer 接口

```cpp
// include/rtp_packetizer.h
#pragma once
#include "rtp_packet.h"

namespace my_webrtc {

enum class H264PacketizationMode {
    kSingleNaluOnly,    // 兼容老设备
    kFullSupport        // 允许 Single / STAP-A / FU-A
};

struct PacketSizeLimits {
    size_t maxPayloadBytes = 1200;
    size_t firstPacketReductionBytes = 0;   // 首包扩展头预留
    size_t lastPacketReductionBytes = 0;    // 尾包预留
};

class IRtpPacketizer {
public:
    virtual ~IRtpPacketizer() = default;

    // 迭代器接口：每次产出一个 RtpPacket 到 outPacket
    // 返回 false 表示已无更多包
    virtual bool NextPacket(RtpPacket* outPacket) = 0;

    // 剩余待发包数（供 Pacer 估算时长）
    virtual size_t RemainingPackets() const = 0;
};

class H264Packetizer : public IRtpPacketizer {
public:
    H264Packetizer(std::span<const uint8_t> encodedFrame,
                   PacketSizeLimits packetLimits,
                   H264PacketizationMode packetizationMode);

    bool NextPacket(RtpPacket* outPacket) override;
    size_t RemainingPackets() const override;

private:
    struct PacketPlan {
        size_t naluIndex;       // 在 input_fragments_ 中的索引
        size_t fuaSubIndex;     // 在 NALU 内的 FU-A 子片索引（非 FU-A 时为 0）
        bool isFirstFuaPiece;
        bool isLastFuaPiece;
        bool isStapAGrouped;
    };

    void PlanAllPackets();
    bool FillSingleNaluPacket(const PacketPlan& plan, RtpPacket* outPacket);
    bool FillFuAPacket(const PacketPlan& plan, RtpPacket* outPacket);
    bool FillStapAPacket(const PacketPlan& plan, RtpPacket* outPacket);

    std::vector<std::span<const uint8_t>> inputNalus_;
    std::vector<PacketPlan> packetPlans_;
    size_t currentPacketIndex_ = 0;
    PacketSizeLimits packetLimits_;
    H264PacketizationMode packetizationMode_;
};

}  // namespace my_webrtc
```

### 6.3 关键算法骨架：FU-A 分片计算（核心 30 行）

```cpp
// src/h264_fu_a_packetizer.cc（节选）

// 计算一个 NALU 需要拆成多少个 FU-A 分片，以及每片的字节范围
struct FuAPlan {
    size_t totalPieces;
    std::vector<std::pair<size_t, size_t>> pieceRanges;  // [startOffset, endOffset)
};

FuAPlan ComputeFuAPlan(size_t naluSizeBytes, size_t maxPayloadBytes) {
    // FU-A 包结构: [FU Indicator 1B][FU Header 1B][NALU 数据片段]
    // 原 NALU 头 1 字节被 FU Header 替代，所以可分片的数据是 (naluSizeBytes - 1)
    constexpr size_t kFuOverheadBytes = 2;
    const size_t maxDataBytesPerPiece = maxPayloadBytes - kFuOverheadBytes;
    const size_t fragmentableDataBytes = naluSizeBytes - 1;  // 去掉原 NALU 头

    // 算总片数（向上取整）
    const size_t totalPieces =
        (fragmentableDataBytes + maxDataBytesPerPiece - 1) / maxDataBytesPerPiece;

    // 均衡分配：避免最后一片极小
    // 平均每片字节 = ⌈fragmentableDataBytes / totalPieces⌉
    const size_t averageBytesPerPiece =
        (fragmentableDataBytes + totalPieces - 1) / totalPieces;

    FuAPlan plan;
    plan.totalPieces = totalPieces;
    plan.pieceRanges.reserve(totalPieces);

    size_t cursorOffset = 1;  // 跳过原 NALU 头（FU Header 已经携带 type 信息）
    for (size_t pieceIndex = 0; pieceIndex < totalPieces; ++pieceIndex) {
        const size_t pieceEndOffset =
            std::min(cursorOffset + averageBytesPerPiece, naluSizeBytes);
        plan.pieceRanges.emplace_back(cursorOffset, pieceEndOffset);
        cursorOffset = pieceEndOffset;
    }
    return plan;
}
```

**算法关键点**：
1. `naluSizeBytes - 1` 是因为 FU Header 替代了原 NALU 头，可分片数据少 1 字节；
2. 用 "向上取整除法" 模式 `(a + b - 1) / b` 避免浮点；
3. 均衡分配让每片字节数尽量相等，最后一片不会"只剩 100 字节"。

### 6.4 IRtpDepacketizer 接口

```cpp
// include/rtp_depacketizer.h
#pragma once
#include "rtp_packet.h"

namespace my_webrtc {

enum class DepacketizeResult {
    kCompleteNalu,           // 当前包解出完整 NALU（Single / STAP-A 之一）
    kFuaInProgress,          // FU-A 分片进行中，需要更多包
    kFuaCompleted,           // FU-A 最后一片，重组完成
    kInvalidPacket,          // 包格式非法
    kSequenceGap             // 检测到序号断层（中间分片丢失）
};

struct DepacketizedNalu {
    std::vector<uint8_t> naluBytes;
    uint32_t rtpTimestamp;
    bool isKeyFrame;
    uint16_t firstSequenceNumber;
    uint16_t lastSequenceNumber;
};

class IRtpDepacketizer {
public:
    virtual ~IRtpDepacketizer() = default;
    virtual DepacketizeResult InsertPacket(const RtpPacket& receivedPacket) = 0;
    virtual bool PopCompletedNalu(DepacketizedNalu* outNalu) = 0;
};

}  // namespace my_webrtc
```

---

## 7. B 层完整代码（待补）

> ⏳ **此节内容在你的 macOS 环境就绪后补充**。
>
> **将产出**（约 500 行代码 + 200 行单元测试）：
> - `阶段三-M4-RTP传输模块/code/CMakeLists.txt`
> - `阶段三-M4-RTP传输模块/code/include/rtp_packet.h`
> - `阶段三-M4-RTP传输模块/code/include/rtp_packetizer.h`
> - `阶段三-M4-RTP传输模块/code/include/rtp_depacketizer.h`
> - `阶段三-M4-RTP传输模块/code/src/rtp_packet.cc`
> - `阶段三-M4-RTP传输模块/code/src/h264_packetizer.cc`
> - `阶段三-M4-RTP传输模块/code/src/h264_depacketizer.cc`
> - `阶段三-M4-RTP传输模块/code/tests/CMakeLists.txt`
> - `阶段三-M4-RTP传输模块/code/tests/rtp_packet_test.cc`（10 个测试点：字段读写、字节序、Parse 正负样本）
> - `阶段三-M4-RTP传输模块/code/tests/h264_packetizer_test.cc`（8 个测试点：Single / STAP-A / FU-A 三种模式 + 边界 case）
> - `阶段三-M4-RTP传输模块/code/tests/h264_depacketizer_test.cc`（6 个测试点：完整解包 + 丢中间分片 + 重组失败）
>
> **触发条件**：你完成 `环境准备清单-macOS.md` 第 8 节自检的"基础工具"和"B 层 CMake 项目"两块后，告诉我"环境就绪"，我会立即补完这部分。

---

## 8. 面试问答映射（8 道高频题）

### Q1. 一个 1080p 视频帧从编码到 RTP 发送经过哪些步骤？

**口语化标准答案**：编码器把一帧 YUV 变成 H264 NALU 流（含 SPS、PPS、IDR/P 帧主体），然后交给 RtpPacketizer 决定打包模式——小 NALU 像 SPS/PPS 通常用 STAP-A 聚合在一个包，大 NALU 像 IDR 主体超过 MTU（典型 1200 字节）就走 FU-A 分片成几十个包。每个 RTP 包打上头部：版本 2、payload type 从 SDP 协商出来、SSRC 唯一标识这路流、SeqNum 单调递增、Timestamp 是同一帧共享一个值（90kHz 基）、Marker 位只在帧的最后一个 RTP 包设 1。Packetizer 用迭代器模式逐个吐包给 Pacer，Pacer 按拥塞控制反馈的码率节流交给 SRTP 加密、走 ICE 选好的 UDP 通道发出去。

### Q2. RTP 头 12 字节里你能记住的字段有哪些？

**口语化标准答案**：12 字节固定头：第 1 字节是版本号 + 几个标志位（V=2, P padding, X 扩展, CC csrc 数）；第 2 字节是 marker + payload type；第 3-4 字节是 sequence number；第 5-8 字节是 timestamp；第 9-12 字节是 SSRC。后面如果 CC > 0 还跟 CSRC 列表，X = 1 还跟扩展头。最常考的是 marker（视频里标记帧的最后一个包）、SeqNum（接收端排序和丢包检测）、Timestamp（同一帧所有包共享、跨流靠 RTCP SR 对齐）、SSRC（唯一标识一路流）。

### Q3. FU-A 分片为什么不能用简单的 "总字节数除以最大包大小" 切？

**口语化标准答案**：简单除法切的话，最后一片可能极小——比如 100KB NALU 除以 1200 字节得 84 片，前 83 片各 1200 字节、最后 1 片只剩 400 字节，带宽利用率低。WebRTC 的做法是先算总片数（向上取整除法），然后用"平均字节数 = 总字节 / 总片数 + 向上取整"分配，让每片尽量满，最后一片不会浪费。这样总片数还是一样，但每个包都接近 MTU 上限，发送效率最高。

### Q4. 接收端怎么判定一帧的边界？

**口语化标准答案**：靠 RTP 头的 Marker 位 + Timestamp。同一帧的所有 RTP 包共享同一个 Timestamp，**Marker = 1 的包是这一帧的最后一个 RTP 包**。接收端两种判定方式：① 看 Marker 位（最直接）；② 看 Timestamp 变化（下一个包的 Timestamp 和当前不一样，说明换帧了）。Jitter Buffer 用这两条规则组合：等到 Marker = 1 的包到达、且这一帧所有 SeqNum 连续没断层时，判定帧完整可解码。

### Q5. 为什么 RTP 用 UDP 不用 TCP？

**口语化标准答案**：核心是实时性 vs 可靠性的取舍。① TCP 有队头阻塞——丢一个包后续包必须等重传到达才能往上层投递，实时通信里这会让所有后续帧都跟着卡；UDP 没这个约束，应用层自己决定丢包恢复策略。② TCP 拥塞控制偏保守——检测到丢包就降速 50%，不适合视频流"宁可糊一会儿也要保住带宽"的需求；WebRTC 用 GCC/BBR 自己做拥塞控制，能更激进。③ 重传 vs 跳过的选择——实时通信里晚到的包等于没用，TCP 强行重传只让后续更晚；UDP 直接丢、用 FEC/NACK 选择性恢复关键帧。RTP/UDP 的可靠性是应用层补的：NACK 选择性重传、FEC 前向纠错、PLC 丢包隐藏。

### Q6. SSRC 冲突了怎么办？

**口语化标准答案**：SSRC 是 32 位随机数，同一会话内不同流必须不同。冲突场景：① 客户端启动时随机初始值碰撞（概率 1/2^32，极低）；② SFU 中转场景多路流汇合时被动碰撞。检测机制：每个端记录见过的 SSRC，收到陌生 RTP 包就更新 SSRC 表；如果发现自己发出的 SSRC 被别人复用了，就**自动切换到新的随机 SSRC** 并发一个 RTCP BYE 包通告旧 SSRC 终止。接收端看到 BYE 后清理对应的 Jitter Buffer 状态。

### Q7. 你的 B 层 RTP 实现和 libwebrtc 的差异在哪？

**口语化标准答案**：主要差异在简化范围。① 我只支持 H264，不支持 VP8/VP9/AV1，因为后者打包规则不同（VP8 用 picture ID + tl0picidx，VP9/AV1 用 scalability 结构）；② 不实现 RTX 重传通道，所以 NACK 重传场景下回退到普通 RTP 流；③ 扩展头只解析基本字段（abs-send-time、transport-cc seq），不支持完整的 RFC 8285 mixed one/two-byte 模式；④ SRTP 加密不做，让 libwebrtc 上层处理；⑤ Pacer 不做。这些简化让总代码量从 libwebrtc 的 1500+ 行降到 500 行，但**核心打包/解包路径（Single/STAP-A/FU-A）的字节输出和 libwebrtc 是字节对齐的**——我用 libwebrtc 的 RtpPacketizerH264 跑相同输入，对比两者输出的 RTP 包字节，验证正确性。

### Q8. 如果发送侧 Packetizer 和接收侧 Depacketizer 都正常工作，但解码端画面有花屏，可能是什么原因？

**口语化标准答案**：花屏意味着解码器收到了部分损坏的 NALU，常见 5 个原因：① **FU-A 重组逻辑错**——中间分片丢失但接收端没检测到 SeqNum 断层，把残缺数据拼接交给解码器；② **emulation prevention 字节没处理**——发送端漏写 0x03 转义、或接收端解析 NALU 时没去转义，导致 RBSP 数据偏移错位；③ **STAP-A 长度字段读错字节序**——把小端读成大端，NALU 边界全乱；④ **关键帧的 SPS/PPS 丢了**——解码器收到 IDR 但参数集缺失，瞎解一通；⑤ **时间戳不一致**——同一帧的多个 RTP 包 timestamp 不同，Jitter Buffer 误判为多帧。排查顺序：先 dump 发送端打的字节、再 dump 接收端解出的 NALU 字节、和原始 NALU 对比，差异在哪一步就定位到哪一层。

---

## 结束语

阶段三 · M4 主文档完成。覆盖：

- ✅ 原理详解（RFC 3550 头 + RFC 6184 三种打包模式 + 时间戳计算）
- ✅ 类图 + 两个时序图（发送 / 接收）
- ✅ 5 个设计取舍（MTU / FU-A vs STAP-A / 序列化方式 / 迭代器 / 中途丢包）
- ✅ 13 个 libwebrtc 源码笔记（每个含路径 + 签名 + 100-150 字注释 + 面试可能问的点）
- ✅ IRtpPacketizer / IRtpDepacketizer 接口骨架
- ✅ FU-A 分片核心算法 30 行
- ✅ 8 道面试问答映射
- ⏳ B 层完整代码（500 行 + 单元测试）待环境就绪后补

### 推荐的并行学习节奏

| 时间 | 你做的事 | 我配合的事 |
|------|---------|-----------|
| 第 1-2 天 | 按环境清单装 brew / Xcode / CMake / coturn | 你边搭边读 M4 文档的"原理"和"源码笔记"部分 |
| 第 3 天 | CocoaPods 装 GoogleWebRTC、B 层 CMake 项目 hello world | 你告诉我"环境就绪"，我立即输出第 7 节完整代码 |
| 第 4-7 天 | 跟着完整代码自己敲一遍、跑通单元测试 | 答疑、补充设计细节 |
| 第 8-10 天 | 写 Adapter 接入 libwebrtc、做字节对齐验证 | 输出"Adapter 接入指南" |

### 等你回复

读完本文档后告诉我：

1. **哪些原理点你还不清楚**？（例如 "Q3 简单除法切片的解释我没看懂"）
2. **libwebrtc 源码笔记的 13 个函数**：有哪些想让我**详细展开**？（最多挑 3 个，因为再多文档会爆炸）
3. **环境就绪了吗**？就绪就说"环境就绪"，我立刻补第 7 节代码。
4. **进度上**：你想"边搭环境边深挖原理"，还是"环境优先全部搞定再回来读文档"？
