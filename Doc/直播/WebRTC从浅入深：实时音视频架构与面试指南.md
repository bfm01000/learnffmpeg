# WebRTC 从浅入深：实时音视频架构与面试指南

## 0. 本篇定位

- 面试复习：先掌握 WebRTC 和 RTMP/HLS/HTTP-FLV 的本质区别，能说清 SDP、ICE、STUN/TURN、DTLS-SRTP、RTP/RTCP 和 SFU。
- 深入学习：重点看 JitterBuffer、NACK、FEC、PLI/FIR、GCC、Simulcast/SVC、getStats 和弱网定位。
- 职责边界：这篇是知识体系入口；可运行项目和开发记录回到 `../../project/14_WebRTC_Interview_Project/`。
这篇文档面向 C++ 高级音视频开发面试和工程理解，不把 WebRTC 当成一个 API 来背，而是把它当成一套完整的实时音视频系统来拆。

你可以先记住一句话：

> WebRTC 不是一个单独协议，而是一整套实时音视频通信技术栈：它用信令交换 SDP 和网络候选地址，用 ICE/STUN/TURN 打通网络，用 DTLS-SRTP 加密媒体，用 RTP/RTCP 传输和反馈音视频，并在上层实现 jitter buffer、NACK、FEC、拥塞控制、音视频同步和设备采集渲染。

如果 RTMP 是“主播把一路流推到服务器，再由 CDN 分发”，WebRTC 更像是“两个端点之间建立一条尽可能低延迟、可自适应弱网、默认加密的实时媒体通道”。

---

## 1. 为什么现在学 WebRTC 收益更高

RTMP 的核心价值在传统直播推流，优点是生态成熟、CDN 支持好、接入简单。但它基于 TCP，弱网时会有重传和队头阻塞，很难稳定做到百毫秒级互动。

WebRTC 的价值在实时互动：

- 视频会议
- 语音通话
- 连麦直播
- 在线教育
- 云游戏
- 远程控制
- 低延迟直播
- 实时协同

它解决的问题不是“怎么把视频文件切片发给观众”，而是：

```text
如何在复杂网络环境下，让两端尽快连上，
并以尽可能低的延迟传输音视频，
同时还能抗丢包、抗抖动、动态适配带宽、保证安全。
```

面试里可以这样对比：

> RTMP 更偏传统直播推流，主要解决主播到服务器的流式上传；WebRTC 面向实时互动通信，目标是端到端低延迟。RTMP 基于 TCP，可靠但弱网下容易队头阻塞；WebRTC 多数媒体走 UDP，在应用层通过 RTP/RTCP、NACK、FEC、JitterBuffer、拥塞控制和带宽估计来做实时性和质量之间的平衡。

---

## 2. WebRTC 的全局地图

WebRTC 很容易学散，因为里面概念太多。先用一张图建立全局：

```text
业务层
  登录 / 房间 / 用户 / 权限 / 信令服务器
      |
      v
信令层，WebRTC 不规定具体协议
  WebSocket / HTTP / MQTT / 自定义 TCP
  交换 SDP offer/answer 和 ICE candidate
      |
      v
连接建立层
  ICE = STUN + TURN + candidate 选择
  找到一条能互通的网络路径
      |
      v
安全传输层
  DTLS 握手
  导出 SRTP 密钥
      |
      v
媒体传输层
  RTP 传音视频
  RTCP 做反馈、统计、同步、拥塞控制
      |
      v
媒体处理层
  采集 / AEC / NS / AGC / 编码 / JitterBuffer / 解码 / 渲染
```

一句话拆分：

- **信令**：负责让双方交换“我是谁、我支持什么、我有哪些网络地址”。
- **ICE**：负责在复杂 NAT/防火墙后面找到能连通的路径。
- **DTLS-SRTP**：负责加密和身份校验。
- **RTP/RTCP**：负责实时媒体传输和反馈。
- **音视频引擎**：负责采集、处理、编码、抗抖动、解码、渲染。

---

## 3. WebRTC 不等于浏览器 API

很多人以为 WebRTC 是浏览器里的 JavaScript API，比如：

```js
getUserMedia()
RTCPeerConnection
RTCDataChannel
```

这只是 WebRTC 在浏览器里的暴露方式。对 C++ 音视频工程师来说，更重要的是底层能力：

```text
libwebrtc
  PeerConnection
  AudioDeviceModule
  AudioProcessing
  VideoCapture
  VideoEncoder / VideoDecoder
  RTP/RTCP module
  CongestionController
  JitterBuffer
  Pacer
  NetworkController
```

你在移动端或桌面端做 RTC SDK，往往不是简单写 JS，而是要处理：

- 摄像头和麦克风采集
- 硬编硬解接入
- OpenGL/Metal/Surface 渲染
- 音频 AEC/NS/AGC
- 网络线程和媒体线程调度
- C++ 到 Java/Objective-C/Swift 的跨语言封装
- 弱网调参、日志、统计、质量监控

---

## 4. WebRTC 和 RTMP/HLS/HTTP-FLV 的本质区别

