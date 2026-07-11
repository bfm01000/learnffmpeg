# 10 - NTP 与 RTCP SR：RTP 时间戳怎么和真实时钟挂钩

> 这篇文档回答两个面试必问题："RTP timestamp 是一个自增序号，接收端怎么转成真实的播放时间？"以及"视频 90kHz 基和音频 48kHz 基，接收端怎么把他们对齐？"
>
> 前置：读完 [06-M4-RTP传输模块.md](./06-M4-RTP传输模块.md) §2（RTP 协议 + 时间戳计算），理解 RTP timestamp 是什么。
> 配套：本文档和 [08-SDP真实样本解析.md](./08-SDP真实样本解析.md) 是控制面的左右手——SDP 管能力协商，RTCP 管时钟同步。

---

## 一、面试速记卡

**一句话**：RTCP SR（Sender Report）是发送端定期发的"时钟翻译报文"——它把 RTP timestamp（媒体世界里的相对时钟）映射到 NTP timestamp（真实世界的墙钟）。接收端有了这个映射，才能把多路独立的 RTP 流（视频 90kHz + 音频 48kHz）对到同一个时间线上做音画同步。

**关键词**：

- **NTP**：Network Time Protocol，墙上时钟（1900 年 1 月 1 日零点为正朔），64 位格式（高 32 位整数秒 + 低 32 位小数秒）
- **RTCP**：RTP Control Protocol，和 RTP 跑同一个会话的反馈通道（端口 = RTP 端口+1）
- **SR**：Sender Report，RTCP 报文类型 PT=200，发送端用它告诉接收端"这个 RTP ts 对应那个 NTP 时刻"
- **RR**：Receiver Report，PT=201，接收端只收不发媒体时用它上报丢包率和抖动
- **NTP ↔ RTP 映射**：SR 携带一对 `(NTP_timestamp, RTP_timestamp)`，接收端用这组数据推算出任意 RTP ts 对应的 NTP 时间
- **音画同步**：接收端把两个 SSRC 的 RTP ts 都换算成 NTP 时间，就能在同一个墙上时钟上比较早晚

---

## 二、先搞清楚 NTP 是什么

### 2.1 NTP 时间的格式

NTP 时间的起点是 **1900 年 1 月 1 日 0 时 0 分 0 秒 UTC**——比 Unix 时间（1970 年）早了正好 70 年。格式是 64 位定点数：

```
64-bit NTP timestamp:

 0                          31  32                         63
┌──────────────────────────┬─┬──────────────────────────────┐
│    integer seconds       │  fractional seconds            │
│    (32 bit 整数秒)        │  (32 bit 纯小数)                │
└──────────────────────────┴─┴──────────────────────────────┘
 高 32 位：从 1900-01-01 起经过的整数秒数
 低 32 位：秒的小数部分（每个 tick = 2⁻³² ≈ 0.233 纳秒）
```

举例：

```
NTP timestamp = 0xE0000000_80000000

高 32 位 = 0xE0000000 = 3,758,096,384 秒
→ 约 119 年（1900 + 119 = 2019 年左右）

低 32 位 = 0x80000000 = 2,147,483,648
→ 代表 2,147,483,648 / 2³² = 0.5 秒

时刻 = 2019 年某天的 xx:xx:xx.500000
```

### 2.2 NTP 时间 ≠ Unix 系统时间

| | Unix/Linux 系统时间 | NTP 时间 |
|---|---|---|
| 起点（epoch）| 1970-01-01 00:00:00 UTC | **1900-01-01 00:00:00 UTC** |
| 格式 | `time_t`（秒整数）+ 微秒/纳秒单独记 | 64 位定点数（高 32 位秒 + 低 32 位分数秒） |
| 精度 | 微秒级（`gettimeofday`）/ 纳秒级（`clock_gettime`） | 理论 2⁻³² 秒（~233 皮秒），实际秒级足矣 |
| 用途 | 文件时间戳、定时器、程序日志 | 网络时间同步、RTP 媒体时钟映射 |

**转换公式**：Unix epoch → NTP epoch，只需加上 70 年的秒数：

```
NTP seconds = Unix seconds + 2,208,988,800
// 2208988800 = 70年 × 365天 × 86400秒 + 17个闰日
```

