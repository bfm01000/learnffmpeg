# RTMP 协议深度解析与面试指南：从会用推流到能说“精通 RTMP”

## 0. 本篇定位

- 面试复习：先掌握 RTMP 握手、Chunk Header、Message 类型、控制消息、AMF 命令和 H.264/AAC 封装。
- 深入学习：重点看服务端 parser、GOP cache、慢客户端背压、首帧黑屏、有声无画、timestamp 和抓包排障。
- 职责边界：这篇偏协议字段和服务端工程；直播业务口述回到 [../直播/RTMP直播协议深入理解与面试指南.md](../直播/RTMP直播协议深入理解与面试指南.md)。
RTMP（Real-Time Messaging Protocol）是传统直播推流链路中最常见的协议之一。很多音视频工程师平时只是用 FFmpeg、OBS、SRS、Nginx-RTMP 或公司 SDK 推一个 `rtmp://` 地址，但面试里如果你说“熟悉 RTMP”甚至“精通 RTMP”，面试官通常会继续追问：**握手怎么做、chunk 怎么拆、message 怎么封、H.264/AAC 怎么放进去、时间戳怎么处理、服务端怎么抗慢客户端、RTMP 为什么延迟高、如何排查推流失败**。

本文目标是把 RTMP 从“会用”补到“能面试、能排障、能写基础实现”的程度。

---

## 一、RTMP 在音视频链路中的位置

典型直播链路：

```text
采集 -> 编码 -> 封装 -> 推流 -> 流媒体服务器 -> 分发 -> 播放器
```

RTMP 主要位于：

```text
编码后的 H.264/AAC
        |
      FLV/RTMP 封装
        |
      TCP 传输
        |
   流媒体服务器
```

常见使用场景：

* 主播端向服务器推流：`OBS/FFmpeg/SDK -> RTMP Server`。
* 服务端之间转推：`Origin -> Edge`。
* 部分老播放器拉流：`RTMP Server -> Player`。

现在 RTMP 更多用于“上行推流”，下行播放常改用：

* HTTP-FLV：低延迟直播拉流。
* HLS：大规模 CDN 分发。
* WebRTC：实时互动、连麦、超低延迟直播。

面试一句话总结：

> RTMP 是基于 TCP 的实时消息协议，常用于直播推流。它把音视频、控制命令和元数据封装成 RTMP Message，再切成 Chunk 在 TCP 字节流上传输。

---

## 二、RTMP 的核心特点

RTMP 的关键特征：

* 基于 TCP，默认端口通常是 `1935`。
* 面向长连接。
* 自定义握手流程。
* 消息按 chunk 拆分传输。
* 可承载音频、视频、元数据和控制命令。
* 常见编码组合是 H.264 + AAC。
* 与 FLV Tag 格式关系非常近。

优点：

* 生态成熟，推流端支持广。
* 服务端实现多，调试资料丰富。
* 基于 TCP，网络穿透比 UDP 方案简单。
* 推流链路稳定，适合传统直播。

缺点：

* TCP 队头阻塞导致弱网延迟容易堆积。
* 原生安全性弱，需要 RTMPS 才有 TLS。
* 浏览器原生不再支持 Flash RTMP 播放。
* 协议较老，对现代自适应和实时互动支持弱。

---

## 三、RTMP URL 与应用模型

一个 RTMP 地址通常长这样：

```text
rtmp://example.com:1935/live/stream123
```

可以拆成：

* `rtmp`：协议。
* `example.com`：服务器域名。
* `1935`：端口。
* `live`：app，也可以理解为业务应用名。
* `stream123`：stream name，也就是流名。

在服务端内部，常见映射是：

```text
vhost + app + stream
```

例如：

```text
__defaultVhost__ / live / stream123
```

工程上经常还会带鉴权参数：

```text
rtmp://example.com/live/stream123?token=xxx&expire=1710000000
```

面试追问：

**问：RTMP URL 里的 app 和 stream name 有什么区别？**

答：

* app 通常表示业务应用或路径分组，比如 `live`。
* stream name 是具体一路流的标识，比如房间号、主播 ID。
* 服务端通常用 app + stream name 定位一路发布或播放流。

---

## 四、RTMP 协议分层

RTMP 可以按这几层理解：

```text
业务层：publish / play / createStream / connect
消息层：Command Message / Audio Message / Video Message / Data Message
Chunk 层：Basic Header / Message Header / Extended Timestamp / Chunk Data
传输层：TCP
```

RTMP 的关键设计是：

> 上层是一个个 RTMP Message，下层传输时会按 chunk size 切成多个 RTMP Chunk。

为什么要切 chunk？

* 避免大视频帧长时间独占 TCP 连接。
* 多路 chunk stream 可以交错发送。
* 降低单次发送颗粒度。

注意：

* RTMP Message 是逻辑消息。
* RTMP Chunk 是传输分片。
* TCP 是字节流，最终都落在连续字节里。

---

## 五、RTMP 握手过程

