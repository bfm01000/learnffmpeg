# FLV 与 HTTP-FLV 直播封装深入理解

## 0. 本篇定位

- 面试复习：先掌握 FLV 是封装格式、HTTP-FLV 是通过 HTTP 长连接持续发送 FLV Tag，以及为什么它适合低延迟播放。
- 深入学习：重点看 FLV Header/Tag、AVC/AAC sequence header、CompositionTime、timestamp 和 RTMP 转 HTTP-FLV 的关系。
- 职责边界：这篇主讲直播封装语义；编码格式细节回到 [../ffmpeg/05-H264-MP4-NALU.md](../ffmpeg/05-H264-MP4-NALU.md) 和 [../ffmpeg/18-FFmpeg音频编解码详解.md](../ffmpeg/18-FFmpeg音频编解码详解.md)。
如果你学 RTMP 时卡在 FLV，通常不是因为 FLV 本身复杂，而是因为几个概念混在一起了：

- `H.264/AAC`：编码格式，描述压缩后的音视频数据。
- `FLV`：封装格式，描述音视频包、时间戳、元数据怎么组织。
- `RTMP`：传输协议，描述这些 FLV 语义的音视频消息怎么通过 TCP 长连接推到服务器。
- `HTTP-FLV`：播放分发方式，描述怎么用 HTTP 长连接把 FLV 字节流发给播放器。

一句话：

> FLV 是一种很适合直播的“流式封装格式”；RTMP 推流和 HTTP-FLV 播放都大量复用了 FLV 的音视频 Tag 组织方式。

---

## 1. 先建立直觉：FLV 到底是什么

FLV 全称是 Flash Video，历史上是 Flash 时代的网页视频封装格式。虽然 Flash 已经淘汰，但 FLV 的结构简单、流式友好，所以在直播系统里依然常见。

一个 FLV 流可以理解成：

```text
FLV Header
    |
    v
PreviousTagSize0
    |
    v
Tag 1: metadata
    |
    v
PreviousTagSize1
    |
    v
Tag 2: video sequence header
    |
    v
PreviousTagSize2
    |
    v
Tag 3: audio sequence header
    |
    v
PreviousTagSize3
    |
    v
Tag 4: video frame
    |
    v
PreviousTagSize4
    |
    v
Tag 5: audio frame
    |
    v
...
```

它的核心就是：**一个 Header 后面跟着一串 Tag**。

每个 Tag 都是一个独立的小包，可以是：

- Script Tag：元数据，例如宽高、帧率、编码格式
- Video Tag：视频数据，例如 H.264 SPS/PPS 或普通视频帧
- Audio Tag：音频数据，例如 AAC 配置或普通 AAC 帧

这就是 FLV 适合直播的原因：**它不要求等整个文件写完才能播放，播放器可以边收到 Tag 边解析、边解码、边播放**。

---

## 2. FLV 和 MP4 最大的区别

面试里如果问“FLV 和 MP4 有什么区别”，不要只答“一个直播一个点播”。可以这样理解：

```text
MP4
  更像一本带目录的书。
  很多关键信息在 moov box 里，适合点播、拖动进度条、精确索引。

FLV
  更像一串按时间排好的快递包裹。
  每个 Tag 带类型、长度、时间戳，适合边写边发、边收边播。
```

对直播来说，FLV 有几个优势：

- 结构简单，解析成本低。
- Tag 天然按时间顺序排列，适合流式传输。
- 每个音视频包都有 timestamp，播放器容易恢复播放节奏。
- 和 RTMP 的音视频消息语义接近，RTMP 转 HTTP-FLV 成本低。

MP4 也能做直播，例如 fragmented MP4 / CMAF / LL-HLS，但传统 MP4 文件更偏点播。

---

## 3. FLV 文件整体结构

FLV 文件由两部分组成：

```text
FLV Header
PreviousTagSize0
Tag
PreviousTagSize
Tag
PreviousTagSize
...
```

### 3.1 FLV Header

FLV Header 通常是 9 字节：

```text
Signature:  3 bytes  "FLV"
Version:    1 byte   通常是 1
Flags:      1 byte   是否有音频/视频
HeaderSize: 4 bytes  通常是 9
```

