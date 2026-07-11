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

#### 字段语义详解（逐字节拆解）

RTP 固定头占 12 字节。下面逐字节说明每个 bit 的含义、典型值和面试考点。

---

**字节 0：版本号 + 三个标志位 + CSRC 计数**

```
Bit:  7  6  5  4  3  2  1  0
     ┌──┬──┬──┬──┬──┬──┬──┬──┐
     │ V=2  │P│X│  CC=0   │
     └──┬──┴──┴──┴──┬──┬──┴──┘
        │   标志位    │  CSRC 数量
        └─ 版本号 ───┘
```

| Bit(s) | 字段 | 说明 |
| :--- | :--- | :--- |
| 7-6 | **V（Version）** | 固定 `10`（十进制 2）。收到 V≠2 的包直接丢弃 |
| 5 | **P（Padding）** | 为 1 表示载荷末尾有填充。**最后一个填充字节的值 = 填充总长度**（不含自身），接收端读最后一字节就知道要去掉多少 padding。B 层实现固定 P=0 |
| 4 | **X（Extension）** | 为 1 表示固定头后接一个扩展头。WebRTC 用扩展头传 `abs-send-time`、`transport-cc` 序号等。B 层实现固定 X=0 |
| 3-0 | **CC（CSRC Count）** | 0-15，表示后面跟几个 CSRC（每个 32 位）。合流/混音场景下列出原始流的 SSRC。单流场景固定 CC=0。B 层只支持 CC=0 |

**B 层实现中字节 0 的常量**：`0x80`（即 `(2 << 6) | 0`，V=2, P=X=CC=0）。

---

**字节 1：Marker 位 + Payload Type**

```
Bit:  7  6  5  4  3  2  1  0
     ┌──┬──┬──┬──┬──┬──┬──┬──┐
     │ M │      PT (0-127)    │
     └──┴──┴──┴──┴──┴──┴──┴──┘
```

| Bit(s) | 字段 | 说明 |
| :--- | :--- | :--- |
| 7 | **M（Marker）** | **视频中标记一帧的最后一个 RTP 包**（M=1 = 帧边界，接收端据此判断一帧是否收齐）。音频中标记静音结束后的第一个包（talk-spurt 起始）。面试高频考点 |
| 6-0 | **PT（Payload Type）** | 7 位，0-127。0-95 是静态分配（IANA 注册，如 PCMU=0、PCMA=8、GSM=3），96-127 是动态范围（由 SDP `a=rtpmap:96 VP8/90000` 协商）。**PT 决定接收端用哪个 Depacketizer 解析载荷** |

**读代码时注意**：`SetPayloadType(0xFF)` 应被截断为 `0x7F`（只取低 7 位），这是测试里 `PayloadTypeUpperBitIgnored` 用例覆盖的点。

---

**字节 2-3：Sequence Number（序列号）**

```
Byte 2          Byte 3
┌────────────┬────────────┐
│  SeqNum 高 8 位  │  SeqNum 低 8 位  │  大端序
└────────────┴────────────┘
```

| 属性 | 说明 |
| :--- | :--- |
| **位宽** | 16 位，范围 0-65535，溢出自动 wraparound 到 0 |
| **递进规则** | 每发一个 RTP 包 +1（不是每帧 +1，一帧可能拆成几十个包） |
| **初始值** | **随机**（防止 known-plaintext 攻击）。不能用绝对值算时长，必须用 delta |
| **接收端用途** | ① 排序（乱序到达时恢复发送顺序）；② 丢包检测（seq 断层 = 中间有包丢了）；③ 去重（同一 seq 重复到达 = 网络 dup） |
| **wraparound 判断** | 不能直接比大小——seq=1 比 seq=65535 新。用 `int16_t((uint16_t)(a - b)) > 0` 判断 a 是否比 b 新。前提是 a 和 b 的距离 < 32768（约 13 秒的包 @ 500 pkt/s） |