```cpp
// C++ 代码：现在时刻 → NTP 64-bit timestamp
uint64_t unix_us = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::system_clock::now().time_since_epoch()
).count();

uint32_t ntp_sec  = (unix_us / 1000000) + 2208988800ULL;
uint32_t ntp_frac = (uint32_t)((unix_us % 1000000) * 4294.967296);
// 4294.967296 ≈ 2³² / 1,000,000 — 把微秒映射到 32 位分数

uint64_t ntp_timestamp = ((uint64_t)ntp_sec << 32) | ntp_frac;
```

### 2.3 NTP 协议本身（联网同步时钟）

**NTP 协议（RFC 5905）**是一个独立的网络协议，在 UDP 123 端口上跑。它的任务：让两台机器的时钟对齐，误差控制在 1-50ms。

```
客户端                               NTP 服务器
  │                                    │
  ├─ 请求(t1=客户端发送时刻) ─────────▶│
  │                                    ├─ (收到,t2=服务器接收时刻)
  │                                    ├─ 回复(t3=服务器回复时刻,...t1,t2)
  │◀───────── 回复(t1,t2,t3) ─────────┤
  │
  ├─ 收到(t4=客户端接收时刻)
  │
  ├─ 计算:
  │   offset = ((t2 - t1) + (t3 - t4)) / 2
  │   delay  = (t4 - t1) - (t3 - t2)
  │
  └─ 修正本地时钟: clock += offset
```

**机器每隔几分钟到几十分钟和 NTP 服务器同步一次**，保证系统墙钟准确。

---

## 三、RTCP 是什么——RTP 的"控制频道"

### 3.1 角色分工

RTP 和 RTCP 被 RFC 3550 **同一个文档**定义，分工明确：

```
同一个 RTP 会话:
  ├─ RTP 流 (UDP port N)   — 驮音视频帧, 带宽占 ~95%
  └─ RTCP 流 (UDP port N+1) — 驮统计/控制信息, 带宽占 ≤5%
```

| | RTP | RTCP |
|---|---|---|
| 干什么 | 运货（音视频数据）| 监控（质量、同步、元数据）|
| 包频率 | 每帧若干包，高频率 | 定期发（通常每 0.5-5 秒一次）|
| 常见负载 | H.264 NALU / Opus 帧 | SR / RR / SDES / BYE / APP |
| 谁发 | 发送端 | 发送端 + 接收端 |

### 3.2 五种 RTCP 报文

| 报文类型 | PT 值 | 英文全称 | 谁发 | 核心内容 |
|---|---|---|---|---|
| **SR** | 200 | Sender Report | **发送端** | NTP ↔ RTP 时间映射 + 发送统计 |
| **RR** | 201 | Receiver Report | **接收端** | 接收统计：丢包率、jitter、累计收包数 |
| **SDES** | 202 | Source Description | 所有人 | 元数据：CNAME（规范名）用于关联同一个参与者的多个 SSRC |
| **BYE** | 203 | Goodbye | 离开者 | "我下线了"——通知对方停止接收并清空状态 |
| **APP** | 204 | Application-defined | 所有人 | 自定义扩展（如 WebRTC 的 transport-cc 反馈） |

> **SR 和 RR 不要混淆（高频考点）**：SR = "我在发送媒体，顺带告诉你我的时钟基准"；RR = "我只在接收，不发媒体，但告诉你怎么收的"。如果同一端口既发又收，那它可以创建一个**复合 RTCP 包**（Compound Packet），把 SR + RR + SDES 都放一起发。

### 3.3 为什么 RTCP 要和 RTP 分开？

两个原因：
1. **带宽控制**：RTP 需要全速跑，RTCP 只需周期性地低速发，通信量分离避免拥塞
2. **解耦关注点**：发送端不关心 RTCP 里有多少个接收端（SFU 场景下有几百个下游），接收端的反馈周期和发送端的发送周期互不干扰

---

## 四、RTCP SR 报文详解

### 4.1 完整报文结构

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|    RC   |   PT=SR=200   |             length            |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         SSRC of sender                        |  ← "谁发的这份 SR"
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                NTP timestamp (64 bit)                         |  ← "现在是几点"
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     RTP timestamp (32 bit)                    |  ← "这个 NTP 时刻对应哪个 RTP ts"
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  sender's packet count (32 bit)               |  ← "至今发了多少个 RTP 包"
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  sender's octet count  (32 bit)               |  ← "至今发了多少字节"
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Report Block 1 (24 bytes)                     |  ← 每个数据源一个 Report
|  - SSRC_n (source 1)                                          |     Block, 最多 31 个
|  - fraction lost (丢包率 %)                                    |
|  - cumulative number of packets lost                          |
|  - extended highest sequence number received                  |
|  - interarrival jitter (到达间隔抖动)                           |
|  - last SR timestamp (LSR)                                    |
|  - delay since last SR (DLSR)                                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                 Report Block 2 ...                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  SDES / APP (可选)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 4.2 核心字段拆解

