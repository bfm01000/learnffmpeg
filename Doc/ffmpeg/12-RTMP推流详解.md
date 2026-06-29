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
       (06/11)            (05 §5.5)           (本篇 §七)
```

---

## 二、面试高频问答（口语化答题模板，开口就能用）

> 这一节是"张嘴就能说"的完整话术，练顺嘴用；文末 §十三 是同样问题的**关键词速记版**，临场背要点用。两节配合：这里练话，那里背词。

**Q1：讲一下 RTMP 推流的完整流程？**

> 嗯，RTMP 是跑在 TCP 上的，所以**第一步是建 TCP 连接**。连上之后它有一套**自己的应用层握手**，叫 C0/C1/C2，双方互发随机数再回显一下，确认通道通了、时钟基准对齐了。
>
> 握手完之后进入**命令交互**：先发 `connect`，告诉服务器"我要连哪个 app"；再发 `createStream`，申请一条消息流，服务器回一个 **message stream id** 给我；然后发 `publish`，带上推流码（streamKey），意思是"我要往这条流里推东西了"；服务器回个 `onStatus` 说准备好了。
>
> 接下来**真正发数据之前**，要先发三样：`onMetaData`（宽高、码率、编码这些元信息），然后是**视频的 sequence header**（也就是 avcC，里面是 SPS/PPS），还有**音频的 sequence header**（AudioSpecificConfig）。这一步特别关键，先发参数集，**不然拉流端解不出来会黑屏**。
>
> 最后才是源源不断地**按时间戳交织发音视频数据**。整个发送过程，每条消息都会被切成 Chunk 一块块发。
>
> 一句话收口：**握手 → connect 连应用 → createStream 拿流 → publish 声明推流 → 先发元信息和音视频参数集 → 再按时间戳交织发数据。**

**Q2：RTMP 为什么要分 Chunk？**

> 因为一条 TCP 连接上，音频、视频、控制命令是**混在一起跑**的。如果不分块，发一帧几十 KB 的关键帧时，就会把后面急着发的音频、控制消息全堵死——这就是队头阻塞。
>
> 所以 RTMP 把每条消息切成小块（默认 128 字节一块），这样大消息发一块、小消息插一块，能**交织着发**，谁也不饿死谁。
>
> 还有个附带好处：Chunk 的头有四种格式（fmt 0/1/2/3），同一路流连续的块，时间戳、长度、类型大多不变，后面的块就用更短的头，最省的 fmt 3 几乎没头——**等于做了头部压缩，省带宽**。

**Q3：RTMP 和 FLV 是什么关系？**

> 你可以这么理解：**RTMP 的音视频消息体，几乎就是 FLV Tag 扒掉那层 tag header 之后的 body**。FLV Tag 头里那些"类型、时间戳"信息，在 RTMP 里被挪到了 Chunk/Message 的头里，**真正的数据体两边是一模一样的**。
>
> 这就解释了一个工程现象：**RTMP 转 HTTP-FLV 几乎零成本**——服务器收到 RTMP，把 message header 那点信息拼回 FLV tag header，肉（body）原封不动，所以叫"换壳不换肉"，CPU 几乎不耗。转 HLS 才要重新切片，转 WebRTC 才要换成 RTP。

**Q4：RTMP 里的视频是 Annex-B 还是 AVCC？**

> 是 **AVCC**，也就是**长度前缀**格式——每个 NALU 前面是 4 字节的长度，不是起始码。
>
> 这点特别容易搞错：裸流、RTP 那种是 **Annex-B**，用 `00 00 00 01` 起始码分隔；而 MP4、FLV、RTMP 都是 **AVCC** 长度前缀。而且 SPS/PPS 不在数据帧里，是单独放在第一帧那个 **sequence header（avcC）** 里发的。
>
> 所以如果你的数据源是 Annex-B（比如编码器直接吐的裸流），推 RTMP 之前得先转 AVCC：去掉起始码、换成长度前缀，再把 SPS/PPS 抽出来塞进 sequence header。

**Q5：为什么推流必须先发 sequence header？**

> 因为 SPS/PPS 是解码器的"说明书"——分辨率、profile、level、参考帧配置全在里面。拉流端**没拿到这个就根本没法初始化解码器**，画面自然出不来。
>
> 表现就是：**有声音但画面黑屏**，或者首帧花屏。所以 publish 之后、发第一个数据帧之前，必须先把视频 avcC 和音频 AudioSpecificConfig 发过去。中途如果有人新进来拉流，服务器也会先把缓存的 sequence header 补给他。

**Q6：RTMP 为什么延迟低，为什么浏览器又播不了，为什么现在还在用？**

> 延迟低是因为它**长连接、来一帧发一帧、没有切片缓冲**——不像 HLS 要先攒几个 `.ts` 小文件才起播，那一攒就是好几秒。
>
> 浏览器播不了是因为 RTMP 当年是给 **Flash** 用的，Flash 2020 年被浏览器彻底干掉了，所以**播放端这一侧 RTMP 死了**。观众端现在用 HTTP-FLV（flv.js 解析喂给 `<video>`）或者 HLS。
>
> 但**推流端它活得好好的**——推流是主播到服务器，不碰浏览器；而且整个生态太成熟了，所有 CDN、所有推流软件都支持，延迟也够低，换掉它成本高收益小。所以格局就是：**推流用 RTMP，分发再转 HTTP-FLV / HLS / WebRTC。**

**Q7：RTMP、SRT、WebRTC 推流怎么选？**

> **RTMP**：跑 TCP，生态最成熟，延迟 1-3 秒，是推流的事实标准，绝大多数直播都用它。缺点是弱网下有队头阻塞，延迟会飙。
>
> **SRT**：基于可靠 UDP，自己做重传和拥塞控制，抗丢包强，延迟几百毫秒到 1 秒，**适合弱网、跨国回传**这种链路差的场景。
>
> **WebRTC**：UDP + RTP，延迟几百毫秒，是**互动级**的（连麦、超低延迟直播），但要 ICE 打洞、要 SFU 转发，整套架构重，运维复杂。
>
> 一句话：**稳妥选 RTMP，弱网回传选 SRT，要互动选 WebRTC。**

---

## 三、场景引入：点"开始直播"后，背后发生了什么

你在 OBS 里填好 `rtmp://live.example.com/app/streamKey`，点"开始直播"。这一瞬间：

1. OBS 抓屏/抓摄像头麦克风（采集）；
2. 用 x264 把画面编成 H.264、用 AAC 编声音（编码）；
3. 把每一帧打包成一小块"消息"；
4. 和服务器握手、登录、声明"我要推这一路流"；
5. 然后源源不断把音视频小块发过去。

几个直觉问题，这篇要回答：**为什么是 RTMP 而不是直接发文件？服务器怎么知道哪块是视频哪块是音频？为什么 Flash 都死了 RTMP 还在用？推流花屏/黑屏一般卡在哪一步？**

---

## 四、背景：RTMP 为什么存在，又为什么"半死不活"还在用

- **出身**：2000 年代 Adobe 为 Flash Player 设计，配合 Flash 做实时音视频。当年浏览器看视频几乎都靠 Flash + RTMP。
- **Flash 之死**：HTML5 `<video>` 普及 + 安全问题，浏览器 2020 年彻底干掉 Flash。**所以 RTMP 在"观众端/浏览器播放"这一侧死了**。
- **为什么推流端活下来**：推流是"主播 → 服务器"，不涉及浏览器；RTMP 生态极其成熟——所有 CDN、所有推流软件（OBS、FFmpeg、手机 SDK）都支持它，延迟也低。换掉它成本高、收益小。所以形成了今天的格局：**推流用 RTMP，分发再转成 HLS / HTTP-FLV / WebRTC**。

一句话：**RTMP 是"打不死的推流协议"——播放端早死了，推流端还是事实标准。**

---