```text
RTMP
  TCP 长连接，常用于推流。
  可靠有序，但弱网可能延迟堆积。

HTTP-FLV
  HTTP 长连接传 FLV Tag，常用于低延迟播放。
  比 HLS 低延迟，但仍基于 TCP。

HLS
  HTTP 分片，兼容性最好，延迟通常更高。
  适合大规模分发和点播式观看。

WebRTC
  多数媒体走 UDP + SRTP。
  端到端加密，内建 NAT 穿透、拥塞控制、丢包恢复和实时反馈。
  适合互动和百毫秒级低延迟。
```

关键差异：

```text
传统直播协议的核心问题：
  如何稳定地把一路流分发给很多观众。

WebRTC 的核心问题：
  如何在不可控网络里，低延迟地双向传输实时媒体。
```

所以 WebRTC 的复杂度不在“封装格式”，而在：

- 如何建连
- 如何穿透 NAT
- 如何估计带宽
- 如何处理丢包、乱序、抖动
- 如何动态调码率、分辨率、帧率
- 如何保证端到端安全
- 如何扩展到多人房间

---

## 5. WebRTC 建连流程总览

一次典型 WebRTC 通话建连大致是：

```text
Alice 创建 PeerConnection
Bob 创建 PeerConnection
      |
      v
Alice createOffer，生成 SDP offer
      |
      v
Alice 通过信令服务器发给 Bob
      |
      v
Bob setRemoteDescription(offer)
Bob createAnswer，生成 SDP answer
      |
      v
Bob 通过信令服务器回给 Alice
      |
      v
双方 setLocalDescription / setRemoteDescription
      |
      v
双方收集 ICE candidate，并通过信令互相交换
      |
      v
ICE connectivity checks，选择最佳 candidate pair
      |
      v
DTLS 握手，协商 SRTP 密钥
      |
      v
开始发送 RTP/RTCP 音视频
```

可以压缩成 4 个阶段：

```text
1. SDP 协商：谈能力
2. ICE 连接：找路径
3. DTLS 握手：建安全通道
4. RTP/RTCP：传媒体和反馈
```

---

## 6. 信令：WebRTC 标准故意不管的部分

WebRTC 标准不规定信令协议。也就是说，下面这些都可以做信令：

- WebSocket
- HTTP
- MQTT
- gRPC
- 自定义 TCP
- 业务已有 IM 通道

信令主要传两类东西：

```text
SDP:
  描述媒体能力、编解码器、方向、加密指纹等。

ICE candidate:
  描述本端可用于连接的网络地址。
```

为什么 WebRTC 不规定信令？

因为信令强依赖业务：

- 房间系统怎么设计
- 用户怎么鉴权
- 一对一还是多人房间
- 是否支持旁路直播
- 是否支持录制
- 是否支持踢人、禁言、权限
- 是否要和 IM 系统打通

面试回答：

> WebRTC 标准不包含信令协议，只要求双方能交换 SDP 和 ICE candidate。实际项目里可以用 WebSocket、HTTP 或业务自己的长连接做信令。SDP 负责媒体能力协商，candidate 负责网络路径发现，房间、鉴权、用户状态这些都属于业务信令。

---

## 7. SDP：双方谈“我能收发什么”

SDP 可以理解成一份媒体能力描述：

```text
我支持哪些音频 codec？
我支持哪些视频 codec？
我想 sendonly、recvonly 还是 sendrecv？
我的 RTP payload type 是什么？
我的音视频同步组是什么？
我的 DTLS fingerprint 是什么？
我的网络 candidate 是什么？
```

SDP 里常见信息：

```text
m=audio ...
m=video ...
a=rtpmap:111 opus/48000/2
a=rtpmap:96 VP8/90000
a=rtpmap:102 H264/90000
a=fmtp:102 profile-level-id=42e01f;packetization-mode=1
a=sendrecv
a=mid:video
a=rtcp-mux
a=fingerprint:sha-256 ...
a=ice-ufrag:...
a=ice-pwd:...
```

重点不是背 SDP，而是知道它在谈什么：

- 媒体类型：audio/video/application
- codec：Opus、H.264、VP8、VP9、AV1
- 参数：H.264 profile、packetization-mode、码率限制等
- 传输：ICE、DTLS、RTP 扩展
- 方向：sendrecv、sendonly、recvonly、inactive

工程上，SDP 问题经常导致：

- 黑屏：双方 codec 不匹配
- 有声无画：video m-line 协商失败
- H.264 播不了：profile-level-id 或 packetization-mode 不兼容
- 收不到包：mid、ssrc、bundle、rtcp-mux 处理异常

---

## 8. ICE：WebRTC 为什么能穿透 NAT

真实用户很少有公网 IP，大多数都在 NAT 后面：

```text
手机 / 电脑
    |
家庭路由器 / 公司 NAT / 运营商 NAT
    |
公网
```

ICE（Interactive Connectivity Establishment）就是 WebRTC 用来找可连通路径的机制。

ICE 会收集多种 candidate：

