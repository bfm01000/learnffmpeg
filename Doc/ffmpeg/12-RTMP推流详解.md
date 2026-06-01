# 12 - RTMP 推流详解（面试导向 + 全景）

> 对应导读第 3.7 节"运输问题"。这是 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §六协议全景里 **RTMP 那一格的深挖**——专讲"主播端怎么把音视频推到服务器"。
> 前置：编码看 [11-H264与H265详解.md](./11-H264与H265详解.md) / [06-编码参数与码控.md](./06-编码参数与码控.md)，FLV 封装看 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5，协议大局看 [08](./08-网络协议与流媒体.md) §六。

---

## 一、面试速记卡（先背这个）

**一句话定义**：RTMP（Real-Time Messaging Protocol）是 Adobe 在 **TCP** 上定义的流媒体协议，**推流端的事实标准**——OBS / 手机直播 App 把音视频推到服务器走的就是它。

**关键点（爱考）**：

- **角色**：**推流**（主播 → 服务器的"第一公里"），不是分发给观众（那是 HLS/HTTP-FLV/WebRTC）。
- **底层 TCP（默认 1935 端口）**，长连接、低延迟（1-3 秒）、但有队头阻塞。
- **承载的音视频体 ≈ FLV Tag 的 body**——这是 RTMP 能和 HTTP-FLV "换壳不换肉"的原因。
- **建连四步**：握手 → `connect` → `createStream` → `publish`，之后才发音视频。
- **视频用 AVCC（长度前缀）打包，不是 Annex-B**（最常被搞错）。
- **首帧必须先发 sequence header**（视频 avcC=SPS/PPS、音频 AudioSpecificConfig），否则拉流端黑屏。

**推流链路一图**：

```
采集 → 编码(H264+AAC) → 打包成 FLV Tag 体 → RTMP(握手+Chunk 分块) → 服务器
       (06/11)            (05 §5.5)           (本篇 §六)
```

---

## 二、场景引入：点"开始直播"后，背后发生了什么

你在 OBS 里填好 `rtmp://live.example.com/app/streamKey`，点"开始直播"。这一瞬间：

1. OBS 抓屏/抓摄像头麦克风（采集）；
2. 用 x264 把画面编成 H.264、用 AAC 编声音（编码）；
3. 把每一帧打包成一小块"消息"；
4. 和服务器握手、登录、声明"我要推这一路流"；
5. 然后源源不断把音视频小块发过去。

几个直觉问题，这篇要回答：**为什么是 RTMP 而不是直接发文件？服务器怎么知道哪块是视频哪块是音频？为什么 Flash 都死了 RTMP 还在用？推流花屏/黑屏一般卡在哪一步？**

---

## 三、背景：RTMP 为什么存在，又为什么"半死不活"还在用

- **出身**：2000 年代 Adobe 为 Flash Player 设计，配合 Flash 做实时音视频。当年浏览器看视频几乎都靠 Flash + RTMP。
- **Flash 之死**：HTML5 `<video>` 普及 + 安全问题，浏览器 2020 年彻底干掉 Flash。**所以 RTMP 在"观众端/浏览器播放"这一侧死了**。
- **为什么推流端活下来**：推流是"主播 → 服务器"，不涉及浏览器；RTMP 生态极其成熟——所有 CDN、所有推流软件（OBS、FFmpeg、手机 SDK）都支持它，延迟也低。换掉它成本高、收益小。所以形成了今天的格局：**推流用 RTMP，分发再转成 HLS / HTTP-FLV / WebRTC**。

一句话：**RTMP 是"打不死的推流协议"——播放端早死了，推流端还是事实标准。**

---

## 四、大局观：RTMP 在直播体系里的位置

```
 [主播端]                      [流媒体服务器]                    [观众端]
 OBS/手机                      SRS / nginx-rtmp / CDN
   │                                │
   │  ── RTMP 推流 ──►              │  ── HLS(.m3u8+.ts) ──►       海量观众(CDN 分发)
   │   (第一公里,本篇)              │  ── HTTP-FLV ──►             低延迟观众(国内主力)
   │                                │  ── WebRTC ──►               互动/超低延迟
   │                                │  (服务器转封装/转协议)
```

记住这条产业链：**RTMP 只负责"推上去"那一段**；服务器收到后，因为 RTMP 内部就是 FLV Tag，转成 HTTP-FLV 几乎零成本（换壳），转 HLS 要重新切片，转 WebRTC 要换成 RTP。这一格在 [08](./08-网络协议与流媒体.md) §6.0 的分层全景里属于"应用层 - 推流"。

