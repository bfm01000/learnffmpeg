# 21 - RTP 详解（面试导向）

> RTP（Real-time Transport Protocol）是实时音视频的**媒体承载层**。它不是"又一个流媒体协议"，而是 **RTSP 和 WebRTC 共同依赖的底层传输标准**——摄像头拉流用 RTP、视频会议用 RTP、连直播推流也有 RTP 的变体（SRT 基于 UDP 自封装，WebRTC 直播推流用 RTP）。理解 RTP 是打通 RTSP、WebRTC、SIP/VoIP 三条技术线的钥匙。
>
> 前置：协议大局观见 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §六，H.264 编码见 [11-H264与H265详解.md](./11-H264与H265详解.md)，RTSP 见 [08](./08-网络协议与流媒体.md) §6.7，WebRTC 见 [08](./08-网络协议与流媒体.md) §十。

---

## 一、面试速记卡（先背这个）

**一句话定义**：RTP 是跑在 UDP 之上的**实时传输协议**，专门用来驮音视频数据包。它不保证可靠送达，但在每个包的头上打了三个"标签"——序列号、时间戳、负载类型——让接收端知道"这是第几个包、什么时候该播、里面装的是什么编码"。

**关键点（高频考点）**：

- **层级定位**：RTP 是**媒体承载层**，不是应用层协议。它被 RTSP 和 WebRTC 使用，RTMP/HLS/FLV **不走 RTP**（它们自带封装）。
- **伴侣协议**：RTP 传媒体 + **RTCP** 传质量反馈，两者永远成对出现。RTP 用偶数端口，RTCP 用相邻奇数端口。
- **底层传输**：通常跑在 **UDP** 上（低延迟、允许丢包），也可以跑在 TCP 上（RTSP interleaved 模式，穿防火墙）。
- **NAT 友好性差**：纯 RTP over UDP 面对 NAT/防火墙很无力——这正是 WebRTC 要加 ICE/STUN/TURN 的原因。
- **不定义 payload 格式**：RTP 只定义**包头**，具体 payload 怎么装（H.264 怎么分包、AAC 怎么打包）由各自的 **RFC** 定义（H.264 over RTP = RFC 6184，H.265 = RFC 7798，AAC = RFC 3640/6416）。
- **SRTP**：RTP 的加密版，WebRTC 强制用 SRTP（RFC 3711），RTSP 可选。

**RTP 在协议栈中的位置**：

```
┌──────────────────────────────────────────────┐
│  应用层                                       │
│  RTSP(控制)  /  WebRTC(互动)  /  SIP(通话)    │
├──────────────────────────────────────────────┤
│  媒体承载层                                   │
│  RTP(驮音视频)  +  RTCP(质量反馈)              │  ← 这一篇的主角
│  SRTP(加密版 RTP)                             │
├──────────────────────────────────────────────┤
│  传输层                                       │
│  UDP(主力,快)  /  TCP(RTSP interleaved)       │
└──────────────────────────────────────────────┘
```

---

## 二、为什么要有 RTP——UDP 的"说明书"问题

UDP 只保证"一段字节从 A 到了 B"，不告诉你：
- 这几个包的**先后顺序**（丢了包 2，包 3 到了你也不知道）
- 这段数据**什么时候该播放**（音视频同步靠什么？）
- 里面装的**是什么编码**（H.264 还是 H.265？Opus 还是 AAC？）

RTP 在 UDP payload 前面加了一个 12 字节的最小头部，把上述信息全部补上。**RTP 本质就是"给 UDP 包加了个说明书"**。

类比：UDP 是快递袋，RTP 是在袋子上贴的标签（编号、时间、内容类型）。没这个标签，收件人拆开袋子看到一堆字节，完全不知道怎么办。

---

## 三、RTP 包头结构——12 字节逐字段拆解

这是面试中**最能体现深度的考点**。不要背，理解每个字段"解决什么问题"。

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       Sequence Number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           SSRC identifier                                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|            CSRC list (0~15 项, 每项 32 位) ...                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 逐字段解释