```text
host candidate
  本机局域网地址，例如 192.168.x.x。

srflx candidate
  server reflexive，通过 STUN 看到的公网映射地址。

relay candidate
  通过 TURN 中继服务器分配的地址。
```

连接优先级通常是：

```text
host 直连优先
  延迟最低，但跨 NAT 往往不可用。

srflx NAT 穿透次之
  能打洞成功就很好。

relay TURN 兜底
  一定程度保证连通，但成本和延迟更高。
```

ICE 做的事情：

```text
1. 收集本端 candidate。
2. 通过信令交换双方 candidate。
3. 组成 candidate pair。
4. 用 STUN binding request 做连通性检查。
5. 选择可用且优先级最高的路径。
```

面试回答：

> ICE 是 WebRTC 的 NAT 穿透框架，它会收集 host、srflx、relay 三类 candidate，通过信令交换后对 candidate pair 做连通性检查。能直连就直连，不能直连就尝试 STUN 打洞，实在不行走 TURN 中继。TURN 能提高成功率，但会增加服务器带宽成本和延迟。

---

## 9. STUN 和 TURN

### 9.1 STUN：看看我在公网长什么样

STUN 的核心作用：

```text
客户端问 STUN 服务器：
  你看到我的公网 IP:Port 是什么？
```

返回结果就是 srflx candidate。

STUN 不转发媒体数据，它只是帮助发现公网映射地址和做连通性检查。

### 9.2 TURN：连不上就找中继

TURN 是兜底方案：

```text
Alice -> TURN -> Bob
```

TURN 会真正转发媒体数据，所以它的成本很高：

- 消耗服务器上行/下行带宽
- 增加延迟
- 需要部署多地域节点
- 需要鉴权和限流，防止被滥用

但没有 TURN，很多严格 NAT 或企业网络下通话会失败。

工程判断：

> STUN 提高直连概率，TURN 保证连通率。RTC 产品不能只配 STUN，不配 TURN；否则一到复杂网络环境就会出现“部分用户永远连不上”。

---

## 10. DTLS-SRTP：WebRTC 默认加密

WebRTC 媒体不是裸 RTP，而是 SRTP：

```text
RTP  -> SRTP
RTCP -> SRTCP
```

密钥怎么来？通过 DTLS 握手导出。

流程：

```text
ICE 选出网络路径
      |
      v
DTLS 握手
      |
      v
校验 fingerprint
      |
      v
导出 SRTP key
      |
      v
开始发送加密媒体 SRTP/SRTCP
```

为什么不用 TLS？

- TLS 通常跑在 TCP 上。
- WebRTC 媒体主要跑 UDP。
- 所以使用适合 datagram 的 DTLS。

面试回答：

> WebRTC 的媒体默认加密。ICE 选路成功后，双方进行 DTLS 握手，并通过 SDP 里的 fingerprint 校验证书身份。DTLS 握手完成后导出 SRTP 密钥，后续音视频通过 SRTP/SRTCP 加密传输。

---

## 11. RTP：真正传音视频的协议

RTP 是实时媒体传输协议。它不保证可靠，也不保证一定到达，但它提供实时媒体需要的基本信息：

```text
sequence number
  包序号，用于检测丢包、乱序、重排。

timestamp
  媒体时间戳，用于播放节奏和同步。

ssrc
  同一路媒体流的标识。

payload type
  表示负载类型，例如 Opus、H.264、VP8。

marker bit
  对视频常用于标记一帧结束。
```

可以这样理解：

```text
UDP 只知道发一坨数据。
RTP 告诉接收端：这是哪一路流、第几个包、属于哪个媒体时间点、是什么 codec。
```

### 11.1 RTP timestamp 不是系统时间

RTP timestamp 使用 codec 时钟：

```text
音频 Opus: 48000Hz
视频 H.264/VP8: 90000Hz
```

例如视频 30fps，每帧 RTP timestamp 大约增加：

```text
90000 / 30 = 3000
```

音频 20ms 一包，48kHz 下每包增加：

```text
48000 * 0.02 = 960
```

---

## 12. RTCP：反馈、同步和控制

RTP 负责传媒体，RTCP 负责反馈和控制。

常见 RTCP 作用：

- 统计丢包率、抖动、RTT
- 音视频同步
- 请求关键帧
- 请求重传
- 辅助拥塞控制
- 反馈接收端带宽估计

常见 RTCP 包或反馈：

```text
SR: Sender Report
  发送端报告，用于把 RTP timestamp 映射到 NTP 时间，帮助音视频同步。

RR: Receiver Report
  接收端报告，包含丢包、jitter、last SR、delay 等。

PLI: Picture Loss Indication
  请求发送端发关键帧。

FIR: Full Intra Request
  请求完整帧，语义比 PLI 更强。

NACK:
  告诉发送端哪些 RTP 包丢了，请求重传。

TCC: Transport-wide Congestion Control
  接收端反馈每个包的到达情况，用于带宽估计。
```

面试回答：

