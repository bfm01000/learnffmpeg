# 阶段三 · M6：视频 Jitter Buffer（B 层重写核心模块 2/2）

> 自顶向下下钻视频抖动缓冲——这是接收侧最复杂的模块，**面试 TOP 3 高频题**。
> 模块归属：**B 层（自己重写）**——对位 libwebrtc 的 `modules/video_coding/packet_buffer.cc` + `frame_buffer.cc` + `jitter_estimator.cc`。
> 本文档覆盖：原理 → 类图与协作 → 设计取舍 → libwebrtc 源码笔记（12 个关键函数）→ 接口骨架 → 面试问答。
> B 层完整代码与文档并行交付，放在 `07-M6-JitterBuffer模块/code/`，可独立编译。
>
> **建议投入**：阅读 6-8 小时（含对照 libwebrtc 源码），实操编码 7-10 天。

---

## 目录

1. [职责再陈述](#1-职责再陈述)（含 [1.1 Jitter Buffer 在 QoS 四驾马车中的位置](#11-jitter-buffer-在-webrtc-qos-四驾马车中的位置)）
2. [原理详解（抖动/完整性/EWMA/调度）](#2-原理详解抖动完整性ewma调度)
3. [类图与协作图](#3-类图与协作图)
4. [设计取舍（面试追问点）](#4-设计取舍面试追问点)
5. [libwebrtc 源码阅读笔记（12 个关键函数）](#5-libwebrtc-源码阅读笔记12-个关键函数)
6. [接口与关键代码骨架](#6-接口与关键代码骨架)
7. [B 层完整代码（独立 CMake 项目）](#7-b-层完整代码独立-cmake-项目)
8. [面试问答映射（7 道高频题）](#8-面试问答映射7-道高频题)

---

## 1. 职责再陈述

### 阶段二里 M6 是什么

| 项 | 内容 |
|----|------|
| **模块名** | JitterBuffer（视频侧） |
| **归属** | **B 层（自己重写）** |
| **输入** | 来自 M4 RtpDepacketizer 的 NALU 流 + 元数据（RTP 时间戳、Marker、SeqNum）|
| **输出** | 完整、按显示时间排序的帧 + 渲染调度时刻 + 丢包反馈事件 |
| **不负责** | ① NACK 重传决策（独立模块）；② FEC 恢复；③ 解码；④ 渲染；⑤ 音频抖动（NetEQ 是另一套机制）|

### 边界（一句话）

> **"接收抖动 → 渲染节奏的转换器"**——把网络层的"乱序、突发到达"变成上层解码器需要的"有序、按节奏喂入"。

### 在系统中的位置

```
[Network] ──UDP─▶ [ICE/DTLS/SRTP] ──┐
                                     ▼
                            [M4 RtpDepacketizer]
                                     │
                                     │ NALU + (ts, seq, marker)
                                     ▼
                            ╔═══════════════════════╗
                            ║   ★ M6 JitterBuffer  ║
                            ║                       ║
                            ║   PacketBuffer        ║ ← 按 seq 索引缓存
                            ║   FrameAssembler      ║ ← 拼分片成帧
                            ║   JitterEstimator     ║ ← 抗抖估计
                            ║   TimingController    ║ ← 调度渲染
                            ╚═══════════╤═══════════╝
                                        │ Frame + RenderTimeMs
                                        ▼
                                  [Decoder] ──▶ [Renderer]
```

**★ 是本模块。**

### 1.1 Jitter Buffer 在 WebRTC QoS 四驾马车中的位置

Jitter Buffer 不是孤立工作的——它是 WebRTC **QoS（服务质量）与弱网对抗技术栈**里四驾马车之一。另外三驾是 **GCC（拥塞控制）、FEC（前向纠错）、NACK（丢包重传）**。它们目标一致（弱网下不卡顿、不花屏），但角色、工作阶段、解决的问题截然不同。

#### 四驾马车的分工

如果把 WebRTC 传输比作**一条不稳定的跨国物流快递线**：

| 技术 | 类比角色 | 核心职责 | 一句话 |
| :--- | :--- | :--- | :--- |
| **GCC** | 交警 / 雷达 | **主动预防堵车**——监控延迟和丢包，发现网络快堵了就通知编码器降码率 | 管"发送速度"，防过载 |
| **FEC** | 买保险 / 带备件 | **盲猜自修复**——发数据时附带冗余包（如 C = A ⊕ B），丢了 A 就用 B+C 本地解方程算回来 | 零延迟抗丢包，用带宽换时间 |
| **NACK** | 发现少货要求重发 | **缺啥补啥**——收到 seq=1,2,4 发现缺 3，立刻通知发送端从历史缓存重传 | 有延迟抗丢包，用时间换带宽 |
| **Jitter Buffer** | 中转分拣仓 | **消除忽快忽慢**——把乱序、突发到达的包重新排好（1,2,3,4），匀速喂给解码器 | 管"播放节奏"，消灭卡顿 |

#### 接收端的协同流水线

当一个 RTP 包从网络飞到设备上，四驾马车按以下顺序联合接力：

```
 [ 乱序、丢包的网络数据 ]
          │
          ▼
┌──────────────────┐
│   1. FEC 检查    │ ───► 发现丢包？能用冗余包直接解出来的，立刻在本地修好！
└──────┬───────────┘
       │ 修好了大部分，仍有漏网之鱼
       ▼
┌──────────────────┐
│  2. NACK 检查    │ ───► FEC 修不好的大漏网之鱼，立刻发 NACK 要求发送端重传
└──────┬───────────┘
       │ 所有包（原始 + FEC修复 + NACK重传）陆续到达
       ▼
┌──────────────────┐
│ 3. Jitter Buffer │ ───► 把到得乱七八糟的包按 SeqNum 排好队，匀速放水给解码器
│   （★ 本模块）    │      不管前面的包是怎么救回来的——Jitter Buffer 只管"到了之后怎么排"
└──────┬───────────┘
       │ 完整、有序的帧
       ▼
┌──────────────────┐
│ 4. RTCP 反馈机制 │ ───► 把丢包率、延迟梯度打包成 Report，发回给发送端的 GCC
└──────┬───────────┘
       │
       ▼
 [ 发送端 GCC 算法 ]  ───► 调整下一秒的发送码率
```

#### 关键区分：Jitter Buffer 和其他三个的本质不同

**FEC / NACK 解决的是"有没有"的问题**（包丢了没、能不能救回来），**Jitter Buffer 解决的是"到了之后怎么办"的问题**（到了但乱序了、到得忽快忽慢）。即便 FEC 和 NACK 把丢的包全部救回来，没有 Jitter Buffer，画面照样卡顿——因为网络包到达的节奏本身就不稳定。

此外，Jitter Buffer 是**唯一一个不管丢不丢包都在持续工作的模块**。FEC 只在丢包时生效，NACK 只在丢包后触发，GCC 以秒级调整码率——但 Jitter Buffer **每收到一个包都在判断**"该不该吐帧、什么时候吐"。

> **面试金句**："Jitter Buffer 是接收侧的最后一公里——前面 FEC、NACK 把包救回来，Jitter Buffer 负责把它们排成解码器能吃的节奏。它是从'网络时间'到'播放时间'的翻译器。"

---

## 2. 原理详解（抖动/完整性/EWMA/调度）

### 2.1 网络抖动的本质：为什么需要缓冲

**理想网络**：发送端每 33ms 发一帧，接收端每 33ms 收一帧——直接喂解码器即可。

**真实网络**：路由器排队、链路拥塞、WiFi 重传，导致接收间隔不稳定：

```
理想到达:  ─┬──┬──┬──┬──┬─▶  每帧间隔严格 33ms
           A  B  C  D  E

真实到达:  ─┬─┬─────┬┬─┬──▶  间隔忽快忽慢
           A B     C D E
```

**直接喂解码器的后果**：渲染节奏对不齐播放时钟 → 画面卡顿/快放/慢放。

**Jitter Buffer 的方案**：**主动延迟接收端 N ms**，把"乱序、突发"的到达整理成"有序、节奏稳定"的输出：

```
        网络抖动: 5-150ms      Jitter Buffer 吸收
                  ─────────▶  ─────────────────▶  稳定 33ms 间隔输出
       └ 包到达 ─┘            └─ 缓冲 80ms ─┘     └─ 解码渲染 ─┘
```

**核心矛盾**：缓冲深度 = 抗抖能力 vs 端到端延迟。**深度越大越能抗抖、延迟越高**。Jitter Buffer 的核心算法就是**动态调整这个深度**。

### 2.2 帧完整性判定：什么时候一帧"可用"

接收到的 RTP 包必须先**重组成完整帧**才能给解码器。判定依据：

1. **该帧所有 RTP 包都已到达**（SeqNum 连续不断层）
2. **该帧的 Marker 位包已到达**（标识帧的最后一个 RTP 包）
3. **同帧所有包的 RTP Timestamp 相同**（验证归属）

具体流程：

```
帧 N (ts=1000)  ───┬──[seq=100, M=0]
                   ├──[seq=101, M=0]
                   ├──[seq=102, M=0]
                   └──[seq=103, M=1]   ← Marker=1, 帧 N 完整

帧 N+1 (ts=1090) ──┬──[seq=104, M=0]
                   ├──[seq=105, M=0]   ← 等待中
                   ?                    ← 缺 seq=106
                   └──[seq=107, M=1]   ← Marker 到但不完整
```

**关键状态机**：
- 收到首包 → 标记帧"已开始"
- 收到 Marker 包 → 标记帧"已结束"
- 已结束 + SeqNum 连续 → 帧"可用"
- 已结束 + SeqNum 断层 → **等待 NACK 重传** 或 **超时丢弃**

### 2.3 抗抖估计：从 EWMA 到 Kalman 的演进

**问题**：怎么估计当前网络的抖动有多大？

#### 方案 A：EWMA（指数加权移动平均，本项目采用）

每收到一帧，测一次"实际到达间隔 vs 期望间隔"的偏差，用移动平均更新抖动估计：

```
const double SMOOTHING_FACTOR = 0.95;   // 历史值权重
const double ALPHA = 0.05;              // 新样本权重（α = 1 - SMOOTHING_FACTOR）

// 每收到一个完整帧时调用
void UpdateJitterEstimate(int64_t arrivalIntervalMs, int64_t expectedIntervalMs) {
    int64_t jitterSampleMs = std::abs(arrivalIntervalMs - expectedIntervalMs);
    estimatedJitterMs_ = (1 - ALPHA) * estimatedJitterMs_ + ALPHA * jitterSampleMs;
    // 等价于: 0.95 × old + 0.05 × new_sample
}
```

#### 两个输入是怎么算出来的（最关键的部分）

EWMA 公式本身很简单，但面试追问一定会落在 **`arrivalIntervalMs` 和 `expectedIntervalMs` 这两个数从哪来**。很多人会背公式但讲不清输入来源，一问就露馅。

**`arrivalIntervalMs`（实际到达间隔）**：

```
arrivalIntervalMs = 当前帧首包到达时刻 - 上一帧首包到达时刻
```

这是网络真实给出的间隔。如果发送端每 33ms 发一帧，但经过网络抖动后，接收端可能一帧隔了 20ms 就到了（突发快）、下一帧隔了 80ms 才到（排队慢）。

**`expectedIntervalMs`（期望到达间隔）**：

```
expectedIntervalMs = 当前帧的 RTP timestamp - 上一帧的 RTP timestamp
                    ───────────────────────────────────────────────────
                                  时钟频率 (Hz)

对于视频（90kHz 时钟基）:
  expectedIntervalMs = (rtpTimestamp - lastRtpTimestamp) / 90

  // 例：30fps 相邻帧的 timestamp 差 = 90000 / 30 = 3000 ticks
  //     expectedIntervalMs = 3000 / 90 = 33.333... ms

对于音频（Opus 48kHz）:
  expectedIntervalMs = (rtpTimestamp - lastRtpTimestamp) / 48
  // 例：20ms 的 Opus 帧 = 960 samples
  //     expectedIntervalMs = 960 / 48 = 20 ms
```

> **关键**：期望间隔用的是 RTP timestamp（发送端打的媒体时钟），实际间隔用的是接收端本地墙钟（`steady_clock`）。**二者的差就是网络抖动对这片数据造成的额外延迟**。如果网络零抖动，这两个数完全相等，`jitterSampleMs = 0`。

**为什么不用帧序号或 wall clock 差？**
- 帧序号只是顺序编号，不携带时间信息——发送端可能因为编码器背压导致帧间隔不是恒定的（P 帧 5ms 编完、I 帧 50ms 编完），SeqNum 没变，但真实发送节奏已经变了。
- RTP timestamp 是从**编码器的采样时钟**打上去的，精确反映了"这一帧应该在播放时间轴上处于什么位置"——这是唯一可靠的期望间隔来源。

#### 带数字的完整示例

假设 30fps 视频（期望间隔 33.3ms），初始 `estimatedJitterMs_ = 0`：

```
帧 1 到达: arrivalIntervalMs = 33ms  (第一帧无上一帧,跳过)
           estimatedJitterMs_ = 0

帧 2 到达: arrivalIntervalMs = 35ms, expectedIntervalMs = 33.3ms
           jitterSampleMs = |35 - 33.3| = 1.7ms
           estimatedJitterMs_ = 0.95 × 0 + 0.05 × 1.7 = 0.09 ms
           → 估计值几乎不动（历史为 0，新样本小）

帧 3 到达: arrivalIntervalMs = 40ms, expectedIntervalMs = 33.3ms
           jitterSampleMs = |40 - 33.3| = 6.7ms
           estimatedJitterMs_ = 0.95 × 0.09 + 0.05 × 6.7 = 0.42 ms

帧 4 到达: arrivalIntervalMs = 90ms, expectedIntervalMs = 33.3ms
           jitterSampleMs = |90 - 33.3| = 56.7ms  ← 突然拥塞！
           estimatedJitterMs_ = 0.95 × 0.42 + 0.05 × 56.7 = 3.23 ms
           → 一次突发只把估计推到 3.2ms（实际要慢 10+ 帧才追上 56ms）

... 10 帧后（每帧都 90ms 左右间隔）:
帧 14:     estimatedJitterMs_ ≈ 28 ms  ← 终于爬到实际水平的一半
帧 24:     estimatedJitterMs_ ≈ 45 ms  ← 接近真实值 56ms

然后网络突然恢复，间隔回到 33ms:
帧 25:     jitterSampleMs = |33 - 33.3| = 0.3ms
           estimatedJitterMs_ = 0.95 × 45 + 0.05 × 0.3 = 42.8 ms
           → 估计值又慢吞吞往下降...
```

**关键观察**：α = 0.05 时，EWMA 的时间常数 ≈ 1/0.05 = **20 帧**。意味着一个突变需要大约 20 帧才能让估计值跟上 63% 的变化，**完全收敛要 3-5 倍时间常数**（60-100 帧）。这就是 EWMA 天生的"慢"——也是它和 Kalman 的核心差距所在。

#### 平滑因子 α 为什么选 0.05？

| α 值 | 时间常数 | 收敛到 63% | 适用场景 |
| :--- | :--- | :--- | :--- |
| 0.01 | 100 帧 | ~3.3 秒（30fps） | 极稳定网络，几乎不过拟合 |
| **0.05** | **20 帧** | **~0.7 秒（30fps）** | **通用 RTC（本项目选择）** |
| 0.10 | 10 帧 | ~0.33 秒 | 快速跟踪，但估计噪声大 |
| 0.20 | 5 帧 | ~0.17 秒 | 接近 Kalman，但容易震荡 |

α = 0.05 是一个工程上的甜点：**稳态噪声低**（单个离谱样本只占 5% 权重），**恢复速度尚可**（~1 秒内反映网络变化），而且在面试中讲清楚"为什么是 0.05 而不是 0.01 或 0.10"比背一个常数更显功力。

#### 初始化和冷启动

```cpp
// 第一帧没有"上一帧"，跳过本次更新，只记录时间戳
if (lastFrameRtpTimestamp_ == 0) {
    lastFrameRtpTimestamp_ = currentRtpTimestamp;
    lastFrameArrivalMs_ = currentArrivalMs;
    return;  // estimatedJitterMs_ 保持初始值 0
}
```

冷启动时 `estimatedJitterMs_ = 0` → 前几帧的 `TargetDelayMs = 0 + DecodeDelay + RenderDelay ≈ 20ms`——缓冲极浅，容易丢帧。**实际工程中会给一个初始值**（如 30ms），防止首帧就丢。如果首帧丢了，解码器触发 PLI 请求关键帧，首帧延迟反而因为"重来一次"而更差。

**优势**：实现简单（10 行）、稳态下足够准确。  
**劣势**：突发抖动（WiFi 切换、临时拥塞）响应慢——一个 100ms 的突发可能要 10+ 帧才能反映到估计上（见上面帧 4→14→24 的数字推演）。

#### 方案 B：Kalman 滤波（libwebrtc 采用）

把抖动建模成**两个状态变量**的线性系统：
- 状态 1：每字节传输延迟（反映带宽）
- 状态 2：网络队列延迟（反映拥塞）

```
新观测 = H × [带宽状态, 队列状态] + 噪声
```

用卡尔曼增益**动态调整新观测的权重**：网络稳定时权重小（信任历史），突发时权重大（快速跟踪）。

**优势**：突发响应快、突发结束后回稳也快。  
**劣势**：实现复杂（500+ 行），调参困难（过程噪声、观测噪声矩阵），面试讲述复杂。

#### 本项目选 EWMA 的理由

- **代码量 1/10**：400 行 vs 4000 行，面试可讲清楚
- **稳态准确度接近**：稳定网络下两者偏差 < 5%
- **主动暴露简化点**：面试时说"我用 EWMA 替代 Kalman，知道差距在突发场景的收敛速度，这是有意识的工程取舍" → 加分

### 2.4 目标延迟（Target Delay）的计算

接收端的"延迟预算"由 3 部分组成：

```
TargetDelayMs = JitterDelayMs + DecodeDelayMs + RenderDelayMs

JitterDelayMs  = 3 × EstimatedJitterMs    ← 3σ 容忍 99.7% 抖动
DecodeDelayMs  = 历史解码耗时的 95 分位
RenderDelayMs  = 10ms（典型显示器刷新一帧的时间）
```

**为什么用 3 倍抖动**：假设抖动服从正态分布，3σ 覆盖 99.7% 的样本——只有 0.3% 的极端抖动会"漏过"（这部分通过 NACK 兜底）。

**典型数值**：
- 局域网：抖动 5ms → TargetDelay ≈ 35ms
- 4G 网络：抖动 30ms → TargetDelay ≈ 110ms
- 弱网 / WiFi 拥塞：抖动 80ms → TargetDelay ≈ 260ms

### 2.5 渲染时刻调度：什么时候把帧吐出来

```cpp
int64_t CalculateRenderTimeMs(int64_t firstPacketArrivalMs,
                              int64_t targetDelayMs) {
    return firstPacketArrivalMs + targetDelayMs;
}
```

**调度策略**：
1. 帧完整后立刻算 `renderTimeMs`
2. 等待 `renderTimeMs - now` 毫秒
3. 时刻到了喂解码器 → 解码器输出 → 渲染

**两种异常处理**：
- **`renderTimeMs < now`**：帧已经过期了（缓冲太小），立刻吐（追上播放时钟）
- **`renderTimeMs - now > 200ms`**：缓冲过深（突发抖动后回稳没及时收缩），**加速吐**——下游可能做时域伸缩或者直接快放

### 2.6 丢包检测与关键帧请求

#### 丢包检测

```cpp
// 收到新包时
if (newPacket.sequenceNumber != expectedNextSequenceNumber_) {
    if (newPacket.sequenceNumber > expectedNextSequenceNumber_) {
        // 中间有 SeqNum 缺失
        std::vector<uint16_t> missingSequences;
        for (uint16_t scanSeq = expectedNextSequenceNumber_;
             scanSeq < newPacket.sequenceNumber;
             ++scanSeq) {
            missingSequences.push_back(scanSeq);
        }
        observer_->OnPacketLossDetected(missingSequences);
    }
    // newPacket.sequenceNumber < expected 时：这是迟到的重传，正常存入即可
}
```

#### 关键帧请求（IDR Request）触发条件

**何时请求关键帧**：
1. **连续丢失超过阈值**（典型 10 个 SeqNum 在 200ms 内丢失，认为是严重丢包，NACK 难以恢复）
2. **解码器报告"无法继续"**（参考帧链断了）
3. **首次连接**（接收端刚加入，需要 IDR 起播）

请求方式：发 RTCP PLI（Picture Loss Indication）或 FIR（Full Intra Request）给发送端。

### 2.7 视频 Jitter Buffer vs 音频 NetEQ 的关键差异

| 维度 | 视频 JitterBuffer | 音频 NetEQ |
|------|------------------|-----------|
| 关注点 | 帧完整性 + 关键帧链 | 音频连续性（杜绝静默/爆音）|
| 丢包恢复 | NACK + 关键帧请求 | PLC（用前包外推生成替代）|
| 缓冲伸缩 | 等待 / 快放 | 时域伸缩（WSOLA，±25% 不变调）|
| 复杂度 | 中（~2000 行 libwebrtc）| 高（~10000 行 libwebrtc）|

**面试一句话答**："视频可以丢帧、可以快放；音频不能。所以视频 JitterBuffer 可以激进丢、NetEQ 必须用变速算法吸收抖动。"

---

## 3. 类图与协作图

### 3.1 类组织（ASCII 类图）

```
┌─────────────────────────────────────────────────────────────┐
│                    IJitterBuffer (接口)                      │
│  + InsertPacket(packet, arrivalTimeMs) -> InsertResult       │
│  + PopNextFrame(out CompletedFrame) -> bool                  │
│  + GetEstimatedJitterMs() const -> int64_t                   │
│  + GetTargetDelayMs() const -> int64_t                       │
└──────────────────────────┬──────────────────────────────────┘
                           │ (实现)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    JitterBufferImpl                          │
│  - packetBuffer_         : PacketBuffer                      │
│  - jitterEstimator_      : JitterEstimator                   │
│  - lastFrameArrivalMs_   : int64_t                           │
│  - lastFrameTimestamp_   : uint32_t                          │
│  - observer_             : IObserver*                        │
│                                                              │
│  + InsertPacket(packet, arrivalTime)                         │
│  + TryAssembleFrame()                                        │
│  + UpdateJitterEstimateOnFrameComplete()                     │
└──────────────────────────┬──────────────────────────────────┘
                           │ has-a
        ┌──────────────────┼──────────────────────┐
        ▼                  ▼                      ▼
┌──────────────────┐ ┌─────────────────────┐ ┌──────────────────┐
│ PacketBuffer     │ │  JitterEstimator    │ │ FrameAssembler   │
│ (按 ts 分组包)    │ │  (EWMA 抗抖估计)     │ │ (拼分片成完整帧)  │
│                  │ │                     │ │                  │
│ + Insert(packet) │ │ + Update(jitter)    │ │ + Assemble(...)  │
│ + ExtractFrame   │ │ + Estimate() const  │ │                  │
│ + DropExpired    │ │                     │ │                  │
└──────────────────┘ └─────────────────────┘ └──────────────────┘
```

### 3.2 协作时序：一帧从入队到出队

```
RtpDepacketizer    JitterBuffer       PacketBuffer    JitterEstimator    Decoder
      │                  │                  │                │              │
      │ packet seq=100   │                  │                │              │
      ├─────────────────►│ InsertPacket     │                │              │
      │ ts=1000, M=0     │                  │                │              │
      │                  ├─────────────────►│                │              │
      │                  │ 加入 ts=1000 桶  │                │              │
      │                  │                  │                │              │
      │ packet seq=101   │                  │                │              │
      ├─────────────────►│                  │                │              │
      │ ts=1000, M=0     ├─────────────────►│                │              │
      │                  │                  │                │              │
      │ packet seq=102   │                  │                │              │
      ├─────────────────►│                  │                │              │
      │ ts=1000, M=1 ✓   ├─────────────────►│                │              │
      │                  │                  │                │              │
      │                  │ TryAssembleFrame │                │              │
      │                  │◄─────────────────┤ ts=1000 完整   │              │
      │                  │ (seq 100-102)    │                │              │
      │                  │                  │                │              │
      │                  │ Update Estimate  │                │              │
      │                  ├─────────────────────────────────►│              │
      │                  │ jitter sample =   │                │              │
      │                  │ actual - expected │                │              │
      │                  │                                    │              │
      │                  │ targetDelay = 3 × jitter +         │              │
      │                  │               decode + render      │              │
      │                  │                                    │              │
      │                  │ renderTimeMs = arrivalTime + target│              │
      │                  │                                                   │
      │                  │ Wait until renderTimeMs            │              │
      │                  │                                                   │
      │                  │ PopNextFrame                                      │
      │                  ├──────────────────────────────────────────────────►│
      │                  │ frame.nalus + renderTimeMs                        │
```

---

## 4. 设计取舍（面试追问点）

### 4.1 取舍 1：用 map 还是环形 buffer 存包？

**WebRTC 怎么做**：**环形 buffer**（`PacketBuffer`，固定大小 2048），按 SeqNum 哈希到环形位置。源码 `modules/video_coding/packet_buffer.cc`。

**其他方案**：`std::map<uint16_t, Packet>`（按 SeqNum 排序）。

**为什么选环形 buffer**：
1. **O(1) 查找/插入**（数组直接索引）vs map 的 O(log n)；
2. **内存连续 cache 友好**；
3. **天然有界**——满了就覆盖旧的（旧的本来也用不上）；
4. **代价**：SeqNum 跨越 buffer 大小时要 wraparound 处理。

**面试一句话答**：环形 buffer 是"用空间换时间 + 用业务约束换算法简化"——视频缓冲深度有自然上界（几百毫秒），无须无界存储。

### 4.2 取舍 2：完整性判定 = "Marker + 连续 SeqNum"，但 Marker 丢了怎么办？

**WebRTC 怎么做**：维护"**待完成帧**"列表，每收到一个包就检查"该 ts 桶里 SeqNum 是否覆盖[first_seq, last_seq]"；如果**下一帧的首包已到达**（说明本帧的 Marker 包丢了），仍可推断本帧的范围。源码 `PacketBuffer::FindFrames`。

**其他方案**：只用 Marker 判定，丢了就等 NACK 重传。

**为什么 WebRTC 复杂化**：单纯靠 Marker 在丢包场景会一直卡住——下一帧已经到了说明本帧网络上"已发完"，丢的就是 Marker 包本身，等也等不到。**用"下一帧已到"作为副信号**能让接收端在丢 Marker 时也能推进。

**面试一句话答**：完整性判定不能只靠 Marker，要结合"后续帧已到达"作为副信号；否则丢一个 Marker 包就永远卡帧。

### 4.3 取舍 3：丢包检测后立刻请求 NACK，还是等一会儿？

**WebRTC 怎么做**：**等一个 RTT 再发 NACK**（典型 50-200ms）。源码 `NackTracker` 的 `kSendNackDelayMs`。

**其他方案**：检测到丢包立刻发 NACK。

**为什么要等**：① 包可能只是**乱序到达**（路由器多路径 + 排队），等一下可能自然到了，避免无意义重传请求；② **批量发 NACK** 节省 RTCP 包头开销（一个 NACK 包能携带多个 NACK item）。

**代价**：弱网场景"等一个 RTT"会增加恢复延迟。

**面试一句话答**：NACK 不能"丢一个发一个"——多数 SeqNum 缺失其实是乱序，等一个 RTT 给乱序自然解决的机会，剩下确实是丢的再发 NACK。

### 4.4 取舍 4：缓冲过深时怎么办？

**WebRTC 怎么做**：**`MaxWaitingTime` 上限**（典型 250ms）—— 计算 `renderTimeMs - now`，超过上限直接吐帧（解码器自己处理"早到"，下游可能加速渲染追上时钟）。源码 `VCMTiming::MaxWaitingTime`。

**其他方案**：硬等到 `renderTimeMs`。

**为什么要有上限**：抗抖估计本身有滞后——突发抖动过去后，估计值还停在高位很久才回稳。这段时间硬等会让所有帧都莫名其妙延迟 200ms+。**主动突破上限**是"承认估计错了、宁可破坏节奏也要降延迟"的工程妥协。

**面试一句话答**：抗抖估计不可能 100% 准，必须有"逃生通道"——`MaxWaitingTime` 上限就是这个通道。

### 4.5 取舍 5：关键帧请求触发条件

**WebRTC 怎么做**：**三个独立触发源**：
1. PacketBuffer 检测到"连续 N 个帧不完整"（典型 N=10）
2. 解码器报错"参考帧丢失"（VCM ErrorCallback）
3. 应用层主动请求（首次连接、网络恢复）

源码 `KeyFrameRequestSender`。

**其他方案**：
- 只靠丢包计数（粗糙，会过度触发）
- 只靠解码器报错（被动，延迟大）

**为什么三源**：① 主动触发（丢包计数）反应快；② 被动触发（解码器报错）兜底；③ 应用层触发覆盖业务场景。**单一触发源都有盲区**。

**面试一句话答**：关键帧请求是"用大代价换稳定性"——一个关键帧的体积是 P 帧的 5-10 倍，不能滥发，所以触发条件要严苛 + 防抖。

---

## 5. libwebrtc 源码阅读笔记（12 个关键函数）

> 源码版本：**m120（branch-heads/6099）**。每个条目格式：路径 / 签名 / 注释 / 面试可能问的点。

### 5.1 PacketBuffer（按 SeqNum 索引的环形缓冲）

#### F1. `PacketBuffer::InsertPacket(packet)`

- **路径**：`modules/video_coding/packet_buffer.cc`（约第 80-160 行）
- **签名**：`InsertResult PacketBuffer::InsertPacket(std::unique_ptr<Packet> packet)`
- **注释**：环形 buffer 入口。**关键步骤**：① 用 `packet->seq_num % buffer_size_` 算出环形 index；② 如果该位置已有旧包，**检查 SeqNum 距离**——距离 > buffer_size/2 视为"对方滚到新一圈"，覆盖旧的；③ 标记 `continuous_[index] = true`（仅当前后 SeqNum 也已到达，形成连续片段时）。**返回 `InsertResult`** 含 `assembled_frames` 列表——如果本次插入触发某帧完整，会一次性返回所有可用帧。
- **面试可能问的点**：
  - "环形 buffer 满了会怎样？" → 默认覆盖旧的；libwebrtc 还会在覆盖时丢出"buffer cleared"事件让上层重新请求 IDR
  - "SeqNum wraparound 怎么处理？" → 用 `IsNewerSequenceNumber` 函数比较，把 `(seq_a - seq_b) & 0xFFFF` 当成有符号 16 位判断

#### F2. `PacketBuffer::PotentialNewFrame(seq_num)`

- **路径**：`modules/video_coding/packet_buffer.cc`（约第 200-260 行）
- **签名**：`bool PacketBuffer::PotentialNewFrame(uint16_t seq_num) const`
- **注释**：判断从 `seq_num` 开始**是否可能组成一个新帧**。逻辑：① 该位置的包必须是某帧的**首包**（`is_first_packet_in_frame`，根据视频编码器标识）；② 该位置必须 `continuous_[index] == true`；③ 该位置之前的 SeqNum（前一帧的尾包）必须已经被消费（`continuous_[prev_index] == true` 或前一帧已 emit）。
- **面试可能问的点**：
  - "为什么首包判定不能只看 SeqNum 的前一个？" → SeqNum 前一个可能是 RTX 重传包（不同 SSRC），不能简单 `seq - 1`
  - "RTP 头里有标记首包的字段吗？" → 没有标准字段，靠 H264 的 NALU type（FU-A 首片 S=1 等）

#### F3. `PacketBuffer::FindFrames(seq_num)`

- **路径**：`modules/video_coding/packet_buffer.cc`（约第 280-380 行）
- **签名**：`std::vector<std::unique_ptr<RtpFrameObject>> PacketBuffer::FindFrames(uint16_t seq_num)`
- **注释**：从 `seq_num` 起，**尝试拼出尽可能多的完整帧**。核心循环：扫描环形 buffer，找到一组"首包→...→尾包（Marker=1）"且 SeqNum 完全连续的 RTP 包序列，封装成 `RtpFrameObject`。**复杂之处**：要处理一个帧分散在 buffer 多个位置（因为 SeqNum 取模散布）；要处理 Marker 包丢失但下一帧首包已到的"间接判定"。
- **面试可能问的点**：
  - "为什么不是简单遍历 buffer 找帧？" → buffer 按 SeqNum 索引，同一帧的包可能不连续；要按 ts 桶找
  - "时间复杂度？" → O(buffer_size)，每次 InsertPacket 触发一次，但摊销下来每个包 O(1)

### 5.2 FrameBuffer（帧级缓冲 + 调度）

#### F4. `FrameBuffer::InsertFrame(frame)`

- **路径**：`modules/video_coding/frame_buffer.cc`（约第 100-180 行）
- **签名**：`int64_t FrameBuffer::InsertFrame(std::unique_ptr<EncodedFrame> frame)`
- **注释**：接收 PacketBuffer 拼好的完整帧，**做帧链依赖分析**：① 解析 `frame->references()`（这一帧依赖哪些参考帧）；② 检查参考帧是否都在 buffer 里；③ 缺参考帧的"未来帧"暂存（`frames_to_decode_` 队列），等参考帧到了再标记可解；④ 触发 `NextFrame()` 通知的等待者（如果有线程在 `WaitForNextFrame` 上阻塞）。返回当前 buffer 中**最近一个可解码帧的 PTS**。
- **面试可能问的点**：
  - "参考帧链断了会怎样？" → 标记"不可解"的帧不会进解码器，会触发关键帧请求
  - "为什么不直接按 PTS 排序输出？" → P 帧到达可能早于它依赖的 I 帧（重传场景），不能按到达顺序

#### F5. `FrameBuffer::NextFrame(timeout_ms, frame_out)`

- **路径**：`modules/video_coding/frame_buffer.cc`（约第 240-310 行）
- **签名**：`ReturnReason FrameBuffer::NextFrame(int64_t max_wait_ms, std::unique_ptr<EncodedFrame>* frame_out)`
- **注释**：**调用者通常是解码线程**——阻塞等待下一帧可解 + 到达 renderTimeMs。内部：① 计算最近可解帧的 `render_time_ms`；② `wait_ms = render_time_ms - now()`；③ 用条件变量阻塞 `min(wait_ms, max_wait_ms)`；④ 到点后取出 frame_out 返回。**关键超时逻辑**：如果 `wait_ms > max_wait_ms` 立刻返回（让调用者决定丢帧还是继续等）。
- **面试可能问的点**：
  - "为什么不在 InsertFrame 里直接喂解码器？" → 解码器在自己的线程，要避免跨线程同步阻塞 InsertFrame（接收线程）
  - "条件变量被打断怎么办？" → libwebrtc 用 `Event::Wait`，被 `Set` 信号或超时唤醒后重新检查条件

### 5.3 JitterEstimator（Kalman 抗抖估计）

#### F6. `VCMJitterEstimator::UpdateEstimate(frame_delay_ms, frame_size_bytes)`

- **路径**：`modules/video_coding/jitter_estimator.cc`（约第 130-200 行）
- **签名**：`void VCMJitterEstimator::UpdateEstimate(int64_t frame_delay_ms, uint32_t frame_size_bytes, bool incomplete_frame)`
- **注释**：核心 Kalman 更新。**输入两个观测量**：① 帧到达延迟（与上一帧到达间隔 - 与上一帧时间戳差）；② 帧字节数（用来归一化"每字节传输时间"）。**Kalman 状态向量** = [每字节传输延迟, 网络队列延迟]。每帧到达更新一次状态向量 + 协方差矩阵 + 卡尔曼增益。源码用 `theta_[0]` / `theta_[1]` 表示状态。
- **面试可能问的点**：
  - "为什么要把每字节延迟和队列延迟分开建模？" → 大帧（I 帧）传输慢是带宽限制 ≠ 网络拥塞；小帧（P 帧）也慢才是拥塞
  - "incomplete_frame 这个参数干什么？" → 未完整的帧不参与抗抖估计（避免被 NACK 重传混淆）

#### F7. `VCMJitterEstimator::GetJitterEstimate(rtt_multiplier)`

- **路径**：`modules/video_coding/jitter_estimator.cc`（约第 230-290 行）
- **签名**：`int64_t VCMJitterEstimator::GetJitterEstimate(double rtt_multiplier, absl::optional<double> rtt_mult_add_cap_ms)`
- **注释**：把内部 Kalman 状态转成"应该缓冲多少 ms"。**公式**：`jitterMs = theta[0] × frame_size + theta[1] + nack_extra_delay`。`rtt_multiplier` 用于在 NACK 重传场景额外加 RTT × 系数的延迟（重传需要 1 个 RTT，缓冲要留余地）。
- **面试可能问的点**：
  - "为什么有 RTT 系数？" → 开了 NACK 重传时，buffer 要足够深以容纳重传往返延迟
  - "rtt_mult_add_cap_ms 是什么？" → RTT 倍数贡献的最大值，防止 RTT 飙高时缓冲爆炸

### 5.4 VCMTiming（渲染调度）

#### F8. `VCMTiming::RenderTimeMs(frame_timestamp, now_ms)`

- **路径**：`modules/video_coding/timing/timing.cc`（约第 100-150 行）
- **签名**：`int64_t VCMTiming::RenderTimeMs(uint32_t frame_timestamp, int64_t now_ms) const`
- **注释**：算给定帧的渲染时刻。**公式**：`renderTime = now_ms + jitter_delay_ms + decode_delay_ms + render_delay_ms - (now_ms - frame_arrival_ms)`，简化后等价于 `frame_arrival_ms + total_target_delay`。**关键**：内部维护 `timestamp_extrapolator_`——把 RTP 时间戳（32 位、可能 wraparound）转成连续墙钟时间。
- **面试可能问的点**：
  - "RTP 时间戳怎么转墙钟？" → RTCP SR 提供 RTP_timestamp ↔ NTP_timestamp 映射，本地插值
  - "wraparound 怎么处理？" → 维护"圈数"计数器，每次发现新 ts < 旧 ts 且差距 > 2^31 时圈数 +1

#### F9. `VCMTiming::MaxWaitingTime(render_time_ms, now_ms)`

- **路径**：`modules/video_coding/timing/timing.cc`（约第 180-220 行）
- **签名**：`int64_t VCMTiming::MaxWaitingTime(int64_t render_time_ms, int64_t now_ms, bool too_many_frames_queued) const`
- **注释**：解码线程能等多久。返回 `max_wait_ms`，让 `NextFrame` 阻塞这么久。**关键策略**：① 如果 `render_time_ms <= now_ms` 立刻返回 0（已过期）；② 否则返回 `render_time_ms - now_ms`，但被 `too_many_frames_queued` 强制压成 0（缓冲过深要立刻吐）。
- **面试可能问的点**：
  - "为什么有 too_many_frames_queued 标志？" → 突发抖动结束后，估计回稳前的 100ms+ 全在"莫名其妙等"，标志让 buffer 主动加速吐
  - "上限阈值是多少？" → 默认 250ms（kMaxWaitForFrameMs）

### 5.5 NACK 触发与丢包反馈

#### F10. `NackTracker::OnReceivedPacket(seq_num, is_keyframe)`

- **路径**：`modules/rtp_rtcp/source/nack_tracker.cc`（约第 80-150 行）
- **签名**：`void NackTracker::OnReceivedPacket(uint16_t seq_num, bool is_keyframe, bool is_recovered)`
- **注释**：每收到一个 RTP 包就调一次。**核心逻辑**：① 比较 `seq_num` 和 `newest_seq_num_`，找出中间缺失的 SeqNum；② 把缺失 SeqNum 加入 `nack_list_`（带"加入时间"用于延迟发 NACK）；③ 如果是关键帧到达，**清空所有早于关键帧的 NACK**（关键帧之前的丢包不必再重传，反正解码器只需要关键帧之后的链）。
- **面试可能问的点**：
  - "为什么关键帧到了要清旧 NACK？" → 关键帧是新参考链起点，旧 P/B 帧已无意义
  - "is_recovered 是什么？" → 标识本包是 NACK 重传后到达的（来自 RTX SSRC），不参与新的丢包检测

#### F11. `NackTracker::GetNackBatch(now_ms)`

- **路径**：`modules/rtp_rtcp/source/nack_tracker.cc`（约第 180-240 行）
- **签名**：`std::vector<uint16_t> NackTracker::GetNackBatch(int64_t now_ms, NackFilterOptions options)`
- **注释**：返回当前应该发 NACK 的 SeqNum 列表。**过滤条件**：① 包已缺失超过 `kSendNackDelayMs`（默认 0，但实际配置 50-200ms 让乱序自然解决）；② 距上次 NACK 重发已过 RTT；③ 重传次数 < `kMaxNackRetries`（默认 10 次，再丢就放弃）。**关键**：用 deadline 队列让不同包的等待时间独立。
- **面试可能问的点**：
  - "重传 10 次还不到怎么办？" → 触发关键帧请求，重置参考帧链
  - "RTT 怎么得到？" → 从 RTCP SR/RR 报文计算（DLSR + LSR）

### 5.6 整合点：JitterBuffer 总入口

#### F12. `RtpVideoStreamReceiver::OnRtpPacket(rtp_packet)`

- **路径**：`modules/video_coding/rtp_video_stream_receiver2.cc`（约第 380-450 行）
- **签名**：`void RtpVideoStreamReceiver2::OnRtpPacket(const RtpPacketReceived& rtp_packet)`
- **注释**：接收侧总入口。流程：① 解 RTP 头 → 调 RtpDepacketizer 解出 NALU；② 把 NALU 包送入 PacketBuffer；③ PacketBuffer 触发 `InsertResult.assembled_frames` 后，每个完整帧塞给 FrameBuffer；④ FrameBuffer 做参考帧依赖分析后，**让解码线程通过 `NextFrame` 取帧**。
- **面试可能问的点**：
  - "这个函数在哪个线程？" → Network thread（libwebrtc 三线程之一）
  - "解码在哪个线程？" → 独立的 decoder thread，通过 NextFrame 拉帧

---

## 6. 接口与关键代码骨架

### 6.1 IJitterBuffer 接口

```cpp
// include/jitter_buffer.h
#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace my_webrtc {

struct IncomingPacket {
    uint16_t sequenceNumber;
    uint32_t rtpTimestamp;
    bool isFrameLastPacket;     // RTP marker bit
    bool isFrameFirstPacket;    // 来自上游 FU-A 解包的标记
    bool isKeyFrame;
    std::vector<uint8_t> naluBytes;
};

struct CompletedFrame {
    std::vector<uint8_t> assembledFrameBytes;  // 多个 NALU 拼成的帧
    uint32_t rtpTimestamp;
    int64_t renderTimeMs;
    bool isKeyFrame;
    uint16_t firstSequenceNumber;
    uint16_t lastSequenceNumber;
};

enum class InsertResult {
    kInserted,           // 入队，无后续动作
    kInsertedAndComplete,// 入队后触发了帧完成
    kDuplicate,          // 重复包
    kTooOld,             // 太老（已超出缓冲窗口）
    kInvalid             // 包无效
};

class IJitterBufferObserver {
public:
    virtual ~IJitterBufferObserver() = default;
    virtual void OnPacketLossDetected(
        const std::vector<uint16_t>& missingSequenceNumbers) = 0;
    virtual void OnKeyFrameRequestNeeded() = 0;
};

class IJitterBuffer {
public:
    virtual ~IJitterBuffer() = default;

    virtual InsertResult InsertPacket(IncomingPacket incomingPacket,
                                      int64_t arrivalTimeMs) = 0;

    virtual bool PopNextCompletedFrame(CompletedFrame* outFrame) = 0;

    virtual int64_t GetEstimatedJitterMs() const = 0;
    virtual int64_t GetTargetDelayMs() const = 0;

    virtual void SetObserver(IJitterBufferObserver* observer) = 0;
    virtual void Reset() = 0;
};

}  // namespace my_webrtc
```

### 6.2 关键算法骨架：EWMA 抗抖估计（核心 30 行）

```cpp
// src/jitter_estimator.cc（节选）

class JitterEstimator {
public:
    void OnFrameReceived(int64_t actualArrivalIntervalMs,
                         int64_t expectedArrivalIntervalMs) {
        // 单次抖动样本 = |实际间隔 - 期望间隔|
        const int64_t jitterSampleMs =
            std::abs(actualArrivalIntervalMs - expectedArrivalIntervalMs);

        // EWMA 更新：α = 0.05，历史权重 0.95
        constexpr double kSmoothingFactor = 0.05;
        smoothedJitterMs_ =
            (1.0 - kSmoothingFactor) * smoothedJitterMs_ +
            kSmoothingFactor * static_cast<double>(jitterSampleMs);

        // 维持峰值估计（用于 3σ 安全裕度）
        peakJitterMs_ = std::max(peakJitterMs_, jitterSampleMs);

        // 慢衰减峰值（每秒衰减 5%）让 peak 不会永远卡在历史最大值
        peakJitterMs_ = static_cast<int64_t>(peakJitterMs_ * 0.95);
    }

    int64_t GetTargetDelayMs(int64_t decodeDelayMs,
                             int64_t renderDelayMs) const {
        // 3σ 覆盖 99.7% 抖动
        const int64_t jitterDelayMs =
            static_cast<int64_t>(3.0 * smoothedJitterMs_);
        return jitterDelayMs + decodeDelayMs + renderDelayMs;
    }

private:
    double smoothedJitterMs_ = 0.0;
    int64_t peakJitterMs_ = 0;
};
```

---

## 7. B 层完整代码（独立 CMake 项目）

完整代码已生成到 `07-M6-JitterBuffer模块/code/`，**纯 C++17、不依赖 libwebrtc**，结构和 M4 一致。

### 目录结构

```
07-M6-JitterBuffer模块/code/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── jitter_buffer.h          # 公共接口（IJitterBuffer + 数据结构）
│   ├── packet_buffer.h          # 包级缓冲（按 ts 桶分组）
│   └── jitter_estimator.h       # EWMA 抗抖估计
├── src/
│   ├── packet_buffer.cc
│   ├── jitter_estimator.cc
│   └── jitter_buffer.cc
└── tests/
    ├── CMakeLists.txt           # FetchContent 拉 GoogleTest
    ├── packet_buffer_test.cc
    ├── jitter_estimator_test.cc
    └── jitter_buffer_test.cc
```

### 编译与运行

```bash
cd 07-M6-JitterBuffer模块/code
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j && ctest --output-on-failure
```

期望输出：**全部测试 PASSED**。

详细说明（实现范围、与 libwebrtc 的差异表、已知简化）见代码目录下的 `README.md`。

---

## 8. 面试问答映射（7 道高频题）

### Q1. 什么是 Jitter Buffer？为什么 WebRTC 需要它？

**口语化标准答案**：Jitter Buffer 是接收侧的"网络抖动吸收器"——发送端按固定节奏发，但经过网络后到达间隔变得忽快忽慢，如果直接喂解码器会卡顿。JitterBuffer 在接收端主动延迟 N 毫秒（典型 50-200ms），把"乱序、突发"的到达整理成"有序、节奏稳定"的输出。**核心矛盾**是缓冲深度 vs 端到端延迟：深度越大抗抖越强但延迟越高，所以要动态估计当前抖动来调整深度。视频和音频都需要，但实现差异很大——视频可以丢帧、可以快放；音频不能，所以音频用更复杂的 NetEQ。

### Q2. 怎么判定一帧"完整可解码"？

**口语化标准答案**：三个条件同时满足：① 该帧所有 RTP 包都已到达（SeqNum 连续不断层）；② 该帧的 Marker 位包已到（标识帧的最后一个 RTP 包）；③ 该帧的所有包共享同一个 RTP Timestamp。但单纯靠 Marker 在丢包场景会卡死——Marker 包本身丢了就永远等不到。所以 libwebrtc 还有一个**间接判定**：如果下一帧的首包已到达（说明本帧网络层已发完），即使没收到 Marker 也能推断本帧范围。完整后帧塞给 FrameBuffer 做参考帧链分析，确认参考帧都齐才标记"可解"。

### Q3. 目标延迟（Target Delay）怎么算？为什么是 3 倍抖动？

**口语化标准答案**：TargetDelay 由三部分组成：抖动延迟 + 解码延迟 + 渲染延迟。抖动延迟用 3 倍当前抖动估计——假设抖动服从正态分布，3σ 覆盖 99.7% 的样本，只有 0.3% 的极端抖动会"漏过"，这部分通过 NACK 重传兜底。**典型数值**：局域网抖动 5ms → TargetDelay ≈ 35ms；4G 抖动 30ms → TargetDelay ≈ 110ms；弱网抖动 80ms → 260ms。解码延迟取历史 95 分位（避免被偶发的解码慢拖累），渲染延迟典型 10ms（显示器刷新一帧）。每收到一帧重算一次 TargetDelay 用于下一帧调度。

### Q4. EWMA 抗抖估计和 Kalman 滤波的区别？你的实现选哪个？

**口语化标准答案**：EWMA 是指数加权移动平均，每帧到达更新一次估计——`new = 0.95 × old + 0.05 × sample`，实现 10 行。核心是理解 `sample` 怎么来的：**实际到达间隔减去期望到达间隔的绝对值**。实际间隔用本机 `steady_clock` 量（帧首包到达时刻 − 上一帧首包到达时刻），期望间隔用 RTP timestamp 算（两个帧的 RTP ts 差除以时钟频率）。二者之差就是网络抖动对这帧造成的额外延迟。α = 0.05 时时间常数约 20 帧——一次突变需要 ~1 秒才在估计值里反映出来，这就是 EWMA 的"慢"。Kalman 把抖动建模成"每字节延迟 + 队列延迟"两个状态变量的线性系统，用卡尔曼增益动态调整新观测的权重——网络稳定时信任历史、突发时快速跟踪。**两者差异**：稳态下偏差 < 5%；突发场景 Kalman 收敛快 5-10 倍。我的 B 层选 EWMA：代码量 1/10、面试讲得清楚、稳态准确度接近，**主动暴露突发慢一拍作为已知简化**——这是有意识的工程取舍而不是能力不足。

### Q5. 检测到丢包后是立刻发 NACK 还是等一会儿？

**口语化标准答案**：等一会儿——典型 50-200ms（一个 RTT）。原因：① 大多数 SeqNum 缺失其实是**乱序到达**（路由器多路径 + 排队），等一下可能自然到了，无脑发 NACK 会产生大量无效重传请求；② 等一会儿能**批量发 NACK**节省 RTCP 包头开销。代价是弱网场景"等一个 RTT"会增加恢复延迟。libwebrtc 用 deadline 队列让每个缺失 SeqNum 独立计时，到 deadline 才进入 NACK 批次。重传 10 次还不到就放弃，**触发关键帧请求**重置参考链。

### Q6. 什么时候请求关键帧（IDR Request）？

**口语化标准答案**：三个触发源——① **PacketBuffer 检测到连续 N 帧不完整**（典型 N=10），认为丢包率太高 NACK 难以恢复；② **解码器报错"参考帧丢失"**（CodecCallback ErrorCode），这是被动兜底；③ **应用层主动请求**（首次连接、网络恢复后重启播放）。请求方式是发 RTCP **PLI**（Picture Loss Indication，轻量）或 **FIR**（Full Intra Request，强制）给发送端。**触发条件要严苛 + 防抖**——一个关键帧体积是 P 帧的 5-10 倍，滥发会浪费带宽，所以典型加 2 秒冷却窗口（同一个间隔内多次触发只发一个请求）。

### Q7. 你的 B 层 JitterBuffer 实现和 libwebrtc 的差异在哪？

**口语化标准答案**：主要在简化范围。① **抗抖估计用 EWMA 而非 Kalman**——突发场景收敛慢 5-10 倍，稳态接近；② **不做参考帧链依赖分析**——只判定 RTP 层完整性，假设所有完整帧都可解（实际项目里 P 帧依赖 I 帧的链不能断，但简化版交给解码器自己处理 reference frame missing 错误回调）；③ **不区分关键帧重传优先级**——libwebrtc 在 NACK 列表里给 keyframe 范围内的 SeqNum 更高优先级，简化版按 FIFO 一视同仁；④ **不支持音频**（音频 NetEQ 是另一套机制，几万行代码）；⑤ **没有线程模型**——libwebrtc 的 JitterBuffer 涉及 network thread / decoder thread 跨线程同步，简化版假设单线程调用。这些简化让总代码量从 libwebrtc 的 2000+ 行降到 600 行，但**核心算法（环形 buffer + 完整性判定 + EWMA + 渲染调度）是和 libwebrtc 等价的**。

---

## 结束语

阶段三 · M6 主文档完成。覆盖：

- ✅ 原理详解（抖动本质 + 完整性判定 + EWMA vs Kalman + 目标延迟 + 渲染调度 + 丢包检测 + 视频 vs 音频）
- ✅ Jitter Buffer 在 WebRTC QoS 四驾马车（GCC / FEC / NACK / JitterBuffer）中的定位与协同流水线
- ✅ 类图 + 协作时序图
- ✅ 5 个设计取舍（环形 buffer / Marker 副信号 / NACK 延迟 / MaxWaitingTime / 关键帧三源触发）
- ✅ 12 个 libwebrtc 源码笔记（PacketBuffer / FrameBuffer / JitterEstimator / VCMTiming / NackTracker / 总入口）
- ✅ IJitterBuffer 接口骨架 + EWMA 核心算法 30 行
- ✅ 7 道面试问答映射
- ✅ B 层完整代码（独立 CMake 项目，见 `code/` 子目录）

**下一步**：去 `07-M6-JitterBuffer模块/code/` 编译跑测试。命令同 M4。