**代码中序列化方式**：`buffer[2] = (seq >> 8) & 0xFF`、`buffer[3] = seq & 0xFF`——手动大端写入。

---

**字节 4-7：Timestamp（时间戳）**

```
Byte 4         Byte 5         Byte 6         Byte 7
┌────────────┬────────────┬────────────┬────────────┐
│  Timestamp 高 8 位 │             ...              │  大端序
└────────────┴────────────┴────────────┴────────────┘
```

| 属性 | 说明 |
| :--- | :--- |
| **位宽** | 32 位，范围 0 到 2³²-1 |
| **时钟频率** | **视频：90kHz**（90000 tick/秒，一帧 33ms@30fps ≈ 3000 tick）。**音频：采样率**（Opus 48kHz、PCMU 8kHz）。由 payload type 决定，SDP 中 `a=rtpmap` 的 clock rate 字段声明 |
| **同一帧规则** | **同一帧的所有 RTP 包共享同一个 Timestamp**。接收端根据 Timestamp + Marker 判定帧边界 |
| **初始值** | **随机**（和 SeqNum 同理）。不能用绝对值 |
| **wraparound** | 90kHz 基下 32 位约 13.25 小时溢出。直播跑超时需要处理 |
| **跨流对齐** | 不同 SSRC 的 Timestamp 不能直接比较（时钟频率不同、初始值不同）。必须通过 RTCP SR 的 NTP↔RTP 时间戳映射对齐到同一 NTP 时间线 |

---

**字节 8-11：SSRC（同步源标识符）**

```
Byte 8         Byte 9         Byte 10        Byte 11
┌────────────┬────────────┬────────────┬────────────┐
│         32 位 SSRC，标识一路 RTP 流               │  大端序
└────────────┴────────────┴────────────┴────────────┘
```

| 属性 | 说明 |
| :--- | :--- |
| **含义** | 唯一标识一路 RTP 流。同一会话内不同流（音频、视频、RTX、FEC）的 SSRC 必须不同 |
| **生成方式** | 随机生成 32 位整数 |
| **冲突处理** | 检测到自己发出的 SSRC 被别人复用 → 自动切换到新的随机 SSRC + 发 RTCP BYE 通告旧 SSRC 终止 |
| **在收端的作用** | ① 区分不同流（哪个包是视频、哪个是音频）；② 关联 RTX 重传流（SSRC-FID）；③ 丢包统计按 SSRC 独立计算 |
| **在 SDP 中的声明** | `a=ssrc:1234 cname:stream1`；RTX 关联：`a=ssrc-group:FID 1234 5678` |

---

**字节 12+：可变部分（CSRC 列表 + 扩展头）**

这两个部分**只在 CC>0 或 X=1 时才存在**，B 层实现不涉及，但面试需要知道存在：

```
固定头 12 字节
    │
    ├── CC > 0? ── 后面跟 CC × 4 字节的 CSRC 列表
    │   每个 CSRC 是 32 位 SSRC（混音/合流时列出原始源）
    │
    └── X = 1? ── 后面跟扩展头（RFC 8285）
        扩展头结构：
        ┌────────────┬────────────┐
        │ 0xBE 0xDE  │  length    │  ← 2 字节 profile + 2 字节长度
        ├────────────┼────────────┤
        │  扩展元素 1  │  扩展元素 2  │  ← one-byte 或 two-byte 格式
        └────────────┴────────────┘
```

| 可变部分 | 何时存在 | 内容 |
| :--- | :--- | :--- |
| **CSRC 列表** | CC > 0 | 每个 32 位，列出被混合的原始流 SSRC。最多 15 个（5×32=480 位=60 字节） |
| **扩展头** | X = 1 | 以 `0xBEDE` 魔数开头。one-byte 模式：`[ID(4)|L(4)][数据 L 字节]`；two-byte 模式：`[ID(8)|L(8)][数据 L 字节]`。WebRTC 用扩展头传 `kAbsSendTime`（abs-send-time，ID=3，3 字节）、`kTransportSequenceNumber`（transport-cc，ID=5，2 字节） |