---

## 五、推流完整链路（端到端走一遍）

| 环节 | 做什么 | 看哪篇 |
|---|---|---|
| ① 采集 | 摄像头出 YUV、麦克风出 PCM | [02](./02-像素格式与内存布局.md) / [04](./04-音频PCM-采样-重采样.md) |
| ② 编码 | YUV→H.264、PCM→AAC；直播要低延迟（无 B 帧 + zerolatency + CBR + 短 GOP） | [06](./06-编码参数与码控.md) / [11](./11-H264与H265详解.md) |
| ③ 打包 | 把编码数据封成 **FLV Tag 体**（视频 AVCC、首帧 avcC；音频 AAC + AudioSpecificConfig） | [05](./05-H264-MP4-NALU.md) §5.5 |
| ④ 建连 | RTMP 握手 → connect → createStream → publish | 本篇 §六 |
| ⑤ 分块发送 | 把消息切成 Chunk，按时间戳交织音视频，发给服务器 | 本篇 §6.2 |

直播编码的关键约束（和点播不同）：**必须低延迟**——无 B 帧（否则 PTS≠DTS 引入重排序延迟）、`tune=zerolatency`、CBR 稳定码率、短 GOP（1-2 秒一个关键帧，方便观众随时切入）。详见 [06](./06-编码参数与码控.md) §6.3。

---

## 六、RTMP 协议原理（核心，面试重点）

### 6.1 握手（Handshake）

建立 TCP 连接后，先做一次三段握手（注意不是 TCP 三次握手，是 RTMP 自己的应用层握手）：

```
客户端                          服务器
  │ ── C0 + C1 ──────────────►  │   C0: 1 字节版本(=3)
  │                             │   C1: 1536 字节(4B 时间 + 4B 0 + 1528B 随机)
  │ ◄────────── S0 + S1 + S2 ── │   S0/S1 同上;S2 = 回显 C1
  │ ── C2 ───────────────────►  │   C2 = 回显 S1
  │      握手完成,开始通信       │
```

本质是双方交换随机数并回显，确认通道通畅、时钟基准对齐。（Flash 时代还有带数字签名的"复杂握手"，但理解简单握手即可。）

### 6.2 Chunk 分块：RTMP 的精髓

RTMP 不直接发整条消息，而是把每条 Message **切成一个个 Chunk** 发送，**默认 Chunk 大小 128 字节**（可用 Set Chunk Size 协商加大）。

**为什么要分块（面试爱问）**：

1. **多路复用**：音频、视频、控制命令共用一条 TCP 连接。如果发一大块视频时不分块，会把后面的音频/控制消息堵死。分块后可以交织发送。
2. **避免大消息阻塞小消息**：一帧关键帧可能几十 KB，切成小块后，急的小消息（如音频）能插空发。
3. **头部压缩**：Chunk header 有 4 种格式（fmt 0/1/2/3）——同一路流连续的块，时间戳/长度/类型大多不变，后续块用更短的 header（fmt 3 几乎无头），省带宽。

**Chunk Stream ID（CSID）**：标识这个块属于哪条"块流"，是多路复用的依据（音频、视频、控制各用不同 CSID）。

### 6.3 Message 类型

RTMP 传的消息分几类（按 message type id）：

| 类别 | 例子 | 作用 |
|---|---|---|
| **协议控制消息** | Set Chunk Size(1)、Ack(3)、Window Ack Size(5)、Set Peer Bandwidth(6) | 调协议参数、流控 |
| **命令消息（AMF）** | connect / createStream / publish / play | 建连、控制流（见 §6.5） |
| **数据消息（AMF）** | `@setDataFrame` onMetaData | 元信息（宽高、码率、编码） |
| **音频消息(8)** | AAC 数据 | 音频帧 |
| **视频消息(9)** | H.264 数据 | 视频帧 |

### 6.4 AMF：命令和元数据的编码格式

**AMF（Action Message Format）** 是 Adobe 的序列化格式（类似 JSON 的二进制版），分 AMF0（老，常用）和 AMF3（新，更省）。命令消息就是用 AMF 编码的——比如 `connect` 命令编码成：命令名 `"connect"` + 事务 ID + 命令对象（含 app 名等）+ 可选参数。理解到"AMF 是 RTMP 控制消息的序列化方式"即可。