RTMP 建连后先做握手。基础握手包括：

```text
Client -> Server: C0 + C1
Server -> Client: S0 + S1 + S2
Client -> Server: C2
```

### 1. C0 / S0

长度：1 字节。

含义：RTMP version。

常见值：

```text
0x03
```

表示普通 RTMP。

### 2. C1 / S1

长度：1536 字节。

结构通常是：

```text
time: 4 bytes
zero/version: 4 bytes
random: 1528 bytes
```

C1 由客户端发送，S1 由服务端发送。

### 3. C2 / S2

长度：1536 字节。

基础握手里：

* S2 通常是对 C1 的回显或加工。
* C2 通常是对 S1 的回显或加工。

### 4. 简单握手与复杂握手

RTMP 有简单握手和复杂握手。

简单握手：

* 实现容易。
* 开源服务端和普通推流大多能兼容。
* 常见直播推流足够使用。

复杂握手：

* 涉及 digest、HMAC、Adobe 校验逻辑。
* 更多是历史兼容 Flash 生态。
* 实现复杂，现代基础推流服务不一定强依赖。

面试回答：

> 普通 RTMP 握手是 C0C1、S0S1S2、C2。C0/S0 表示版本，C1/S1 是 1536 字节握手块，C2/S2 用于确认对端握手数据。大多数直播场景实现简单握手即可，但需要知道历史上还有复杂握手用于兼容 Adobe 生态。

---

## 六、RTMP Chunk 结构

RTMP 的传输单位是 chunk。

一个 chunk 由几部分组成：

```text
Basic Header
Message Header
Extended Timestamp（可选）
Chunk Data
```

### 1. Basic Header

Basic Header 至少 1 字节，包含：

* `fmt`：2 bit，表示 Message Header 类型。
* `csid`：chunk stream id，表示 chunk stream。

第一字节布局：

```text
  0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+
|fmt|   csid    |
+-+-+-+-+-+-+-+-+
```

`fmt` 有 4 种：

* `0`：完整 Message Header，11 字节。
* `1`：不带 message stream id，7 字节。
* `2`：只有 timestamp delta，3 字节。
* `3`：没有 Message Header，复用前一个 chunk header。

`csid` 不是业务流名，而是 RTMP chunk stream 的 ID。

常见 csid：

* `2`：协议控制消息。
* `3`：命令消息。
* `4` 或 `5`：音视频数据，具体实现可不同。

### 2. Message Header

fmt = 0 时，Message Header 长度 11 字节：

```text
timestamp: 3 bytes
message length: 3 bytes
message type id: 1 byte
message stream id: 4 bytes, little-endian
```

注意大小端：

* timestamp：大端 3 字节。
* message length：大端 3 字节。
* message stream id：小端 4 字节。

这是 RTMP 实现里非常容易写错的点。

fmt = 1 时：

```text
timestamp delta: 3 bytes
message length: 3 bytes
message type id: 1 byte
```

fmt = 2 时：

```text
timestamp delta: 3 bytes
```

fmt = 3 时：

```text
无 message header，沿用上一个 header 上下文
```

### 3. Extended Timestamp

RTMP 普通 timestamp 字段只有 24 bit。

当 timestamp 或 timestamp delta >= `0xFFFFFF` 时：

* header 中 3 字节 timestamp 写 `0xFFFFFF`。
* 后面追加 4 字节 Extended Timestamp。

面试常见坑：

> Extended Timestamp 是否出现，取决于当前 chunk header 的 timestamp 字段是否为 `0xFFFFFF`。解析 fmt=3 时也要结合前一个 chunk stream 的上下文判断。

### 4. Chunk Size

RTMP 默认 chunk size 是 128 字节。

连接建立后，双方可以通过协议控制消息 `Set Chunk Size` 修改。

例如服务端或客户端设置：

```text
chunk_size = 4096
```

影响：

* chunk size 越小，header 开销越多，但更容易交错。
* chunk size 越大，header 开销少，但大帧可能占用更久。

常见推流端会设置较大的 chunk size，例如 4096 或更大，以减少开销。

---

## 七、RTMP Message 类型

RTMP Message Type ID 非常重要。

常见类型：

| Type ID | 名称 | 作用 |
|---|---|---|
| 1 | Set Chunk Size | 设置 chunk size |
| 2 | Abort Message | 中止某个 chunk stream 的消息 |
| 3 | Acknowledgement | 确认收到字节数 |
| 4 | User Control Message | 用户控制事件 |
| 5 | Window Acknowledgement Size | 设置 ACK 窗口 |
| 6 | Set Peer Bandwidth | 设置对端带宽 |
| 8 | Audio Message | 音频数据 |
| 9 | Video Message | 视频数据 |
| 15 | AMF3 Data Message | AMF3 数据 |
| 16 | AMF3 Command Message | AMF3 命令 |
| 18 | AMF0 Data Message | AMF0 数据 |
| 20 | AMF0 Command Message | AMF0 命令 |

面试重点不是死背表，而是知道：