> RTP 负责实时媒体数据传输，RTCP 负责反馈和控制。WebRTC 通过 RTCP 获取丢包、jitter、RTT 等信息，也通过 PLI/FIR 请求关键帧，通过 NACK 请求重传，通过 Sender Report 把音视频 RTP 时间戳映射到同一个 NTP 时间基来做音画同步。

---

## 13. WebRTC 的媒体发送链路

发送端可以按这条链路理解：

```text
摄像头 / 麦克风采集
      |
      v
音视频前处理
  音频: AEC / NS / AGC / VAD
  视频: 裁剪 / 缩放 / 美颜 / 旋转 / 颜色转换
      |
      v
编码
  Audio: Opus
  Video: VP8 / VP9 / H.264 / AV1
      |
      v
RTP packetization
  大帧切成多个 RTP 包
      |
      v
Pacer 平滑发送
      |
      v
SRTP 加密
      |
      v
UDP socket 发送
```

发送端关键模块：

- 编码器：决定画质、码率、延迟。
- RTP packetizer：把编码帧切成 RTP 包。
- Pacer：控制发送节奏，避免瞬时突发打爆网络。
- Congestion controller：根据网络反馈调整目标码率。
- NACK/RTX buffer：保留最近发送过的包，便于接收端请求重传。

---

## 14. WebRTC 的媒体接收链路

接收端链路：

```text
UDP socket 收包
      |
      v
SRTP 解密
      |
      v
RTP 包解析
      |
      v
JitterBuffer
  处理乱序、抖动、等待短时重传
      |
      v
Frame reassembly
  RTP 包组回完整编码帧
      |
      v
解码
      |
      v
渲染 / 播放
```

接收端关键模块：

- JitterBuffer：抗网络抖动，但会引入缓冲延迟。
- NACK：发现缺包后请求发送端重传。
- FEC：用冗余包恢复丢失数据。
- Decoder：解码 H.264/VP8/VP9/AV1。
- A/V sync：根据音视频时间戳和 RTCP SR 同步播放。

实时通信不是“不缓存”，而是**只缓存足够少、足够聪明的一点点**。

---

## 15. 弱网对抗：WebRTC 的核心竞争力

WebRTC 比 RTMP 更适合实时互动，不是因为 UDP 神奇，而是因为它在 UDP 上做了一套媒体级弱网对抗。

### 15.1 JitterBuffer

网络包到达时间不稳定：

```text
发送间隔: 20ms, 20ms, 20ms, 20ms
到达间隔: 10ms, 40ms, 5ms, 25ms
```

JitterBuffer 会做：

- 乱序重排
- 平滑播放节奏
- 给短时间重传留窗口
- 动态调整缓冲深度

缓冲越大，越抗抖动，但延迟越高。

### 15.2 NACK

如果接收端发现包序号断了：

```text
收到: 100, 101, 103, 104
缺少: 102
```

它可以通过 RTCP NACK 请求发送端重传 102。

NACK 适合：

- RTT 不太高
- 丢包不太严重
- 被重传的数据还来得及播放

如果延迟窗口已经过了，重传回来也没意义。

### 15.3 FEC

FEC 是前向纠错：

```text
发 10 个媒体包
额外发 1~2 个冗余校验包
丢少量包时可以直接恢复
```

优点：

- 不等 RTT
- 对实时性友好

缺点：

- 增加带宽开销
- 丢包太严重也恢复不了

### 15.4 PLI/FIR

如果丢包导致视频参考链断了，接收端可能请求关键帧：

```text
PLI: 我画面坏了，请发一个关键帧。
FIR: 请发一个完整帧。
```

关键帧能恢复画面，但也会瞬间增大码率，所以不能频繁请求。

### 15.5 动态码率、分辨率、帧率

WebRTC 会根据网络状况动态调整：

```text
网络变差:
  降码率
  降分辨率
  降帧率
  增加 FEC
  更激进地丢帧

网络变好:
  逐步升码率
  恢复分辨率和帧率
```

面试回答：

> WebRTC 弱网对抗不是单点能力，而是一组策略：接收端用 JitterBuffer 处理乱序和抖动，用 NACK 请求短时重传，用 FEC 对抗部分丢包，用 PLI/FIR 请求关键帧恢复画面；发送端根据 RTCP/TCC 反馈做带宽估计，通过拥塞控制动态调整码率、分辨率和帧率。

---

## 16. 拥塞控制：为什么 WebRTC 不会傻发

实时音视频最怕发送端不管网络情况一直发。

如果网络实际只能承载 1Mbps，发送端还发 4Mbps：

```text
队列堆积
  |
  v
延迟上升
  |
  v
丢包增加
  |
  v
重传更多
  |
  v
更卡
```

WebRTC 需要实时估计可用带宽，并控制发送码率。

常见反馈信息：

- 包到达时间
- 丢包率
- RTT
- jitter
- 接收端反馈的 transport-wide sequence

现代 WebRTC 常见基于 TCC 的带宽估计：

```text
发送端给每个包带 transport-wide sequence number
      |
      v
接收端记录每个包到达时间
      |
      v
通过 RTCP transport feedback 回传
      |
      v
发送端估计网络趋势
      |
      v
调整 target bitrate
```