### 6.5 命令消息流程：从登录到开推

推流的标准时序（握手之后）：

```
客户端                                服务器
  │ ── connect("app名") ───────────►   │  声明要连哪个应用
  │ ◄──────────── _result ──────────   │  (含 Window Ack/Set Peer BW)
  │ ── createStream ───────────────►   │  申请一条消息流
  │ ◄──────────── _result(streamId)─   │  返回 stream id
  │ ── publish("streamKey","live") ─►  │  声明"我要推这一路"
  │ ◄────── onStatus(Publish.Start) ─  │  服务器准备好接收
  │ ── @setDataFrame(onMetaData) ───►   │  推元信息(宽高/码率/编码)
  │ ── 视频 sequence header(avcC) ──►   │  先发 SPS/PPS!
  │ ── 音频 sequence header ────────►   │  AudioSpecificConfig
  │ ── 音/视频数据消息(按时间戳交织)─►  │  源源不断推流
```

**面试收口**："RTMP 推流＝握手 → connect 连应用 → createStream 拿流 → publish 声明推流 → 先发音视频 sequence header（参数集）→ 再按时间戳交织发音视频数据。"

---

## 七、RTMP 和 FLV Tag 的关系（关键认知）

这是把 RTMP 和前面 FLV 串起来的一条：**RTMP 的音视频消息体，几乎就是 FLV Tag 去掉那层 tag header 后的 body**。

- FLV Tag = `[类型/大小/时间戳 头部] + [body]`；RTMP 把"类型/时间戳"放进了 **Chunk/Message header**，**body 部分和 FLV 完全一样**。
- 所以**视频消息体 = FLV VIDEODATA**：帧类型 + CodecID(7=AVC) + AVCPacketType(0=序列头 / 1=NALU) + 数据。**NALU 是 AVCC（长度前缀）格式，不是 Annex-B！**
- **第一个视频消息**：AVCPacketType=0，载 **AVCDecoderConfigurationRecord（avcC，即 SPS/PPS）**——等价 MP4 的 `avcC`、FLV 的 sequence header。
- **第一个音频消息**：载 **AudioSpecificConfig**（AAC 的"配置头"）。

这正是 [08](./08-网络协议与流媒体.md) §6.4 说的"RTMP 和 HTTP-FLV 内部 FLV Tag 二进制几乎一致、服务器换壳几乎不耗 CPU"的底层原因。FLV 结构本身见 [05](./05-H264-MP4-NALU.md) §5.5。

> ⚠️ 高频混淆点：**裸流 / RTP / Annex-B 用起始码 `00 00 01`；MP4 / FLV / RTMP 用 AVCC 长度前缀**。推 RTMP 时如果从 Annex-B 源拿数据，要先转成 AVCC（去起始码、加长度前缀、参数集进 sequence header）。AVCC↔Annex-B 见 [05](./05-H264-MP4-NALU.md) §四。

---

## 八、实战：用 ffmpeg / C API 推流

### 8.1 命令行（最快验证）

```bash
# 文件转直播推流(注意 -re: 按原始帧率推,否则会瞬间全推完冲垮服务器)
ffmpeg -re -i input.mp4 \
    -c:v libx264 -preset veryfast -tune zerolatency \
    -b:v 2500k -maxrate 2500k -bufsize 5000k -g 50 \
    -c:a aac -b:a 128k -ar 44100 \
    -f flv rtmp://live.example.com/app/streamKey
```

- **`-f flv`**：RTMP 推流的封装格式就是 **flv**（因为 RTMP 内部是 FLV Tag）。
- **`-re`**：按原速读，直播链路必加（实时采集源不需要，它本来就是实时的）。
- 码控参数对应 [06](./06-编码参数与码控.md) §6.3 的直播组合。

### 8.2 C API（avformat 路线，主流）

FFmpeg 的 `librtmp` / 内置 rtmp 协议 + flv muxer 把细节都封装了，你按"输出 flv 到 rtmp:// URL"写标准 muxing 流程即可：