## 五、大局观：RTMP 在直播体系里的位置

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

## 六、推流完整链路（端到端走一遍）


| 环节     | 做什么                                                                 | 看哪篇                                                  |
| ------ | ------------------------------------------------------------------- | ---------------------------------------------------- |
| ① 采集   | 摄像头出 YUV、麦克风出 PCM                                                   | [02](./02-像素格式与内存布局.md) / [04](./04-音频PCM-采样-重采样.md) |
| ② 编码   | YUV→H.264、PCM→AAC；直播要低延迟（无 B 帧 + zerolatency + CBR + 短 GOP）         | [06](./06-编码参数与码控.md) / [11](./11-H264与H265详解.md)    |
| ③ 打包   | 把编码数据封成 **FLV Tag 体**（视频 AVCC、首帧 avcC；音频 AAC + AudioSpecificConfig） | [05](./05-H264-MP4-NALU.md) §5.5                     |
| ④ 建连   | RTMP 握手 → connect → createStream → publish                          | 本篇 §七                                                |
| ⑤ 分块发送 | 把消息切成 Chunk，按时间戳交织音视频，发给服务器                                         | 本篇 §7.2                                              |


直播编码的关键约束（和点播不同）：**必须低延迟**——无 B 帧（否则 PTS≠DTS 引入重排序延迟）、`tune=zerolatency`、CBR 稳定码率、短 GOP（1-2 秒一个关键帧，方便观众随时切入）。详见 [06](./06-编码参数与码控.md) §6.3。

---

## 七、RTMP 协议原理（核心，面试重点）

### 7.1 握手（Handshake）

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

### 7.2 Chunk 分块：RTMP 的精髓

RTMP 不直接发整条消息，而是把每条 Message **切成一个个 Chunk** 发送，**默认 Chunk 大小 128 字节**（可用 Set Chunk Size 协商加大）。

**为什么要分块（面试爱问）**：

1. **多路复用**：音频、视频、控制命令共用一条 TCP 连接。如果发一大块视频时不分块，会把后面的音频/控制消息堵死。分块后可以交织发送。
2. **避免大消息阻塞小消息**：一帧关键帧可能几十 KB，切成小块后，急的小消息（如音频）能插空发。
3. **头部压缩**：Chunk header 有 4 种格式（fmt 0/1/2/3）——同一路流连续的块，时间戳/长度/类型大多不变，后续块用更短的 header（fmt 3 几乎无头），省带宽。

> **最关键的一句话：`chunk stream = 一组 chunk 的编号`。**
>
> `chunk` 是碎片，`message` 是完整消息，`chunk stream` 就是这些碎片的分组编号。接收端看到相同的 `csid`，就知道这些 chunk 该归到同一组上下文里，后续可以拼回完整 message，并复用前一次的 header 信息。

**Chunk Stream ID（CSID）**：标识这个块属于哪条"块流"，是多路复用的依据（音频、视频、控制各用不同 CSID）。

### 7.3 Message 类型

RTMP 传的消息分几类（按 message type id）：


| 类别            | 例子                                                                | 作用             |
| ------------- | ----------------------------------------------------------------- | -------------- |
| **协议控制消息**    | Set Chunk Size(1)、Ack(3)、Window Ack Size(5)、Set Peer Bandwidth(6) | 调协议参数、流控       |
| **命令消息（AMF）** | connect / createStream / publish / play                           | 建连、控制流（见 §7.5） |
| **数据消息（AMF）** | `@setDataFrame` onMetaData                                        | 元信息（宽高、码率、编码）  |
| **音频消息(8)**   | AAC 数据                                                            | 音频帧            |
| **视频消息(9)**   | H.264 数据                                                          | 视频帧            |


### 7.4 AMF：命令和元数据的编码格式

**AMF（Action Message Format）** 是 Adobe 的序列化格式（类似 JSON 的二进制版），分 AMF0（老，常用）和 AMF3（新，更省）。命令消息就是用 AMF 编码的——比如 `connect` 命令编码成：命令名 `"connect"` + 事务 ID + 命令对象（含 app 名等）+ 可选参数。理解到"AMF 是 RTMP 控制消息的序列化方式"即可（字节级编码见 §7.6 C 段）。

### 7.5 命令消息流程：从登录到开推

推流的标准时序（握手之后）：

```
客户端                                服务器
  │ ── connect("app名") ───────────►   │  声明要连哪个应用
  │ ◄──────────── _result ──────────   │  (含 Window Ack/Set Peer BW)
  │ ── createStream ───────────────►   │  申请一条消息流
  │ ◄────── _result(messageStreamId)─  │  返回 message stream id
  │ ── publish("streamKey","live") ─►  │  声明"我要推这一路"
  │ ◄────── onStatus(Publish.Start) ─  │  服务器准备好接收
  │ ── @setDataFrame(onMetaData) ───►   │  推元信息(宽高/码率/编码)
  │ ── 视频 sequence header(avcC) ──►   │  先发 SPS/PPS!
  │ ── 音频 sequence header ────────►   │  AudioSpecificConfig
  │ ── 音/视频数据消息(按时间戳交织)─►  │  源源不断推流
```

**面试收口**："RTMP 推流＝握手 → connect 连应用 → createStream 拿流 → publish 声明推流 → 先发音视频 sequence header（参数集）→ 再按时间戳交织发音视频数据。"

### 7.6 字节级解析：手撕一次推流的二进制流（精确到字节）

前面讲的是"流程"，这一节把流程**落到字节**——模拟从握手到推第一帧，服务器收到的字节到底长什么样、怎么一个个解出来。看懂这一节，前面所有概念会从"知道"变成"看得见"。

> 约定：下文 `XX` 都是十六进制；带省略号的随机/载荷部分用 `…` 表示。SPS/PPS 的具体字节是示例值，实际由编码器决定。

#### A. 握手字节（C0/C1/C2）

```
客户端                          服务器
  │ ── C0 + C1 ──────────────►  │   C0: 1 字节版本(=3)
  │                             │   C1: 1536 字节(4B 时间 + 4B 0 + 1528B 随机)
  │ ◄────────── S0 + S1 + S2 ── │   S0/S1 同上;S2 = 回显 C1
  │ ── C2 ───────────────────►  │   C2 = 回显 S1
  │      握手完成,开始通信       │
```

```
C0:  03
     └─ 1 字节版本号，固定 3

C1:  00 00 00 00   00 00 00 00   AB 12 7F … (共 1528 字节随机)
     └─ time(4B)   └─ zero(4B)   └─ random(1528B)
     合计 1 + 1536 = 1537 字节
```

- **C0** 就一个字节 `03`，声明 RTMP 版本 3。
- **C1** 1536 字节：前 4 字节是时间戳（简单握手填 0 也行）、再 4 字节填 0、剩下 1528 字节是随机数。
- **S0/S1** 和 C0/C1 同构；**S2 回显 C1**、**C2 回显 S1**。回显是为了证明"我确实收到了你的随机数"，确认双向通道都通。

#### B. 一个 Chunk 的字节结构

每个 Chunk 的字节布局：

```
┌──────────────┬─────────────────┬───────────────────┬───────────┐
│ Basic Header │ Message Header  │ Extended Timestamp│ Chunk Data│
│  (1~3 字节)   │ (0/3/7/11 字节) │  (0 或 4 字节)     │  (载荷)    │
└──────────────┴─────────────────┴───────────────────┴───────────┘
```

把 `Message Header` 展开后，更容易看清 `message type id` 在哪里：

```text
RTMP Chunk
┌──────────────┬─────────────────────────────────────────────┬───────────────────┬────────────┐
│ Basic Header │ Message Header                              │ Extended Timestamp│ Chunk Data │
│              │ timestamp / message length / message type id│                   │ message 的  │
│ fmt + csid   │ message stream id                           │                   │ 一段 payload│
└──────────────┴─────────────────────────────────────────────┴───────────────────┴────────────┘
```