* 控制消息负责连接参数和 ACK。
* command message 负责 connect、createStream、publish、play。
* data message 常用于 metadata。
* audio/video message 承载媒体数据。

---

## 八、RTMP 控制消息

### 1. Set Chunk Size

用于通知对端后续发送 chunk data 的最大大小。

注意：

* 只影响发送方向。
* 双方各自维护入方向和出方向 chunk size。

### 2. Window Acknowledgement Size

设置接收窗口大小。

对端收到这么多字节后，需要发送 Acknowledgement。

例如：

```text
Window Acknowledgement Size = 5000000
```

### 3. Acknowledgement

表示已经收到的字节数。

注意这不是业务消息 ACK，而是 RTMP 协议层接收字节数确认。

### 4. Set Peer Bandwidth

告诉对端带宽窗口。

携带：

* acknowledgement window size。
* limit type。

limit type 常见：

* hard。
* soft。
* dynamic。

### 5. User Control Message

常见事件：

* Stream Begin。
* Stream EOF。
* Stream Dry。
* Set Buffer Length。
* Ping Request。
* Ping Response。

面试回答：

> RTMP 控制消息主要用于设置 chunk size、ACK 窗口、带宽窗口和流状态事件。它们不承载音视频，但影响连接行为和传输参数。

---

## 九、AMF 与 RTMP 命令

RTMP 命令消息通常用 AMF 编码。

常见是 AMF0。

AMF0 是一种二进制对象编码格式，可以表示：

* Number。
* Boolean。
* String。
* Object。
* Null。
* ECMA Array。

RTMP 命令消息里常见字段：

```text
command name
transaction id
command object
additional arguments
```

### 1. connect

客户端连接 app：

```text
connect(transactionId=1, commandObject={
  app: "live",
  type: "nonprivate",
  tcUrl: "rtmp://example.com/live",
  flashVer: "...",
  objectEncoding: 0
})
```

服务端响应：

```text
_result(transactionId=1, properties, information)
```

### 2. releaseStream / FCPublish

推流端常见历史命令，用于兼容 Flash/FMS 生态。

服务端可以响应 `_result` 或忽略部分兼容命令，取决于实现。

### 3. createStream

客户端请求创建 stream。

服务端返回 stream id：

```text
_result(transactionId, null, streamId)
```

后续 publish/play 会带这个 stream id。

### 4. publish

推流命令：

```text
publish(transactionId=0, null, streamName, publishingType)
```

publishingType 常见：

* `live`
* `record`
* `append`

服务端通常返回 `onStatus`：

```text
NetStream.Publish.Start
```

### 5. play

拉流命令：

```text
play(transactionId=0, null, streamName, start, duration, reset)
```

服务端通常返回：

```text
NetStream.Play.Start
```

### 6. onMetaData

元数据通常用 AMF0 Data Message 发送，常见字段：

* width。
* height。
* framerate。
* videocodecid。
* audiocodecid。
* audiosamplerate。
* audiosamplesize。
* stereo。

面试重点：

> RTMP 的控制命令不是文本协议，而是 AMF 编码的 command/data message。connect、createStream、publish、play 是最核心的命令链路。

---

## 十、RTMP 推流完整流程

典型推流流程：

```text
1. TCP connect
2. RTMP handshake: C0C1 -> S0S1S2 -> C2
3. connect(app)
4. 服务端返回 _result
5. releaseStream(streamName)
6. FCPublish(streamName)
7. createStream()
8. 服务端返回 stream id
9. publish(streamName, "live")
10. 服务端返回 NetStream.Publish.Start
11. 发送 onMetaData
12. 发送 AAC sequence header
13. 发送 AVC sequence header
14. 发送音视频数据
```

不同客户端命令可能略有差异，但核心是：

```text
handshake -> connect -> createStream -> publish -> media data
```

FFmpeg 推流可以这样抓包观察：

```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://127.0.0.1/live/test
```

面试中如果要描述 RTMP 推流，不要只说“先握手再推数据”，要能说出：

* TCP 建连。
* RTMP C/S 握手。
* AMF connect。
* createStream。
* publish。
* metadata。
* sequence header。
* audio/video message。

---

## 十一、RTMP 拉流完整流程

典型拉流流程：

```text
1. TCP connect
2. RTMP handshake
3. connect(app)
4. createStream()
5. play(streamName)
6. 服务端发送 Stream Begin
7. 服务端发送 onStatus(NetStream.Play.Start)
8. 服务端发送 metadata
9. 服务端发送 sequence header
10. 服务端发送音视频数据
```

播放端关键点：

* 必须拿到音视频参数才能初始化解码器。
* H.264 播放需要 SPS/PPS。
* AAC 播放需要 AudioSpecificConfig。
* 新观众通常要从 GOP cache 中最近 IDR 开始播放。

---

## 十二、RTMP 如何封装 H.264

RTMP Video Message 承载 FLV Video Tag 风格的数据。

Video Tag 第一个字节：