```cpp
AVFormatContext* outputCtx = nullptr;
// 第三参 "flv" 指定封装格式,URL 用 rtmp://;avformat 会自动选 rtmp 协议
avformat_alloc_output_context2(&outputCtx, nullptr, "flv", "rtmp://live.example.com/app/key");

// ... 给 outputCtx 添加视频流(H264)、音频流(AAC),拷贝好 codecpar(含 extradata/avcC) ...

// rtmp 是网络输出,需要打开 IO(flv muxer 不是 NOFILE)
if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
    avio_open(&outputCtx->pb, outputCtx->url, AVIO_FLAG_WRITE);

avformat_write_header(outputCtx, nullptr);   // 这里完成 RTMP 握手 + connect/publish

// 主循环:把编码好的 AVPacket 按时间戳交织写出
//   关键:packet 的 pts/dts 要用输出流的 time_base、且单调;用 interleaved 自动按 dts 交织音视频
while (haveMorePackets) {
    av_packet_rescale_ts(packet, inputTimeBase, outputStream->time_base);
    av_interleaved_write_frame(outputCtx, packet);
}

av_write_trailer(outputCtx);
avio_closep(&outputCtx->pb);
avformat_free_context(outputCtx);
```

要点：

- 输出格式写 **`"flv"`**，URL 给 `rtmp://`——FFmpeg 自动走 RTMP 协议并完成握手/connect/publish，你不用手撸协议。
- **flv muxer 会自动把 H.264 转成 AVCC、生成 sequence header**，但前提是你的视频流 `codecpar->extradata` 里有正确的 avcC（从解码/编码侧拿）。
- **时间戳**：`av_interleaved_write_frame` 按 DTS 自动交织音视频；DTS 必须单调递增、用输出流的 `time_base`，否则花屏/卡顿（见 §十）。

### 8.3 librtmp（更底层，了解即可）

`librtmp` 是独立的 RTMP 库，直接暴露 `RTMP_Connect` / `RTMP_SendPacket` 等，要自己拼 FLV Tag、管时间戳。除非要做极致定制（如自研推流 SDK），否则用 FFmpeg 的 avformat 封装更省心。

---

## 九、关键技术点（建立直觉）

- **为什么 RTMP 延迟低**：长连接 + 没有切片缓冲（不像 HLS 要攒几个 `.ts`）。来一帧发一帧。
- **为什么走 TCP**：要可靠（推流丢数据＝主播端花屏，不可接受）；代价是弱网下有队头阻塞、延迟会涨。这也是为什么超低延迟/弱网场景开始转 SRT（可靠 UDP）/ WebRTC。
- **为什么浏览器播不了 RTMP**：依赖 Flash，Flash 已死。观众端要用 HTTP-FLV（flv.js）/ HLS。
- **RTMPS**：RTMP over TLS，加密版，走 443 也能穿透更严的防火墙。

---

## 十、常见坑与误区（必看）

| 症状 / 误区 | 根因 | 怎么修 |
|---|---|---|
| 推流端口连不上 | 1935 偏门端口被企业/校园防火墙封 | 换 RTMPS(443) 或在能出网的环境推 |
| 文件推流瞬间结束/服务器卡死 | 忘了 `-re`，FFmpeg 以最快速度把整个文件推完 | 文件源加 `-re` 按原速推 |
| 拉流端**黑屏但有声音 / 首帧花屏** | 没先发**视频 sequence header(avcC/SPS/PPS)** | publish 后先发 sequence header 再发数据帧 |
| 画面花屏、忽快忽慢、音画不同步 | **时间戳(DTS)不单调或没对齐**、音视频没按时间戳交织 | DTS 单调 + 用输出 time_base + `av_interleaved_write_frame` |
| 误以为 RTMP 视频是 Annex-B | RTMP/FLV 用 **AVCC 长度前缀**，不是起始码 | 从 Annex-B 源推流要先转 AVCC（见 [05](./05-H264-MP4-NALU.md) §四） |
| 推了 B 帧导致延迟高/兼容差 | 直播不该用 B 帧（PTS≠DTS 引入重排序延迟） | 编码端无 B 帧 + zerolatency（[06](./06-编码参数与码控.md)） |
| 码率忽高忽低冲垮带宽 | 用了 CRF/VBR 导致瞬时码率尖峰 | 直播用 CBR + maxrate + bufsize（[06](./06-编码参数与码控.md) §6.3 VBV） |
| 推 H.265 拉流端放不出 | RTMP/FLV 对 HEVC 支持是非标扩展，很多服务器/播放器不认 | H.265 直播优先走别的容器/协议，或确认全链路支持 |

---

## 十一、行业现状 / 生态