所以要分清两层：

```text
Chunk Header 负责：这块属于哪条 chunk stream、怎么把块拼回 message。
Message Header 负责：这条 message 是音频、视频、命令还是 metadata。
Chunk Data 负责：真正的 FLV VideoTag / AudioTag / AMF0 数据。
```

**① Basic Header（这里 1 字节）**：高 2 位是 `fmt`，低 6 位是 `csid`。

```
 bit:  7 6 | 5 4 3 2 1 0
       fmt |   csid
```

- csid 在 2~63 → 1 字节搞定；csid=0 → 再补 1 字节（csid = 第二字节 + 64）；csid=1 → 再补 2 字节。
- 例：视频用 csid=6、fmt=0 → `00` 拼 `000110` = `**0x06**`。

**② Message Header 按 fmt 四种长度**：


| fmt | 长度  | 字段                                                                   | 用在哪              |
| --- | --- | -------------------------------------------------------------------- | ---------------- |
| 0   | 11B | timestamp(3) + length(3) + type id(1) + **message stream id(4，小端!)** | 一路流的第一个块 / 时间戳回绕 |
| 1   | 7B  | timestamp delta(3) + length(3) + type id(1)                          | 同流，长度变了          |
| 2   | 3B  | timestamp delta(3)                                                   | 同流，长度也没变         |
| 3   | 0B  | 无                                                                    | 全沿用上一块（含续块）      |


> ⚠️ **面试细节**：除了 message stream id 是**小端（little-endian）**，其余多字节字段都是**大端**。这是 RTMP 里唯一一个小端字段，很爱考。

这里还有一个容易漏掉的点：`**message type id` 只在 `fmt=0` 和 `fmt=1` 的 Message Header 里出现**。

```text
fmt=0：带完整 message 信息，包含 message type id 和 message stream id。
fmt=1：复用同一条 message stream id，但会带新的 message length 和 message type id。
fmt=2：只带 timestamp delta，不带 message type id。
fmt=3：连 Message Header 都没有，完全复用前面的上下文。
```

为什么 `fmt=2/3` 不带 `message type id` 也能解析？因为接收端会按 `csid` 保存这条 chunk stream 上一次的 message 上下文。后续块只要 `csid` 相同，就能复用之前的 `message type id`、message length、message stream id 等信息。

同理，`**message stream id` 只有 `fmt=0` 的完整 Message Header 里才出现**：

```text
csid：
  在 Basic Header 里，每个 chunk 都有。
  用来找这组 chunk 的上下文。

message stream id：
  在 Message Header 里，但只有 fmt=0 才携带。
  fmt=1/2/3 都通过 csid 找上下文，复用之前的 message stream id。
```

所以不要把 `csid` 和 `message stream id` 混成一个东西：

```text
csid = chunk 层的分组编号，用来拼 chunk、复用 header。
message stream id = RTMP message 层的业务流编号，表示这条消息属于哪一路 publish/play 流。
```

例如一条很大的视频 message 被拆成多个 chunk：

```text
第 1 个 chunk：fmt=0，csid=6，message type id=9，声明这是视频消息。
第 2 个 chunk：fmt=3，csid=6，没有 message header，沿用前面的 type id=9。
第 3 个 chunk：fmt=3，csid=6，继续沿用，直到 message length 收满。
```

也就是说：**chunk 是传输分片，message 才是业务语义。`message type id` 属于 message 这一层，不是每个 chunk 都重复携带。**

常见 `message type id`：


| type id | 含义                   | 常见内容                                                    |
| ------- | -------------------- | ------------------------------------------------------- |
| 1       | Set Chunk Size       | 协商 chunk 大小                                             |
| 8       | Audio Message        | FLV AudioTag body，例如 AAC sequence header / AAC raw      |
| 9       | Video Message        | FLV VideoTag body，例如 H.264 sequence header / H.264 NALU |
| 18      | AMF0 Data Message    | `@setDataFrame` / `onMetaData`                          |
| 20      | AMF0 Command Message | `connect` / `createStream` / `publish`                  |


**③ Extended Timestamp**：当 timestamp（或 delta）字段写满 `FF FF FF` 时，真实时间戳放到这 4 个额外字节里。

**完整解析一个视频 sequence header 的 fmt=0 块**（假设 body 长 0x33=51 字节，时间戳 0）：

```
06             Basic Header: fmt=0, csid=6
00 00 00       timestamp = 0
00 00 33       message length = 51
09             message type id = 9 (视频)
01 00 00 00    message stream id = 1  ← 小端！读成 0x00000001
[51 字节 body] ← 见 D 段
```

逐字节读完，服务器就知道："这是 **message stream id = 1** 上的一条 51 字节的视频消息，时间戳 0。" 然后开始读 body。

接收端继续看 body，才能知道这条视频消息到底是 sequence header 还是普通视频帧：

```text
RTMP Message Header:
  message type id = 9
  只能说明这是一条视频消息

Chunk Data / Message Payload:
  FLV VideoTag body
  第 1 字节看 CodecID
  第 2 字节看 AVCPacketType
```

对应关系：

```text
message type id = 18
  -> AMF0 Data Message
  -> payload 里解析 @setDataFrame / onMetaData

message type id = 9
  -> Video Message
  -> CodecID=7 表示 H.264/AVC
  -> AVCPacketType=0 表示视频 sequence header，payload 是 avcC
  -> AVCPacketType=1 表示普通 H.264 NALU 数据

message type id = 8
  -> Audio Message
  -> SoundFormat=10 表示 AAC
  -> AACPacketType=0 表示音频 sequence header，payload 是 AudioSpecificConfig
  -> AACPacketType=1 表示普通 AAC raw data
```

#### C. connect 命令的 AMF0 字节

命令消息是 type id 20（`0x14`），载荷用 AMF0 编码。AMF0 常用类型标记：


| 标记   | 类型         | 编码                                |
| ---- | ---------- | --------------------------------- |
| `00` | Number     | 8 字节 IEEE754 双精度（大端）              |
| `01` | Boolean    | 1 字节                              |
| `02` | String     | 2 字节长度 + UTF8                     |
| `03` | Object     | 一串 `键(裸String) 值`，以 `00 00 09` 结尾 |
| `05` | Null       | 无                                 |
| `09` | Object End | 配合 `00 00` 收尾                     |


`connect` 命令载荷逐字节：

```
02 00 07 63 6F 6E 6E 65 63 74          String "connect"  (长度7 + 'connect')
00 3F F0 00 00 00 00 00 00             Number 1.0        (transaction id；0x3FF0…=1.0)
03                                     Object 开始(命令对象)
   00 03 61 70 70                        键 "app"        (裸String: 长度3 + 'app')
   02 00 04 6C 69 76 65                  值 String "live"
   00 05 74 63 55 72 6C                  键 "tcUrl"      (长度5 + 'tcUrl')
   02 00 1C 72 74 6D 70 3A 2F 2F …       值 String "rtmp://…"(长度0x1C=28)
00 00 09                               Object 结束
```

- `63 6F 6E 6E 65 63 74` = `connect` 的 ASCII；`61 70 70` = `app`；`6C 69 76 65` = `live`。
- 事务 ID 用一个 double 表示，`connect` 固定是 1.0。服务器回的 `_result` 会带同一个事务 ID，客户端靠它对上号。
- 解到 `00 00 09` 就知道命令对象结束。`createStream` / `publish` 同理，只是命令名和参数不同（`publish` 还会带流名和 `"live"` 这种发布类型）。

#### D. 视频 sequence header（avcC）字节

C 段那条 51 字节视频 body，逐字节就是一个 **FLV VIDEODATA + AVCDecoderConfigurationRecord**：