```text
FrameType: 4 bit
CodecID:   4 bit
```

常见：

* FrameType = 1：keyframe。
* FrameType = 2：inter frame。
* CodecID = 7：AVC/H.264。

当 CodecID = 7 时，后面是 AVC Video Packet：

```text
AVCPacketType: 1 byte
CompositionTime: 3 bytes
Data: n bytes
```

AVCPacketType：

* `0`：AVC sequence header。
* `1`：AVC NALU。
* `2`：AVC end of sequence。

### 1. AVC Sequence Header

AVC sequence header 里放的是 `AVCDecoderConfigurationRecord`，核心包含：

* profile。
* level。
* NALU length size。
* SPS。
* PPS。

解码器需要它初始化。

注意：

* RTMP/FLV 中 H.264 通常使用 AVCC 格式。
* 裸 H.264 文件常见 Annex-B 格式，使用 start code：`00 00 01` 或 `00 00 00 01`。
* 推 RTMP 时通常要把 Annex-B 转成 AVCC。

### 2. AVC NALU

AVC NALU 数据通常是：

```text
length-prefixed NALU
```

而不是 start code 分隔。

例如：

```text
4 bytes length + NALU bytes
4 bytes length + NALU bytes
```

### 3. CompositionTime

CompositionTime 用于处理 B 帧：

```text
CompositionTime = PTS - DTS
```

RTMP message timestamp 通常对应 DTS。

如果没有 B 帧：

```text
PTS == DTS
CompositionTime = 0
```

如果有 B 帧，CompositionTime 可能非 0。

面试高频点：

> RTMP/FLV 封装 H.264 时，视频 message timestamp 通常是 DTS，CompositionTime 表示 PTS 和 DTS 的差值。没有 B 帧时 CompositionTime 为 0。

---

## 十三、RTMP 如何封装 AAC

RTMP Audio Message 同样接近 FLV Audio Tag。

Audio Tag 第一个字节：

```text
SoundFormat: 4 bit
SoundRate:   2 bit
SoundSize:   1 bit
SoundType:   1 bit
```

常见：

* SoundFormat = 10：AAC。
* SoundRate = 3：44kHz，AAC 时这个字段不完全代表真实采样率。
* SoundSize = 1：16-bit。
* SoundType = 1：stereo。

当 SoundFormat = AAC 时，后面有：

```text
AACPacketType: 1 byte
Data: n bytes
```

AACPacketType：

* `0`：AAC sequence header。
* `1`：AAC raw。

### 1. AAC Sequence Header

AAC sequence header 放的是 AudioSpecificConfig。

它包含：

* audioObjectType。
* samplingFrequencyIndex。
* channelConfiguration。

解码器需要它初始化。

### 2. AAC Raw

AAC raw 里通常是不带 ADTS header 的 AAC frame。

因此：

* 本地 `.aac` 文件常见 ADTS。
* 推 RTMP 需要去掉 ADTS header。
* 从 RTMP 拉流保存成 ADTS AAC，需要重新加 ADTS header。

面试高频点：

> RTMP/FLV 中 AAC 数据不是 ADTS 流，而是先发 AudioSpecificConfig，再发 AAC raw frame。

---

## 十四、RTMP 与 FLV 的关系

RTMP 音视频 message payload 与 FLV Tag body 非常接近。

常见理解：

```text
FLV Tag Header
  + Tag Body

RTMP Message Header
  + Message Payload（接近 FLV Tag Body）
```

因此：

* RTMP 转 HTTP-FLV 通常不需要重新编码。
* 大多数情况下只是协议拆封装和重封装。
* H.264/AAC 数据可以直接复用。

RTMP 转 FLV 文件：

* 写 FLV Header。
* 写 metadata tag。
* 写 audio/video tag。
* 写 previous tag size。

RTMP 转 HLS：

* 需要从 RTMP 中取出 H.264/AAC。
* 重新封装成 TS 或 fMP4。
* 按 segment 切片。

---

## 十五、时间戳、首帧和 GOP Cache

### 1. RTMP 时间戳

RTMP timestamp 单位是毫秒。

视频：

* message timestamp 通常按 DTS。
* CompositionTime 补充 PTS 偏移。

音频：

* 通常按音频采样推进。
* AAC-LC 每帧 1024 samples。

例如 44.1kHz AAC：

```text
一帧时长 = 1024 / 44100 * 1000 ≈ 23.22ms
```

### 2. 时间戳常见问题

常见 bug：

* 时间戳回退。
* 音频和视频起始时间不对齐。
* B 帧 CompositionTime 写错。
* 毫秒、微秒、90kHz 时钟混用。
* 32 位时间戳回绕没有处理。

面试排障：

> 播放卡顿、音画不同步、首帧慢、拖动异常，都可能和时间戳处理有关。排查时要分别看编码时间戳、封装时间戳和播放器渲染时间戳。

### 3. GOP Cache

直播服务端通常维护 GOP Cache。

GOP Cache 一般包含：

