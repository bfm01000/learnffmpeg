# RTMP 直播协议深入理解与面试指南

这篇文档面向 C++ 音视频开发面试，目标不是背协议字段，而是把 RTMP 放到完整直播链路里理解：

- RTMP 到底解决什么问题
- 它和 TCP、HTTP、FLV、H.264、AAC 是什么关系
- 推流时一次连接里发生了什么
- FFmpeg / 移动端编码器如何把音视频包送进 RTMP
- 面试中怎么从协议讲到低延迟、弱网、卡顿和工程优化

先纠正一个非常常见的误区：**RTMP 不是基于 HTTP 的协议，它是基于 TCP 的应用层长连接协议**。默认端口是 `1935`。如果走 TLS 加密，叫 `RTMPS`；如果为了穿透 HTTP 代理，把 RTMP 包裹进 HTTP 请求里，叫 `RTMPT`。但传统直播推流里说的 RTMP，核心是 `RTMP over TCP`。

---

## 1. 一句话讲清 RTMP

**RTMP（Real-Time Messaging Protocol）是 Adobe 设计的实时消息传输协议，通常基于 TCP 长连接，把音频、视频、控制命令、元数据切成一个个 Chunk，持续发送到流媒体服务器。**

在直播系统里，RTMP 最常见的位置是：

```text
主播端
  摄像头 / 麦克风
      |
      v
  采集 -> 处理 -> 编码 H.264/AAC
      |
      v
  FLV 封装 / RTMP 推流
      |
      v
  CDN / SRS / nginx-rtmp / 云直播
      |
      v
  HTTP-FLV / HLS / WebRTC / RTMP 播放
      |
      v
  观众端
```

现在浏览器已经不直接播放 RTMP，因为 Flash 淘汰了。但 **RTMP 在推流端仍然非常常见**，原因是生态成熟、CDN 支持好、OBS/FFmpeg/硬件编码器都支持，工程接入成本低。

面试时可以这样答：

> RTMP 是基于 TCP 的应用层长连接协议，主要用于直播推流。它不是 HTTP 协议，和 HLS 那种基于 HTTP 分片下载的方式不同。RTMP 建立连接后会持续发送音视频消息，消息会被切成 Chunk 传输，音视频负载通常是 FLV Tag 语义里的 H.264/AAC 数据。它的优点是推流生态成熟、延迟比 HLS 低；缺点是基于 TCP，弱网下会受到重传和队头阻塞影响，浏览器端也不再原生支持 RTMP 播放。

---

## 2. RTMP 和这些概念的关系

很多人学 RTMP 会混，是因为 RTMP、FLV、H.264、AAC、TCP、HTTP 同时出现。可以用这张图建立边界：

```text
H.264 / H.265 / AAC
  编码格式，回答“音视频压缩后长什么样”

FLV
  封装语义，回答“音视频包、时间戳、元数据怎么组织”

RTMP
  传输协议，回答“这些音视频消息怎么通过网络长连接发出去”

TCP
  传输层协议，回答“字节流如何可靠、有序地从 A 到 B”
```

典型 RTMP 推流负载是：

```text
H.264 NALU / AAC frame
      |
      v
FLV Video Tag / Audio Tag
      |
      v
RTMP Message
      |
      v
RTMP Chunk
      |
      v
TCP byte stream
```

注意几个关键点：

- RTMP 本身不是编解码协议，它不规定视频必须怎么压缩。
- 实际直播中 RTMP 最常承载 `H.264 + AAC`。
- RTMP 和 FLV 关系非常近，很多服务器会把 RTMP 流直接转成 HTTP-FLV，因为两者的音视频 Tag 语义高度一致。
- RTMP 基于 TCP，所以可靠、有序，但弱网下会有队头阻塞。

---

## 3. RTMP 推流 URL 怎么看

一个常见推流地址：

```text
rtmp://live.example.com/live/stream_123?token=xxx
```

可以拆成：

```text
scheme: rtmp
host:   live.example.com
port:   1935，没写时默认
app:    live
stream: stream_123
query:  token=xxx
```

对服务器来说，一般会拆成两个概念：

- `tcUrl`：连接到哪个应用，例如 `rtmp://live.example.com/live`
- `streamName`：发布哪一路流，例如 `stream_123?token=xxx`