```
17             FrameType=1(关键帧)<<4 | CodecID=7(AVC)  → 0x17
00             AVCPacketType=0  → 这是 sequence header(参数集)
00 00 00       CompositionTime = 0 (seq header 恒为 0)
── 下面是 avcC (AVCDecoderConfigurationRecord) ──
01             configurationVersion
64             AVCProfileIndication = 100 (High Profile)
00             profile_compatibility
1F             AVCLevelIndication = 31 (Level 3.1)
FF             111111 + 2位lengthSizeMinusOne(11=3) → NALU 长度前缀 4 字节
E1             111 + 5位 numOfSPS(00001=1) → 1 个 SPS
00 19          SPS 长度 = 25
67 64 00 1F …  SPS 数据 (0x67 = nal_ref_idc + type 7，正是 SPS)
01             numOfPPS = 1
00 05          PPS 长度 = 5
68 EE 3C 80 …  PPS 数据 (0x68 = type 8，正是 PPS)
```

- 头两字节 `17 00` 是"关键帧 + AVC + 序列头"的招牌组合，一眼就能认出这是 avcC。
- `FF` 的低 2 位决定了**后续每个 NALU 用几字节长度前缀**（这里是 4），E 段就靠它。
- SPS 起始字节 `0x67`、PPS 起始 `0x68`，对应 NALU header 的 type 7/8（NALU header 解析见 [05](./05-H264-MP4-NALU.md) §四）。

#### E. 一个普通视频数据帧（AVCC，不是 Annex-B！）

后续的真实画面帧 body：

```
27             FrameType=2(非关键帧)<<4 | CodecID=7  → 0x27   (关键帧则是 0x17)
01             AVCPacketType=1  → 这是 NALU 数据
00 00 00       CompositionTime(cts) = PTS-DTS；无 B 帧时为 0
00 00 02 5A    NALU 长度 = 0x025A = 602 字节  ← 4 字节大端长度前缀(AVCC!)
41 9A …        NALU 数据 (0x41 = 非IDR slice；关键帧这里是 0x65 = IDR)
[若一帧多个 NALU，继续: 长度(4B) + NALU …]
```

> **这就是 AVCC vs Annex-B 的字节级铁证**：这里是 `00 00 02 5A` 这种**长度**，**不是** `00 00 00 01` 起始码。从 Annex-B 源推流，必须把起始码换成长度前缀（见 §八末尾的提醒和 [05](./05-H264-MP4-NALU.md) §四）。

#### F. 音频字节（AAC，是裸 AAC 不是 ADTS）

音频 sequence header（先发一次）：

```
AF             SoundFormat=10(AAC)<<4 | Rate=3 | Size=1(16bit) | Type=1(立体声) → 0xAF
00             AACPacketType=0 → AudioSpecificConfig(参数头)
12 10          AudioSpecificConfig:
               0001 0=AOT 2(AAC LC) | 0100=freqIndex 4(44100) | 0010=声道 2 | 000
```

音频数据帧：

```
AF             同上招牌字节
01             AACPacketType=1 → 裸 AAC 数据
FF F1 …        裸 AAC 帧载荷
```

> **面试点**：RTMP/FLV 里的 AAC 是**裸帧（raw）**，**没有 ADTS 头**（那 7/9 字节同步头 `FF F1…` 在这里不该出现，配置信息已经在 AudioSpecificConfig 里给过了）。从带 ADTS 的源推流要先剥掉 ADTS 头。

#### G. 大消息分块 + fmt=3 续块

如果一条消息 body 超过 chunk size（默认 128），就要拆成多个 Chunk。比如一条 256 字节的视频消息：

```
06  [11字节 fmt=0 头]  [前 128 字节 body]     ← 第 1 块：完整头
C6  [后 128 字节 body]                         ← 第 2 块：续块
└─ Basic Header: fmt=3(11) + csid=6(000110) = 0xC6，没有 message header
```

- 续块用 **fmt=3**（`0xC6`），**只有 1 字节 Basic Header，没有 message header**——因为类型、长度、时间戳全沿用同一条消息，不用重发。这就是"头部压缩"省带宽的来源。
- 服务器按 csid 把这些块的 body 拼回完整消息（已知 length=256，收满 256 字节才算一条消息完成）。
- 想减少分块开销，推流端开头会发 **Set Chunk Size**（type 1）把 128 调大到 4096 甚至 60000，FFmpeg 默认会调。

#### 把它串起来：一次推流的字节时间线

```
1.  C0(03) C1(1536B)                 ── 握手发起
2.  收 S0 S1 S2，回 C2               ── 握手完成
3.  Set Chunk Size(type 1) 把 128→4096
4.  connect 命令(type 20, AMF0)       ── §C
5.  收 _result + Window Ack + Set Peer BW
6.  createStream(type 20)
7.  收 _result(message stream id=1)
8.  publish("streamKey","live")(type 20)
9.  收 onStatus(NetStream.Publish.Start)
10. onMetaData(type 18, @setDataFrame)
11. 视频 sequence header(type 9, body 17 00 …avcC)  ── §D  必须先发！
12. 音频 sequence header(type 8, body AF 00 …ASC)   ── §F
13. 视频/音频数据帧(type 9 / 8，按 DTS 交织，超长就 fmt=3 续块)  ── §E/§G
```

这条时间线就是 §7.5 那张时序图的"字节版"。抓包（Wireshark 选 RTMPT/RTMP 解析）能一字不差看到这些字节，对着上面逐段读就全通了。

---

## 八、FLV 与 RTMP 的关系（关键认知）

要理解 RTMP 的推流链路，必须先搞懂 FLV 是什么——因为 **RTMP 本质上就是把 FLV 的"肉"（Tag Body）拆出来，包上 RTMP 自己的协议头在网络上传输**。这一节先讲 FLV，再讲它和 RTMP 怎么串起来。

---

### 8.1 FLV 是什么

**FLV（Flash Video）** 是 Adobe 设计的一种**容器/封装格式**。它的作用和 MP4、MKV 一样——把编码好的音视频数据按一定规则"打包"，让播放器知道哪段是视频、哪段是音频、时间戳是多少、先播哪个。

一句话：**编码器负责把画面压成 H.264 码流，FLV 负责把这个码流"装箱"，标好标签，方便传输和播放。**

#### 为什么直播用 FLV 而不是 MP4

MP4 的文件结构决定了它**不适合流式传输**：

- MP4 把文件的索引信息（`moov` box，里面记录着每一帧在文件中的位置偏移）写在文件末尾。播放一个 MP4 文件，必须先跳到文件末尾读 `moov`，才知道去哪找每一帧。这在网络上意味着要么文件已完整下载（点播场景），要么用 `faststart` 把 `moov` 挪到开头——但这也只是"文件准备好之后"的优化。
- MP4 的 `mdat`（媒体数据区）不要求音视频按时间戳严格交替排列——可以连续写一大段视频再写音频，这对于流式传输来说意味着"要么全拿到、要么播不了"。

**FLV 的设计哲学和 MP4 完全相反——它是为流式传输而生的**：

- **没有全局索引**。FLV 不需要 `moov` 这种结构，每个 Tag 自带时间戳和类型标记，收到一个就能播一个。
- **音视频天然交织**。FLV 的 Tag 是按时间顺序逐个写进去的，天然就是"音频 Tag、视频 Tag、音频 Tag、视频 Tag……"交替排列，适合网络边收边播。
- **极简结构**。FLV 的文件头只有 9 字节，Tag 头只有 11 字节，解析成本极低——这对于性能敏感的直播服务器来说非常重要。

> 可以这样类比：MP4 像一个打包完整的快递箱，箱子打开里面有一张清单（moov）告诉你每件东西在哪，你需要先看清单才能取货。FLV 像一条传送带，一个个贴着标签的包裹按顺序从你面前经过，你不需要清单，来一个拆一个。

#### FLV 的字节结构