**头部（和 RTP 头一样，共 8 字节）**：

| 字段 | 宽度 | 含义 |
|---|---|---|
| V | 2 bit | 版本 = 2 |
| P | 1 bit | padding 位 |
| RC | 5 bit | Report Count — 这个 SR 里带了几个 Report Block |
| PT | 8 bit | Packet Type = **200**（固定值，标识这是 SR） |
| length | 16 bit | 报文长度（不包含头部首 4 字节） |

**SR 专属体（20 字节固定）**：

| 字段 | 宽度 | 含义 |
|---|---|---|
| SSRC | 32 bit | 这个 SR 是**谁**发的 |
| NTP timestamp | 64 bit | 发送 SR 的**真实时刻**（NTP 时间格式） |
| RTP timestamp | 32 bit | 在上述 NTP 时刻，**同一个 SSRC 的 RTP 时间戳**是多少 |
| packet count | 32 bit | 从开始到发 SR 为止，发了多少个 RTP 包 |
| octet count | 32 bit | 从开始到发 SR 为止，发了多少字节 |

### 4.3 核心操作：NTP ↔ RTP 映射

SR 的精髓在第三、第四字段——**"在 NTP 时刻 T，对应 RTP timestamp = S"**。这两个字段建立了一个线性函数：

```
RTP ts → NTP 时间的映射:

  任意 RTP ts 时刻 = T_ntp + (RTP_ts - S_rtp) / RTP_clock_rate

  T_ntp      = SR 里的 NTP timestamp（64 位）
  S_rtp      = SR 里的 RTP timestamp（32 位）
  clock_rate = RTP timestamp 基准频率（视频 90kHz, 音频 48kHz）
```

**视频帧的例子**：

```
收到 SR:
  NTP timestamp = 0xE1234567_80000000 (记作 T)
  RTP timestamp = 2000000                (记作 S)

之后收到视频帧: RTP ts = 2006000

该帧的 NTP 时间:
  T + (2006000 - 2000000) / 90000 = T + 6000/90000 = T + 0.0667秒
  → "这个视频帧在 SR 发出后 66.7ms 播放"
```

**两个 SSRC 间的音画对齐**：

```
收到视频 SSRC 的 SR:
  NTP = T0,  video  RTP ts = V0 (90kHz 基)
  
收到音频 SSRC 的 SR:
  NTP = T0,  audio  RTP ts = A0 (48kHz 基)
  （注意两个 SR 的 NTP 一样 → 同一时刻发出的 → 可以对齐）

当前接收端有一条视频帧 ts=V1, 一条音频帧 ts=A1:

  视频播放时刻 = T0 + (V1 - V0) / 90000
  音频播放时刻 = T0 + (A1 - A0) / 48000
  
  audio_lag = 视频播放时刻 - 音频播放时刻
  → 如果 audio_lag > 0: 音频快了，视频追音频，sleep video
  → 如果 audio_lag < 0: 视频快了，音频追（变速），或视频等
```

---

## 五、RTCP SR 的实际运作流程

### 5.1 发送端视角

```
while (sending) {
    // 1. 采集 + 编码 + 发 RTP 包
    encode_and_send_rtp_packets();

    // 2. 每隔一定时间（通常 0.5-5 秒），发一次 SR
    if (should_send_rtcp()) {
        RTCP_SR sr;
        sr.ssrc          = my_ssrc;
        sr.ntp_timestamp = get_ntp_now();           // 当前墙上时刻
        sr.rtp_timestamp = get_last_rtp_timestamp(); // 对应哪个 RTP ts
        sr.packet_count  = total_packets_sent;
        sr.octet_count   = total_bytes_sent;
        
        // 3. 每个已知的远程 SSRC 补一个 Report Block
        for (auto& peer : known_peers) {
            ReportBlock rb;
            rb.ssrc      = peer.ssrc;
            rb.fraction  = peer.fractionLost();
            rb.jitter    = peer.interarrivalJitter();
            sr.addReportBlock(rb);
        }
        
        send_rtcp_packet(sr);
    }
}
```