| 字段 | 位宽 | 记什么 | 解决什么问题 |
|---|---|---|---|
| **V** (Version) | 2 bit | 永远是 `2`（RFC 1889 v2） | 协议版本标识 |
| **P** (Padding) | 1 bit | =1 表示末尾有填充字节，最后一个字节是填充长度 | 某些加密算法要求块对齐 |
| **X** (Extension) | 1 bit | =1 表示有扩展头（很少用） | 预留扩展能力 |
| **CC** (CSRC Count) | 4 bit | CSRC 列表的条目数（0~15） | 混流器（mixer）标记原始源，普通端都是 0 |
| **M** (Marker) | 1 bit | **视频帧的最后一个包**=1、音频**说话开始**=1 | 标记帧边界！这是拆包后拼回帧的关键信号 |
| **PT** (Payload Type) | 7 bit | 标记编码类型，和 SDP 里的编号对应 | 告诉接收端用什么解码器 |
| **Sequence Number** | 16 bit | 每发一个 RTP 包 +1，初始值随机 | **发现丢包和乱序**——缺号=丢了，倒序=乱了 |
| **Timestamp** | 32 bit | 标记这一帧的**采样时刻**（不是墙上时钟，是采样时钟） | **音视频同步和播放定时**。视频通常 90kHz 时钟，音频是采样率（如 48kHz） |
| **SSRC** | 32 bit | 随机生成，标识**一路流的唯一 ID** | 区分多路流（如同时收音频+视频，SSRC 不同就知道谁是谁） |
| **CSRC** | 0~15×32 bit | 混流器填充，列出**原始参与源**的 SSRC | 多人混音混流场景，标明"这一包里有谁的声音" |

### 三个"灵魂字段"深入

**1. Sequence Number（序列号）—— 丢包探测器**

- 初始值随机（防攻击），之后每发一包严格 +1
- 16 位 → 范围 0~65535 → 溢出后回绕
- 接收端检测：`收到的 seq - 期望的 seq > 0` → 中间丢了包
- 判断乱序：`收到的 seq < 期望的 seq` → 这个包是"迟到的老人"

**面试例题**：发了 seq=65535，下一个包的 seq 应该是什么？
> 答：`0`。16 位无符号整数溢出回绕，接收端需要正确处理回绕逻辑（比较时用 `(new - old) mod 2^16`，不是直接比大小）。

**2. Timestamp（时间戳）—— 播放时钟**

- 视频通常用 **90 kHz 时钟**（每 tick = 1/90000 秒，即约 11.1μs）。为什么是 90k？因为 90k 是 24/25/30/50/60 fps 的整数倍，一帧的时长能用整数个 tick 表示（30fps 一帧 = 3000 ticks，25fps = 3600 ticks），不会有浮点误差。
- 音频通常用**采样率**作为时钟频率（48kHz 音频 = 每 tick 1/48000 秒）
- **同一帧的所有 RTP 包 timestamp 相同**（因为它们是同一帧的不同切片）
- Timestamp 的初始值也是**随机**的（防攻击）

**关键认知**：RTP timestamp 和墙上时钟（NTP/Wall Clock）**没有直接关系**。它只标记"采样时刻的相对先后"。如果要把 RTP timestamp 映射到墙上时间，需要用 **RTCP SR**（Sender Report）里的 NTP↔RTP timestamp 对应关系来换算。这也是为什么**音视频同步要靠 RTCP**。

**3. SSRC（同步源标识符）—— 流的身份证**

- 会议里有三个人，每个人发一路音频+一路视频 → 总共 6 个 RTP 流，6 个不同的 SSRC
- SSRC 随机生成，碰撞概率极低（32 位空间 + 随机种子）
- **同一个 SSRC 的 sequence number 和 timestamp 空间是独立连续的**
- 接收端按 SSRC 分拣：这个 SSRC 是张三的视频流，丢包只在这路里算

---

## 四、RTCP——RTP 的影子协议

RTP 只管发，不管"发得怎么样"。RTCP 是它的反向通道——接收端把质量数据喂回发送端。

### 4.1 RTCP 的 5 种报文类型