可以画成：

```text
46 4C 56   01   05   00 00 00 09
 F  L  V   ver  A/V  header size
```

`Flags` 常见值：

```text
0x01: 有视频
0x04: 有音频
0x05: 音频 + 视频
```

Header 后面紧跟一个 `PreviousTagSize0`，通常是 `0x00000000`。

### 3.2 PreviousTagSize

每个 Tag 后面都有一个 4 字节的 `PreviousTagSize`，表示前一个 Tag 的总大小：

```text
PreviousTagSize = TagHeaderSize(11) + TagDataSize
```

它对直播播放不是最核心，但在文件解析、校验和反向遍历时有用。

---

## 4. FLV Tag 结构

每个 FLV Tag 都有 11 字节 Header：

```text
TagType:            1 byte
DataSize:           3 bytes
Timestamp:          3 bytes
TimestampExtended:  1 byte
StreamID:           3 bytes，通常为 0
TagData:            DataSize bytes
```

画成一张图：

```text
+----------+----------+-----------+-------------------+----------+----------+
| TagType  | DataSize | Timestamp | TimestampExtended | StreamID | TagData  |
| 1 byte   | 3 bytes  | 3 bytes   | 1 byte            | 3 bytes  | N bytes  |
+----------+----------+-----------+-------------------+----------+----------+
```

TagType 常见值：

```text
8   Audio Tag
9   Video Tag
18  Script Tag，也就是 metadata
```

Timestamp 单位是毫秒。低 24 bit 放在 `Timestamp`，高 8 bit 放在 `TimestampExtended`。也就是说，FLV 可以表达超过 24 bit 的毫秒时间戳。

注意：FLV 字段里很多 3 字节整数是大端序，这和常见 C++ 里直接读 `uint32_t` 不一样。手写 parser 时不要直接强转结构体。

---

## 5. Script Tag：metadata 是什么

Script Tag 通常出现在流开头，用 AMF 编码保存元数据。最常见名字是 `onMetaData`。

里面可能包含：

```text
duration
width
height
framerate
videocodecid
audiocodecid
audiosamplerate
audiosamplesize
stereo
encoder
```

直播场景里 `duration` 通常没有意义，因为直播不知道什么时候结束。

metadata 的作用：

- 让播放器提前知道宽高、帧率、编码格式。
- 让服务器录制、转码、统计更方便。
- 对播放不是绝对必须，但缺失会影响兼容性和体验。

面试可以这样说：

> FLV 的 Script Tag 主要承载元数据，常见是 AMF 编码的 onMetaData，里面有宽高、帧率、音视频 codec id 等信息。直播里 duration 往往未知，但 width、height、framerate、videocodecid 这些信息对播放器初始化和服务端处理很有帮助。

---

## 6. Video Tag：H.264 怎么放进 FLV

FLV Video Tag 的 TagData 第一个字节包含两个信息：

```text
高 4 bit: FrameType
低 4 bit: CodecID
```

常见值：

```text
FrameType:
  1 = keyframe，关键帧
  2 = inter frame，非关键帧

CodecID:
  7 = AVC，也就是 H.264
```

所以 H.264 关键帧 Video Tag 第一个字节通常是：

```text
0x17 = 0001 0111
       keyframe + AVC
```

非关键帧通常是：

```text
0x27 = 0010 0111
       inter frame + AVC
```

### 6.1 AVC Video Packet

如果 `CodecID == 7`，后面还会有 AVC 相关字段：

```text
AVCPacketType:  1 byte
CompositionTime: 3 bytes
AVC data:       N bytes
```

`AVCPacketType`：

```text
0 = AVC sequence header
1 = AVC NALU
2 = AVC end of sequence，少见
```

整体结构：

```text
Video TagData for H.264:

+----------------------+---------------+-----------------+----------------+
| FrameType + CodecID  | AVCPacketType | CompositionTime | AVC data       |
| 1 byte               | 1 byte        | 3 bytes         | N bytes        |
+----------------------+---------------+-----------------+----------------+
```

### 6.2 AVC sequence header：SPS/PPS 在这里

H.264 解码器启动需要 SPS/PPS。FLV 里通常先发一个 AVC sequence header：