* metadata。
* AAC sequence header。
* AVC sequence header。
* 最近一个 IDR 开始到当前的音视频包。

新观众进来时：

* 先发 metadata。
* 再发音视频 sequence header。
* 再从最近 IDR 开始发 GOP。

为什么？

* 没有 SPS/PPS，H.264 解码器不能初始化。
* 从 P 帧开始，解码依赖缺失，会花屏或黑屏。
* GOP cache 可以降低首屏等待时间。

---

## 十六、RTMP 服务端设计要点

一个简化 RTMP 服务端可以分层：

```text
TCP 网络层
  -> RTMP handshake
  -> RTMP chunk parser
  -> RTMP message dispatcher
  -> AMF command handler
  -> stream/session manager
  -> media cache / GOP cache
  -> publisher/subscriber fanout
```

### 1. 网络层

需要处理：

* 非阻塞 socket。
* epoll / kqueue。
* 输入缓冲区。
* 输出缓冲区。
* 连接超时。
* 慢连接。
* 断线清理。

### 2. Chunk Parser

解析时要维护每个 csid 的上下文：

* 上一次 fmt=0/1/2 的 header。
* 当前 message 已收长度。
* message length。
* timestamp / timestamp delta。
* extended timestamp 状态。

因为 fmt=3 依赖历史上下文，所以不能把每个 chunk 当成独立包解析。

### 3. Command Handler

需要处理：

* connect。
* createStream。
* publish。
* play。
* deleteStream。
* closeStream。

还要处理：

* 鉴权。
* app/stream 解析。
* 重复推流策略。
* 推流断开后的订阅者处理。

### 4. Media Fanout

一条上行推流可能对应多个下行播放器。

关键问题：

* 慢播放器不能拖慢推流端。
* 每个播放器都有自己的发送缓冲。
* 缓冲超过阈值后要丢帧或断开。
* 音频、关键帧和 metadata 优先级更高。

### 5. 背压与慢客户端

如果某个客户端网络很差：

* 服务端写缓冲区会堆积。
* 延迟越来越高。
* 内存越来越大。

处理策略：

* 设置输出缓冲高水位。
* 丢弃过期视频帧。
* 保留关键帧和音频。
* 超过阈值断开连接。
* 对推流端做限速或断开异常发布者。

面试回答：

> 直播服务器不能让慢客户端拖垮全局。每个订阅者需要独立缓冲和高水位策略，超过阈值时丢弃非关键视频帧或直接断开。

---

## 十七、RTMP 延迟从哪里来？

RTMP 延迟通常来自：

* 编码器缓存。
* GOP 太大。
* B 帧重排序。
* RTMP/TCP 发送缓冲。
* 服务端队列。
* CDN 分发链路。
* 播放器缓冲。
* TCP 队头阻塞和重传。

降低 RTMP 延迟的手段：

* 减小 GOP，例如 1 到 2 秒。
* 禁用 B 帧。
* 编码器使用 low-latency preset。
* 减小发送端和播放器缓冲。
* 服务端不堆积过期帧。
* 弱网下主动丢帧或断开。
* 下行改用 HTTP-FLV 或 WebRTC。

注意：

> RTMP 可以做到较低延迟直播，但很难稳定做到 WebRTC 那种百毫秒级互动延迟，因为它基于 TCP 且协议本身缺少现代实时拥塞控制和媒体级反馈。

---

## 十八、RTMP 常见故障排查

### 1. 推流连接不上

排查：

* DNS 是否解析正确。
* TCP 1935 是否可达。
* 防火墙是否拦截。
* URL app/stream 是否正确。
* 鉴权 token 是否过期。
* 服务端是否支持该 vhost/app。

工具：

```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://host/live/test
ffprobe rtmp://host/live/test
tcpdump -i any port 1935
```

### 2. 握手失败

可能原因：

* 版本不是 0x03。
* C1/S1 长度不对。
* 简单/复杂握手兼容问题。
* TLS 和非 TLS 端口混用。

### 3. publish 失败

可能原因：

* app 不存在。
* 鉴权失败。
* stream name 被占用。
* 服务端不允许 publish。
* AMF 命令解析错误。

### 4. 播放黑屏

可能原因：

* 没有发送 AVC sequence header。
* SPS/PPS 缺失或格式错误。
* 从非 IDR 开始播放。
* Annex-B / AVCC 格式混用。
* 视频时间戳异常。

### 5. 有声音没画面

可能原因：

* H.264 sequence header 错误。
* 关键帧没发。
* Video Tag Header 写错。
* CodecID 不是 7。
* 解码器不支持 profile/level。

### 6. 有画面没声音

可能原因：

* AAC sequence header 缺失。
* ADTS header 没有去掉。
* AudioSpecificConfig 错。
* 采样率或声道数不匹配。
* SoundFormat 不是 10。

### 7. 音画不同步

可能原因：

* 音视频起始 timestamp 没归零。
* PTS/DTS 处理错误。
* CompositionTime 写错。
* 音频帧 duration 计算错误。
* 服务端转封装时改坏时间戳。