---

#### 12 字节的内存视图（一次看清楚）

```
Offset  +0       +1       +2       +3       +4       +5       +6       +7       +8       +9       +10      +11
       ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
       │V P X CC│M    PT │  SeqNum (大端)  │            Timestamp (大端)          │              SSRC (大端)            │
       │2 0 0 0 │0/1 96 │  0x12  0x34     │  0xDE  0xAD  0xBE  0xEF             │  0x11  0x22  0x33  0x44             │
       └────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘
        Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7   Byte 8   Byte 9   Byte 10  Byte 11
```

**代码层面的对应**——`rtp_packet_test.cc` 里每个测试都在验证这张图的一个局部：

| 测试 | 验证内容 |
| :--- | :--- |
| `SequenceNumberSerializedAsBigEndian` | SeqNum 写入字节 2-3，高字节在前 |
| `TimestampSerializedAsBigEndian` | Timestamp 写入字节 4-7，高字节在前 |
| `SsrcSerializedAsBigEndian` | SSRC 写入字节 8-11，高字节在前 |
| `MarkerBitOccupiesHighBitOfSecondByte` | M 位占据字节 1 的 bit 7，PT 占据 bit 6-0 |
| `VersionFieldAlwaysTwoInSerializedOutput` | V=2 占据字节 0 的 bit 7-6，其余标志位为 0 |
| `ParseRejectsShortBuffer` | 包 < 12 字节直接拒（固定头不完整） |
| `ParseRejectsWrongVersion` | V≠2 直接拒 |
| `ParseRejectsNonZeroCsrcCount` | CC≠0 直接拒（B 层不支持 CSRC） |
| `ParseRejectsExtensionFlag` | X=1 直接拒（B 层不支持扩展头） |
| `PayloadTypeUpperBitIgnored` | PT 只取低 7 位 |

#### Marker 位的视频用法（重点）

一个 H264 关键帧可能 200KB+，必须拆成多个 RTP 包。接收端怎么知道一帧结束了？**Marker=1 的包是这一帧的最后一个**：

```
帧 1 (IDR 200KB):  RTP[seq=100, M=0] RTP[seq=101, M=0] ... RTP[seq=205, M=1]  ← 帧结束
帧 2 (P 30KB):     RTP[seq=206, M=0] RTP[seq=207, M=0] ... RTP[seq=220, M=1]  ← 帧结束
```

**Jitter Buffer 用这个标志判定帧完整性**。

### 2.3 H264 over RTP（RFC 6184）的 3 种打包模式

H264 编码器输出的是 **NALU 流**——每个 NALU 是一个独立的编码单元（IDR、P、SPS、PPS、SEI 都是不同类型的 NALU）。NALU 大小从几十字节（PPS）到几百 KB（4K 关键帧）不等。

**核心矛盾**：UDP 的 MTU 限制单包载荷不能超过约 1400 字节（典型以太网 1500 - IP/UDP 头），所以 RFC 6184 定义了 3 种打包策略。

#### RFC 6184 的协议层定位（面试常问：它是新协议吗？）

面试里有时会追问："RFC 6184 到底算不算一个协议？三种打包模式是 H.264 标准里就有的吗？"——这个问题的关键是理解 RFC 6184 在协议栈里的位置。

H.264 标准和 RTP 之间有一个**天然的阻抗不匹配**：

| | H.264 假设的世界 | RTP 的现实世界 |
|---|---|---|
| 数据单元 | NALU，大小任意（SPS 十几字节 ~ IDR 几十 KB） | UDP 包，受 MTU 限制（~1500 字节） |
| 边界识别 | Annex-B 用 `00 00 00 01` 起始码切分 | RTP 没有起始码概念，靠 seq 序号和 timestamp 识别 |
| 帧类型 | 写在 NALU header 里（type=5 表示 IDR） | RTP 头里没有"帧类型"字段，只有 PT 标识编码器 |