鉴权通常不在 RTMP 协议核心里，而是在 URL query、签名参数、服务器回源逻辑或业务网关里实现。

---

## 4. 一次 RTMP 推流发生了什么

从客户端连接服务器到开始发送音视频，大致经历 4 个阶段：

```text
TCP 三次握手
    |
    v
RTMP 握手 C0/C1/C2 <-> S0/S1/S2
    |
    v
命令交互 connect -> createStream -> publish
    |
    v
发送 metadata / sequence header / 音视频数据
```

### 4.1 TCP 连接

客户端先和服务器建立 TCP 连接。默认端口是 `1935`：

```text
client  -- SYN -->  server
client  <-- SYN/ACK -- server
client  -- ACK -->  server
```

这一步之后，RTMP 拿到的是一条可靠、有序的 TCP 字节流。

### 4.2 RTMP 握手

RTMP 握手不是 HTTP 请求，而是二进制握手：

```text
client -> server: C0 + C1
server -> client: S0 + S1 + S2
client -> server: C2
```

含义可以这样理解：

- `C0/S0`：版本号，通常是 `3`
- `C1/S1`：1536 字节，包含时间戳、随机数等
- `C2/S2`：对对方握手数据的响应

面试不需要死背每个字节，但要知道：**RTMP 在真正发命令和音视频前，会先做自己的协议握手**。

### 4.3 命令交互

握手完成后，客户端通过 AMF 编码的命令消息和服务器交互。典型推流命令顺序：

```text
client -> server: connect(app, tcUrl, flashVer...)
server -> client: _result(NetConnection.Connect.Success)

client -> server: createStream()
server -> client: _result(streamId)

client -> server: publish(streamName, "live")
server -> client: onStatus(NetStream.Publish.Start)
```

这一步可以理解为：

- `connect`：我要连接哪个应用
- `createStream`：给我创建一路媒体流
- `publish`：我要开始推这个 streamName

### 4.4 发送元数据和音视频

推流开始后，通常先发：

```text
onMetaData
    |
    v
AAC sequence header
    |
    v
AVC sequence header
    |
    v
AAC raw frame / AVC NALU
```

元数据里常见字段：

```text
width, height, framerate, videocodecid,
audiocodecid, audiosamplerate, audiosamplesize,
stereo, encoder
```

播放器或服务器需要 sequence header 来初始化解码器。例如：

- H.264 的 SPS/PPS 会放在 AVC sequence header 里
- AAC 的采样率、声道等会放在 AudioSpecificConfig 里

没有 sequence header，后面的裸音视频帧通常无法正确解码。

---

## 5. RTMP Message 和 Chunk

RTMP 的核心设计是：**应用层先组织 Message，再把大的 Message 切成多个 Chunk 发送**。

```text
RTMP Message
  type: audio / video / command / metadata ...
  timestamp
  message length
  stream id
  payload
        |
        | 按 chunk size 切分
        v
Chunk 1, Chunk 2, Chunk 3 ...
```

为什么要切 Chunk？

- 大视频帧可能很大，不能一直霸占连接。
- 音频包、控制消息、视频包可以更细粒度地交错发送。
- 减少头部重复，后续 Chunk 可以用更短的 header。

常见 RTMP Message Type：

```text
1   Set Chunk Size
3   Acknowledgement
4   User Control Message
5   Window Acknowledgement Size
6   Set Peer Bandwidth
8   Audio Message
9   Video Message
18  Data Message，AMF0，常用于 onMetaData
20  Command Message，AMF0，connect/createStream/publish
```

### 5.1 Chunk Size

默认 chunk size 通常是 `128` 字节。推流时服务器或客户端一般会通过 `Set Chunk Size` 调大，比如 `4096` 或更大。

chunk size 太小：

- Chunk header 开销变大
- 系统调用和解析成本增加

chunk size 太大：

- 大包占用连接更久
- 控制消息和音频消息插队能力变差

工程上一般让库或服务器处理，不会手写调太多，但面试时知道它影响传输粒度即可。

---

## 6. H.264 在 RTMP/FLV 里怎么放

RTMP 常用 FLV Video Tag 语义承载 H.264。一个视频 Tag 里有几个关键字段：