整个 FLV 文件由三部分组成：**文件头（9 字节）+ 若干 Tag + 文件尾**。每个 Tag 前面还有一个 4 字节的 `PreviousTagSize`，用来做后向索引。

```
FLV 文件布局：

┌─────────────────────────┐
│ FLV Header (9 字节)      │  文件头：签名 "FLV" + 版本 + 标志位 + 头大小
├─────────────────────────┤
│ PreviousTagSize (4 字节) │  第一个永远是 0（前面没有 Tag）
├─────────────────────────┤
│ Tag 1                   │  通常第一个是 Script Tag（onMetaData）
│  ├─ Tag Header (11 字节) │    类型(1B) + 数据大小(3B) + 时间戳(3B+1B扩展) + StreamID(3B)
│  └─ Tag Data (变长)      │    header 中标明的数据
├─────────────────────────┤
│ PreviousTagSize (4 字节) │  = Tag 1 的 header + data 总大小
├─────────────────────────┤
│ Tag 2                   │  视频 sequence header / 音频 AudioSpecificConfig
├─────────────────────────┤
│ PreviousTagSize (4 字节) │
├─────────────────────────┤
│ Tag 3 ...               │  音视频数据 Tag 交替排列
├─────────────────────────┤
│ PreviousTagSize (4 字节) │
└─────────────────────────┘
```

**① FLV Header（9 字节）**

```
46 4C 56         签名 "FLV" (ASCII: F=0x46, L=0x4C, V=0x56)
01               版本 = 1
05               标志位: 00000101
                   bit 0: 有视频 → 1
                   bit 2: 有音频 → 1
                   bit 1 和 bit 3~7: 保留，填 0
                   → 0x05 = 有视频 + 有音频
00 00 00 09      头大小 = 9 (从文件开头到第一个 Tag 的偏移)
```

9 字节之后，就是第一个 `PreviousTagSize`，紧接着第一个 Tag。

**② Tag Header（11 字节）**

每个 Tag 的头部固定 11 字节：

```
┌──────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ TagType(1B)  │ DataSize(3B) │ Timestamp(3B)│TimestampExt │ StreamID(3B) │
│              │              │              │   (1B)       │              │
└──────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
```

- **TagType（1 字节）**：`08` = 音频，`09` = 视频，`12` = Script Data（元数据，如 onMetaData）
- **DataSize（3 字节，大端）**：后面 Tag Data 的字节数（不含 11 字节 Tag Header），最大约 16MB
- **Timestamp（3 字节，大端）**：时间戳的低 24 位，单位毫秒
- **TimestampExtended（1 字节）**：时间戳的高 8 位，合在一起是完整的 32 位时间戳
- **StreamID（3 字节）**：始终为 0（FLV 规范保留字段）

> 时间戳字段之所以拆成 3+1 字节，是历史遗留设计——早期版本只支持 24 位（约 4.6 小时），后来加了 1 字节扩展位。

**③ Tag Data：视频、音频、脚本三种**

三种 Tag 的 data 部分结构不同：

**Script Tag（TagType=0x12）**：内容是一段 AMF0/AMF3 编码的键值对，通常第一个 Script Tag 就是 `onMetaData`，里面存着视频的宽度、高度、帧率、码率、编码格式等信息，播放器/服务器靠它做初始化和能力判断。

**视频 Tag（TagType=0x09）**：body 紧随 Tag Header 之后，结构如下：

```
┌──────────────┬──────────────┬──────────────┬──────────────────┐
│ FrameType(4) │ CodecID(4)   │ AVCPacketType│ CompositionTime  │
│ + CodecID    │ 合占 1 字节   │   (1 字节)   │   (3 字节)       │  → 正文：NALU 数据
└──────────────┴──────────────┴──────────────┴──────────────────┘
                                      ↑
                              这 5 字节合称 VIDEODATA 头
```

- **首字节**：高 4 位是帧类型（`1`=关键帧，`2`=非关键帧），低 4 位是 CodecID（`7`=AVC/H.264，`12`=HEVC/H.265）
- **AVCPacketType**：`0`=sequence header（SPS/PPS），`1`=普通 NALU 数据，`2`=end of sequence
- **CompositionTime（3 字节）**：即 CTS = PTS − DTS，用于处理 B 帧的重排序。无 B 帧时为 0

以最常见的组合 `17 00` 开头的 Tag 为例：
- `0x17` = `0001 0111` → FrameType=1（关键帧），CodecID=7（AVC）
- `0x00` = AVCPacketType=0 → 这是 sequence header
- 后面跟的 3 字节 CompositionTime 恒为 0
- 再后面就是 avcC（AVCDecoderConfigurationRecord）的完整字节

以 `17 01` 开头的 Tag：
- `0x17` 同上（关键帧 + AVC）
- `0x01` = AVCPacketType=1 → 这是普通 NALU 数据
- 后面跟 CompositionTime，然后是 4 字节长度前缀 + NALU 数据（AVCC 格式）

**音频 Tag（TagType=0x08）**：body 结构类似：

```
┌──────────────┬──────────────┬──────────────────┐
│ SoundFormat  │ SoundRate    │ AACPacketType    │  → 正文：AAC 裸帧数据
│ + Rate       │ + Size       │   (1 字节)        │
│ + Size       │ + Type       │                  │
│ (1 字节)      │ 合占 1 字节   │                  │
└──────────────┴──────────────┴──────────────────┘
```

- **首字节**：高 4 位是音频格式（`10`=AAC），接着 2 位采样率、1 位采样位深、1 位声道类型
- **AACPacketType**：`0`=AudioSpecificConfig（AAC 参数头），`1`=普通 AAC 裸帧数据

常见的音频 sequence header 以 `AF 00` 开头：`0xAF` = `1010 1111` → SoundFormat=10（AAC），Rate=3（44100Hz），Size=1（16bit），Type=1（立体声）。`0x00` = AACPacketType=0（这是 AudioSpecificConfig）。后面跟 2 字节的 AudioSpecificConfig 数据。

> ⚠️ FLV 里 AAC 必须**裸帧（raw AAC）**，不能带 **ADTS 头**（那 7 字节的同步头 `FF F1…`）。因为 ADTS 头里的采样率、声道数等信息，在 AudioSpecificConfig 里已经给过了，再带一份属于浪费带宽 + 让解析变复杂。

**④ PreviousTagSize（4 字节）**

每个 Tag 前面都有一个 4 字节的 `PreviousTagSize`，记录前一个 Tag 的总大小（`Tag Header 的 11 字节 + Tag Data 的实际字节数`），大端序。第一个 `PreviousTagSize` 恒为 0。

它的作用是让解析器可以**倒着遍历**：从文件末尾开始，读 4 字节知道最后一个 Tag 多大，往前跳这么多，再读、再跳……所以即使用了 FLV 这种"为流式设计"的格式，播放器仍然可以在完整文件里做快速的 seek 操作——靠的就是这个 PreviousTagSize 链。

#### FLV 的设计取舍总结

| 维度     | FLV                               | MP4                               |
| -------- | --------------------------------- | --------------------------------- |
| 索引     | 无全局索引，靠 PreviousTagSize 后向链 | moov box 做全局索引                |
| 流式友好 | ✅ 天然适合，Tag 即收即播           | ⚠️ 需要 faststart 或完整下载       |
| 结构复杂度 | 极简（9 字节头 + 11 字节 Tag 头）   | 较复杂（box 嵌套，多种 box 类型）    |
| 编码支持 | H.264/AAC 为主（H.265 非标扩展）    | 几乎支持所有编码                    |
| 适用场景 | 直播推流、实时传输                  | 点播、本地存储、归档                |

> 关于 FLV 结构的更多细节（包括 onMetaData 的具体字段、AVCC 和 Annex-B 在封装层的区别、AVCDecoderConfigurationRecord 的字节解析），见 [05](./05-H264-MP4-NALU.md) §四 ~ §5.5。