**RFC 6184 就是填这个坑的——它是 H.264 之上、RTP 之下的适配层协议**，专门解决"NALU 怎么适配 RTP 包"：

```
H.264 编码器
    │  产出: NALU（SPS/PPS/IDR/非IDR，大小任意）
    ▼
┌─────────────────────────────────────────────┐
│  RFC 6184 适配层（它就是协议，不是"补充说明"）│
│  - 判断 NALU 大小，选打包模式                  │
│  - 小           → Single NALU（不改动）       │
│  - 大（> MTU）  → FU-A（拆包 + S/E 标记）     │
│  - 多小         → STAP-A（合包 + 长度前缀）    │
│  - 定义接收端重组状态机（S=1 开始→E=1 提交）   │
│  - 定义丢包/超时处理（丢片→整帧丢弃→发 PLI）   │
└────────────────────┬────────────────────────┘
                     │  产出: RTP payload（已适配过）
                     ▼
┌─────────────────────────────────────────────┐
│  RTP 协议（RFC 3550）                        │
│  - 加 12 字节头（seq/ts/SSRC/PT/M）          │
│  - 不关心载荷是什么编码                       │
└────────────────────┬────────────────────────┘
                     ▼
                 UDP/IP → 网络
```

IETF 的术语里把 RFC 6184 这类文档叫 **"RTP Payload Format"** 而非 **"Protocol"**，但这是命名惯例，不是本质区别。从工程角度，RFC 6184 具备协议的全部特征：

| 协议特征 | RFC 6184 的实现 |
|---|---|
| 定义数据格式（字段语义、字节序） | FU indicator / FU header / STAP-A 2B 长度前缀 |
| 定义状态机（分片、重组规则） | S=1 开缓冲 → E=1 拼接 NALU 头 → 提交完整 NALU |
| 定义错误处理 | 丢中间片 → 整 NALU 丢弃；超时 → 发 PLI 请求 IDR |
| 定义版本兼容 | type=28(FU-A) vs type=29(FU-B，很少用)；packetization-mode=0/1 |
| 有独立 RFC 编号 | RFC 6184（2008，取代 RFC 3984） |

**一句话**：如果把 RTP 比作快递运单（seq/ts/SSRC），那 RFC 6184 就是"H.264 货物的装箱规范"——规定了多大才拆箱、怎么标记"第几箱/最后一箱"、接收方怎么拼回原样。H.264 标准本身完全没有定义这些。装箱规范也是规范，它就是一种协议。

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

### 2.4 Opus over RTP（RFC 7587）

> **Opus 码流结构（TOC 字节、帧打包、均分边界、DTX/FEC）的完整讲解已移至 [18-FFmpeg音频编解码详解.md](../../Doc/ffmpeg/18-FFmpeg音频编解码详解.md) §3.2.5**——那里是编解码层视角。本节只保留 RTP 传输层的核心要点。

Opus 和 H.264 在 RTP 打包上的本质区别：**H.264 的核心矛盾是"拆包"（大 NALU 超过 MTU → FU-A 分片），Opus 的核心问题是"省头"和"怎么处理不连续"**（一帧 Opus 几乎永不超 MTU）。

**一帧 Opus = TOC（1 字节）+ 压缩数据**。TOC 是编解码层（RFC 6716）的产物——`opus_encode()` 的第一个字节。RTP 直接原样塞 payload。

**RTP 打包，四种情况**：