| 类型 | 全称 | 内容 | 谁发 |
|---|---|---|---|
| **SR** (200) | Sender Report | 发送端统计：发了多少包/字节 + NTP↔RTP timestamp 映射 | 正在发媒体的一端 |
| **RR** (201) | Receiver Report | 接收端统计：丢包率、累计丢包数、最高收到的 seq、jitter、LSR/DLSR(算 RTT) | 只收不发的一端，或 SR 之外额外的接收统计 |
| **SDES** (202) | Source Description | 源的元信息，至少包含 **CNAME**（Canonical Name，同一参与者的多路流通过相同 CNAME 关联） | 所有端 |
| **BYE** (203) | Goodbye | 告知"我的 SSRC 要退出了" | 离开的端 |
| **APP** (204) | Application-defined | 自定义扩展（如 WebRTC 的 REMB 带宽估计消息就是通过 APP 类型承载的） | 任何端 |

**核心认知**：SR 和 RR 的区别不是"一个发一个收"，而是"发送端发 SR（含发送统计），所有端发 RR（接收统计）"。一个同时收发媒体的端，既要发 SR（报告自己发的情况），也要发 RR（报告对方发过来的情况）。

### 4.2 RTCP 带宽限制——为什么不是每包反馈

RTCP 报文也走 UDP，但**不能每发一个 RTP 包就跟一个 RTCP**——那样 RTCP 自己就占满带宽了。RFC 3550 规定：

- **RTCP 总带宽不超过会话带宽的 5%**
- 其中 1.25% 给发送端（SR），3.75% 给接收端（RR）
- RTCP 报告间隔有最小值（通常 5 秒），且带随机抖动防止同步风暴

**工程意义**：即使你用 RTP 推 10Mbps 的视频，接收端回传的 RTCP RR 也就几百 kbps。这是协议设计层面对"反馈不能吃掉业务带宽"的考量。

### 4.3 RTCP 复合包——一次发一捆

RTCP 报文可以**复合**：一个 UDP 包里塞 SR + SDES + APP，一次发一捆。每个 RTCP 包有自己的长度字段，接收端逐个解析。这是 RTCP 的高频实现细节——抓包看到的一个 RTCP 报文里往往有 2~3 种类型。

---

## 五、H.264 怎么塞进 RTP——分包三模式

这是面试最爱考的具体实现问题。H.264 一帧可能几十 KB，而以太网 MTU 通常 1500 字节（UDP payload 约 1472 字节）。一帧往往要**拆成多个 RTP 包**发送。RFC 6184 定义了三种分包模式：

### 5.1 三种模式速查

| 模式 | NAL 头类型 | 干什么 | 什么时候用 |
|---|---|---|---|
| **Single NAL Unit** | 1~23（原始 NAL type） | 整个 NALU 装进一个 RTP 包 | NALU ≤ MTU，最常见的小包 |
| **STAP-A** (聚合包) | 24 | 一个 RTP 包里塞**多个小 NALU**（如 SPS+PPS 一起发） | 多个极小的 NALU，减少包数、降低开销 |
| **FU-A** (分片包) | 28 | 一个**大 NALU 切碎**成多个 RTP 包 | IDR 帧 / 大 P 帧超过 MTU |

### 5.2 FU-A 分片详解（最高频考点）

一个大 NALU（比如 IDR 关键帧可能有 50KB）被切成多个 FU-A 包。每个 FU-A 包的 RTP payload 结构：

```
+---------------+---------------+
| FU indicator  |  FU header    |  FU payload (NALU 数据的一块)
+---------------|---------------+
  1 byte          1 byte
```

**FU indicator（第 1 字节）**：

```
+---------------+
|F|NRI|  Type   |
+---------------+
```

- **F**（forbidden_zero_bit）：=1 表示有传输错误，正常是 0
- **NRI**（nal_ref_idc）：直接拷贝原始 NALU 的 NRI（标记这帧是否被参考）
- **Type**：固定为 **28**（即 FU-A 的 NAL type）

**FU header（第 2 字节）**：

```
+---------------+
|S|E|R|  Type   |
+---------------+
```

- **S**（Start）：=1 表示**这是该 NALU 的第一个分片**
- **E**（End）：=1 表示**这是该 NALU 的最后一个分片**
- **R**（Reserved）：保留，填 0
- **Type**：原始 NALU 的 type（1=非 IDR 切片、5=IDR 切片、7=SPS、8=PPS 等）