```text
FrameType
CodecID
AVCPacketType
CompositionTime
AVC payload
```

### 6.1 FrameType

```text
1 = keyframe，关键帧
2 = inter frame，非关键帧
```

H.264 IDR 帧通常标记为 keyframe。播放器首屏能不能快速出画面，很依赖是否尽快收到关键帧和 SPS/PPS。

### 6.2 AVCPacketType

```text
0 = AVC sequence header，里面放 SPS/PPS
1 = AVC NALU，普通视频帧
2 = AVC end of sequence，少见
```

也就是说，RTMP 推 H.264 时，一般不是简单把 Annex-B 的 `00 00 00 01` 起始码原样塞进去，而是按 FLV/AVC 格式组织：

```text
AVC sequence header:
  AVCDecoderConfigurationRecord
  包含 SPS/PPS

AVC NALU:
  NALU length + NALU data
  NALU length + NALU data
```

### 6.3 CompositionTime 和 B 帧

FLV Video Tag 里有 `CompositionTime`，它表示：

```text
CompositionTime = PTS - DTS
```

如果没有 B 帧，`PTS == DTS`，CompositionTime 通常是 `0`。

如果有 B 帧，显示顺序和解码顺序不同，CompositionTime 就非常关键。直播低延迟场景通常会关闭 B 帧，因为 B 帧会增加编码器 reorder 延迟，也会让时间戳处理更复杂。

面试可以这样讲：

> RTMP/FLV 里视频消息的 timestamp 更接近解码时间 DTS，而 PTS 和 DTS 的差值通过 FLV Video Tag 的 CompositionTime 表示。低延迟直播里一般禁用 B 帧，这样 PTS/DTS 基本一致，链路延迟和时间戳处理都会简单很多。

---

## 7. AAC 在 RTMP/FLV 里怎么放

AAC 音频 Tag 也分两类：

```text
AACPacketType = 0: AAC sequence header
AACPacketType = 1: AAC raw frame
```

AAC sequence header 里放的是 `AudioSpecificConfig`，它描述：

- AAC profile，例如 AAC-LC
- sample rate，例如 44100 / 48000
- channel count，例如 1 / 2

后续 AAC raw frame 通常是不带 ADTS 头的 AAC 数据。

常见坑：

- 采集侧或编码器输出的是带 ADTS 头的 AAC，需要在封装 FLV/RTMP 前处理。
- 服务器或播放器没收到 AAC sequence header，会不知道采样率和声道，导致无法解码或声音异常。
- 音频时间戳要按采样数推进，例如 48kHz 下 1024 samples 的 AAC 帧时长约 `21.333ms`。

---

## 8. H.265 能不能走 RTMP

传统 RTMP/FLV 生态最标准、兼容性最好的是 `H.264 + AAC`。H.265/HEVC 并不是早期 FLV 官方规范里的标准组合，后来业界有一些扩展方案，例如 Enhanced RTMP / Enhanced FLV，用新的 CodecID 或扩展头承载 H.265。

面试里可以这样回答：

> H.265 理论上可以通过扩展 FLV/RTMP 承载，但兼容性取决于 CDN、服务器和播放器是否都支持对应扩展。传统 RTMP 推流最稳妥的组合还是 H.264 + AAC。如果业务要上 H.265，要重点验证推流端封装、服务端转封装、录制、转码、播放端解码和回退策略。

工程判断：

- 面向最大兼容性：优先 `H.264 + AAC`。
- 面向 4K/高画质/省带宽：可以考虑 H.265，但要确认 CDN 和播放端全链路支持。
- 面向浏览器播放：通常还要考虑转 HLS/DASH/WebRTC，以及浏览器对 H.265 的支持差异。

---

## 9. 时间戳：直播链路的主线

RTMP timestamp 单位是毫秒。推流时音频和视频都必须使用单调递增的时间戳。

典型编码链路：

```text
采集时间 / 系统时钟
      |
      v
编码器输入 PTS
      |
      v
编码器输出 packet PTS/DTS
      |
      v
封装成 FLV/RTMP timestamp
      |
      v
服务器转发 / 录制 / 转协议
```

常见问题：