| # | 情况 | RTP 包内容 | 场景 |
|---|---|---|---|
| 1 | **一帧一发**（默认）| `[RTP Hdr] [TOC][Frame]` | 正常通话，20ms 帧 |
| 2 | **多帧合包** | `[RTP Hdr] [TOC c=N][Frame0..N]` | 低码率省 RTP 头开销 |
| 3 | **一帧拆多包** | ❌ 不支持 | — |
| 4 | **DTX + FEC** | 静音不发包/带 FEC 副本 | 省带宽/抗丢包 |

**和 H.264 的关键差异**：

| | H.264 over RTP | Opus over RTP |
|---|---|---|
| 分片拆包 | ✅ FU-A | ❌ |
| 多帧合包 | ✅ STAP-A（SPS+PPS）| ✅ Frame Bundling（c 字段）|
| 时间戳基 | 90kHz | **48kHz** |
| Marker 位语义 | 帧最后 RTP 包 | DTX 恢复首帧 |
| 丢包恢复 | NACK/PLI 请求 IDR | FEC 副本兜底 |

> 多帧合包的边界计算（均分）、TOC 字节位结构、DTX/FEC 的工作机制详见 [18-FFmpeg音频编解码详解.md §3.2.5](../../Doc/ffmpeg/18-FFmpeg音频编解码详解.md)。

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

> NTP 时间格式、RTCP SR 报文结构与实践代码详见 [10-NTP与RTCP-SR详解.md](./10-NTP与RTCP-SR详解.md)。

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

**WebRTC 怎么做**：默认 RTP 载荷最大 **1200 字节**（不是 1400！）。源码在 `media/engine/webrtc_video_engine.cc` 的 `kMaxPayloadSize`。

---

#### ⚠️ 起点：为什么 WebRTC 要极力避免 IP 分片？

以太网默认 MTU 是 **1500 字节**——链路层一次能传输的最大数据。如果一个 UDP 包的总大小（含所有协议头）超过 1500，或超过路径上某个路由器的限制（即 **PMTU，Path MTU**），这个包就会在 **IP 层被强行分片**。

**IP 分片对 UDP 是致命的**。UDP 没有重传——若一个 3000 字节的视频帧被分片成 2 个 IP 片，传输中 **丢任意一片**，接收端就永远拼不出完整 UDP 报，**整个视频帧报废**，只能等上层 NACK 重传整个 RTP 包。弱网下丢包率被指数级放大：丢 1 个分片 = 丢 2 个分片 = 丢整个帧。

> TCP 分段可以只重传丢的那一个段；IP 分片丢一片 → 整个 IP 数据报丢弃 → 所有分片全白费。

---

#### 1. 为什么 1400 字节（PMTU 方案）风险高？

从 1500 往下减，**实验室最简情况下**确实够：

```
IPv4: 1500 - 20(IP) - 8(UDP) = 1472 → 保守取 1400 看起来很安全
IPv6: 1500 - 40(IP) - 8(UDP) = 1452
```

**但现实网络是个黑盒**。用户的 WebRTC 通话可能穿过：

- 家庭宽带 **PPPoE 拨号**（占 8 字节）→ 实际 MTU = 1500 - 8 = **1492**
- 公司 **IPsec VPN 隧道**（额外 IP 头 + ESP 约 50~70 字节）
- 云环境 **GRE / VXLAN overlay**（24~50 字节）
- 运营商骨干 **MPLS 标签栈**（每层 4 字节，常见 2~3 层）

这些隧道技术会在原始 IP 包外面**再套一层甚至多层新的 IP 头和隧道头**。

**举个 PPPoE + VPN 的实际路径**：

```
发出一个 RTP 载荷 1400 字节的包:

层                           开销       总包长
────────────────────────────────────────────
RTP payload (1400)
+ RTP header                 12        1412
+ SRTP auth tag (HMAC-SHA1)  10        1422
+ UDP header                 8         1430
+ IP header (IPv4)           20        1450
────────────────────────────────────────────  发出去了
中间跳: PPPoE 隧道           8         1458
→ 总包长 1458 > 1492(PPPoE 路径 MTU)  ✓ 暂时安全

再叠加 VPN (IPsec):
+ 外层 IP header             20        1478
+ ESP header                 8         1486
+ ESP trailer                16        1502
→ 总包长 1502 > 1492 > 1500  ✗ IP 分片！
```