```text
FrameType + CodecID = 0x17
AVCPacketType = 0
CompositionTime = 0
AVC data = AVCDecoderConfigurationRecord
```

`AVCDecoderConfigurationRecord` 里包含：

- H.264 profile
- level
- NALU length size，常见是 4 字节
- SPS
- PPS

播放器收到它之后，才能初始化 H.264 解码器。

### 6.3 普通 H.264 视频帧

后续普通视频帧：

```text
FrameType + CodecID = 0x17 或 0x27
AVCPacketType = 1
CompositionTime = PTS - DTS
AVC data = NALU length + NALU data
```

注意：FLV/AVC 里通常不是 Annex-B 格式。

Annex-B：

```text
00 00 00 01 NALU
00 00 00 01 NALU
```

FLV/AVC：

```text
00 00 02 A1 NALU
00 00 00 1F NALU
```

也就是用 `length` 表示每个 NALU 的大小，而不是用 start code 分隔。

这也是为什么做封装时经常要处理：

- Annex-B 转 AVCC
- 从 SPS/PPS 生成 extradata
- 关键帧前补 SPS/PPS

---

## 7. CompositionTime：为什么 FLV 里还要 PTS-DTS

FLV Tag Header 里的 `Timestamp` 更接近 DTS，也就是解码时间。

Video Tag 里的 `CompositionTime` 表示：

```text
CompositionTime = PTS - DTS
```

如果没有 B 帧：

```text
PTS == DTS
CompositionTime = 0
```

如果有 B 帧：

```text
解码顺序和显示顺序不同
PTS != DTS
CompositionTime != 0
```

低延迟直播通常关闭 B 帧，因为：

- B 帧需要等待未来参考帧，增加编码和解码延迟。
- PTS/DTS 处理更复杂。
- 弱网下丢帧策略更难做。

面试时可以这样答：

> FLV Tag Header 里的 timestamp 通常按解码时间 DTS 理解，H.264 Video Tag 里还有 3 字节 CompositionTime，用来表达 PTS 和 DTS 的差值。有 B 帧时必须正确写这个字段，否则画面显示时间会乱。低延迟直播一般关闭 B 帧，这样 CompositionTime 基本为 0。

---

## 8. Audio Tag：AAC 怎么放进 FLV

FLV Audio Tag 的 TagData 第一个字节包含：

```text
SoundFormat: 4 bit
SoundRate:   2 bit
SoundSize:   1 bit
SoundType:   1 bit
```

常见 AAC：

```text
SoundFormat = 10，AAC
SoundRate   = 3，44kHz 标记；AAC 实际采样率看 sequence header
SoundSize   = 1，16-bit
SoundType   = 1，stereo
```

第一个字节常见是：

```text
0xAF = 1010 1111
       AAC + 44kHz + 16-bit + stereo
```

如果是 AAC，后面还有：

```text
AACPacketType: 1 byte
AAC data:      N bytes
```

`AACPacketType`：

```text
0 = AAC sequence header
1 = AAC raw frame
```

### 8.1 AAC sequence header

AAC sequence header 里是 `AudioSpecificConfig`，包含：

- AAC profile，例如 AAC-LC
- sample rate
- channel count

播放器需要它初始化 AAC 解码器。

### 8.2 AAC raw frame

后续普通 AAC 帧：

```text
SoundFormat... = 0xAF
AACPacketType = 1
AAC raw data
```

注意：FLV 里的 AAC raw data 通常不带 ADTS 头。

如果编码器输出：

```text
ADTS header + AAC payload
```

封装 FLV 时通常要去掉 ADTS header，只放 AAC payload，并通过 AAC sequence header 告诉解码器采样率和声道。

---

## 9. HTTP-FLV 是什么

HTTP-FLV 不是新的封装格式，它本质上是：

```text
HTTP response body 里持续发送 FLV 字节流
```

也就是：

```text
播放器 -> 服务器:
GET /live/stream.flv HTTP/1.1
Host: live.example.com

服务器 -> 播放器:
HTTP/1.1 200 OK
Content-Type: video/x-flv
Transfer-Encoding: chunked

FLV Header
PreviousTagSize0
metadata tag
video sequence header
audio sequence header
video tag
audio tag
video tag
audio tag
...
```