**接收端拼回逻辑**：
1. 收集同一 timestamp 的所有 RTP 包
2. 按 seq 排序
3. 找 S=1 的包（开始），中间的包（S=0,E=0），E=1 的包（结束）
4. 去掉每个 FU-A 包的 FU indicator + FU header（2 字节）
5. 拼接 payload，前面补上重构的 NAL 头（F + NRI + 原始 Type）
6. 得到完整的 NALU

**面试坑**：中间丢了任何一个 FU-A 分片 → 整个 NALU 报废 → 解码器要么丢帧花屏，要么等下一个 IDR 重置。所以才需要 **PLI/FIR** 请求关键帧（参见 [08](./08-网络协议与流媒体.md) §10.2）。

### 5.3 STAP-A 聚合——SPS/PPS 的最佳拍档

编码器开局必发的 SPS 和 PPS 各只有几十字节，各自发一个 RTP 包浪费（IP+UDP+RTP 头就 40+8+12=60 字节开销）。STAP-A 把它们**打包成一捆**：

```
RTP payload:
  NAL size(2B) | NALU 1 (SPS) | NAL size(2B) | NALU 2 (PPS)
```

一个包搞定，省了包头开销。STAP-A 绝不能用来打包 VCL NALU（实际编码数据），只用于非 VCL 的小 NALU（SPS/PPS/SEI 等）。

### 5.4 Marker bit 与帧边界

- 一帧的**最后一个** RTP 包的 **M 位 = 1**（"这帧完了"）
- 前面所有包 M=0
- 接收端解码时机：**收到 M=1 的包后，把攒的这一帧全部拼好，送去解码**

没有 M 位的话，接收端不知道一帧什么时候结束，只能靠超时猜测——要么早了解出不完整帧，要么多等增加延迟。

---

## 六、AAC 怎么塞进 RTP

AAC 走 RTP 比 H.264 简单，但也有容易踩的坑。

### 6.1 RFC 3640 模式（最常见）

AAC 以 **AU（Access Unit）** 为单位打包，一个 AU 就是一次音频编码的输出（通常 1024 个 sample）。RTP payload 结构：

```
+----------+-----------+----------+----------+
| AU Header | AU Header |  ...     | AU data  |
| (2 bytes) | (2 bytes) |          | (多个 AU)|
+----------+-----------+----------+----------+
```

每个 AU Header 包含：
- **AU-size**（13 bit）：该 AU 的数据字节数
- **AU-Index**（3 bit）：AU 序号

**一个 RTP 包可以装多个 AU**（多个音频帧一起发），减少包数。典型配置：20ms 一帧 → 50 帧/秒 → 每个 RTP 包装 2~5 帧，发包频率降到 10~25 包/秒，大幅降低 IP/UDP/RTP 包头开销比例。

### 6.2 和 H.264 over RTP 的关键区别

| | H.264 over RTP | AAC over RTP |
|---|---|---|
| 封装 RFC | **RFC 6184** | **RFC 3640 / 6416** |
| 分包模式 | FU-A 拆大帧、STAP-A 聚合小 NALU | 多个完整 AU 装一包 |
| 一包多个数据单元 | STAP-A 多个小 NALU | 天然支持多 AU（帧） |
| 包头额外开销 | FU indicator + FU header (2B) | AU Header (每 AU 2B) |

**面试记忆口诀**：视频要**切碎**（一帧太大塞不进），音频要**打包**（一帧太小多发浪费）。

---

## 七、RTP 的两种传输模式

### 7.1 RTP over UDP（主力模式）

```
IP 头(20B) | UDP 头(8B) | RTP 头(12B) | Payload
```

- 延迟最低，不需要等 TCP ACK
- 丢包不重传（要重传走 NACK，上层决定）
- NAT/防火墙**穿透性差**——UDP 端口映射容易过期，严苛防火墙直接 block

### 7.2 RTP over TCP / RTSP Interleaved

RTSP 提供了一种把 RTP 包**复用到 RTSP 那条 TCP 连接上**的方案：

```
$ | Channel(1B) | Length(2B) | RTP/RTCP 数据
```

- `$` 是 magic byte（0x24），标记这是一个 interleaved 帧
- Channel：偶数=RTP，奇数=RTCP（和 SETUP 协商的通道号对应）
- Length：后面 RTP 数据的字节长度