- 时间戳不递增：服务器拒包、播放器卡顿、画面倒退。
- 音视频时间戳基准不同：音画不同步。
- 暂停/重连后时间戳跳变：播放端卡住或延迟异常。
- B 帧导致 PTS/DTS 不一致：需要正确写 CompositionTime。

工程建议：

- 直播低延迟场景关闭 B 帧，让 `PTS == DTS`。
- 音频用采样数推时间戳，比系统时间更稳定。
- 视频用采集或编码输入时钟，不要用发送时刻代替采集时刻。
- 推流重连时重新建立流的时间基，避免沿用旧连接的大时间戳造成异常。

---

## 10. RTMP 为什么延迟比 HLS 低，又为什么不是超低延迟

RTMP 延迟通常可以做到 `1~3s`，比传统 HLS 的 `10~30s` 低很多。原因是：

- RTMP 是长连接，编码出一帧就可以发送一帧。
- HLS 要切片、生成 m3u8、播放器还要缓存多个 ts/fmp4 分片。
- RTMP 不需要等一个完整大分片生成。

但 RTMP 也不是超低延迟协议，因为：

- 基于 TCP，弱网丢包会触发重传和队头阻塞。
- 编码器有缓存，尤其 B 帧、lookahead、码控都会增加延迟。
- 服务器、CDN、播放器都有缓冲队列。
- 如果码率超过上行带宽，发送队列会越积越多，延迟持续增长。

所以 RTMP 的定位是：**传统直播推流低延迟，但不是实时音视频通话级低延迟**。如果目标是 300ms 以内互动，通常会考虑 WebRTC。

---

## 11. 弱网下 RTMP 的典型问题

RTMP 基于 TCP，最大优点是可靠，最大问题也来自可靠。

### 11.1 队头阻塞

TCP 必须保证字节流有序。如果某个 TCP segment 丢了，后面的数据即使已经到达内核，也不能交给应用层。直播表现就是：

```text
网络抖动 / 丢包
    |
    v
TCP 重传等待
    |
    v
RTMP 数据无法继续上交
    |
    v
播放器卡顿 / 推流端发送队列堆积
```

### 11.2 发送队列堆积

如果编码码率高于实际上行带宽：

```text
编码器持续产出 8Mbps
网络实际只能发送 4Mbps
    |
    v
发送队列越来越长
    |
    v
端到端延迟越来越大
    |
    v
内存上涨 / 卡顿 / 断流
```

这就是很多直播推流弱网优化的核心：**不是只保证不丢，而是要控制队列时长**。

### 11.3 工程优化手段

常见策略：

- 动态码率 ABR：根据上行带宽、发送队列、RTT、丢包/重传情况降低码率。
- 丢帧保实时：队列过长时优先丢非关键视频帧，保留音频和关键帧。
- 缩短 GOP：让弱网恢复后观众更快等到下一个关键帧。
- 禁用 B 帧：降低编码延迟和时间戳复杂度。
- 控制发送队列：设置最大缓存时长，例如超过 500ms 或 1s 就进入降码率/丢帧。
- 独立发送线程：编码线程不要被网络阻塞拖死。
- 关键帧策略：断线重连或网络恢复时主动请求/触发 IDR。

结合你的项目经历，可以这样组织：

> 我做 RTMP 推流优化时，不会只看编码 fps，而是把采集、渲染、编码、封装、网络发送队列都打点。RTMP 基于 TCP，弱网下如果码率超过上行能力，发送队列会堆积，延迟会越来越大。所以我会用异步发送队列隔离编码线程和网络线程，再根据队列时长、发送耗时、实际吞吐做动态码率调节，必要时丢弃非关键视频帧，保证直播实时性。

---

## 12. FFmpeg 推 RTMP 的核心流程

用 FFmpeg 推 RTMP，本质上通常是使用 `flv` muxer 写到一个 `rtmp://` URL。

```text
avformat_alloc_output_context2(..., "flv", rtmp_url)
      |
      v
创建 video/audio stream，填 codecpar 和 time_base
      |
      v
avio_open2(&pb, rtmp_url, AVIO_FLAG_WRITE, ...)
      |
      v
avformat_write_header()
      |
      v
循环写 AVPacket: av_interleaved_write_frame()
      |
      v
av_write_trailer()
```

关键点：