### 5.2 接收端视角

```
while (receiving) {
    if (rtp_packet_arrives()) {
        // 缓存 RTP 包
        jitter_buffer.insert(pkt);
    }
    
    if (rtcp_packet_arrives()) {
        RTCP_SR sr = parse(pkt);
        
        // 存储时钟映射
        ntps[ sr.ssrc ] = sr.ntp_timestamp;
        rtps[ sr.ssrc ] = sr.rtp_timestamp;
        
        // 更新对端统计
        update_rtt(sr.ssrc, sr.report_blocks);
    }
    
    // 需要渲染时
    if (ready_to_render()) {
        for (auto& frame : ready_frames) {
            // 通过 NTP-RTP 映射推算绝对播放时刻
            int64_t play_time = rtp_to_ntp(frame->ssrc, frame->rtp_ts);
            schedule_render(frame, play_time);
        }
    }
}
```

### 5.3 时间线图解

```
发送端:
  ──┬──────────────────────┬──────────────────────▶
    │ RTP ts=1000000       │ RTP ts=1003000
    │ 发 SR: NTP=T0        │ 发 SR: NTP=T0+33ms
    │   RTP ts=1000000     │   RTP ts=1003000
    │                          ↑
    │              两次 SR 都建立同一个线性映射:
    │              RTP ts = NTP * 90000 + const
    │
接收端:
    ├─ 收 SR1 → 存 (T0, 1000000)
    ├─ 收 SR2 → 可验证映射是否一致（wraparound 除外）
    ├─ 收帧 ts=1006000 → NTP时间 = T0 + (6000/90000)
    └─ 和音频帧的 NTP 时间比较 → 决定谁追谁
```

---

## 六、和 RTCP RR 的对比（面试常问）

| 维度 | SR（Sender Report） | RR（Receiver Report） |
|---|---|---|
| PT 值 | **200** | **201** |
| 谁发 | 正在发送媒体的一方 | 只接收不发送的一方 |
| 时间映射 | ✅ NTP ↔ RTP timestamp | ❌ 没有这个字段 |
| 发送统计 | ✅ packet count / octet count | ❌ |
| 接收统计 | ✅ Report Block（可选） | ✅ Report Block |
| LSR/DLSR | ✅ | ✅（用于 RTT 计算）|

**RTT 计算**（利用 LSR 和 DLSR 两个字段）：

```
A 发 SR:
  NTP timestamp = T_send

B 收到 SR, 记录下 T_send
B 发 RR:
  LSR = T_send 的高 32 位  (last SR timestamp)
  DLSR = 从收到 SR 到发 RR 的延迟

A 收到 RR:
  RTT = 当前 NTP - T_send - DLSR
```

---

## 七、WebRTC 里的 RTCP 实践要点

### 7.1 复合包（Compound Packet）

WebRTC 的 RTCP 包通常不是单独的 SR 或 RR，而是一个复合包：`SR/RR + SDES`。SDES 主要传 `CNAME`（Canonical Name）——同一个参与者的多个 SSRC（如一个视频 + 一个音频）共享同一个 CNAME，接收端通过 CNAME 把不同 SSRC 关联为同一用户。

### 7.2 带宽限制

RFC 3550 建议 RTCP 带宽不超过会话总带宽的 **5%**，且发送间隔有最小限制（默认 ≥ 5 秒）。WebRTC 放宽了这个限制——因为 RTC 场景总带宽低（几百 kbps），5% 可能意味着 10 秒才能发一次 RTCP 太慢。现代 WebRTC 实现（libwebrtc）可以每 0.5-1 秒发一次 RTCP。

### 7.3 RTCP 复用（multiplexing）

**同一个 UDP 端口同时跑 RTP 和 RTCP**——这叫 RTCP-mux（RFC 5761）。WebRTC 里强制开启，好处是 NAT 穿透只需打一个洞、ICE 候选只需检查一个端口对。对比传统视频会议（SIP/H.323）都是 RTP/RTCP 走两个端口。

---

## 八、面试高频问答

**Q1：RTP timestamp 是一个相对值，接收端怎么知道什么时候播放？**