**优点**：只用一个 TCP 端口（554），穿透性好；**代价**：TCP 队头阻塞——丢了一个 RTP 包 TCP 会卡住等重传，后面的 RTP 包全被阻塞。

**FFmpeg 实战**：

```bash
# 默认 RTP over UDP
ffmpeg -i rtsp://camera/stream -c copy output.mp4

# 强制走 TCP interleaved（穿透防火墙 / NAT 的常用手段）
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream -c copy output.mp4
```

---

## 八、SRTP——加密版 RTP

### 8.1 SRTP vs RTP

| | RTP | SRTP (RFC 3711) |
|---|---|---|
| 加密 | ❌ 明文 | ✅ AES 加密 payload |
| 完整性 | ❌ 无 | ✅ HMAC-SHA1 认证标签 |
| 防重放 | ❌ 无 | ✅ 序列号窗口检查 |
| 包头 | 12 字节最小 | 12 + 认证标签（通常 10 字节，可选 4/10 字节） |
| 使用场景 | RTSP 拉摄像头（默认明文） | **WebRTC 强制**，SIP 可协商 |

### 8.2 密钥交换

SRTP 本身只定义**加密格式**，不定义密钥怎么来。密钥交换由上层协议负责：
- **WebRTC**：DTLS-SRTP（DTLS 握手交换密钥 → 喂给 SRTP）
- **SIP/RTP**：SDES（在 SDP 里明文传密钥——极不安全，不推荐）、ZRTP、DTLS-SRTP

**面试考点**：为什么 WebRTC 用 DTLS-SRTP 而不是 SDES？因为 **SDES 在 SDP 信令里明文传密钥**，任何中间人截到 SDP 就能解密所有媒体。DTLS-SRTP 走完整的 DTLS 握手，密钥从不离开两端，即使信令被截也无法解密媒体。

### 8.3 SRTP 加密了什么

SRTP **只加密 payload**（音视频数据），**不加密 RTP 包头**（序列号、时间戳、SSRC 仍是明文）。这样中间网络设备（SFU、负载均衡器）不需要解密就能做转发决策、统计——一个很精妙的设计权衡。

---

## 九、RTP 在实际场景中的角色

### 9.1 RTSP 拉摄像头

```
客户端 --TCP 554--> RTSP DESCRIBE --> 摄像头
       <--SDP(描述媒体: H.264, G.711, RTP 端口)-- 
客户端 --RTSP SETUP--> 协商传输模式
客户端 --RTSP PLAY---> 
       <== UDP: RTP(视频) + RTCP(质量) ==> 开始传媒体
```

RTSP 是控制通道，RTP 是数据通道。这是"控制与媒体分离"的经典范式。

### 9.2 WebRTC 视频会议

```
端 A <== SRTP(媒体) + SRTCP(反馈) ==> SFU <== SRTP + SRTCP ==> 端 B
          UDP                                       UDP
     ICE/STUN/TURN 打洞                      ICE/STUN/TURN 打洞
           DTLS 密钥交换                           DTLS 密钥交换
```

WebRTC 对 RTP 的增强：SRTP 加密 + RTCP 反馈（NACK/PLI/FIR/REMB/transport-cc）+ ICE 穿透 + 拥塞控制。详见 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §十。

### 9.3 直播推流（RTP 方式）

传统直播推流用 RTMP，但也有 RTP 推流的场景：

```bash
# FFmpeg RTP 推流
ffmpeg -re -i input.mp4 \
  -c:v libx264 -c:a aac \
  -f rtp rtp://127.0.0.1:5004

# 配合 SDP 文件
ffmpeg -re -i input.mp4 \
  -c:v libx264 -c:a aac \
  -f rtp rtp://127.0.0.1:5004 \
  -sdp_file stream.sdp
```

### 9.4 SRT——基于 UDP 的"TCP 级可靠"传输

SRT（Secure Reliable Transport）不是标准 RTP，是自研的 UDP 封装协议。但它借鉴了 RTP 的设计思想——序列号 + 时间戳 + 重传机制（ARQ）——在 UDP 上实现了低延迟的可靠传输。定位介于 RTMP（TCP 可靠）和 WebRTC（UDP 实时）之间。

---

## 十、FFmpeg 中的 RTP 操作实战