- RTMP 推流时 `AVFormatContext` 的封装格式通常是 `flv`。
- `avformat_write_header()` 阶段会建立 RTMP 连接并写必要头信息。
- `AVPacket` 的 `pts/dts/duration` 必须按 stream 的 `time_base` 正确设置。
- 推流是实时链路，写包可能阻塞，所以工程上常放到独立网络线程。
- 如果编码器输出的 H.264/AAC 格式和 FLV muxer 期望不一致，需要正确处理 extradata、SPS/PPS、AAC sequence header 等。

伪代码：

```cpp
AVFormatContext* oc = nullptr;
avformat_alloc_output_context2(&oc, nullptr, "flv", rtmp_url);

// add video/audio streams, copy codec parameters
// video: H.264, audio: AAC

avio_open2(&oc->pb, rtmp_url, AVIO_FLAG_WRITE, nullptr, nullptr);
avformat_write_header(oc, nullptr);

while (running) {
    AVPacket pkt = get_encoded_packet();
    // pkt.stream_index = video_index / audio_index
    // pkt.pts / pkt.dts / pkt.duration use stream time_base
    av_interleaved_write_frame(oc, &pkt);
}

av_write_trailer(oc);
```

面试追问经常不在 API 名字，而在这些细节：

- `pts/dts` 怎么设置？
- 编码器输出 Annex-B 还是 AVCC？
- SPS/PPS 什么时候发？
- 网络阻塞会不会卡住编码线程？
- 重连后时间戳怎么处理？
- 弱网时队列堆积怎么处理？

---

## 13. 移动端 RTMP 推流链路

移动端常见链路：

```text
Camera
  |
  v
SurfaceTexture / AHardwareBuffer
  |
  v
OpenGL / Metal 渲染、滤镜、水印、全景拼接
  |
  v
MediaCodec / VideoToolbox 硬编码 H.264
  |
  v
取出 SPS/PPS + encoded frame
  |
  v
FLV mux / RTMP sender
  |
  v
CDN
```

如果是 Android 硬编码，常见注意点：

- `MediaCodec` 可能通过 `csd-0/csd-1` 输出 SPS/PPS。
- 关键帧前需要保证服务端/播放器能拿到 SPS/PPS。
- `Surface` 输入编码器可以避免 CPU 拷贝，适合 4K 直播。
- 编码输出线程和 RTMP 发送线程要解耦，避免网络阻塞影响编码器 drain。
- 弱网时不要让编码器无限产出，应该通过码率调节、丢帧、队列上限控制延迟。

这部分非常适合和 4K RTMP 直播项目结合讲：

> 4K 直播瓶颈通常不只在 RTMP 协议本身，而是整条链路：GPU 渲染到编码器是否零拷贝、编码输出是否及时 drain、网络发送是否阻塞、队列是否可控、弱网时码率是否能降下来。RTMP 只是最后的传输层入口，但它基于 TCP 的特性决定了弱网下必须关注发送队列和端到端延迟。

---

## 14. RTMP、HTTP-FLV、HLS、WebRTC 对比

```text
RTMP
  主要用于推流，TCP 长连接，延迟低于 HLS，浏览器播放生态弱。

HTTP-FLV
  HTTP 长连接传 FLV Tag，适合播放端，延迟接近 RTMP，容易穿透 CDN/HTTP 基础设施。

HLS
  HTTP 分片，兼容性最好，延迟较高，适合大规模分发和移动端播放。

WebRTC
  UDP/SRTP，面向实时互动，延迟最低，但服务器架构、拥塞控制、NAT 穿透更复杂。
```

面试回答可以这样说：

> RTMP 现在更多用于主播推流到 CDN；播放端由于 Flash 淘汰，常见会转成 HTTP-FLV、HLS 或 WebRTC。HTTP-FLV 延迟低、走 HTTP 基础设施，HLS 兼容性强但延迟高，WebRTC 适合连麦和超低延迟互动。选型要看业务目标：普通秀场直播可以 RTMP 推流 + HTTP-FLV/HLS 播放；强互动就要 WebRTC。

---

## 15. 高频面试题

### Q1：RTMP 是基于 HTTP 吗？

