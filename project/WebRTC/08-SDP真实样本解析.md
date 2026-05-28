# 读懂一段真实 SDP：从浏览器 step-02 抓出的生产级样本

> 这篇文档是**实战版 SDP 解读**——不是从协议手册抄字段定义，而是从浏览器跑 Google WebRTC codelab step-02 抓出的真实 SDP，**一行一行讲清楚每个字段为什么在这里**。
>
> **用法**：
> - 第一次读：跟着分块讲解理解整体结构
> - 复习时：直接看"面试金句"和"高频追问"段
> - 面试前：用"自检题"测自己能不能口述
>
> **配套**：本文档是 `阶段一` / `M4-RTP` / `M6-JitterBuffer` 学习后的实战验证——你之前在文档里学的概念（RTP / NACK / PLI / SSRC / DTLS 等），这里能在真实 SDP 里**一一指认出来**。

---

## 目录

1. [为什么要读真实 SDP](#1-为什么要读真实-sdp)
2. [真实样本（从 step-02 截取）](#2-真实样本从-step-02-截取)
3. [SDP 整体结构：会话级 + 媒体级](#3-sdp-整体结构会话级--媒体级)
4. [判断首选编码：一个容易踩的坑](#4-判断首选编码一个容易踩的坑)
5. [m=video 行：传输栈一行说清](#5-mvideo-行传输栈一行说清)
6. [rtcp-fb：抗弱网与拥塞控制的协商](#6-rtcp-fb抗弱网与拥塞控制的协商)
7. [Codec 全家福](#7-codec-全家福)
8. [安全凭证：DTLS 指纹 + ICE 凭证](#8-安全凭证dtls-指纹--ice-凭证)
9. [SSRC + RTX 配对（FID 组）](#9-ssrc--rtx-配对fid-组)
10. [RTP 头扩展（extmap）](#10-rtp-头扩展extmap)
11. [Offer vs Answer：方向协商](#11-offer-vs-answer方向协商)
12. [ICE candidate：连通性的入口](#12-ice-candidate连通性的入口)
13. [面试金句（背下来）](#13-面试金句背下来)
14. [5 道高频追问 + 标准答案](#14-5-道高频追问--标准答案)
15. [自检题](#15-自检题)

---

## 1. 为什么要读真实 SDP

之前在《WebRTC 入门导读》和 M4 文档里，你看到的 SDP 都是**简化示例**——只列了关键字段、省去了一堆 `a=...` 行。但真实生产里的 SDP 一段就 100+ 行，**每个字段都不是装饰**。

读懂一段真实 SDP，你能：
- 一眼看出"双方协商出了什么编码、什么拥塞控制机制、什么抗丢包策略"
- 面试时面对"解释一下这段 SDP"这道高频题不慌
- 对接 SFU / TURN / 录制等中间件时知道哪些字段不能乱改

---

## 2. 真实样本（从 step-02 截取）

下面是 Chrome 浏览器跑 [Google WebRTC codelab step-02](https://codelabs.developers.google.com/codelabs/webrtc-web) 时，`localPeerConnection.createOffer()` 生成的真实 offer（已截取，保留所有结构性字段）：

```sdp
v=0
o=- 5003389001937456968 2 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0
a=extmap-allow-mixed
a=msid-semantic: WMS a1226bea-ba30-4ab3-b02c-839df6da408e
m=video 9 UDP/TLS/RTP/SAVPF 96 97 103 104 107 108 109 114 115 116 117 118 39 40 45 46 98 99 100 101 119 120 123 124 125
c=IN IP4 0.0.0.0
a=rtcp:9 IN IP4 0.0.0.0
a=ice-ufrag:bXbl
a=ice-pwd:DNFH1pL2kZxFfghA65p9HPdb
a=ice-options:trickle
a=fingerprint:sha-256 36:E5:B9:3B:95:95:56:ED:...
a=setup:actpass
a=mid:0
a=extmap:1 urn:ietf:params:rtp-hdrext:toffset
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
... (更多 extmap)
a=sendrecv
a=msid:a1226bea-ba30-4ab3-b02c-839df6da408e 67771b25-fbf5-4f89-9885-4387d5223bde
a=rtcp-mux
a=rtcp-rsize
a=rtpmap:96 VP8/90000
a=rtcp-fb:96 goog-remb
a=rtcp-fb:96 transport-cc
a=rtcp-fb:96 ccm fir
a=rtcp-fb:96 nack
a=rtcp-fb:96 nack pli
a=rtpmap:97 rtx/90000
a=fmtp:97 apt=96
a=rtpmap:103 H264/90000
a=rtcp-fb:103 goog-remb
... (相同的 5 个 rtcp-fb)
a=fmtp:103 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
a=rtpmap:104 rtx/90000
a=fmtp:104 apt=103
... (更多 H264/VP9/AV1 变种)
a=rtpmap:123 red/90000
a=rtpmap:124 rtx/90000
a=fmtp:124 apt=123
a=rtpmap:125 ulpfec/90000
a=ssrc-group:FID 3696450200 2399172641
a=ssrc:3696450200 cname:E7AyobTBeSsvrxBq
a=ssrc:3696450200 msid:a1226bea-ba30-4ab3-b02c-839df6da408e 67771b25-fbf5-4f89-9885-4387d5223bde
a=ssrc:2399172641 cname:E7AyobTBeSsvrxBq
a=ssrc:2399172641 msid:a1226bea-ba30-4ab3-b02c-839df6da408e 67771b25-fbf5-4f89-9885-4387d5223bde
```

下面把它分块讲透。

---

## 3. SDP 整体结构：会话级 + 媒体级

```
v=0
o=- ...
s=-
t=0 0
a=group:BUNDLE 0          ← ┐
a=extmap-allow-mixed       ←  │ 【会话级 session-level】
a=msid-semantic: WMS ...   ← ┘   整段会话共享的"信封"
─────────────────────────────────────────────────────
m=video 9 UDP/TLS/RTP/SAVPF 96 97 103 ...  ← ┐
c=IN IP4 0.0.0.0                              │
a=rtcp:9 IN IP4 0.0.0.0                       │
a=ice-ufrag:bXbl                              │ 【媒体级 media-level】
... (一大堆 a= 行)                            │  这一路媒体的所有细节
a=rtpmap:96 VP8/90000                         │  每个 m= 一段
a=ssrc:3696450200 cname:...                ← ┘
```

**关键约定**：
- 会话级行（`v` / `o` / `s` / `t` 及之后到第一个 `m=` 之前的 `a=`）对整段会话生效
- 每个 `m=` 开启一段媒体级，到下一个 `m=` 或文件结束
- 一段会话**通常有多个 `m=` 段**（音频一个、视频一个、DataChannel 一个）——本样本只有一个 `m=video` 因为 step-02 只协商视频

**面试问"SDP 怎么组织的"** → 答这个二分结构。

---

## 4. 判断首选编码：一个容易踩的坑

看 `m=` 行：

```
m=video 9 UDP/TLS/RTP/SAVPF 96 97 103 104 107 ... 119 120 123 124 125
                            └┬┘
                          payload 列表第一个 = 首选
```

`96` 对应 `a=rtpmap:96 VP8/90000` → **浏览器首选 VP8**。

### ⚠️ 容易踩的坑

只看局部 `fmtp` 行（比如只看到 `packetization-mode=1;profile-level-id=64001f` 这种 H264 特征）就下结论"协商的是 H264" —— **这是错的**。

**真相**：浏览器 offer 里列出**全部支持的编码**（这段样本里有 VP8 / H264（多个 profile 变种）/ VP9 / AV1，共 7-8 种），按**优先级排序**。**只有 `m=` 行第一个 payload 才是首选**。

H264 在这段 SDP 里排在 96(VP8) / 97(VP8-rtx) 之后才出现，所以**首选是 VP8，不是 H264**。

### 怎么正确判断首选编码

```
1. 找 m= 行
2. 取 payload 列表第一个数字（例如 96）
3. 找 a=rtpmap:96 ...  → 这就是首选编码
```

---

## 5. m=video 行：传输栈一行说清

```
m=video 9 UDP/TLS/RTP/SAVPF 96 97 ...
        │ └──────┬───────┘ └── payload type 优先级列表
        │   传输协议组合
        端口（9 = 占位符，真实端口在 candidate 里）
```

**`UDP/TLS/RTP/SAVPF` 是 WebRTC 媒体面的完整传输栈**：

| 层 | 角色 |
|----|------|
| **UDP** | 底层不可靠传输（《入门导读》解释过为什么用 UDP 不用 TCP）|
| **TLS** | 即 **DTLS**——基于 UDP 的 TLS，协商加密密钥 |
| **RTP** | 实时传输协议（你 M4 模块做的）|
| **SAVPF** | Secure A/V Profile with Feedback。**S=Secure=SRTP 加密**，**F=Feedback=支持 RTCP 反馈** |

**面试金句**：这一行就是把"UDP + DTLS + SRTP + RTCP 反馈"四层栈拍扁成一行。

### 端口为什么是 9？

`9` 是 IANA 保留的"discard"端口，这里是**占位符**——真实端口由后续 ICE candidate 决定（每个 candidate 自带端口）。

---

## 6. rtcp-fb：抗弱网与拥塞控制的协商

每个编码下面都跟着 **5 行 `a=rtcp-fb`**，以 VP8（96）为例：

```
a=rtcp-fb:96 nack          ← NACK 重传
a=rtcp-fb:96 nack pli      ← PLI (Picture Loss Indication) 关键帧请求
a=rtcp-fb:96 ccm fir       ← FIR (Full Intra Request) 全帧请求
a=rtcp-fb:96 transport-cc  ← 传输级拥塞控制
a=rtcp-fb:96 goog-remb     ← REMB 带宽估计（GCC 早期机制）
```

### 这 5 行覆盖了你学过的所有抗弱网/拥塞机制

| 字段 | 对应你学过的概念 | 文档位置 |
|------|---------------|--------|
| `nack` | NACK 选择性重传 | M4 / M6 / 入门导读 |
| `nack pli` | 关键帧请求 | M6 `OnKeyFrameRequestNeeded` |
| `ccm fir` | 关键帧请求（另一种） | M6 文档"关键帧请求"段 |
| `transport-cc` | 传输级拥塞控制 | 入门导读 GCC/BBR 段 |
| `goog-remb` | REMB 带宽估计 | 入门导读 GCC 段 |

**双方在协商**："我支持 NACK 重传 / PLI+FIR 关键帧请求 / transport-cc 拥塞反馈"——你 M6 代码里 `consecutiveLossEvents >= 10 → OnKeyFrameRequestNeeded()` 触发后，发的就是 PLI 报文。

### PLI 和 FIR 的区别（面试常问）

- **PLI**：轻量请求"对方发个关键帧"，发送端可以自行选择是否生成
- **FIR**：强制对方"必须立刻生成关键帧"（命令式）
- 实践中 PLI 用得更多（轻、不强制对端打断 GoP）

---

## 7. Codec 全家福

样本里 `a=rtpmap` 列出了浏览器支持的所有编码：

| payload | 编码 | 备注 |
|---------|------|------|
| 96 | VP8 | **首选** |
| 97 | rtx | VP8 的重传流（`apt=96`）|
| 103 / 107 / 109 / 115 / 117 / 119 / 39 | H264 | 多个 profile 变种（baseline / constrained baseline / main / high）|
| 104 / 108 / 114 / 116 / 118 / 120 / 40 | rtx | 对应 H264 各 profile 的重传流 |
| 45 | AV1 | |
| 46 | rtx | AV1 重传 |
| 98 / 100 | VP9 | profile-id=0 和 2 |
| 99 / 101 | rtx | VP9 重传 |
| 123 | red | RED 冗余编码（FEC 载体）|
| 124 | rtx | RED 重传 |
| 125 | ulpfec | **ULP FEC 前向纠错** |

### 几个关键观察

1. **H264 出现 7 次**：每个 profile（baseline 42001f / 42e01f / 4d001f / 64001f）+ 每个 packetization-mode（0 或 1）的组合都列出来了——给对端最多的选择
2. **每个编码都有一个 rtx 配对**：通过 `a=fmtp:N apt=M` 表达"payload N 是 payload M 的重传流"
3. **red + ulpfec = FEC 前向纠错**：你导读学的"额外发冗余数据，丢包能直接恢复"，在 SDP 里就是这两行

### profile-level-id 怎么读

`a=fmtp:103 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f`

- `profile-level-id=42001f`：3 字节十六进制
  - `42` = profile_idc（Baseline）
  - `00` = profile_iop（兼容标志位）
  - `1f` = level_idc（3.1）
- `packetization-mode=1`：**支持 FU-A 分片**（你 M4 代码里的 `kNonInterleaved`）
- `level-asymmetry-allowed=1`：双方 level 可以不一样

常见 profile 取值：
- `42` Baseline（兼容性最好，老设备）
- `4d` Main
- `64` High（画质好，CPU 重）

---

## 8. 安全凭证：DTLS 指纹 + ICE 凭证

```
a=ice-ufrag:bXbl                 ← ICE 用户名（连通性检查鉴权）
a=ice-pwd:DNFH1pL2kZxFfghA65p9HPdb   ← ICE 密码
a=ice-options:trickle            ← 支持 trickle ICE（candidate 一边收集一边发）
a=fingerprint:sha-256 36:E5:B9:...   ← DTLS 证书指纹
a=setup:actpass                  ← DTLS 角色协商
```

### a=fingerprint 是干什么的

DTLS 握手会交换证书。**接收端怎么知道证书没被中间人替换**？答案：**通过 SDP 提前告知的指纹**。

流程：
1. A 在 SDP 里告诉 B："我的证书指纹是 36:E5:..."
2. SDP 通过**业务信令通道**（WebSocket / HTTPS，本身已加密）传给 B
3. DTLS 握手时 B 收到 A 的真实证书，**算指纹和 SDP 里的对比**——一致才信任

这样**端到端加密信任的根基是信令通道的可信度**——所以 WebRTC 强烈建议信令走 HTTPS / WSS。

### a=setup 的 actpass / active / passive

DTLS 是 client-server 模型，但 WebRTC 两端是对等的——谁当 client？靠 `setup` 协商：

| offer 端 | answer 端 | 含义 |
|---------|---------|------|
| `actpass` | `active` | offer 端无所谓，answer 端主动当 client 发起 DTLS 握手 |
| `actpass` | `passive` | offer 端当 client（少见）|
| `active` | `passive` | 显式指定 |

**实践中几乎都是 `offer:actpass / answer:active`**——这意味着真正发起 DTLS 握手的是 **answer 端**。

---

## 9. SSRC + RTX 配对（FID 组）

```
a=ssrc-group:FID 3696450200 2399172641
a=ssrc:3696450200 cname:E7AyobTBeSsvrxBq
a=ssrc:3696450200 msid:... ...
a=ssrc:2399172641 cname:E7AyobTBeSsvrxBq
a=ssrc:2399172641 msid:... ...
```

### 解读

- **`a=ssrc`** 声明这一路媒体用到的 SSRC（流标识，你 M4 学的 32 位字段）
- 这里出现 **两个 SSRC**——为什么？
  - `3696450200` = 主视频流（VP8 / H264 等数据）
  - `2399172641` = **RTX 重传流**（NACK 触发的重传走这条独立 SSRC）
- **`a=ssrc-group:FID 3696450200 2399172641`** = 把这两个 SSRC 标记为"主流 + 重传流"配对（FID = Flow ID）

### 这里印证了你 M4 学的什么

你 M4 源码笔记 F11 学的："NACK 重传不能用原 SSRC，要用独立 RTX SSRC 走另一条 RTP 流（RFC 4588）"——**真实 SDP 里就长这样**：

- 主流 SSRC 跑 VP8 / H264 数据，payload type = 96 / 103 / ...
- RTX 流 SSRC 跑重传数据，payload type = 97 / 104 / ...（apt 指向对应主 payload）

### cname 是什么

`cname:E7AyobTBeSsvrxBq` = **Canonical Name**，标识"哪几个 SSRC 属于同一个用户/媒体源"。多路流（视频 + 音频 + 屏幕分享）的 SSRC 不一样，但 cname 相同，接收端用 cname 把它们归属到同一发送者。

---

## 10. RTP 头扩展（extmap）

```
a=extmap:1 urn:ietf:params:rtp-hdrext:toffset
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay
a=extmap:6 http://www.webrtc.org/experiments/rtp-hdrext/video-content-type
a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid
...
```

### 这是什么

RTP 头 12 字节装不下所有信息，需要**扩展头**（RFC 8285）。`a=extmap:N URI` 把扩展类型映射到一个**短 ID（1-14）**，省得每个 RTP 包都写一长串 URI。

### 几个关键扩展

| 扩展名 | 作用 | 谁用 |
|--------|------|------|
| `abs-send-time` | 发送端绝对时间戳 | GCC 拥塞控制估带宽 |
| `transport-wide-cc` | 传输级 transport-cc 序号 | transport-cc 拥塞控制（你导读学的）|
| `toffset` | 时间戳偏移 | 抗抖估计 |
| `playout-delay` | 期望渲染延迟 | 端到端延迟控制 |
| `sdes:mid` | media-id | BUNDLE 分流 |

### 印证你 M4 学的什么

M4 文档里提过："扩展头里有 abs-send-time、transport-cc seq"——**真实 SDP 里就是这几行 `extmap`** 在声明它们。

---

## 11. Offer vs Answer：方向协商

样本里 offer 是 `a=sendrecv`，answer 是 `a=recvonly`。

### 为什么 answer 是 recvonly

step-02 的 JS 代码：
```js
localPeerConnection.addStream(localStream);
// remotePeerConnection 没有 addStream!
```

只有 local（offer 方）加了摄像头流，remote（answer 方）没加。所以协商结果是：

| 端 | 方向 | 含义 |
|----|------|------|
| Offer (local) | `sendrecv` | 我能发流、也愿意收 |
| Answer (remote) | `recvonly` | 我只收，没流可发 |

**最终数据单向**：local → remote（你看到右边画面，但右边画面没回传到左边）。

### 4 种方向值

| 值 | 含义 |
|----|------|
| `sendrecv` | 双向收发 |
| `sendonly` | 只发不收 |
| `recvonly` | 只收不发 |
| `inactive` | 暂停（保留协商但不流）|

---

## 12. ICE candidate：连通性的入口

日志末尾出现的 candidate 行：

```
candidate:3052707706 1 udp 2122260223 10.0.106.42 63194 typ host generation 0 ufrag bXbl network-id 1 network-cost 10
candidate:3714139205 1 udp 2122194687 10.251.1.1  63780 typ host generation 0 ufrag bXbl network-id 2 network-cost 50
```

### 字段拆解

```
candidate:<foundation> <component> <protocol> <priority> <IP> <port> typ <type> ...
                                              └────┬────┘
                                              优先级（越大越优先）
```

- `typ host` = host candidate（本机直接地址，你导读学的三种之一）
- `network-id 1` / `2` = 本机有两块网卡（两个 IP）
- `network-cost 10` vs `50` = **cost 低的优先**——10 那块是主网卡

### 三种 candidate 类型

| typ | 含义 | 何时用 |
|-----|------|------|
| `host` | 本机直接地址 | 局域网直连 |
| `srflx` | STUN 反射地址（公网映射）| NAT 穿透 |
| `relay` | TURN 中继地址 | 实在打不通时兜底 |

step-02 因为两个 PeerConnection 在同一台机器，host candidate 直接通——**根本没用上 STUN/TURN**。

---

## 13. 面试金句（背下来）

> "SDP 分**会话级**和**媒体级**两层。`m=` 行的传输是 `UDP/TLS/RTP/SAVPF`——UDP 上跑 DTLS 加密的 RTP，SAVPF 表示支持 RTCP 反馈。**payload 列表第一个是首选编码**（不能只看局部 fmtp 行下结论）。
>
> 每个编码下的 **`rtcp-fb`** 声明支持的反馈机制：`nack` 重传 / `nack pli` 和 `ccm fir` 关键帧请求 / `transport-cc` 拥塞控制 / `goog-remb` 带宽估计。
>
> **`a=fingerprint`** 是 DTLS 证书指纹（防中间人替换证书），**`a=ice-ufrag/pwd`** 是 ICE 连通性检查的鉴权凭证。**`a=ssrc-group:FID`** 把主流和 RTX 重传流配对——印证了 NACK 重传走独立 SSRC 的设计。
>
> Offer 通常 `sendrecv`，Answer 视实际方向可能是 `recvonly`。ICE candidate 有 `host / srflx / relay` 三种，按 priority 和 network-cost 选最优路径。"

---

## 14. 5 道高频追问 + 标准答案

### Q1. 为什么需要 `a=rtcp-mux`？

**答**：把 RTP 和 RTCP 复用一个 UDP 端口（不开两个端口）。优点：① 减少 ICE 打洞开销（只打一个洞）；② 简化 NAT 穿透（一个映射搞定）；③ 现代 WebRTC 标准强制开启。早期 RTP 协议规定 RTCP 用 RTP 端口+1，但 WebRTC 把它们 mux 到同一端口。

### Q2. `a=group:BUNDLE 0` 是干什么的？

**答**：BUNDLE 让多个媒体流（音频 + 视频 + DataChannel）**复用同一个 ICE/DTLS 传输通道**。优点：① 只打一次 NAT 洞；② 只做一次 DTLS 握手；③ 节省端口和带宽。本样本只有一个 `m=video`，BUNDLE 只列了 mid 0；如果有音频会变成 `BUNDLE 0 1`。**没有 BUNDLE 的话每路媒体要走独立 ICE/DTLS**，开销爆炸。

### Q3. `setup:actpass` 和 `setup:active` 的差异？谁先发 DTLS ClientHello？

**答**：DTLS 是 client-server 模型，必须明确角色。Offer 端通常给 `actpass` 表示"我都行"；Answer 端必须给 `active` 或 `passive` 明确角色——**`active` 的一端主动发 ClientHello**。WebRTC 实践中几乎都是 `offer:actpass / answer:active`，所以**真正发起 DTLS 握手的是 answer 端**。

### Q4. 一个 NALU 被分成多个 RTP 包传输（FU-A），接收端怎么从 SDP 知道发送端支持 FU-A？

**答**：看 H264 的 `a=fmtp:N` 里的 **`packetization-mode`** 字段：
- `packetization-mode=0` → 只支持 Single NALU 模式（小 NALU）
- `packetization-mode=1` → 支持 Single NALU / STAP-A / FU-A 三种（**你 M4 代码里写的 `kNonInterleaved`**）

样本里 H264 各 profile 同时列出了 mode=0 和 mode=1 两个 payload，让对端按需选择。

### Q5. 接收端怎么从 SDP 知道哪个 SSRC 是重传流？

**答**：通过 **`a=ssrc-group:FID <主SSRC> <重传SSRC>`** 显式声明。两个 SSRC 同 cname 但角色不同，主流的 payload 是 96/103/...，重传流的 payload 是 97/104/...，重传 payload 通过 `a=fmtp:N apt=M` 指向对应主 payload。**接收端解 NACK 重传包时**：① 看到 SSRC 是 RTX SSRC；② 从 SDP 知道它对应哪条主流；③ 还原后塞回主流的 Jitter Buffer。

---

## 15. 自检题

读完本文档，闭着文档**口述回答**下面问题。能流畅答出来 = 你真懂了。

1. SDP 分哪两层结构？分界在哪？
2. 怎么从 SDP 判断浏览器首选的编码？为什么不能只看 `fmtp` 行下结论？
3. `m=video` 行 `UDP/TLS/RTP/SAVPF` 拆开是哪 4 层？SAVPF 里 S 和 F 各代表什么？
4. `rtcp-fb` 行里你能认出几种反馈机制？哪几个是你 M4/M6 代码里实现/触发过的？
5. `fingerprint` 是什么？为什么必须在 SDP 里提前给（不能 DTLS 握手时再给）？
6. `setup:actpass` 和 `setup:active` 的关系是什么？真实场景谁主动发起 DTLS 握手？
7. `a=ssrc-group:FID` 把两个 SSRC 配成什么对？为什么需要？
8. ICE candidate 三种类型 `host / srflx / relay` 分别对应什么网络场景？
9. Offer 一般是 `sendrecv`，什么场景下 Answer 会是 `recvonly`？
10. `packetization-mode=1` 在 SDP 里出现，意味着发送方支持哪种 H264 RTP 打包模式？这和你 M4 代码里的哪个枚举值对应？

---

## 结束语

读懂一段真实 SDP，**不只是面试加分**——而是你**真正理解了 WebRTC 控制面在做什么**。

之前你在 M4 / M6 学的是数据面（RTP 怎么打、Jitter Buffer 怎么抗抖），那是"协商完成之后"的事。SDP 是"协商本身"——双方在通话开始前**用一段文本相互声明能力、对齐参数**。

这段 SDP 你能逐字段读懂，就意味着你已经**真正跨越了控制面**——而控制面是 75% 的初学者卡住的地方。

下次面试官说"解释一下这段 SDP"，你可以淡定地从 `v=0` 一路讲到 `a=ssrc-group:FID`。