### 10.1 拉 RTP 流

```bash
# 从 RTSP 拉（底层是 RTP）
ffmpeg -rtsp_transport tcp -i rtsp://admin:12345@192.168.1.100:554/stream \
  -c copy output.mp4

# 直接拉裸 RTP（需要 SDP 文件）
ffmpeg -protocol_whitelist file,udp,rtp \
  -i stream.sdp -c copy output.mp4
```

### 10.2 推 RTP 流

```bash
# 推 H.264 到 RTP
ffmpeg -re -i input.mp4 \
  -an -c:v libx264 -f rtp rtp://224.0.0.1:5004

# 推多播 + 生成 SDP
ffmpeg -re -i input.mp4 \
  -c:v libx264 -c:a aac \
  -f rtp rtp://224.0.0.1:5004 \
  -sdp_file multicast.sdp
```

### 10.3 RTP 到 RTMP 转推

```bash
# 摄像头 RTSP(RTP) → RTMP 推流服务器
ffmpeg -rtsp_transport tcp -i rtsp://camera/stream \
  -c copy -f flv rtmp://live.server.com/app/stream
```

---

## 十一、高频面试问答（口语化答题模板）

**Q1：RTP 包头有哪些关键字段？各解决什么问题？**

> RTP 包头最少 12 个字节，核心字段有四个。**Sequence Number** 是 16 位的包序号，每发一个包加一，用来发现丢包和乱序——收到 1、3、4 就知道 2 丢了。**Timestamp** 是 32 位采样时刻，视频一般用 90kHz 时钟，同一帧的所有包 timestamp 一样，用来控制播放时机和音视频同步。**SSRC** 是 32 位随机标识符，唯一标记一路流，有多个参会者时靠 SSRC 区分谁的音频谁的视频。**Payload Type** 是 7 位，标明编码类型，比如 H.264、Opus，和 SDP 协商的编号对应。另外 **Marker 位**也很重要，一帧的最后一个包 M=1，接收端收到它才知道一帧收齐了可以开始解码。

**Q2：H.264 一帧 50KB，怎么装进 MTU 1500 的 RTP 包里？**

> 用 **FU-A 分片**。RFC 6184 定义了 FU-A（Fragmentation Unit-A）模式：把 NALU 切成多个分片，每个分片前面加 FU indicator 和 FU header 两个字节。FU indicator 标记这是 FU-A（type=28），FU header 里有 S/E 位——S=1 是第一个分片，E=1 是最后一个分片，中间的都=0。接收端按序列号排序，找到 S 开始 E 结束，去掉每片的 2 字节头，拼起来，重建原始 NAL 头，就拿到了完整的 NALU。

> 顺便，小的非 VCL NALU 比如 SPS 和 PPS，会用 **STAP-A**（聚合包）把它们塞进一个 RTP 包，省包头开销。

**Q3：RTP 和 RTCP 什么关系？RTCP 有哪些报文类型？**

> RTP 传媒体，RTCP 传质量反馈，两者成对出现，RTP 用偶数端口、RTCP 用相邻奇数端口。RTCP 主要有五种报文：**SR**（发送端报告，带 NTP↔RTP timestamp 映射和发送统计）、**RR**（接收端报告，丢包率、jitter、RTT）、**SDES**（源描述，至少包含 CNAME）、**BYE**（通知离开）、**APP**（自定义扩展，WebRTC 的 REMB 就走这一路）。协议规定 RTCP 不能超过带宽的 5%，所以间隔通常是秒级。

**Q4：SRTP 和 RTP 有什么区别？WebRTC 为什么用 DTLS-SRTP？**

> SRTP 是加密版 RTP，加了 AES 加密和 HMAC-SHA1 完整性认证。但 SRTP 只定义加密格式不定义密钥怎么来。WebRTC 用 DTLS 握手交换密钥——在媒体传输前先在 UDP 上跑完整的 TLS 握手，协商出密钥后喂给 SRTP。为什么不用更简单的 SDES（在 SDP 里直接写密钥）？因为 **SDES 密钥明文在信令里**，中间人截获信令就能解密所有媒体；DTLS-SRTP 是端到端密钥协商，信令即使被截也无法解密。

**Q5：RTP timestamp 和墙上时钟是什么关系？**