- **推流软件**：OBS Studio（最广）、FFmpeg、各厂手机直播 SDK。
- **流媒体服务器**：SRS（国产、流行）、nginx-rtmp-module、Wowza、各云厂商直播服务。
- **现状与趋势**：**推流仍以 RTMP 为主**；但低延迟/弱网场景，**SRT（可靠 UDP）和 WebRTC 推流**在抬头。分发端 RTMP 早已退给 HTTP-FLV / HLS / WebRTC（见 [08](./08-网络协议与流媒体.md) §六）。

---

## 十二、面试常考题（带答题要点）

- **Q：讲一下 RTMP 推流的完整流程。**
  TCP 连接 → RTMP 握手(C0/C1/C2) → connect 连应用 → createStream 拿 stream id → publish 声明推流 → 先发 onMetaData + 音视频 sequence header(avcC/AudioSpecificConfig) → 按时间戳交织发音视频数据消息。

- **Q：RTMP 为什么要分 Chunk？**
  一条 TCP 连接上多路复用音频/视频/控制；把大消息切小避免堵死小消息；Chunk header 有 4 种格式做头部压缩省带宽。

- **Q：RTMP 和 FLV 什么关系？**
  RTMP 的音视频消息体≈FLV Tag 的 body（类型/时间戳挪到了 RTMP message header）。所以 RTMP 转 HTTP-FLV 几乎零成本——换壳不换肉。

- **Q：RTMP 里视频是 Annex-B 还是 AVCC？**
  **AVCC（长度前缀）**，SPS/PPS 在第一帧的 sequence header(avcC) 里——和裸流/RTP 的 Annex-B 起始码不同。

- **Q：为什么 RTMP 延迟比 HLS 低？**
  RTMP 长连接、来帧即发、无切片缓冲；HLS 要把流切成 `.ts` 小文件、播放器还要攒几个切片才起播。

- **Q：为什么浏览器不能直接播 RTMP？怎么办？**
  RTMP 依赖已停用的 Flash。观众端改用 HTTP-FLV（flv.js 解析喂 `<video>`）或 HLS。

- **Q：推流黑屏/花屏怎么排查？**
  先看有没有发 sequence header（黑屏常因没发 avcC）；再看时间戳是否单调、音视频是否按 DTS 交织；再看是否误用 Annex-B / B 帧。

- **Q：RTMP vs SRT vs WebRTC 推流？**
  RTMP：TCP、生态最成熟、1-3s、推流事实标准；SRT：可靠 UDP、抗丢包、几百 ms-1s、适合弱网回传；WebRTC：UDP+RTP、几百 ms、互动级但要 ICE/SFU，重。

---

## 十三、当前阶段定位 + 下一步

- **这篇在协议全景里的位置**：[08](./08-网络协议与流媒体.md) §六"推流"那一格的深挖。读完你应该能完整讲清"主播点开始直播 → 服务器收到"这条链路，以及 RTMP 协议握手/Chunk/命令/和 FLV 的关系。
- **它和你其它笔记的衔接**：上游接编码（[06](./06-编码参数与码控.md)/[11](./11-H264与H265详解.md)）和封装（[05](./05-H264-MP4-NALU.md)），下游接分发（[08](./08-网络协议与流媒体.md)）。RTMP 不是孤立的，是这条链上的"运输"环。
- **下一步**：带着这条推流链路，回到核心待办——**动手写一条最简管线**（先播放器 demux→decode→渲染，再考虑推流器 encode→mux flv→rtmp）。把"读过"变成"骨头里的"，比再读协议管用十倍（见 [99-学习进度.md](./99-学习进度.md)）。

---

## 十四、自检

- 点"开始直播"后，从采集到服务器收到，完整链路有哪几环？RTMP 负责哪一环？
- RTMP 握手 C0/C1/C2 各是什么？为什么需要握手？
- 为什么 RTMP 要把消息切成 Chunk？分块带来哪三个好处？
- 推流建连的命令顺序是什么？（connect → createStream → publish）publish 之后第一件事该发什么？
- RTMP 的音视频消息体和 FLV Tag 是什么关系？这解释了什么工程现象？
- RTMP 里的 H.264 是 AVCC 还是 Annex-B？从 Annex-B 源推流要做什么转换？
- 用 FFmpeg C API 推流，输出格式应该填什么？时间戳要注意什么？
- 拉流端黑屏但有声音，最可能是哪一步出了问题？
- 为什么 RTMP 延迟比 HLS 低、却又被认为"该被淘汰"？现在推流端为什么还在用它？
- RTMP / SRT / WebRTC 推流各自的延迟量级和适用场景？