**一旦经过两层常见隧道，1400 直接撑爆路径 MTU，引发 IP 分片。**

---

#### 2. 为什么 600 字节（小包模式）开销太大？

那索性切小（比如 600 字节），永远不分片？代价是**协议头开销占比暴涨**。

一个标准 WebRTC 视频包的协议头开销：

| 头 | 字节数 |
|---|---|
| IP header | 20 |
| UDP header | 8 |
| RTP 固定头 | 12 |
| RTP 扩展头（AbsSendTime / TransportWideCC 等） | ~16 |
| SRTP 加密尾部（ROC + Auth Tag） | ~12 |
| **合计** | **≈ 68 字节** |

**算占比**：

- 载荷 600 字节：头开销 = 68 / (600 + 68) ≈ **10.2%**——十分之一带宽用来运快递箱子、不运货
- 载荷 1200 字节：头开销 = 68 / (1200 + 68) ≈ **5.4%**——省了一半

高码率 1080p 视频下，10% 的带宽浪费相当于把 4Mbps 码率中的 400kbps 用来发空头，无法接受。

---

#### 3. 为什么 WebRTC 偏偏选中 1200 字节？

WebRTC 经过大量网络评测后，把 `kMaxPayloadSize` 锁死在 1200。这是一个极其优雅的折中值。

**逆向推导——1200 在最极端路径下的安全性**：

```
1200  (RTP 载荷)
+ 68  (全部协议头: IP+UDP+RTP+扩展+SRTP)
+ 20  (如果从 IPv4 切换到 IPv6: IP 头 20B→40B，净增 20B)
+ 80  (预留给多层 VPN、GRE、PPPoeE 等隧道封装的极端开销)
─────
= 1368 字节 ≪ 1500 字节（以太网标准 MTU）
```

即便用户走 **IPv6 + 套了两层 VPN 隧道**，包总长也才 1368 字节，**依然完美躲过绝大多数常规隧道切片阈值**（通常 1400~1450 字节）。

**一句话**：1200 既吃满了大部分带宽效率（头开销仅 ~5.4%），又买了一份"不分片保险"（留了近 130 字节安全余量）。

---

#### 方案对比总表

| 方案 | 载荷 | 头开销占比 | 分片风险 | 适用场景 |
|---|---|---|---|---|
| **1400 字节**（激进） | 接近 PMTU | ~4.6% | ⚠️ 高（VPN/PPPoE 即爆） | 可控内网专线 |
| **1200 字节**（WebRTC 默认） | 折中 | ~5.4% | ✅ 极低（留 130B+ 缓冲） | **互联网 RTC** |
| **600 字节**（保守） | 小包 | ~10.2% | ✅ 无 | 极端弱网（卫星/2G） |

---

#### 为什么不探测 PMTU？

理论上可以先探测再调整，但 WebRTC 不这么做：

1. **UDP 没有内核级 PMTU 缓存**——TCP 有，UDP 的应用层要自己做，实现和测试复杂
2. **探测需要多轮往返**，首帧延迟等不起
3. **路径 MTU 动态变化**——WiFi ↔ 4G/5G 切换后原来的探测值立刻过时
4. **1200 的边际收益极大**：吞吐损失几乎可忽略（~0.8% 头开销差），但换来的稳定覆盖全路径

---

#### 对本项目 B 层的影响

B 层 `kMaxPayloadSize = 1200`。FU-A 分片时的有效载荷上限：

```
每片有效载荷(不含 FU 双头) = 1200 - 2(FU Indicator + FU Header) = 1198 字节
每片最大载荷(含 FU 双头)   = min(1200, NALU 剩余)
```

---

#### 💡 面试满分作答模板