面试里不用深挖算法公式，但要知道目标：

> 拥塞控制的核心不是追求最高码率，而是在画质、流畅度和低延迟之间找平衡。WebRTC 通过接收端反馈估计带宽，控制编码器码率和 pacer 发送节奏，避免网络队列持续堆积。

---

## 17. 音频为什么 WebRTC 很强

WebRTC 音频模块非常成熟，原因是实时通话对音频极其敏感。

音频链路：

```text
麦克风采集
    |
    v
AudioProcessing
  AEC 回声消除
  NS 噪声抑制
  AGC 自动增益
  VAD 语音活动检测
    |
    v
Opus 编码
    |
    v
RTP 发送
```

接收端：

```text
RTP 接收
    |
    v
NetEQ
  jitter buffer
  packet loss concealment
  time stretch
    |
    v
Opus 解码
    |
    v
扬声器播放
```

关键概念：

- **AEC**：消除扬声器声音被麦克风再次采集形成的回声。
- **NS**：抑制环境噪声。
- **AGC**：自动调节音量，避免太小或爆音。
- **VAD**：判断是否有人声，用于降噪、静音检测、码率优化。
- **NetEQ**：WebRTC 的音频 jitter buffer 和丢包隐藏核心模块。
- **PLC**：Packet Loss Concealment，丢包隐藏，用估计信号填补短时丢失。

面试回答：

> WebRTC 音频强在端到端实时通话优化。发送端有 AEC、NS、AGC、VAD 等 AudioProcessing 能力，接收端有 NetEQ 做 jitter buffer、丢包隐藏和播放节奏调整。音频对卡顿和爆音非常敏感，所以 WebRTC 在音频抗抖动和丢包隐藏上做了很多工程优化。

---

## 18. 视频：从编码到抗丢包

WebRTC 视频常见 codec：

- VP8：兼容好，WebRTC 生态传统默认。
- VP9：压缩效率更高，支持 SVC。
- H.264：硬件支持广，移动端常用。
- AV1：效率高，但编码复杂度和兼容性要评估。

视频发送端关注：

- 分辨率
- 帧率
- 码率
- keyframe 间隔
- 硬编支持
- 编码延迟
- 是否支持 simulcast / SVC

视频接收端关注：

- 丢包后能否恢复
- 是否及时请求关键帧
- 解码器是否能处理乱序和缺帧
- 渲染队列是否堆积
- 首帧时间

### 18.1 Simulcast

Simulcast 是同时发送多路不同质量的视频流：

```text
低清: 180p, 150kbps
中清: 360p, 500kbps
高清: 720p, 1500kbps
```

SFU 可以根据接收端网络情况转发不同层。

适合多人会议：

- 大画面订阅高清
- 小窗口订阅低清
- 网络差的用户订阅低清

### 18.2 SVC

SVC 是可伸缩视频编码，一路码流内部有多层：

```text
Base layer
Enhancement layer 1
Enhancement layer 2
```

网络差时只发基础层，网络好时加增强层。

Simulcast 更容易理解和落地，但上行带宽开销更高；SVC 更优雅，但依赖 codec 和生态支持。

---

## 19. WebRTC 多人架构：Mesh、MCU、SFU

### 19.1 Mesh

每个人和其他人都 P2P：

```text
A <-> B
A <-> C
B <-> C
```

优点：

- 架构简单
- 小房间可用
- 服务器压力低

缺点：

- 人数增加后上行爆炸
- 每端要编码/发送多路

适合 2~3 人小通话。

### 19.2 MCU

服务器把多路音视频解码、混流、重新编码成一路：

```text
A -> MCU
B -> MCU
C -> MCU
MCU -> 混合后一路流给所有人
```

优点：

- 客户端简单
- 下行压力小
- 兼容传统播放器

缺点：

- 服务器 CPU 成本高
- 重新编码增加延迟和画质损失

### 19.3 SFU

服务器不解码混流，只转发 RTP 包：

```text
A -> SFU -> B/C
B -> SFU -> A/C
C -> SFU -> A/B
```

优点：

- 服务器成本低于 MCU
- 延迟低
- 可以按需转发不同质量层
- 当前实时音视频会议主流架构

缺点：

- 客户端要处理多路解码和布局
- 服务端要理解 RTP/RTCP、带宽估计、订阅关系
- 架构复杂度高于简单转发

面试回答：

> 多人 WebRTC 常见架构有 Mesh、MCU、SFU。Mesh 适合很小房间，但人数多上行压力爆炸；MCU 服务端混流，客户端简单但服务器成本和延迟高；SFU 不解码媒体，只做 RTP 转发和订阅控制，延迟和成本较好，是现在多人会议和连麦直播的主流架构。

---

## 20. WebRTC 低延迟来自哪里

WebRTC 能低延迟，不是因为某一个点，而是整条链路都按实时性设计：