不是。传统 RTMP 是基于 TCP 的应用层长连接协议，默认端口 `1935`。它有自己的握手、命令消息、Chunk 分片机制。和 HTTP 有关系的是 `RTMPT`，它是为了穿透 HTTP 代理把 RTMP 封装进 HTTP；还有播放侧常见的 HTTP-FLV/HLS，它们才是基于 HTTP 的直播分发协议。

### Q2：RTMP 为什么适合推流？

RTMP 是长连接，推流端编码出音视频包后可以持续发送，不需要像 HLS 那样等待切片生成；同时它的生态成熟，OBS、FFmpeg、CDN、SRS、nginx-rtmp 都支持。它的延迟通常比 HLS 低，工程接入成本也低，所以长期作为直播推流事实标准。

### Q3：RTMP 推流的连接流程是什么？

先建立 TCP 连接，然后 RTMP 握手 `C0/C1/C2` 和 `S0/S1/S2`，之后发送 AMF 命令：`connect` 连接应用、`createStream` 创建流、`publish` 发布流。服务器返回成功后，客户端开始发送 `onMetaData`、AAC/H.264 sequence header，以及后续音视频数据。

### Q4：RTMP 中 Chunk 和 Message 有什么区别？

Message 是 RTMP 应用层消息，比如音频消息、视频消息、命令消息、元数据消息；Chunk 是传输层面的切片。一个 Message 可能很大，会按 chunk size 切成多个 Chunk 在 TCP 字节流里发送。这样可以降低大消息长期占用连接的问题，也能减少重复头部开销。

### Q5：H.264 在 RTMP 里怎么封装？

RTMP 通常按 FLV Video Tag 语义承载 H.264。先发送 AVC sequence header，里面包含 SPS/PPS；后续发送 AVC NALU。普通视频帧里一般是 `NALU length + NALU data`，不是简单裸塞 Annex-B 起始码。关键帧会标记为 keyframe，播放器首屏依赖关键帧和 SPS/PPS。

### Q6：AAC 在 RTMP 里怎么封装？

AAC 会先发送 AAC sequence header，里面是 AudioSpecificConfig，用来描述 profile、采样率、声道数。后续发送 AAC raw frame。很多场景下编码器输出 ADTS AAC，需要封装 RTMP/FLV 前去掉 ADTS 头或转换成 muxer 期望的格式。

### Q7：RTMP 时间戳怎么处理？

RTMP timestamp 单位是毫秒，音视频时间戳要单调递增，并且使用统一基准。视频如果没有 B 帧，PTS/DTS 基本一致；如果有 B 帧，FLV 里要通过 CompositionTime 表示 `PTS - DTS`。直播低延迟一般关闭 B 帧，降低编码延迟和时间戳复杂度。

### Q8：RTMP 弱网为什么会卡？

因为 RTMP 基于 TCP。TCP 保证可靠有序，丢包后会重传，并发生队头阻塞。对于直播来说，如果网络带宽不足，发送队列会堆积，端到端延迟越来越大；如果等待重传，播放端会卡顿。优化方向是动态码率、队列控制、丢帧保实时、缩短 GOP、禁用 B 帧和独立网络发送线程。

### Q9：RTMP 和 HLS 的区别？

RTMP 是 TCP 长连接持续传输，延迟较低，常用于推流；HLS 是基于 HTTP 的切片协议，把直播切成 m3u8 + ts/fmp4 分片，兼容性强、适合大规模分发，但传统 HLS 延迟较高。现在常见架构是 RTMP 推流到服务器，再转 HLS 给观众播放。

### Q10：如果你设计一个 RTMP 推流 SDK，会关注什么？

我会把链路拆成采集、渲染处理、编码、封装、发送、状态反馈几个模块。核心关注点是：编码器和网络线程解耦；发送队列有时长上限；支持重连和时间戳重置；支持动态码率和关键帧请求；完整埋点采集 fps、编码耗时、队列长度、发送耗时、实际吞吐、丢帧数；同时保证 SPS/PPS、AAC sequence header、PTS/DTS 等封装细节正确。

### Q11：RTMP 能推 H.265 吗？

传统 RTMP/FLV 对 H.264 + AAC 支持最好，H.265 通常依赖 Enhanced RTMP/FLV 或厂商扩展。能不能推不只看推流端，还要看 CDN、服务器录制/转码、播放端是否全链路支持。如果面向通用直播平台，H.264 最稳；如果是自控链路或 4K 高码率场景，可以评估 H.265，但必须设计兼容性和回退方案。