这里的 `Transfer-Encoding: chunked` 是常见写法，但不是 HTTP-FLV 的本质要求。有些实现也可能不写 `Content-Length`，直接保持连接不断开并持续写 body。核心不是响应头长什么样，而是：**HTTP 响应体里是一条连续的 FLV 字节流**。

重点：HTTP-FLV 里的 `chunked` 是 HTTP 的分块传输，和 RTMP 的 Chunk 不是一个概念。

```text
HTTP chunk
  HTTP 传输层面的分块，给 HTTP body 流式发送用。

RTMP Chunk
  RTMP 协议内部把 Message 切片的机制。

FLV Tag
  FLV 封装层面的音频、视频、元数据包。
```

三者不要混。

---

## 10. HTTP-FLV 为什么延迟低

HTTP-FLV 延迟低，是因为它和 HLS 不一样：

```text
HTTP-FLV
  一个 HTTP 长连接
  服务器持续往 response body 写 FLV Tag
  播放器边收边播

HLS
  服务器先切成 ts/fmp4 分片
  播放器定期请求 m3u8
  再下载分片
  通常要缓存多个分片后播放
```

所以 HTTP-FLV 可以做到接近 RTMP 的播放延迟，常见在 `1~3s` 量级，具体取决于 GOP、播放器缓冲、CDN、网络情况。

它的优点：

- 基于 HTTP，容易穿透防火墙和代理。
- 可以复用 CDN 的 HTTP 基础设施。
- 浏览器可以通过 MSE 播放，不依赖 Flash。
- 服务器从 RTMP 转 HTTP-FLV 成本低。

它的缺点：

- 仍然基于 TCP，弱网下有队头阻塞。
- 原生 `<video>` 不一定直接支持 FLV，Web 端通常要 flv.js + MSE。
- 不像 HLS 那样是系统级广泛原生支持。
- 长连接多时，对服务端连接数和内存管理有压力。

---

## 11. RTMP 转 HTTP-FLV 为什么方便

RTMP 和 FLV 的关系很近。可以粗略理解为：

```text
RTMP 推流收到：
  metadata message
  video message
  audio message

HTTP-FLV 输出：
  FLV Header
  metadata tag
  video tag
  audio tag
```

音视频负载很多时候不需要重新编码，只需要重新组织外层封装：

```text
RTMP Message payload
      |
      v
FLV TagData
      |
      v
HTTP response body
```

这叫转封装 / remux，不是转码 / transcode。

```text
转封装 remux:
  H.264/AAC 数据不变，只改容器/协议外壳。
  成本低，速度快，画质无损。

转码 transcode:
  解码成 YUV/PCM，再重新编码。
  成本高，可能损失画质，但可以改分辨率、码率、编码格式。
```

面试里可以这样说：

> RTMP 转 HTTP-FLV 通常是转封装，不是转码。因为 RTMP 本身承载的音视频消息和 FLV Tag 语义很接近，服务器可以把 RTMP 里的 metadata、audio、video message 重新包装成 FLV Header + FLV Tag，通过 HTTP 长连接发给播放器。H.264/AAC 码流本身一般不需要重新编码。

---

## 12. FFmpeg 视角：FLV muxer 做了什么

FFmpeg 里写 FLV 文件或推 RTMP，常见都走 `flv` muxer：

```cpp
avformat_alloc_output_context2(&oc, nullptr, "flv", url);
```

如果 `url` 是文件：

```text
output.flv
```

就是写本地 FLV 文件。

如果 `url` 是 RTMP：

```text
rtmp://live.example.com/live/stream
```

就是把 FLV 语义的音视频包通过 RTMP 协议推走。

大致流程：

```text
编码器输出 AVPacket
      |
      v
设置 stream_index / pts / dts / duration
      |
      v
av_interleaved_write_frame()
      |
      v
flv muxer
      |
      v
写 metadata / sequence header / audio tag / video tag
      |
      v
文件 / RTMP 网络 IO
```

你要重点关注：