---

### 8.2 FLV 和 RTMP 到底是什么关系

搞清楚了 FLV 的结构，再看它和 RTMP 的关系就一目了然了。

**核心结论：RTMP 的音视频消息体，就是 FLV Tag 的 body——类型和时间戳信息被"搬家"到了 RTMP 自己的协议头上，肉（载荷）原封不动。**

具体来说，FLV Tag 的 11 字节头部拆成了两部分：

```
FLV Tag:
  ┌──────────────────────┬──────────────────────────────┐
  │ Tag Header (11 字节)  │ Tag Data (变长)               │
  │ 类型/时间戳/大小       │ 真正的音视频数据                │
  └──────────┬───────────┴──────────────────────────────┘
             │
             ▼
RTMP Message:
  ┌──────────────────────────────┬──────────────────────┐
  │ Chunk/Message Header          │ Message Body         │
  │ (类型→message type id,        │ = FLV Tag Data       │
  │  时间戳→timestamp/delta,       │   一模一样!           │
  │  大小→message length)          │                      │
  └──────────────────────────────┴──────────────────────┘
```

也就是说：
- FLV Tag Header 里的 **TagType**（08=音频/09=视频/12=脚本）→ 在 RTMP 里变成了 Message Header 里的 **message type id**（8/9/18）
- FLV Tag Header 里的 **Timestamp** → 在 RTMP 里变成了 Chunk Header 里的 **timestamp / timestamp delta**
- FLV Tag Header 里的 **DataSize** → 在 RTMP 里变成了 Message Header 里的 **message length**
- FLV Tag Header 里的 **StreamID** → RTMP 不需要（RTMP 有自己的 message stream id）

**而 Tag Data（body）呢？原封不动搬进 RTMP Message Body。** 这就是"换壳不换肉"的精确定义。

#### 为什么这个设计如此重要

这个设计带来了一个巨大的工程红利：**RTMP 转 HTTP-FLV，服务器几乎零成本。**

直播服务器（如 SRS、nginx-rtmp）收到的 RTMP 推流，要分发给浏览器观众。浏览器不能直接播 RTMP（Flash 已死），但可以通过 HTTP 接收 FLV 数据，然后由 `flv.js` 把 FLV Tag 解析出来喂给 `<video>` 标签的 Media Source Extension——这就是 HTTP-FLV 方案。

服务器做这个转换做了什么？几乎什么都没做：

```
RTMP 推流 → 服务器收起 Chunk → 拼回完整 Message
          → Message Body 就是 FLV Tag Data
          → 前面拼上 FLV Tag Header（11 字节） + PreviousTagSize（4 字节）
          → 最前面再拼一个 FLV File Header（9 字节）
          → 这就是一段标准的 FLV 流！
          → 通过 HTTP 长连接发给浏览器
          → flv.js 解析后喂给 <video>
```

整个过程服务器**不需要解码再重编码**，不需要理解 H.264 码流内部结构，只是在做**二进制级别的拼头和拆头**——从 RTMP Message 里把 body 取出来，套一个 FLV Tag 的壳。即使上万路流的服务器，这个转换也只耗极少的 CPU。

> **"换壳不换肉"**：RTMP 是一套"壳"（Chunk + Message 协议头），HTTP-FLV 是另一套"壳"（HTTP + FLV Tag Header），但两套壳包的是同一块"肉"（FLV Tag Body）。这也是为什么 FFmpeg 推流时输出格式写 `-f flv` 就行——flv muxer 只管生成 FLV Tag，RTMP 协议层自动把 Tag 拆开套上自己的头。

对比一下其他协议的转换成本，就更清楚这个设计的价值：

| 转换路径          | 服务器要做什么                                      | CPU 开销     |
| --------------- | ------------------------------------------------ | ------------ |
| RTMP → HTTP-FLV | 拆 RTMP Message 头、套 FLV Tag 头                   | 几乎为零      |
| RTMP → HLS      | 重新切片生成 `.m3u8` + `.ts` 文件，需要攒几秒数据才出片  | 中等（涉及文件 I/O 和分片逻辑） |
| RTMP → WebRTC   | 解 FLV Tag → 拿裸 H.264/AAC → 重新封成 RTP 包       | 较高（需要理解码流结构、拆 NALU） |

#### 从字节角度看：同一个视频帧在 FLV 和 RTMP 里长什么样

假设编码器吐出一个 H.264 非关键帧 NALU，大小 602 字节，时间戳 1234ms。

**在 FLV 文件里**：

```
PreviousTagSize (4 字节):  XX XX XX XX    ← 前一个 Tag 的大小
Tag Header (11 字节):
  09             TagType = 视频
  00 02 74       DataSize = 0x0274 = 628  (605 字节 body + 11 字节 Tag Header = 639 = 0x027F...)
                 ← 等一下，这里 DataSize 只算 body，不含 Tag Header；body = 5 + 602 = 607 = 0x025F
                 ← 不对，让我重新算：VIDEODATA 头 5 字节 + NALU 602 字节 = 607 字节 = 0x025F
  00 00 00       Timestamp 低 24 位 (先忽略,后面统一)
  00             TimestampExtended
  00 00 00       StreamID (恒0)
Tag Data (607 字节):
  27             FrameType=2(非关键帧)<<4 | CodecID=7(AVC) = 0x27
  01             AVCPacketType=1 (NALU 数据)
  00 00 00       CompositionTime = 0 (无 B 帧)
  00 00 02 5A    NALU 长度 = 602 (4 字节大端)
  41 9A ...      NALU 数据 (602 字节)
```

**同一个视频帧在 RTMP 推流里**（假设 Chunk Size > 607 字节，一条消息一个 Chunk 就够）：

```
Basic Header:    06             fmt=0, csid=6
Message Header (fmt=0 = 11 字节):
  timestamp:     00 04 D2       1234ms (0x04D2)
  msg length:    00 00 02 5F    607 字节 (body 部分, = 5 + 602)
  type id:       09             视频消息
  msg stream id: 01 00 00 00    stream id = 1 (小端!)
Message Body (607 字节):
  27 01 00 00 00 00 00 02 5A 41 9A ...    ← 和 FLV Tag Data 完全一样！
```

**对比一下就能看到**：
- FLV 的 TagType=09 → RTMP 的 message type id=09
- FLV 的 Timestamp → RTMP 的 timestamp 字段
- FLV 的 DataSize（只算 body）= 607 → RTMP 的 message length = 607
- FLV 的 Tag Data 从 `27 01 ...` 开始的 607 字节 → RTMP Message Body 从 `27 01 ...` 开始的 607 字节，**逐字节相同**

> 这就是为什么在 §7.6 的字节解析里，RTMP 视频消息的 body 和 FLV VIDEODATA 结构完全一致——因为它们本来就是同一个东西。区别只在外层的"壳"：FLV 用 11 字节 Tag Header 包，RTMP 用 Chunk/Message Header 包。

#### 记忆口诀

> **FLV 负责"装箱"（把音视频数据按类型 + 时间戳打包成 Tag），RTMP 负责"运输"（把 Tag 的肉拆出来、套上 Chunk 头在 TCP 上发）。换壳不换肉，肉就是 FLV Tag Body。**

---

> ⚠️ 高频混淆点：**裸流 / RTP / Annex-B 用起始码 `00 00 01`；MP4 / FLV / RTMP 用 AVCC 长度前缀**。推 RTMP 时如果从 Annex-B 源拿数据，要先转成 AVCC（去起始码、加长度前缀、参数集进 sequence header）。AVCC↔Annex-B 见 [05](./05-H264-MP4-NALU.md) §四。

---

## 九、实战：用 ffmpeg / C API 推流

### 9.1 命令行（最快验证）