### 8. 延迟越来越大

可能原因：

* TCP 发送缓冲堆积。
* 服务端输出队列不丢帧。
* 播放器缓冲过大。
* 网络弱但仍持续发送高码率。
* CDN 节点缓存策略不合理。

---

## 十九、面试会问哪些 RTMP 问题？

### 1. 基础理解类

**RTMP 是什么？基于什么传输？**

答：

> RTMP 是实时消息协议，常用于直播推流，基于 TCP 长连接。它通过握手建立会话，再用 AMF 命令控制 publish/play，用 RTMP chunk 承载音视频和控制消息。

**RTMP 和 HTTP-FLV 有什么关系？**

答：

> 二者常承载相同的 FLV tag body。RTMP 是自定义协议和 chunk 传输，HTTP-FLV 是 HTTP 长连接持续输出 FLV Tag。服务端从 RTMP 转 HTTP-FLV 通常主要是拆 RTMP message 后重封装 FLV Tag，不需要重新编码。

**RTMP 为什么现在主要用于推流，不常用于浏览器播放？**

答：

> 浏览器 Flash 退场后不再原生支持 RTMP 播放。下行播放更常用 HTTP-FLV、HLS、DASH 或 WebRTC，但 RTMP 推流生态仍然成熟，OBS、FFmpeg、直播 SDK 都广泛支持。

### 2. 握手类

**RTMP 握手流程是什么？**

答：

> 客户端先发 C0+C1，服务端回 S0+S1+S2，客户端再发 C2。C0/S0 是版本，通常 0x03；C1/S1/S2/C2 都是 1536 字节握手块。

**简单握手和复杂握手区别？**

答：

> 简单握手主要是时间戳和随机数据回显，复杂握手涉及 digest 和 HMAC 校验，主要用于兼容 Adobe 早期生态。现代普通直播推流大多简单握手即可。

### 3. Chunk 解析类

**RTMP chunk header 有哪些部分？**

答：

> Basic Header、Message Header、可选 Extended Timestamp 和 Chunk Data。Basic Header 中有 fmt 和 csid；Message Header 根据 fmt 有 11、7、3、0 字节几种形式。

**fmt=0/1/2/3 分别表示什么？**

答：

* fmt=0：完整 header，包含 timestamp、message length、type id、message stream id。
* fmt=1：省略 message stream id，使用 timestamp delta。
* fmt=2：只有 timestamp delta。
* fmt=3：复用上一个 chunk stream 的 header 上下文。

**为什么解析 RTMP chunk 要维护 csid 上下文？**

答：

> 因为 fmt=1/2/3 都依赖同一个 chunk stream 上之前的 header 信息。尤其 fmt=3 完全不带 message header，如果不保存上下文就无法还原 message。

**Extended Timestamp 什么时候出现？**

答：

> 当 3 字节 timestamp 或 timestamp delta 为 0xFFFFFF 时，后面会追加 4 字节 Extended Timestamp。解析 fmt=3 时也要根据历史 header 判断是否需要读扩展时间戳。

### 4. 命令流程类

**RTMP 推流流程是什么？**

答：

> TCP 建连后做 RTMP 握手，然后 AMF connect，createStream，publish，服务端返回 NetStream.Publish.Start，之后发送 metadata、AAC/AVC sequence header 和音视频 message。

**connect、createStream、publish 分别干什么？**

答：

* connect：连接到某个 app，并交换客户端信息。
* createStream：创建一个逻辑媒体流，服务端返回 stream id。
* publish：声明要发布某个 stream name。

**RTMP 命令为什么要了解 AMF？**

答：

> RTMP command/data message 通常用 AMF0 编码。connect、publish、play、onStatus、onMetaData 这些命令和数据都需要按 AMF 解析和生成。

### 5. H.264/AAC 封装类

**RTMP 里 H.264 怎么封装？**

答：

> RTMP video message 使用类似 FLV Video Tag 的格式。H.264 的 CodecID 是 7，先发送 AVC sequence header，里面包含 SPS/PPS，然后发送 AVC NALU 数据。NALU 通常是 AVCC length-prefixed 格式，不是 Annex-B start code。

**AVC sequence header 里有什么？**

答：

> 它是 AVCDecoderConfigurationRecord，包含 profile、level、NALU length size、SPS 和 PPS，用于初始化解码器。

**Annex-B 和 AVCC 有什么区别？**

答：

> Annex-B 用 start code 分隔 NALU，常见于裸 H.264 码流；AVCC 用长度前缀描述 NALU，常见于 MP4/FLV/RTMP。推 RTMP 时通常要把 Annex-B 转成 AVCC。

**RTMP 里 AAC 怎么封装？**

答：

> Audio Message 中 SoundFormat=10 表示 AAC。先发送 AAC sequence header，也就是 AudioSpecificConfig；后续发送 AAC raw frame，不带 ADTS header。