---

## 16. 一套面试中的完整回答模板

如果面试官问：“你了解 RTMP 吗？讲一下。”

可以按这个顺序回答：

```text
1. 定义
RTMP 是基于 TCP 的应用层长连接协议，传统直播里主要用于推流，默认端口 1935。

2. 链路位置
主播端把 H.264/AAC 编码数据按 FLV Tag 语义封装，通过 RTMP 发到 CDN。

3. 建连流程
TCP 连接 -> RTMP C0/C1/C2/S0/S1/S2 握手 -> connect/createStream/publish -> 发送 metadata 和音视频。

4. 协议核心
RTMP 把音频、视频、命令、元数据组织成 Message，再按 chunk size 切成 Chunk 发送。

5. 音视频封装
H.264 要有 AVC sequence header 传 SPS/PPS，AAC 要有 sequence header 传 AudioSpecificConfig。

6. 时间戳
timestamp 是毫秒级，直播低延迟通常关闭 B 帧，让 PTS/DTS 简化；有 B 帧则要处理 CompositionTime。

7. 优缺点
优点是生态成熟、推流延迟低、CDN 支持好；缺点是基于 TCP，弱网下会队头阻塞，播放端浏览器生态弱。

8. 工程优化
实际做推流要关注异步发送队列、动态码率、弱网丢帧、GOP、重连、埋点和端到端延迟。
```

完整口述版：

> RTMP 是一个基于 TCP 的应用层长连接协议，不是 HTTP。它在直播里主要用于主播端推流，推流端一般把 H.264 视频和 AAC 音频按 FLV Tag 的语义封装，然后通过 RTMP 发到 CDN。一次推流会先建立 TCP 连接，再做 RTMP 的 C0/C1/C2、S0/S1/S2 握手，然后通过 AMF 命令执行 connect、createStream、publish，成功后开始发送 metadata、H.264 的 SPS/PPS sequence header、AAC sequence header 和后续音视频帧。协议内部会把音频、视频、命令等组织成 RTMP Message，再切成 Chunk 传输。RTMP 的优点是推流生态成熟、延迟比 HLS 低；缺点是基于 TCP，弱网丢包会重传并产生队头阻塞，所以工程上要做异步发送、动态码率、队列时长控制和必要的丢帧保实时。

---

## 17. 最容易踩的坑

- 把 RTMP 说成基于 HTTP：错误，RTMP 基于 TCP，RTMPT 才是 HTTP 隧道。
- 只知道 API，不知道 sequence header：H.264/AAC 初始化信息缺失会导致播放失败。
- 忽略时间戳：RTMP 推流问题很多都来自 PTS/DTS 不递增或音视频基准不一致。
- 默认 H.265 一定可用：传统 RTMP/FLV 对 H.265 不是最稳兼容路径，要确认全链路扩展支持。
- 弱网只想着“不丢包”：直播更关注实时性，发送队列无限堆积比主动丢帧更糟。
- 编码线程直接写网络：`av_interleaved_write_frame` 或 socket send 阻塞时会拖垮编码链路。
- 4K 推流只看编码性能：还要看 GPU 到编码器是否零拷贝、发送队列、码率和上行带宽是否匹配。
- 以为 RTMP 还能浏览器原生播放：现代浏览器不再支持 Flash RTMP，播放端通常转 HTTP-FLV/HLS/WebRTC。

---

## 18. 复习抓手

复习 RTMP 不要从字段表开始背，建议抓住 5 条主线：

```text
协议定位：RTMP 是 TCP 长连接推流协议，不是 HTTP。

连接流程：TCP -> RTMP handshake -> connect/createStream/publish。

数据模型：Message 承载音视频/命令/元数据，Chunk 负责切片传输。

音视频封装：H.264 SPS/PPS、AAC AudioSpecificConfig、PTS/DTS/CompositionTime。

工程优化：弱网、队列、ABR、丢帧、GOP、重连、线程解耦、埋点。
```

面试能讲到这五条，基本就不是“只会调 FFmpeg API”，而是能从协议理解到直播工程实践。