```bash
# 文件转直播推流(注意 -re: 按原始帧率推,否则会瞬间全推完冲垮服务器)
ffmpeg -re -i input.mp4 \
    -c:v libx264 -preset veryfast -tune zerolatency \
    -b:v 2500k -maxrate 2500k -bufsize 5000k -g 50 \
    -c:a aac -b:a 128k -ar 44100 \
    -f flv rtmp://live.example.com/app/streamKey
```

- `**-f flv**`：RTMP 推流的封装格式就是 **flv**（因为 RTMP 内部是 FLV Tag）。
- `**-re`**：按原速读，直播链路必加（实时采集源不需要，它本来就是实时的）。
- 码控参数对应 [06](./06-编码参数与码控.md) §6.3 的直播组合。

### 9.2 C API（avformat 路线，主流）

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

- 输出格式写 `**"flv"**`，URL 给 `rtmp://`——FFmpeg 自动走 RTMP 协议并完成握手/connect/publish，你不用手撸协议。
- **flv muxer 会自动把 H.264 转成 AVCC、生成 sequence header**，但前提是你的视频流 `codecpar->extradata` 里有正确的 avcC（从解码/编码侧拿）。
- **时间戳**：`av_interleaved_write_frame` 按 DTS 自动交织音视频；DTS 必须单调递增、用输出流的 `time_base`，否则花屏/卡顿（见 §十一）。

### 9.3 librtmp（更底层，了解即可）

`librtmp` 是独立的 RTMP 库，直接暴露 `RTMP_Connect` / `RTMP_SendPacket` 等，要自己拼 FLV Tag、管时间戳（也就是要手写 §7.6 那些字节）。除非要做极致定制（如自研推流 SDK），否则用 FFmpeg 的 avformat 封装更省心。

---

## 十、关键技术点（建立直觉）

- **为什么 RTMP 延迟低**：长连接 + 没有切片缓冲（不像 HLS 要攒几个 `.ts`）。来一帧发一帧。
- **为什么走 TCP**：要可靠（推流丢数据＝主播端花屏，不可接受）；代价是弱网下有队头阻塞、延迟会涨。这也是为什么超低延迟/弱网场景开始转 SRT（可靠 UDP）/ WebRTC。
- **为什么浏览器播不了 RTMP**：依赖 Flash，Flash 已死。观众端要用 HTTP-FLV（flv.js）/ HLS。
- **RTMPS**：RTMP over TLS，加密版，走 443 也能穿透更严的防火墙。

---

## 十一、常见坑与误区（必看）

> 推流出问题，**先分清是花屏、黑屏还是卡顿**——三者根因和排查路径不同，别一上来只查 sequence header。下面 §11.1~§11.3 是系统排查指南；§11.4 是速查表。

### 11.1 先看现象：花屏 vs 黑屏 vs 卡顿

| 现象 | 观众/拉流端看到什么 | 大概率原因 | 优先查什么 |
| ---- | ------------------- | ---------- | ---------- |
| **黑屏** | 有声音、画面全黑 | 没发 / 没收 **sequence header（avcC）**；解码器根本没初始化 | publish 后是否先发 `0x17 0x00`；SPS/PPS 是否有效 |
| **花屏** | 马赛克、绿块、撕裂、局部乱码 | 码流格式错（Annex-B/AVCC）、**参考帧断了**、NALU 边界错、IDR 丢失 | 裸流格式、丢帧策略、GOP/关键帧 |
| **卡顿** | 帧率低、一顿一顿、像 PPT | 推流端掉帧、**TCP 反压**、编码/发送同步阻塞、发热降频 | 发送队列水位、本地录制对比、FPS 打点 |
| **忽快忽慢 / 音画不同步** | 画面变速、嘴型对不上 | **时间戳(DTS/PTS)乱**、没交织发送、B 帧重排序 | DTS 单调性、`av_interleaved_write_frame`、关 B 帧 |

**易混点**：

- **首帧花一下再正常**：常是 sequence header 晚到或首包不是 IDR，偏黑屏/关键帧问题。
- **全程花**：多半是 AVCC 封装从头到尾就错了。
- **弱网后突然花几秒**：丢参考帧但没补 IDR，偏花屏 + 丢帧策略。
- **本地录制也花**：编码器/颜色格式问题，**别先查 RTMP**，先查编码输出。

### 11.2 推荐排查流程（公司里也这么干）

```text
Step 1  现象定性
        ├─ 全程花？偶发花？刚开播就花？播久了才花？
        └─ 本地录制正常、只有 RTMP 花？→ 偏网络/发送侧

Step 2  隔离编码/封装
        ├─ dump 推流前裸 H.264 或 FLV，ffplay 本地播
        ├─ 本地也花 → 编码/颜色格式/AVCC 转换问题
        └─ 本地正常、观众花 → 传输/服务器/拉流端

Step 3  查 sequence header
        ├─ publish 后是否先发 avcC + AudioSpecificConfig
        ├─ 中途改分辨率/旋转是否重发 sequence header
        └─ Wireshark：第一个视频包是否 17 00（AVC sequence header）

Step 4  查封装格式
        ├─ 是否误把 Annex-B（00 00 00 01）塞进 RTMP
        ├─ AVCC 长度前缀字节数是否和 avcC 里 lengthSizeMinusOne 一致
        └─ AAC 是否误带 ADTS 头

Step 5  查时间戳
        ├─ 视频 DTS 是否严格单调递增
        ├─ 是否 av_interleaved_write_frame 交织
        └─ time_base 是否正确 rescale

Step 6  查编码与丢帧
        ├─ 队列满时是否随机丢帧（应丢整条 GOP 或立刻 IDR）
        ├─ 弱网后是否 request IDR / insert keyframe
        └─ 直播是否关了 B 帧
```

**面试 30 秒收口**：「封装（AVCC + sequence header）→ 时间戳（DTS 单调 + 交织）→ 参考帧（GOP 级丢帧 + IDR）→ 本地录制隔离编码 vs 网络。」

### 11.3 花屏专项：常见原因与处理

| 原因 | 典型表现 | 处理方法 |
| ---- | -------- | -------- |
| **Annex-B 误当 AVCC 推** | 全程花、马赛克 | 去起始码、改 4 字节长度前缀；SPS/PPS 放进 sequence header（见 [05](./05-H264-MP4-NALU.md) §四） |
| **sequence header 缺失/过期** | 开播花、新观众进来花几秒 | publish 后先发 avcC；分辨率/旋转变化后**重发** sequence header |
| **DTS 回退 / 时间基准错** | 撕裂、忽快忽慢 | DTS 单调；rescale 到输出 time_base；`av_interleaved_write_frame` |
| **丢参考帧（丢帧策略错）** | 弱网/队列满后突然花 | **按 GOP 丢**，不随机丢 P 帧；队列满立刻 `request IDR` |
| **IDR 来得太晚** | 中途切入、重连后花 | 缩短 GOP（1~2s）；重连后强制关键帧；服务器缓存最近 IDR |
| **NALU 长度前缀不一致** | 部分机型花、间歇花 | avcC 声明与打包逻辑一致（通常 4 字节）；抓包看数据帧非起始码 |
| **编码器吐坏流** | **本地录制也花** | 查 stride/颜色格式（[14](./14-Android硬件编解码.md) §3.4）；CBR + 关 B 帧 |
| **中途 SPS 变了没通知** | 旋转/切分辨率后花 | 停流或重发 sequence header + IDR |
| **H.265 链路不认** | 部分 CDN/播放器花 | 确认全链路支持 enhanced RTMP，或换协议 |
| **发送侧组包 bug** | 本地正常、RTMP 花 | 同参数写本地 MP4 vs RTMP 对比；查发送队列并发写 |

**一眼区分 AVCC vs Annex-B（抓包/xxd）**：

```text
AVCC 数据帧：17 01 [4B长度] 65/41/01...   ← RTMP 正确形态
Annex-B：    00 00 00 01 67/68/65...       ← 进 RTMP 必花
```