**为什么从 RTMP 拉下来的 AAC 保存成文件可能播不了？**

答：

> 因为 RTMP 里的 AAC raw 没有 ADTS header。如果要保存成普通 .aac，需要根据 AudioSpecificConfig 给每帧补 ADTS header。

### 6. 时间戳类

**RTMP timestamp 单位是什么？**

答：

> 毫秒。

**RTMP 里 PTS/DTS 怎么处理？**

答：

> 视频 message timestamp 通常是 DTS，CompositionTime 表示 PTS-DTS。没有 B 帧时 CompositionTime 为 0，有 B 帧时需要正确写入偏移。

**音画不同步你会怎么排查？**

答：

* 看音视频 timestamp 是否同源。
* 看起始时间是否归零。
* 看 CompositionTime 是否正确。
* 看 AAC 帧 duration 是否按采样率计算。
* 看转封装时是否修改了时间戳。

### 7. 服务端工程类

**RTMP 服务端如何处理多个观众？**

答：

> 一路 publisher 对应多个 subscriber。服务端把 publisher 的 metadata、sequence header 和音视频包分发给每个 subscriber。每个 subscriber 应有独立发送缓冲，慢客户端需要丢帧或断开，不能阻塞推流和其他观众。

**GOP Cache 是什么？为什么需要？**

答：

> GOP Cache 保存最近一个关键帧开始的一组媒体包，通常还包括 metadata 和 sequence header。新观众进入时从最近 IDR 开始发，避免从 P 帧开始导致黑屏或花屏，也能降低首屏等待。

**慢客户端怎么处理？**

答：

> 设置输出缓冲高水位。超过阈值说明客户端消费能力不足，可以丢弃过期视频帧、只保留关键帧和音频，严重时断开连接，避免内存膨胀和延迟无限增加。

### 8. 协议对比类

**RTMP 和 WebRTC 有什么区别？**

答：

* RTMP 基于 TCP，适合传统直播推流。
* WebRTC 多基于 UDP，适合实时互动。
* RTMP 延迟通常秒级。
* WebRTC 可以做到百毫秒级。
* WebRTC 有 NACK、FEC、JitterBuffer、拥塞控制、NAT 穿透、安全传输等完整实时通信体系。

**RTMP 和 HLS 怎么选？**

答：

* RTMP 常用于主播推流。
* HLS 常用于大规模下行分发。
* HLS 基于 HTTP 和分片，CDN 友好但延迟高。
* RTMP 长连接推流实时性更好，但浏览器播放生态弱。

**RTMP 延迟高的根因是什么？**

答：

> RTMP 基于 TCP，弱网下会有队头阻塞和重传等待。同时编码器、GOP、服务端队列、播放器缓冲、CDN 分发都会增加延迟。

---

## 二十、如果你说“精通 RTMP”，需要掌握到什么程度？

“精通 RTMP”不是会用 FFmpeg 推一个地址，也不是知道 RTMP 基于 TCP。至少要达到下面几个层级。

### 1. 会用：工具和现象层

你应该能做到：

* 用 FFmpeg/OBS 推 RTMP。
* 用 SRS/Nginx-RTMP 搭服务。
* 知道 RTMP URL 结构。
* 知道 RTMP 常用于直播推流。
* 会看基本日志。

这只能算“用过 RTMP”。

### 2. 熟悉：协议流程层

你应该能清楚说出：

* RTMP handshake 流程。
* connect/createStream/publish/play 命令链路。
* RTMP Message 和 Chunk 的区别。
* 常见 Message Type ID。
* metadata、AAC sequence header、AVC sequence header 的作用。
* RTMP 和 FLV 的关系。

这可以说“熟悉 RTMP”。

### 3. 深入：封装与解析层

你应该能独立处理：

* RTMP chunk header 解析。
* fmt=0/1/2/3 上下文复用。
* Extended Timestamp。
* AMF0 编解码。
* H.264 Annex-B 与 AVCC 转换。
* AAC ADTS 与 AudioSpecificConfig 转换。
* PTS/DTS/CompositionTime。
* GOP Cache。

这可以说“深入理解 RTMP”。

### 4. 工程：服务端实现层

你应该能设计或维护：

* 基于非阻塞 IO 的 RTMP 服务端。
* 推流 session 和拉流 session 管理。
* publisher/subscriber 分发模型。
* 慢客户端背压策略。
* 鉴权和重复推流策略。
* 断线重连和资源释放。
* RTMP 转 HTTP-FLV/HLS。
* 延迟、首帧、卡顿、黑屏排查。

这时你可以在工程简历里写“熟悉 RTMP 服务端开发”。

### 5. 精通：协议、性能、排障、演进层

如果你要说“精通 RTMP”，最好能覆盖：