> "WebRTC 在源码中默认的视频 RTP 最大载荷是 1200 字节。核心权衡有三个层次：
>
> 第一，**不能更大（如 1400）**。用户的网络路径是黑盒——PPPoE 拨号会吃掉 8 字节、公司 VPN 再套一层 IP 头约 50-70 字节，加上 SRTP 加密和 IPv6 扩大的头开销，1400 的包轻而易举撑爆真实 PMTU，触发 IP 分片。UDP 分片丢任意一个 = 整个 IP 数据报作废 = RTP 包全丢，弱网下丢包率指数级放大。
>
> 第二，**不能更小（如 600）**。一个 WebRTC 包的全部协议头（IP/UDP/RTP/扩展/SRTP）约 68 字节。载荷 600 时头占比超过 10%，载荷 1200 时仅 5.4%。高码率 1080p 下 10% 的带宽浪费完全不可接受。
>
> 第三，**1200 的余量足够宽**。1200 + 68(头) + 20(IPv6 扩增) + 80(隧道缓冲) = 1368，即使在最恶劣的路径下也远小于 1500 的以太网 MTU。它用仅仅 0.8% 的额外头开销，买了一张'全路径零分片'的保险。"

### 4.2 取舍 2：FU-A 优先还是 STAP-A 优先？

这个问题本身有歧义——它们根本不是竞争关系，而是服务完全不同的数据类别。WebRTC 打包器（`modules/rtp_rtcp/source/rtp_format_h264.cc`）的决策逻辑不是一个"优先级排序"，而是一个**按 NALU 类型分流的两路决策树**。

#### WebRTC 的分流决策树

```
输入: EncodedImage (一帧 H264 的连续 NALU 流)
  │
  ├─ 参数集 NALU (SPS / PPS / SEI)
  │   → 永久 STAP-A 聚合在一起发
  │     原因: 每个只有十几到几十字节, 单独发包头开销 > 80%
  │          聚合后一个包搞定, 解码器一次拿到全部解码参数
  │
  └─ 视频数据 NALU (IDR slice / 非 IDR slice)
      │
      ├─ size ≤ 1200 (kMaxPayloadSize)
      │   → Single NALU (单包, 零额外开销)
      │
      └─ size > 1200
          → FU-A 分片
             - 先算总片数: n_fragments = ⌈NALU_size / 1200⌉
             - 平均分配字节到每片 (不是简单除法! 保证各片大小尽量接近)
             - 例: 2000 字节 NALU → 2 片各 ~1000 字节, 不出现一片 1200 一片 800
```

#### 为什么 SPS/PPS 必须 STAP-A？

不算不知道——SPS 约 15~30 字节，PPS 约 4~10 字节，SEI 几十字节。这些是**微型 NALU**。

```
如果 SPS(20B) 和 PPS(8B) 各自单独成包 Single NALU:

  RTP 包 1: [68B 协议头][20B SPS] = 88B → 头占比 77%
  RTP 包 2: [68B 协议头][8B  PPS] = 76B → 头占比 89%

两个包发了 164 字节, 有效数据只占 28 字节 (17%)

如果用 STAP-A 聚合:
  STAP-A 包: [68B 协议头][2B size][20B SPS][2B size][8B PPS]
            = 100 字节, 有效数据 32%, 且省了一个 RTP 发包
```

**对于几十字节的数据，单包发送的协议头开销完全不可接受**。STAP-A 是唯一合理的选择——不是"优先"，是必须。

#### 为什么视频 Slice 不积极 STAP-A？

有人想过：如果帧里有多个小 slice（比如 slice 1 = 200B, slice 2 = 300B），能不能用 STAP-A 聚合为一个包？