> **没有直接关系**。RTP timestamp 是采样时钟，不是墙上时钟。视频通常是 90kHz 时钟，从 0 开始计数（初始值随机），每采样一个 tick 加一。要想把 RTP timestamp 映射到墙上时间，必须通过 RTCP SR 里的 NTP timestamp 和对应的 RTP timestamp 做换算——这是音视频同步的基础。RTCP SR 就是那个"翻译官"，告诉接收端"RTP timestamp 等于 3000 的时候，对应的墙上时间是 12:00:00.000"。

**Q6：一个 RTP 包丢了会怎样？怎么恢复？**

> 分两层看。**检测**层面：接收端看 Sequence Number——期望 seq=100，收到的是 102，就知道 101 丢了。**恢复**层面：少量丢包可以发 NACK 请求重传这一个包（按 RTP 序列号精准指定）；如果丢太多、解码器参考链崩了，就只能发 PLI/FIR 请求对端产一个 IDR 关键帧重新起步。注意如果用 RTP over TCP interleaved，丢包会自动被 TCP 重传，但代价是队头阻塞——后续所有 RTP 包都得等。

---

## 十二、抓包分析要点（Wireshark 实战）

### 12.1 快速过滤

```
rtp                          # 只看 RTP 包
rtcp                         # 只看 RTCP 包
rtsp                         # 只看 RTSP 控制包
rtp.ssrc == 0x12345678       # 只看某个 SSRC 的流
rtp.payload_type == 96       # 只看某种编码的包（96+ 是动态 PT，97、98 等常见于 H.264/H.265）
```

### 12.2 关键分析菜单

- **Telephony → RTP → RTP Streams**：列出所有 RTP 流，一目了然看到每个 SSRC 的丢包率、jitter、码率
- **Telephony → RTP → RTP Stream Analysis**：深入一路流，看丢包分布图、jitter 随时间变化、包到达间隔
- **Statistics → Flow Graph**：看 RTP/RTCP 的交互时序（RTSP 控制 → RTP 媒体 → 中间丢了几个包 → 发 RTCP RR 反馈）

### 12.3 怎么看丢包和乱序

Wireshark 的 RTP 解析器会自动计算：
- **Delta**：相邻两个 RTP 包的时间间隔
- **Sequence**：序列号差值，=1 正常，>1 中间有丢包，<0 是乱序到达
- **Jitter**：包到达间隔的统计方差

这些数据在 RTP Stream Analysis 里都有图表，丢包的位置一眼就能看到。

---

## 十三、自检清单

- [ ] RTP 在协议栈中处于哪一层？它和 RTSP、WebRTC、RTMP 是什么关系？
- [ ] RTP 包头最少多少字节？Sequence Number、Timestamp、SSRC 各解决什么问题？
- [ ] Timestamp 为什么视频用 90kHz？和墙上时钟怎么映射？
- [ ] SSRC 是什么？一个视频会议里有 3 个人发音频+视频，共有几个 SSRC？
- [ ] RTCP 有哪五种报文？SR 和 RR 的区别是什么？
- [ ] RTCP 带宽为什么限制在 5%？
- [ ] H.264 over RTP 有哪三种模式？FU-A 的 S/E 位是什么？
- [ ] 丢了 NALU 中间的一个 FU-A 分片会怎样？
- [ ] AAC 怎么打包进 RTP？和 H.264 打包的关键区别是什么？
- [ ] RTP over UDP 和 RTP over TCP interleaved 各有什么优劣？
- [ ] SRTP 加密了什么？为什么 WebRTC 用 DTLS-SRTP 而不是 SDES？
- [ ] Wireshark 怎么快速过滤出一路 RTP 流的丢包情况？
- [ ] RTP 和 RTCP 的端口有什么关系？
- [ ] Marker bit 是干什么的？什么时候 M=1？

---

> 下一篇建议：RTP 理解到位后，回头重读 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §6.7 RTSP 和 §十 WebRTC——RTSP 是"RTSP 控制 + RTP 媒体"，WebRTC 是"ICE+DTLS+SRTP+GCC"。RTP 是这两条技术线共享的"通用语"，掌握了它，RTSP 和 WebRTC 的协议栈就只剩"在外面包了什么"的区别了。