- UDP 避免 TCP 队头阻塞。
- RTP 支持实时包序、时间戳和媒体同步。
- RTCP 提供及时反馈。
- NACK/FEC/PLI 做媒体级恢复。
- JitterBuffer 动态控制缓冲。
- 拥塞控制避免队列无限堆积。
- 编码器可以动态调码率、分辨率、帧率。
- 默认禁用或少用会增加延迟的编码结构。
- SFU 转发不转码，降低服务端处理延迟。

但 WebRTC 低延迟不是免费午餐：

- 网络太差仍然会卡。
- TURN 中继会增加延迟和成本。
- 过小 jitter buffer 会导致花屏和音频断续。
- 频繁关键帧会造成码率尖峰。
- 多人房间会带来订阅、转发、带宽分配复杂度。

---

## 21. C++ 工程视角：一个 RTC SDK 怎么设计

如果让你设计一个 C++ WebRTC SDK，可以拆成这些模块：

```text
RtcEngine
  对外统一入口
  init / joinRoom / leaveRoom / publish / subscribe

SignalingClient
  WebSocket/HTTP
  房间、鉴权、SDP、candidate、用户状态

PeerConnectionManager
  创建和管理 PeerConnection
  offer/answer/candidate/state callback

MediaCapture
  摄像头、麦克风、屏幕采集

AudioPipeline
  AEC/NS/AGC
  音频设备、采集播放、路由切换

VideoPipeline
  前处理、旋转、缩放、纹理输入
  硬编硬解、渲染

NetworkQuality
  RTT、丢包、jitter、码率、帧率、卡顿统计

ThreadModel
  signaling thread
  worker thread
  network thread
  capture/render thread

PlatformBridge
  JNI / Objective-C++ / Swift / Kotlin 封装
```

### 21.1 C++ 面试重点

面试官可能不要求你写 libwebrtc 源码，但会看你有没有工程意识：

- 线程模型是否清楚
- 回调在哪个线程触发
- 音视频数据生命周期怎么管理
- 是否避免大内存拷贝
- 硬编硬解怎么接入
- JNI/OC 回调如何防止悬空引用
- 网络状态如何统计和上报
- 重连、切网、前后台如何处理
- 如何定位黑屏、无声、卡顿、回声

可以这样答：

> 我会把 WebRTC SDK 分为信令、PeerConnection 管理、音视频采集处理、编码解码、网络质量统计和平台桥接几个模块。C++ 层负责核心媒体和连接状态，Java/Objective-C 层负责平台设备和 UI 生命周期。工程上要特别注意线程切换、回调生命周期、零拷贝纹理输入、硬编硬解兼容性，以及 getStats 数据上报，用于定位丢包、RTT、jitter、码率、首帧和卡顿问题。

---

## 22. 常见问题怎么定位

### 22.1 连不上

优先看：

- 信令是否成功交换 SDP
- ICE candidate 是否收集和交换
- ICE state 是否 failed
- STUN/TURN 是否可达
- TURN 鉴权是否过期
- 防火墙是否阻断 UDP

常见解决：

- 配 TURN 兜底
- 检查 candidate 是否被业务信令丢失
- 检查 SDP 里的 ice-ufrag/ice-pwd 是否匹配
- 检查网络切换后是否 ICE restart

### 22.2 有声音没画面

优先看：

- video m-line 是否协商成功
- codec 是否匹配
- 是否收到 RTP video 包
- 是否收到关键帧
- 是否 PLI 后仍无关键帧
- 解码器初始化是否失败
- 渲染纹理/Surface 是否有效

### 22.3 有画面没声音

优先看：

- 麦克风权限
- audio track 是否 publish
- Opus 是否协商成功
- audio RTP 是否到达
- 音频设备路由
- 播放音量和系统静音
- AEC/AudioDeviceModule 是否初始化异常

### 22.4 卡顿和延迟高

优先看：

- RTT
- packet loss
- jitter
- available outgoing bitrate
- target bitrate / actual bitrate
- encode time
- decode time
- render queue
- jitter buffer delay
- 是否走 TURN

判断方向：

```text
RTT 高:
  网络路径差，可能跨地域或 TURN 中继。

丢包高:
  看 NACK/FEC 是否有效，是否需要降码率。

jitter 高:
  接收端缓冲会变大，延迟上升。

编码耗时高:
  硬编失败或分辨率/码率过高。

发送码率高于可用带宽:
  拥塞控制没压住或业务层设置过激。
```

---

## 23. getStats：RTC 质量监控的入口

WebRTC 的 `getStats` 是排查问题的重要工具。

常看指标：

```text
candidate-pair:
  currentRoundTripTime
  availableOutgoingBitrate
  bytesSent / bytesReceived

outbound-rtp:
  framesEncoded
  framesPerSecond
  qpSum
  packetsSent
  retransmittedPacketsSent
  targetBitrate

inbound-rtp:
  packetsLost
  jitter
  framesDecoded
  framesDropped
  freezeCount
  jitterBufferDelay

remote-inbound-rtp:
  roundTripTime
  fractionLost
```

面试里说出这些指标，比泛泛说“看日志”更有说服力。

---

## 24. WebRTC 和直播系统怎么结合

WebRTC 不只用于会议，也可以用于直播。

### 24.1 连麦直播