**WebRTC 不做这件事**。因为这不是 STAP-A 的设计场景——STAP-A 的"Single-Time"前提是**聚合的 NALU 共享同一个 PTS 且语义上天然成套**（SPS+PPS 天然就是一组参数集，丢了任何一个剩下的都没意义）。视频 slice 之间语义独立——丢一个 slice，解码器可以部分恢复画面（错误掩盖）。硬把它们捆在一起发，丢一个包 = 同时损失多个 slice = 画面破损面积反而大了。

> **一句话**：STAP-A 解决的是"小到不值得单独发包"的参数集；视频 slice 要么够小走 Single NALU，要么够大走 FU-A——STAP-A 根本不在视频 slice 的候选集里。

#### FU-A 分片的分片均衡原则

FU-A 不是简单地把前 N-1 片塞满 1200、最后一片收尾——那样最后一片可能很小（例如 100KB / 1200 = 84 片，前 83 片各 1200，第 84 片只剩 400 字节），带宽利用率差。

WebRTC 的算法：**按总片数平均分配每片大小**，让各片尽量接近，减少 MTU 空间的浪费：

```
NALU: 2000 字节 (不含 NALU 头)
每片有效载荷(不含 FU 双头) = 1198 字节
总片数 = ⌈2000 / 1198⌉ = 2

平均: 2000 / 2 = 1000 字节/片  →  包 1 有效 = 1000, 包 2 有效 = 1000
不是: 1198 + 802                 →  包 1 满, 包 2 浪费
```

---

#### 💡 面试满分作答模板

> "这个问题不能简单说谁优先——它们**服务的场景不同**，在 WebRTC 打包器里是两个独立的决策分支：
>
> 对于 **SPS / PPS / SEI 等参数集**，它们只有十几到几十字节。如果每个单独走 Single NALU 发包，协议头（IP+UDP+RTP+SRTP ~68 字节）的占比会飙到 80% 以上——大部分带宽用来运快递箱子，不是运货。所以 WebRTC 固定用 STAP-A 把 SPS、PPS 聚合在一个包里发，解码器一次性拿到全部解码参数，既省带宽又省发包数。
>
> 对于**视频 Slice 数据**，决策依据是 MTU——≤1200 字节走 Single NALU，>1200 走 FU-A 分片，且分片时按平均分配保证每个包大小均衡。STAP-A 不用于视频 slice 聚合，因为视频的多个 slice 语义独立，捆在一起发一旦丢包（会同时损失多个 slice），破损面积反而扩大。
>
> 所以准确的说法是：**参数集 STAP-A 是唯一选择（不是优先，是必须），视频数据按 MTU 卡——小则单包、大则 FU-A。不存在一个脱离 NALU 类型的通用优先级。**"

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
> - `06-M4-RTP传输模块/code/CMakeLists.txt`
> - `06-M4-RTP传输模块/code/include/rtp_packet.h`
> - `06-M4-RTP传输模块/code/include/rtp_packetizer.h`
> - `06-M4-RTP传输模块/code/include/rtp_depacketizer.h`
> - `06-M4-RTP传输模块/code/src/rtp_packet.cc`
> - `06-M4-RTP传输模块/code/src/h264_packetizer.cc`
> - `06-M4-RTP传输模块/code/src/h264_depacketizer.cc`
> - `06-M4-RTP传输模块/code/tests/CMakeLists.txt`
> - `06-M4-RTP传输模块/code/tests/rtp_packet_test.cc`（10 个测试点：字段读写、字节序、Parse 正负样本）
> - `06-M4-RTP传输模块/code/tests/h264_packetizer_test.cc`（8 个测试点：Single / STAP-A / FU-A 三种模式 + 边界 case）
> - `06-M4-RTP传输模块/code/tests/h264_depacketizer_test.cc`（6 个测试点：完整解包 + 丢中间分片 + 重组失败）
>
> **触发条件**：你完成 `05-环境准备清单-macOS.md` 第 8 节自检的"基础工具"和"B 层 CMake 项目"两块后，告诉我"环境就绪"，我会立即补完这部分。

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