- `AVCodecParameters` 是否正确，例如 codec id、extradata。
- H.264 extradata 是否包含 SPS/PPS。
- AAC extradata 是否包含 AudioSpecificConfig。
- `AVPacket.pts/dts` 是否单调递增。
- `time_base` 转换是否正确。
- 编码器输出格式是 Annex-B 还是 AVCC。

FFmpeg 通常会帮你做很多封装细节，但前提是你给它的 codec parameters、extradata、timestamp 是正确的。

---

## 13. 手写 FLV parser 时怎么读

如果你要自己解析 FLV，不要一上来解析 H.264。先按层次读：

```text
1. 读 FLV Header，确认 "FLV"、version、flags。
2. 读 PreviousTagSize0。
3. 循环读 Tag Header。
4. 根据 TagType 判断 audio/video/script。
5. 根据 DataSize 读 TagData。
6. 读 PreviousTagSize。
7. 如果是 video，再解析 FrameType/CodecID/AVCPacketType。
8. 如果是 audio，再解析 SoundFormat/AACPacketType。
```

伪代码：

```cpp
while (read_tag_header(header)) {
    uint8_t tag_type = header.tag_type;
    uint32_t data_size = read_u24_be(header.data_size);
    uint32_t timestamp =
        read_u24_be(header.timestamp) |
        (uint32_t(header.timestamp_extended) << 24);

    std::vector<uint8_t> data = read_bytes(data_size);

    if (tag_type == 9) {
        parse_video_tag(data, timestamp);
    } else if (tag_type == 8) {
        parse_audio_tag(data, timestamp);
    } else if (tag_type == 18) {
        parse_script_tag(data);
    }

    uint32_t previous_tag_size = read_u32_be();
}
```

C++ 常见坑：

- 3 字节整数不能直接用 `uint32_t*` 强转。
- 网络/文件字段多是大端，要手动组装。
- TagData 可能很大，要做长度校验，防止越界。
- 不要假设第一个视频 Tag 一定是 sequence header，但正常直播流应该尽早给到。
- 时间戳可能回绕或重连重置，播放器/服务端要有容错。

---

## 14. FLV、RTMP、HTTP-FLV、HLS 对比

```text
FLV
  封装格式，本质是 Header + 一串 Tag。
  可以是本地 .flv 文件，也可以作为 HTTP-FLV 的响应 body。

RTMP
  推流协议，基于 TCP，有握手、命令、Message、Chunk。
  音视频负载常用 FLV Tag 语义。

HTTP-FLV
  播放分发方式，基于 HTTP 长连接。
  response body 持续输出 FLV 字节流。

HLS
  播放分发协议，基于 HTTP 分片。
  m3u8 + ts/fmp4，兼容性强，但传统延迟更高。
```

一句话记忆：

```text
RTMP 是“主播推上来”的管道。
FLV 是“音视频包怎么装”的盒子。
HTTP-FLV 是“把这个盒子流式发给观众”的方式。
HLS 是“切成一段段文件让观众下载”的方式。
```

---

## 15. 高频面试题

### Q1：FLV 是编码格式还是封装格式？

FLV 是封装格式，不是编码格式。它负责组织音视频包、时间戳和元数据；里面常见承载 H.264 视频和 AAC 音频。H.264/AAC 才是编码格式。

### Q2：FLV 为什么适合直播？

FLV 结构简单，Header 后面就是按时间顺序排列的一串 Tag。每个 Tag 都有类型、长度、时间戳，播放器可以边接收边解析边播放，不需要等完整文件写完。它和 RTMP 的音视频消息语义接近，所以直播系统里转封装成本很低。

### Q3：FLV Tag 有哪些类型？

常见三类：`8` 是 Audio Tag，`9` 是 Video Tag，`18` 是 Script Tag。Script Tag 常放 `onMetaData`，Video Tag 最常见是 H.264，H.265 通常依赖扩展 FLV；Audio Tag 常见是 AAC/MP3 等音频数据。

### Q4：H.264 在 FLV 里怎么放？

H.264 在 FLV Video Tag 里通过 AVC 格式承载。开头有 `FrameType + CodecID`，然后是 `AVCPacketType` 和 `CompositionTime`。`AVCPacketType=0` 表示 AVC sequence header，里面放 SPS/PPS；`AVCPacketType=1` 表示普通 NALU 数据。普通 NALU 通常是 length-prefixed，不是 Annex-B start code。