* 能手写一个最小 RTMP publisher 或 server。
* 能抓包分析完整 handshake、AMF 命令和 chunk。
* 能解释复杂握手、chunk stream、message stream 的差异。
* 能处理各种异常流：时间戳回退、缺 SPS/PPS、ADTS 混入、B 帧 CTTS 错误。
* 能做 RTMP 到 HTTP-FLV、HLS、WebRTC 网关设计。
* 能优化延迟和首屏。
* 能处理高并发下的 fanout、队列、内存和背压。
* 能解释 RTMP 的历史局限，并给出替代方案选型。

面试中“精通 RTMP”的安全表达：

> 我不只是用过 RTMP 推流，也实现和排查过 RTMP 协议链路。包括握手、AMF 命令、chunk 拆装包、H.264/AAC 在 RTMP/FLV 中的封装、GOP Cache、RTMP 转 HTTP-FLV/HLS，以及服务端慢客户端和延迟问题处理。

如果没有真的写过完整服务端，不建议直接说“精通”。可以说：

> 我熟悉 RTMP 推流链路和协议封装，能定位常见推流、黑屏、音画不同步、延迟堆积问题，也了解服务端实现中的 chunk parser、GOP cache 和慢客户端处理。

---

## 二十一、建议做的实战项目

想把 RTMP 学扎实，建议按这个顺序做：

### 1. 抓包分析

用 FFmpeg 推流到本地 SRS：

```bash
ffmpeg -re -i input.mp4 -c copy -f flv rtmp://127.0.0.1/live/test
```

抓包看：

* C0/C1/S0/S1/S2/C2。
* connect。
* createStream。
* publish。
* metadata。
* AAC sequence header。
* AVC sequence header。
* video/audio message。

### 2. 写一个 RTMP Chunk Parser

目标：

* 能从 TCP 字节流中解析 chunk。
* 能按 csid 维护上下文。
* 能重组完整 RTMP Message。
* 能识别 audio/video/command/data message。

### 3. 写一个最小 RTMP Publisher

目标：

* TCP connect。
* 简单握手。
* connect/createStream/publish。
* 发送 metadata。
* 发送 AVC/AAC sequence header。
* 发送 H.264/AAC 数据。

### 4. 写一个最小 RTMP Server

目标：

* 接收 FFmpeg 推流。
* 完成握手和命令响应。
* 解析音视频 message。
* 缓存 metadata 和 sequence header。
* 打印帧类型、时间戳、大小。

### 5. 做转封装

目标：

* RTMP -> FLV 文件。
* RTMP -> HTTP-FLV。
* RTMP -> HLS。

这一步能打通“协议 + 封装 + 时间戳 + 工程”的完整链路。

---

## 二十二、RTMP 复习 Checklist

面试前至少确认自己能回答：

* RTMP 基于 TCP 还是 UDP？
* RTMP 握手 C0/C1/S0/S1/S2/C2 分别是什么？
* RTMP Message 和 Chunk 有什么区别？
* chunk fmt=0/1/2/3 分别是什么？
* Extended Timestamp 什么时候出现？
* connect/createStream/publish/play 的顺序是什么？
* AMF0 在 RTMP 里做什么？
* H.264 SPS/PPS 在 RTMP 里放在哪里？
* AAC AudioSpecificConfig 在 RTMP 里放在哪里？
* Annex-B 和 AVCC 怎么转换？
* ADTS 和 AAC raw 怎么转换？
* CompositionTime 是什么？
* RTMP timestamp 单位是什么？
* GOP Cache 是什么？
* 新观众为什么不能直接从 P 帧开始播放？
* RTMP 为什么有延迟？
* 慢客户端怎么处理？
* RTMP 如何转 HTTP-FLV？
* RTMP 和 WebRTC/HLS/SRT 怎么选型？

---

## 二十三、最短答题模板

如果面试官问：“你对 RTMP 熟悉到什么程度？”

可以这样答：

> 我熟悉 RTMP 的完整推流链路。底层它基于 TCP，连接后先做 C0C1/S0S1S2/C2 握手，然后通过 AMF0 命令完成 connect、createStream、publish/play。媒体数据会封装成 RTMP audio/video message，再按 chunk size 切成 chunk 传输。H.264 在 RTMP 中通常是 FLV/AVCC 格式，需要先发 AVC sequence header，也就是 SPS/PPS；AAC 需要先发 AudioSpecificConfig，后续发不带 ADTS 的 AAC raw。服务端侧我关注 GOP cache、首帧、时间戳、慢客户端背压和 RTMP 转 HTTP-FLV/HLS 这些工程问题。

如果面试官继续追问：“那你觉得精通 RTMP 应该掌握什么？”

可以这样答：

> 我认为精通 RTMP 至少要能独立实现 chunk parser 和基本 publisher/server，能处理 AMF 命令、fmt 上下文、extended timestamp、H.264/AAC 封装转换和 PTS/DTS；工程上还要能设计推拉流 session、GOP cache、fanout、慢客户端处理，并能通过抓包和日志定位黑屏、无声、音画不同步、延迟堆积等问题。同时也要知道 RTMP 的局限，能和 HTTP-FLV、HLS、WebRTC、SRT 做合理选型。