> RTP timestamp 本身不告诉接收端"什么时候播"——它只告诉"这一帧和同一 SSRC 的前一帧隔了多久"。把 RTP ts 映射到真实时间，靠的是 RTCP SR。SR 里有一对 `(NTP timestamp, RTP timestamp)`——NTP 是发送端的墙上时钟，RTP ts 是同一个时刻该 SSRC 的媒体时间戳。接收端拿到 SR 后，就能用公式 `NTP_time = T_ntp + (rtp_ts - S_rtp) / clock_rate` 把任意 RTP ts 换算成 NTP 时间。换成同一 NTP 基准后，视频帧和音频帧就可以比较早晚，决定谁追谁。

**Q2：NTP 是什么？和 Unix 时间有什么区别？**

> NTP 时间是网络上同步时钟的标准格式。起点是 1900 年 1 月 1 日（Unix 是 1970 年），用 64 位表示——高 32 位是整数秒，低 32 位是秒的小数部分。Unix 时间转 NTP 只要加 70 年的秒数（2208988800）。RTCP SR 里用 NTP 而不是 Unix 时间，纯粹是历史原因——RTP 标准制定时（1996）NTP 已经是网络领域通用的时钟格式。

**Q3：SR 和 RR 的区别？什么时候用哪个？**

> SR 是发送媒体的一方发的，携带 NTP↔RTP 时间映射关系；RR 是只接收不发送的一方发的，只有接收统计（丢包率、抖动），没有时间映射。如果一方既在发又在收（WebRTC 通话场景的每一端），那就发 SR（既带自己的时间映射，也通过 Report Block 报告对方的接收统计）。核心：**有 RTP 流出去 → 发 SR；只在接收 → 发 RR**。

**Q4：音频 48kHz 基、视频 90kHz 基，怎么把它们的 RTP ts 对齐？**

> 各自用各自的 SR 算出 NTP 时间即可——两个 SSRC 各自独立发 SR，接收端存两套映射 `(video: T0 ↔ V0, audio: T0 ↔ A0)`。然后把当前要渲染的视频帧和音频帧各换算到同一个 NTP 时间线：`video_time = T0 + (V_current - V0) / 90000`，`audio_time = T0 + (A_current - A0) / 48000`。两个值在同一 NTP 时间线上，直接比较就能决定谁快谁慢。

**Q5：接收端多久能拿到第一次 RTCP SR？如果 SR 丢了怎么办？**

> 通常 0.5-5 秒发一次 SR，首帧可能没有 SR 可用。接收端的方法：先缓存前几个 RTP 包，等到第一个 SR 到，立即建立映射。如果 SR 中途丢了，下一个 SR（通常几秒内）到就可以重建映射——SR 的 NTP 和 RTP ts 都是新的，映射照样有效（只要 RTP 时间线没有 wraparound）。WebRTC 还会用 RTCP 的 `last SR timestamp (LSR)` 和 `delay since last SR (DLSR)` 字段来检测和计算 RTT。

---

## 九、与现有文档的衔接点

| 文档 | 关联点 |
|---|---|
| [06-M4-RTP传输模块.md](./06-M4-RTP传输模块.md) §2.2 | RTP 头格式：seq/ts/SSRC 字段 → 本文解释 ts 怎么翻译成 NTP |
| [06-M4-RTP传输模块.md](./06-M4-RTP传输模块.md) §2.5 | 跨流时间戳对齐 → 本文 §4.3 是它的公式展开和代码实现 |
| [01-入门导读.md](./01-入门导读.md) | 控制面 vs 数据面 → RTCP 是控制面核心，和 SDP/ICE/DTLS 平级 |
| [03-音视频基础self-check.md](./03-音视频基础self-check.md) Q21 | 音画同步三种策略 → 本文 §4.3 是"追外部墙钟"和"视频追音频"拿到主时钟的方式 |

---

## 十、关键数字速记表

| 数字 | 含义 |
|---|---|
| 1900-01-01 | NTP epoch |
| 1970-01-01 | Unix epoch |
| 2,208,988,800 | NTP epoch → Unix epoch 的秒数差 |
| 2³² | NTP 分数部分的模 |
| 4,294,967,296 | 2³²，把微秒映射到 NTP 分数的除数 |
| 200 | RTCP SR 的 PT 值 |
| 201 | RTCP RR 的 PT 值 |
| 90,000 | 视频 RTP 时间戳基准频率（Hz） |
| 48,000 | Opus 音频 RTP 时间戳基准频率（Hz） |
| 5% | RFC 3550 建议的 RTCP 带宽上限 |
| ~0.5-1s | WebRTC 实际 RTCP 发送间隔 |