常见架构：

```text
主播 <-> SFU <-> 连麦嘉宾
      |
      v
旁路混流 / 转推
      |
      v
CDN: HLS / HTTP-FLV / RTMP
      |
      v
普通观众
```

原因：

- 主播和嘉宾需要低延迟互动，用 WebRTC。
- 大量普通观众不需要互动，用 CDN 分发更省成本。
- SFU 或媒体服务器可以旁路混流，转成 RTMP/HLS/HTTP-FLV。

### 24.2 超低延迟直播

如果所有观众都用 WebRTC 拉流：

优点：

- 延迟低
- 可互动
- 抗弱网能力强

缺点：

- CDN 成本和架构复杂度高
- 大规模分发比 HLS/HTTP-FLV 难
- 服务端要支持海量 PeerConnection 或专门的 RTC 分发网络

工程选型：

```text
普通大规模观看:
  RTMP 推流 + HLS/HTTP-FLV 播放

低延迟但不强互动:
  HTTP-FLV / LL-HLS / WebRTC 拉流，看成本和生态

强互动:
  WebRTC

连麦直播:
  WebRTC 连麦 + CDN 旁路分发
```

### 24.3 WHIP / WHEP：WebRTC 进入直播基础设施

传统 WebRTC 信令没有统一标准，业务一般自己用 WebSocket 交换 SDP 和 candidate。为了让 WebRTC 更容易接入直播/CDN 系统，业界开始使用更标准化的接入方式：

```text
WHIP: WebRTC-HTTP Ingestion Protocol
  用 HTTP 做 WebRTC 推流信令，常用于主播/编码器把 WebRTC 流推到媒体服务器。

WHEP: WebRTC-HTTP Egress Protocol
  用 HTTP 做 WebRTC 拉流信令，常用于观众从媒体服务器拉 WebRTC 低延迟流。
```

可以这样理解：

```text
RTMP 推流:
  rtmp://... 作为传统直播 ingest 标准入口。

WHIP 推流:
  用 HTTP POST SDP 的方式，把 WebRTC ingest 标准化。

WHEP 拉流:
  用 HTTP 交换 SDP，让播放器以 WebRTC 方式低延迟观看。
```

它们解决的是“WebRTC 信令不统一导致接入复杂”的问题，不改变 WebRTC 底层媒体传输本质。真正传媒体时，仍然是 ICE、DTLS、SRTP、RTP/RTCP 那一套。

面试里可以这样说：

> WebRTC 本身不规定信令，所以传统 RTC 系统多用自定义 WebSocket 信令。直播场景为了标准化推拉流接入，出现了 WHIP 和 WHEP：WHIP 面向 WebRTC 推流 ingest，WHEP 面向 WebRTC 拉流 egress。它们用 HTTP 完成 SDP 交换，让 WebRTC 更容易接入媒体服务器和 CDN，但底层媒体仍然走 WebRTC 的 ICE、DTLS-SRTP、RTP/RTCP。

---

## 25. 高频面试题

### Q1：WebRTC 是什么？

WebRTC 是一套实时音视频通信技术栈，不是单一协议。它包括信令协商、NAT 穿透、加密传输、RTP/RTCP 媒体传输、拥塞控制、音视频采集处理、编解码、JitterBuffer、NACK/FEC 等能力。它主要用于视频会议、语音通话、连麦直播和低延迟互动场景。

### Q2：WebRTC 为什么主要用 UDP？

实时音视频更看重低延迟而不是每个包都可靠到达。TCP 丢包会重传并导致队头阻塞，弱网下延迟可能持续堆积。WebRTC 用 UDP 承载媒体，在应用层通过 RTP/RTCP、NACK、FEC、JitterBuffer 和拥塞控制做媒体级可靠性和实时性平衡。

### Q3：WebRTC 建连流程是什么？

双方先通过业务信令交换 SDP offer/answer，协商 codec、媒体方向、DTLS fingerprint 等信息；然后收集并交换 ICE candidate，通过 ICE 做连通性检查，选出最佳 candidate pair；连接成功后进行 DTLS 握手，导出 SRTP 密钥；最后通过 SRTP/SRTCP 发送音视频 RTP/RTCP 包。

### Q4：SDP 有什么作用？

SDP 描述媒体能力和传输参数，包括音视频 m-line、codec、payload type、fmtp 参数、媒体方向、ICE ufrag/pwd、DTLS fingerprint、RTP 扩展等。它本质上是双方协商“我能发什么、能收什么、用什么参数传”的能力描述。

### Q5：ICE、STUN、TURN 的关系是什么？

ICE 是 NAT 穿透框架，STUN 和 TURN 是它使用的工具。STUN 用来发现公网映射地址和做连通性检查；TURN 用来在直连失败时中继媒体。ICE 会收集 host、srflx、relay candidate，并选择最合适的可连通路径。

### Q6：TURN 为什么重要？

因为很多用户处在严格 NAT、企业防火墙或运营商网络下，P2P 打洞不一定成功。TURN 通过服务器中继媒体，能显著提高连通率。但 TURN 消耗服务器带宽并增加延迟，所以通常作为兜底路径。