### Q5：AAC 在 FLV 里怎么放？

AAC 在 FLV Audio Tag 里承载。第一个字节描述 SoundFormat、SoundRate、SoundSize、SoundType。AAC 还会有 `AACPacketType`：`0` 是 AAC sequence header，里面放 AudioSpecificConfig；`1` 是 AAC raw frame。FLV 里的 AAC raw frame 通常不带 ADTS 头。

### Q6：HTTP-FLV 是什么？

HTTP-FLV 是用 HTTP 长连接传输 FLV 字节流。服务器返回 `Content-Type: video/x-flv`，然后在 response body 里持续写 FLV Header 和后续 FLV Tag。它不是新的编码格式，也不是新的封装格式，本质上是“HTTP 承载 FLV 流”。

### Q7：HTTP-FLV 为什么比 HLS 延迟低？

HTTP-FLV 是一个长连接，服务器可以生成一个 Tag 就发一个 Tag，播放器边收边播。传统 HLS 要先切片、更新 m3u8，播放器再下载并缓存多个分片，所以延迟通常更高。

### Q8：HTTP-FLV 和 RTMP 有什么区别？

RTMP 是独立的应用层协议，有自己的握手、命令、Message 和 Chunk，常用于推流；HTTP-FLV 是基于 HTTP 的播放方式，本质是 HTTP response body 里持续传 FLV Tag。两者都基于 TCP，但协议层不同。RTMP 更常用于主播到服务器，HTTP-FLV 更常用于服务器到观众。

### Q9：RTMP 转 HTTP-FLV 需要转码吗？

通常不需要。大多数情况下是转封装，也就是把 RTMP 收到的 metadata、audio、video message 重新包装成 FLV Header + FLV Tag，通过 HTTP 发出去。H.264/AAC 码流本身不变。

### Q10：FLV 里 timestamp 是 PTS 还是 DTS？

对 H.264 来说，FLV Tag Header 里的 timestamp 通常按 DTS 理解，Video Tag 里的 CompositionTime 表示 `PTS - DTS`。没有 B 帧时 PTS/DTS 相等，CompositionTime 为 0；有 B 帧时必须正确写 CompositionTime，否则显示顺序会错。

---

## 16. 一套口述模板

如果面试官问：“你讲一下 FLV 和 HTTP-FLV。”

可以这样答：

> FLV 是一种流式友好的封装格式，不是编码格式。它的结构很简单，前面是 FLV Header，后面是一串 Tag，每个 Tag 有类型、长度、时间戳和数据。常见 Tag 有 Script Tag、Video Tag、Audio Tag。直播里 Video Tag 通常承载 H.264，先通过 AVC sequence header 发送 SPS/PPS，再发送普通 NALU；Audio Tag 通常承载 AAC，先发送 AAC sequence header，也就是 AudioSpecificConfig，再发送 AAC raw frame。HTTP-FLV 本质上就是通过 HTTP 长连接持续发送 FLV 字节流，服务器返回 `video/x-flv`，然后不断往 response body 写 FLV Tag。它比 HLS 延迟低，因为不需要切片和等待多个分片；它和 RTMP 的关系也很近，RTMP 推流到服务器后通常可以不转码，直接转封装成 HTTP-FLV 给观众播放。

---

## 17. 复习抓手

把 FLV 记成 4 层：

```text
第一层：FLV Header
  说明这是 FLV，有没有音频/视频。

第二层：Tag
  每个 Tag 有类型、长度、时间戳。

第三层：Audio/Video TagData
  区分 AAC、H.264、sequence header、raw frame。

第四层：真正的编码数据
  H.264 NALU、AAC raw payload。
```

再把 HTTP-FLV 记成一句话：

```text
HTTP-FLV = HTTP 长连接 + FLV 字节流。
```

只要这两个模型建立起来，再回头看 RTMP 文档里“FLV Tag 语义”“AVC sequence header”“AAC sequence header”“RTMP 转 HTTP-FLV”，就会顺很多。