**实用命令**：

```bash
# 拉流存 FLV 本地验证
ffmpeg -i rtmp://... -c copy -t 30 dump.flv && ffplay dump.flv

# 看包结构
ffprobe -show_packets -select_streams v dump.flv
```

### 11.4 坑与误区速查表

| 症状 / 误区              | 根因                                      | 怎么修                                                         |
| -------------------- | --------------------------------------- | ----------------------------------------------------------- |
| 推流端口连不上              | 1935 偏门端口被企业/校园防火墙封                     | 换 RTMPS(443) 或在能出网的环境推                                      |
| 文件推流瞬间结束/服务器卡死       | 忘了 `-re`，FFmpeg 以最快速度把整个文件推完            | 文件源加 `-re` 按原速推                                             |
| 拉流端**黑屏但有声音**        | 没先发**视频 sequence header(avcC/SPS/PPS)** | publish 后先发 sequence header 再发数据帧（见 §11.1）                |
| **首帧花屏**后恢复           | sequence header 晚到或首包不是 IDR              | 确保 avcC 在首帧数据前；首包尽量 IDR（见 §11.3）                            |
| **全程花屏 / 马赛克**        | Annex-B 误用、NALU 边界错                      | 转 AVCC；查长度前缀（见 §11.3）                                        |
| **弱网后突然花屏**           | 丢参考帧、没补 IDR                             | GOP 级丢帧 + `request IDR`；异步发送队列避免反压（见 §11.3）                  |
| 画面花屏、忽快忽慢、音画不同步      | **时间戳(DTS)不单调或没对齐**、音视频没按时间戳交织          | DTS 单调 + 用输出 time_base + `av_interleaved_write_frame`       |
| **卡顿 / 帧率掉成 PPT**      | TCP 反压、编码推流同步阻塞、发热降频                   | 异步发送队列 + 队列水位 ABR；本地录制对比隔离网络（见 §11.1）                      |
| 误以为 RTMP 视频是 Annex-B | RTMP/FLV 用 **AVCC 长度前缀**，不是起始码          | 从 Annex-B 源推流要先转 AVCC（见 [05](./05-H264-MP4-NALU.md) §四）     |
| 音频推上去拉流端解不出          | RTMP/FLV 的 AAC 是**裸帧**，误带了 ADTS 头       | 推流前剥掉 ADTS 头（见 §7.6 F 段）                                    |
| 推了 B 帧导致延迟高/兼容差      | 直播不该用 B 帧（PTS≠DTS 引入重排序延迟）              | 编码端无 B 帧 + zerolatency（[06](./06-编码参数与码控.md)）               |
| 码率忽高忽低冲垮带宽           | 用了 CRF/VBR 导致瞬时码率尖峰                     | 直播用 CBR + maxrate + bufsize（[06](./06-编码参数与码控.md) §6.3 VBV） |
| 推 H.265 拉流端放不出       | RTMP/FLV 对 HEVC 支持是非标扩展，很多服务器/播放器不认     | H.265 直播优先走别的容器/协议，或确认全链路支持                                 |


---

## 十二、行业现状 / 生态

- **推流软件**：OBS Studio（最广）、FFmpeg、各厂手机直播 SDK。
- **流媒体服务器**：SRS（国产、流行）、nginx-rtmp-module、Wowza、各云厂商直播服务。
- **现状与趋势**：**推流仍以 RTMP 为主**；但低延迟/弱网场景，**SRT（可靠 UDP）和 WebRTC 推流**在抬头。分发端 RTMP 早已退给 HTTP-FLV / HLS / WebRTC（见 [08](./08-网络协议与流媒体.md) §六）。

---

## 十三、面试常考题（关键词速记版）

> 完整口语化话术见开头 §二，这里只列张嘴时要命中的关键词。

- **Q：讲一下 RTMP 推流的完整流程。**
TCP 连接 → RTMP 握手(C0/C1/C2) → connect 连应用 → createStream 拿 message stream id → publish 声明推流 → 先发 onMetaData + 音视频 sequence header(avcC/AudioSpecificConfig) → 按时间戳交织发音视频数据消息。
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
- **Q：推流黑屏/花屏/卡顿怎么排查？**
先三分法（§11.1）：黑屏→sequence header；花屏→AVCC/参考帧/IDR；卡顿→发送反压/掉帧。再按 §11.2 六步：本地录制隔离 → 查 avcC → 查 AVCC → 查 DTS → 查 GOP/IDR。花屏专项见 §11.3。
- **Q：RTMP vs SRT vs WebRTC 推流？**
RTMP：TCP、生态最成熟、1-3s、推流事实标准；SRT：可靠 UDP、抗丢包、几百 ms-1s、适合弱网回传；WebRTC：UDP+RTP、几百 ms、互动级但要 ICE/SFU，重。
- **Q：Chunk header 里哪个字段是小端？**
只有 **message stream id（4 字节）是小端**，其余多字节字段都是大端（见 §7.6 B 段）。

---

## 十四、当前阶段定位 + 下一步

- **这篇在协议全景里的位置**：[08](./08-网络协议与流媒体.md) §六"推流"那一格的深挖。读完你应该能完整讲清"主播点开始直播 → 服务器收到"这条链路，以及 RTMP 协议握手/Chunk/命令/和 FLV 的关系，并且能**精确到字节**地解释抓包看到的东西（§7.6）。
- **它和你其它笔记的衔接**：上游接编码（[06](./06-编码参数与码控.md)/[11](./11-H264与H265详解.md)）和封装（[05](./05-H264-MP4-NALU.md)），下游接分发（[08](./08-网络协议与流媒体.md)）。RTMP 不是孤立的，是这条链上的"运输"环。
- **下一步**：带着这条推流链路，回到核心待办——**动手写一条最简管线**（先播放器 demux→decode→渲染，再考虑推流器 encode→mux flv→rtmp）。把"读过"变成"骨头里的"，比再读协议管用十倍（见 [99-学习进度.md](./99-学习进度.md)）。

---

## 十五、自检

- 点"开始直播"后，从采集到服务器收到，完整链路有哪几环？RTMP 负责哪一环？
- RTMP 握手 C0/C1/C2 各是什么？各多少字节？为什么需要握手？
- 为什么 RTMP 要把消息切成 Chunk？分块带来哪三个好处？
- Chunk 的 Basic Header 怎么拆出 fmt 和 csid？Message Header 四种 fmt 各多少字节、差在哪？
- Chunk Message Header 里哪个字段是小端字节序？
- `connect` 命令的 AMF0 载荷大致由哪几段组成？事务 ID 用什么类型编码？
- 推流建连的命令顺序是什么？（connect → createStream → publish）publish 之后第一件事该发什么？
- 视频 sequence header 的头两个字节是什么、代表什么？avcC 里靠哪个字节决定 NALU 长度前缀几字节？
- RTMP 的音视频消息体和 FLV Tag 是什么关系？这解释了什么工程现象？
- RTMP 里的 H.264 是 AVCC 还是 Annex-B？字节上怎么一眼区分？从 Annex-B 源推流要做什么转换？
- RTMP/FLV 里的 AAC 带不带 ADTS 头？
- 用 FFmpeg C API 推流，输出格式应该填什么？时间戳要注意什么？
- 拉流端黑屏但有声音，最可能是哪一步出了问题？
- 花屏、黑屏、卡顿三者怎么区分？各自优先查什么？（见 §11.1）
- 弱网后突然花屏，最可能是什么丢帧/关键帧问题？（见 §11.3）
- 怎么用「本地录制 vs RTMP 推流」隔离编码问题和网络问题？
- 为什么 RTMP 延迟比 HLS 低、却又被认为"该被淘汰"？现在推流端为什么还在用它？
- RTMP / SRT / WebRTC 推流各自的延迟量级和适用场景？