### Q7：RTP 和 RTCP 分别做什么？

RTP 传输实时音视频数据，提供 sequence number、timestamp、SSRC、payload type 等字段；RTCP 做反馈和控制，包括丢包、jitter、RTT 统计，NACK 重传请求，PLI/FIR 关键帧请求，Sender Report 音视频同步，以及拥塞控制反馈。

### Q8：WebRTC 怎么抗弱网？

WebRTC 通过 JitterBuffer 处理乱序和抖动，通过 NACK 请求短时重传，通过 FEC 做前向纠错，通过 PLI/FIR 请求关键帧恢复画面，通过拥塞控制和带宽估计动态调整码率、分辨率和帧率。它不是保证不丢包，而是在实时性和质量之间动态平衡。

### Q9：WebRTC 如何做音视频同步？

音视频各自有 RTP timestamp，但它们的时钟不同。RTCP Sender Report 会把 RTP timestamp 映射到同一个 NTP 时间基，接收端据此计算音频和视频的播放时间关系，再结合 jitter buffer 控制渲染和播放，实现音画同步。

### Q10：WebRTC 为什么默认安全？

WebRTC 媒体使用 SRTP/SRTCP 加密。ICE 选路后双方进行 DTLS 握手，并通过 SDP 中的 fingerprint 校验证书身份。DTLS 握手完成后导出 SRTP 密钥，后续媒体包都加密传输。

### Q11：Mesh、MCU、SFU 有什么区别？

Mesh 是端到端互连，适合小房间，但人数多时上行压力爆炸；MCU 是服务端解码混流再编码，客户端简单但服务器成本和延迟高；SFU 是服务端按需转发 RTP 包，不混流不转码，延迟和成本较好，是多人会议和连麦直播常见架构。

### Q12：WebRTC 和 RTMP 怎么选？

RTMP 适合传统直播推流，生态成熟、接入简单，但基于 TCP，弱网下容易延迟堆积。WebRTC 适合实时互动、连麦、会议和超低延迟直播，延迟更低、弱网反馈更强，但架构复杂、服务端成本和开发难度更高。普通大规模直播可以 RTMP 推流 + HLS/HTTP-FLV 分发；强互动场景优先 WebRTC。

### Q13：WHIP 和 WHEP 是什么？

WHIP 是 WebRTC-HTTP Ingestion Protocol，主要用于用 HTTP 标准化 WebRTC 推流接入；WHEP 是 WebRTC-HTTP Egress Protocol，主要用于用 HTTP 标准化 WebRTC 拉流播放。它们主要解决 WebRTC 信令接入不统一的问题，底层媒体传输仍然是 ICE、DTLS-SRTP、RTP/RTCP。

---

## 26. 一套完整口述模板

如果面试官问：“你讲一下 WebRTC。”

可以这样回答：

> WebRTC 不是一个单独协议，而是一整套实时音视频通信技术栈，主要用于视频会议、连麦直播、语音通话这类低延迟互动场景。它和 RTMP 最大的区别是，RTMP 基于 TCP 更偏传统推流，而 WebRTC 媒体通常走 UDP，在应用层通过 RTP/RTCP、NACK、FEC、JitterBuffer 和拥塞控制来保证实时性和弱网体验。
>
> 一次 WebRTC 建连通常先通过业务信令交换 SDP offer/answer，SDP 里描述 codec、媒体方向、ICE 参数、DTLS fingerprint 等；然后双方收集并交换 ICE candidate，通过 ICE/STUN/TURN 做 NAT 穿透和连通性检查，选出一条可用路径；路径建立后进行 DTLS 握手，导出 SRTP 密钥，之后音视频通过 SRTP/SRTCP 传输。
>
> 媒体链路上，发送端采集音视频，音频经过 AEC/NS/AGC，视频经过前处理和编码，然后打成 RTP 包，通过 pacer 和拥塞控制发送；接收端解密 SRTP 后进入 jitter buffer，处理乱序、抖动和短时重传，再解码渲染。多人场景通常会用 SFU 架构，服务器不解码混流，只做 RTP 转发和订阅控制，兼顾低延迟和成本。

---

## 27. 复习抓手

学习 WebRTC 不要从 API 开始背，建议抓住 6 条主线：

```text
1. 定位
WebRTC 是实时音视频通信技术栈，不是单一协议。

2. 建连
信令交换 SDP/candidate，ICE/STUN/TURN 找路径。

3. 安全
DTLS 握手，SRTP/SRTCP 加密媒体。

4. 传输
RTP 传媒体，RTCP 做反馈、同步和控制。

5. 弱网
JitterBuffer、NACK、FEC、PLI、拥塞控制、动态码率。

6. 架构
P2P、Mesh、MCU、SFU，以及和直播/CDN 的结合。
```

如果能把这 6 条讲顺，再结合 C++ SDK 工程里的线程、硬编硬解、音频处理、JNI/OC 封装、getStats 监控和问题定位，你在 WebRTC 面试里就不会停留在“我会调 API”的层面。
