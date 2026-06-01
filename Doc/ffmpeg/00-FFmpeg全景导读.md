# FFmpeg 全景导读

> 这是一份"先建地图、再走路"的导读。
> 目标是让你合上文档时，脑子里有一张 ffmpeg 的全景图、知道自己在哪、下一步往哪走。
> 具体 API 怎么用、字段什么含义，留给本目录下其他细节文档下钻。

---

## 一、场景引入：从一段视频说起

假设你正在做这样几件事：

- 你下载了一段 `input.mp4`，想要在浏览器里能播放、能拖动进度条、首屏要秒开。
- 同样这段视频，你要在 WebRTC 通话里推给对端，对方那头是另一个浏览器。
- 你又想把这段视频压成更小的体积，丢到 CDN 上分发给一千万人看直播。
- 直播过程中，主播这一端把摄像头采到的画面实时编码、推给服务器。

四个场景，表面看都是"处理一段视频"。但只要你试着自己写一行代码，就会立刻撞上一堆问题：

- MP4 里的 H.264 数据，为什么不能直接把字节扔给一个 H.264 解码器？
- 解码出来的画面，为什么不是 RGB 而是一堆叫做 YUV 的东西？
- 一帧画面在内存里放了三块独立的数据，分别叫 Y、U、V，宽度和文件里的分辨率还对不上？
- 音频解码出来一会儿是浮点一会儿是整数，一会儿左右声道交错一会儿又分开放，到底什么时候转、转成什么？
- 视频和音频的时间戳为什么是一串大整数，不是秒？
- 为什么一段视频在网络上传播要选 TCP 还是 UDP，选错了用户会怎么骂街？

**ffmpeg 就是把上面这些问题，全部用一套统一的抽象包起来，让你不用每个场景从零写一遍的工具。** 它既是命令行工具（`ffmpeg`、`ffprobe`、`ffplay`），也是一组 C 库（`libavformat`、`libavcodec` 等），全球几乎所有视频相关的产品底层都在用它。

---

## 二、ffmpeg 是什么 / 它从哪来

**一句话**：ffmpeg 是一个可以处理几乎所有音视频格式的开源框架。

- 2000 年由 Fabrice Bellard（同时也是 QEMU 作者）发起。
- 当时的痛点是：每种封装容器（AVI / MOV / FLV…）、每种编码（MPEG-2 / H.263 / MP3…）都有各自孤立的库或工具，开发者要写转码、播放、推流，得自己拼接七八套互不兼容的东西。
- ffmpeg 做的事是：**把封装、编码、滤镜、协议、设备这一整条流水线，统一抽象成一组结构体和 API**。所有格式都通过同一套 `AVFormatContext` / `AVCodecContext` / `AVPacket` / `AVFrame` 来表达，业务代码只需要写一次。

**它今天的地位**：

| 谁在用 ffmpeg | 用在哪 |
|---|---|
| YouTube / B 站 / 抖音 / 快手 | 服务端转码、转封装、缩略图、转 HLS / DASH |
| OBS / 斗鱼直播姬 | 主播端采集、编码、推流 |
| VLC / mpv / IINA | 桌面播放器整套解码渲染 |
| Chrome / Firefox / Safari | 浏览器内的部分解码路径 |
| WebRTC（chromium 编译时） | `rtc_use_h264=true` + `ffmpeg_branding=Chrome` 启用 H.264 |
| Premiere / Final Cut / DaVinci | 部分格式的导入导出 |

> 你之前写在 `project/WebRTC/05-环境准备清单-macOS.md` 里那一行 gn 参数，就是 WebRTC 为了支持 H.264 把 ffmpeg 链进去的方式。**ffmpeg 不是 WebRTC 的对手，更多时候是 WebRTC 的零件**。

---

## 三、它解决的 7 个核心问题（每个用"类比 + 技术现实 + 解法 + 总结"四段式）

ffmpeg 之所以这么庞大，是因为音视频这一整条链路上要处理的事情，远比"按播放键"看起来复杂。把它的工作拆开看，本质就是这 7 件事。

### 3.1 压缩问题（编码 / Codec）

**类比**：你要寄一本厚书，原书一千页。如果你能找到办法只寄"和上一页相比改了哪几个字"，对方就能根据上一页推出这一页，包裹就轻多了。

**技术现实**：一段 1080p / 30fps 的视频，如果完全不压缩，每秒数据量约 1080 × 1920 × 3 × 30 ≈ 178 MB。一分钟就是 10 GB。没人能扛得住存储和传输。

**解法**：用 H.264 / H.265 / VP9 / AV1 等编码器，利用**帧间冗余**（前后两帧大部分像素一样）和**空间冗余**（同一帧里相邻像素颜色接近）把数据压到原始的几十分之一甚至几百分之一。代价是引入了 I/P/B 帧的依赖关系——B 帧需要参考它后面的 P 帧，导致**解码顺序和播放顺序不一致**，这就是后面会反复出现的 DTS / PTS 区分的根源。

**一句话**：编码是用"重建依赖"换"体积"。便宜了带宽，复杂了时间戳。

### 3.2 组织问题（封装 / Container）

**类比**：编码器产出的只是一段段压缩字节，像一堆散装的乐高积木。但你最终要的是一个完整的 MP4 文件——它得告诉播放器"有几路流、各路是什么格式、第 5 秒应该播哪一帧、关键帧分布在哪里"。这部分元信息和组织规则，由**容器格式**负责。

**技术现实**：

- MP4 把视频字节存在 `mdat` box 里，把"哪一段属于第几秒、关键帧在哪、SPS/PPS 是啥"放在 `moov` box 里。
- FLV、TS、MKV、WebM、AVI 各有各的盒子结构和元信息组织方式。
- 同样一段 H.264 字节，在 MP4 里是用**长度前缀 NALU**（AVCC）切割的，在 TS / 直播流里是用**起始码**（Annex-B）切割的。**编码内容一样，包装方式不同**。

**解法**：`libavformat` 模块——`avformat_open_input` 打开容器，`av_read_frame` 一帧一帧把样本读出来，下游再交给 `libavcodec` 解码。反过来用 `avformat_write_header` / `av_write_frame` / `av_write_trailer` 写出新的容器。**ffmpeg 的 `-c copy` 之所以能在不重新编码的前提下把 MP4 转成 FLV，靠的就是 `libavformat` 这一层在做"换壳"。**

**一句话**：容器是"目录 + 索引 + 时间表"，编码是"内容"。两件事可以独立换。

### 3.3 解释问题（像素格式 / 采样格式）

**类比**：同样一串数字 `[128, 64, 200, ...]`，你说它是温度，是身高，还是颜色？解释方式不同，含义完全不同。视频里的每一帧、音频里的每一段，都是这种"裸字节 + 解释规则"的组合。

**技术现实**：

- 视频帧里同样是"YUV"三个分量，物理排布可以是 **YUV420P**（Y 一整块、U 一整块、V 一整块）、**NV12**（Y 一整块，UV 交错）、**NV21**（Y 一整块，VU 交错，Android 摄像头默认）。一旦解码端按错的格式去读，画面会绿屏、花屏、红蓝反色。
- 音频帧里同样是"双声道 16 位 PCM"，可以是**交错排列**（`L R L R L R…`，给声卡用）或**平面排列**（`L L L L… R R R R…`，给 AAC 编码器用）。解码 AAC 默认输出的是 32 位浮点、平面格式（`FLTP`）；直接把它当成 16 位整数交错（`S16`）扔给 SDL 播放，会听到刺耳杂音。

**解法**：每个 `AVFrame` 都自带一个 `format` 字段（视频是 `AVPixelFormat`，音频是 `AVSampleFormat`），明确告诉下游"我这块字节应该怎么读"。当上下游格式不一致时，用 **`libswscale`**（视频）和 **`libswresample`**（音频）做转换。

**一句话**：格式不是"画质"，是"读字节的规则"。规则错了，画面/声音就一定错。

### 3.4 节奏问题（时间戳 / time_base）

**类比**：电影院放映员有两份单子。一份是"什么时候放下一卷胶片"（DTS，解码时间戳），一份是"屏幕上什么时候出现这张画面"（PTS，显示时间戳）。如果没有 B 帧，这两份单子完全一致；有了 B 帧，胶片要先放后面那一卷，画面却要先显示这一张，两份单子就错开了。

**技术现实**：

- ffmpeg 里所有时间都是**整数**，单位由所在流的 `time_base` 决定。`pts = 90000`、`time_base = 1/90000` 意味着这帧的真实时间是 1 秒。
- 不同模块的 `time_base` 不一样。封装层（demuxer）、编码器、滤镜、muxer 各自有各自的 time_base。一帧数据在它们之间流转时，必须用 `av_rescale_q` 做换算，否则要么播放速度异常，要么时长错乱。
- 音频和视频的同步，本质就是不停比对两路 PTS。**主流播放器以音频时钟为基准**，因为人耳对音频卡顿和变速极其敏感，对视频丢一两帧不敏感。

**解法**：永远记住三件事——时间是整数刻度而非秒；跨模块要转 time_base；同步以音频为主。

**一句话**：时间戳不是时间，是"在某把尺子下的刻度"。换尺子要重新换算。

### 3.5 内存问题（引用计数 / 零拷贝）

**类比**：一份压缩的视频包从解封装器流到解码器，再到滤镜，再到编码器，可能经过 4 - 5 个模块。如果每个模块都拷贝一份，几兆的画面数据来回 `memcpy`，CPU 直接烧穿。

**技术现实**：`AVPacket` / `AVFrame` 内部用**引用计数**管理底层缓冲区。一份缓冲区被多个对象引用，谁都能读，谁都不必拷贝。直到所有人都 `unref` 才真正释放。

**解法**：ffmpeg 提供了一套约定俗成的命名：

| 命名 | 含义 | 比喻 |
|---|---|---|
| `*_alloc` | 分配一个"壳"，里面还没装数据 | 买一个空信封 |
| `*_ref` | 引用计数 +1（浅拷贝） | 给信件加把锁，别人不能扔 |
| `*_unref` | 引用计数 -1，壳还在，下次能复用 | 信件倒掉，信封留着 |
| `*_free` | 壳一起销毁 | 信封也扔进垃圾桶 |
| `*_uninit` | 清理内部状态，对象本身（栈变量）还在 | 清掉家具，房子还在 |
| `*_close` / `*_closep` | 关闭一个已经打开的资源 | 关门 / 关门并把地址擦掉 |

**循环里反复处理一帧时，永远用 `unref` 而不是 `free`**——`alloc` 一次，循环里 `unref` 复用，循环外 `free`，这是 ffmpeg 代码的肌肉记忆。

**一句话**：理解 ffmpeg 的内存模型，就是理解"壳 vs 数据 vs 引用计数"三件事。

### 3.6 加工问题（缩放 / 重采样 / 滤镜）

**类比**：你解码出来的是一帧 1920 × 1080 的 YUV，但下游想要 1280 × 720 的 RGB；或者你解码出来是 44100 Hz 立体声浮点，但下游想要 48000 Hz 单声道 16 位整数。中间这一层"加工车间"必不可少。

**技术现实**：

- **`libswscale`**：处理视频帧的**像素格式转换 + 分辨率缩放**。常见的 YUV420P → RGB24、NV12 → YUV420P 都靠它。
- **`libswresample`**：处理音频的**采样率转换 + 采样格式转换 + 声道布局转换**。FLTP → S16 packed、44.1k → 48k、5.1 downmix 到 stereo 都靠它。
- **`libavfilter`**：更复杂的滤镜图，能在视频上加水印、做画中画、变速、抠像，或者在音频上做均衡、压缩、混音。命令行里 `-vf` `-af` 那一串就是这个模块的接口。

**重要提醒**：`libswscale` 是**纯 CPU 实现**的。如果你的 `AVFrame` 来自硬件解码（NVDEC、VideoToolbox 等），数据还在 GPU 显存里，直接喂给 `sws_scale` 要么崩溃要么极慢。要么先用 `av_hwframe_transfer_data` 把数据下载到内存，要么改用硬件滤镜（`scale_cuda` / `scale_qsv`）在 GPU 上做。

**关于 avfilter（滤镜图）的地图位置**：`libavfilter` 的工作单位是**滤镜图（filtergraph）**——一个有向图，源头是 `buffersrc`（往里灌 AVFrame），出口是 `buffersink`（取处理后的 AVFrame），中间串/并联各种滤镜节点。命令行 `-vf "scale=1280:720,drawtext=..."` 这串就是在描述这张图。本系列对 avfilter 只做地图级介绍（属于阶段 3 内容），真正写滤镜图代码时再展开。

**地图上还有几块拼图（本系列暂浅或未展开，先知道存在）**：

- **第三种流：字幕**。流不只视频+音频，容器里还可能有字幕流（`AVSubtitle`，SRT/ASS 软字幕、PGS 图形字幕）。
- **SEI**（NALU type 6）：H.264/265 里塞自定义数据（时间码、私有 metadata）的口子，WebRTC 常用它透传业务数据。见 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)。
- **DASH / fMP4**：和 HLS 同类的自适应码率分发标准，切片用 fragmented MP4。见 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md)。
- **AV1 / VVC(H.266)**：H.264/H.265 之后的新一代编码（更高压缩率、更高复杂度），移动端正逐步硬件支持。
- **HDR / 宽色域**：BT.2020 色域 + PQ/HLG 传输函数 + 10bit（P010），02 只覆盖 8bit/BT.601/709，HDR 是其延伸。

**一句话**：加工车间分软硬两条线。混着用之前，先确认数据在 CPU 还是 GPU。

### 3.7 运输问题（协议 / 流媒体）

**类比**：本地文件像快递取件，连上去就能拿；网络流像送外卖，路上会堵车、会掉单、会限速。不同的协议是不同的"配送员"——有的求稳（TCP / RTMP / HLS），有的求快（UDP / WebRTC / QUIC），有的伪装成普通包裹混过防火墙（HTTP-FLV）。

**技术现实**：

| 协议 | 底层 | 延迟 | 典型场景 |
|---|---|---|---|
| RTMP | TCP | 1-3 秒 | 主播端推流到服务器（行业事实标准） |
| HLS | HTTP / TCP | 10-30 秒 | 大规模分发（CDN 极友好） |
| HTTP-FLV | HTTP / TCP | 1-3 秒 | 国内观看端主力（穿透防火墙） |
| WebRTC | UDP | < 500 ms | 视频会议、连麦、超低延时直播 |
| QUIC / HTTP/3 | UDP | 极低 | Google 推动的下一代标准 |

**解法**：`libavformat` 不仅认识本地文件，还内置了大量协议实现（`rtmp://` / `http://` / `rtp://` / `srt://` 都能当作输入或输出 URL 用），让业务层用同一套 API 就能切换本地 / 网络源。

**一句话**：协议层决定的是"快、稳、能不能穿防火墙"。和编码、容器是三个独立的轴。

### 3.8 直播协议与封装格式地图（RTMP / WebRTC / MP4 / TS / FLV）

这块最容易混，因为大家口头上会说"RTMP 格式""WebRTC 流""MP4 视频"，但它们其实不在同一层：

```text
编码 Codec：   H.264 / H.265 / VP8 / VP9 / AV1 / AAC / Opus
封装 Container：MP4 / fMP4 / FLV / TS / WebM
协议 Protocol： RTMP / HTTP-FLV / HLS / DASH / WebRTC / RTSP / SRT
```

类比一下：

```text
编码 = 货物怎么压缩打包          H.264 / AAC
封装 = 货物装进什么箱子          MP4 / FLV / TS
协议 = 箱子走哪条运输路线        RTMP / HTTP / UDP / WebRTC
```

所以同样是 H.264 + AAC，可以装进 MP4 做点播文件，也可以装进 FLV 走 RTMP 推流，还可以切成 TS 片段走 HLS 分发。**编码内容可能一样，封装和协议完全不同。**

常见组合先记这张表：

| 场景 | 典型协议/方式 | 常见封装 | 常见编码 | 为什么这么选 |
|---|---|---|---|---|
| 本地文件 / 点播存储 | 文件读取 / HTTP 下载 | **MP4** | H.264/H.265 + AAC | 索引完整、可 seek、兼容性最好 |
| 主播推流到服务器 | **RTMP** | **FLV Tag** | H.264 + AAC | 推流生态成熟，OBS/直播服务器都支持 |
| 低延迟观看端 | **HTTP-FLV** | **FLV** | H.264 + AAC | 走 HTTP/TCP，穿透好，延迟比 HLS 低 |
| 大规模 CDN 分发 | **HLS** | **TS** 或 **fMP4** | H.264/H.265 + AAC | HTTP CDN 友好，抗波动，延迟较高 |
| 低延迟 DASH/CMAF | **DASH / LL-HLS** | **fMP4** | H.264/H.265/AV1 + AAC | 现代点播/直播切片体系，利于 ABR |
| 连麦 / 视频会议 | **WebRTC** | 不走 MP4/FLV/TS，走 RTP/SRTP 包 | VP8/H.264/AV1 + Opus | 端到端实时、弱网控制、延迟最低 |
| 监控 / 摄像头控制 | **RTSP** | RTP 承载，不是 MP4 文件 | H.264/H.265 + AAC/G.711 | 控制和媒体分离，安防生态常见 |

几个重点关系：

- **RTMP vs FLV**：RTMP 是传输协议，FLV 是封装格式。直播推流里常见的是"RTMP 传 FLV Tag"，里面装 H.264/AAC。RTMP 负责连接、握手、分块和发送；FLV 负责把音视频数据组织成 Audio/Video/Script Tag。
- **HTTP-FLV vs RTMP**：两者都常用 FLV 封装。区别是 RTMP 走自己的协议，常用于主播端推流；HTTP-FLV 把 FLV 数据通过 HTTP 响应持续吐给播放器，常用于观看端低延迟播放。
- **HLS vs TS/fMP4**：HLS 是分发协议/播放列表体系，核心是 `.m3u8` 索引 + 一段段媒体切片。早期切片常用 TS，现在也常用 fMP4。HLS 最大优势是 CDN 友好，缺点是天然有切片延迟。
- **MP4 vs fMP4**：普通 MP4 适合完整文件，因为 `moov` 索引描述全片；直播没有"录完"这一刻，所以要么用 FLV/TS 这种流式封装，要么用 fragmented MP4（`moof + mdat`）一段段写。
- **WebRTC 和 MP4/FLV/TS 不是一类东西**：WebRTC 是实时通信栈，媒体通常被编码成 VP8/H.264/AV1 + Opus 后，按 RTP/SRTP 包实时发送；它不把媒体先封成 MP4 或 FLV。WebRTC 强在低延迟、NACK/FEC/Jitter Buffer/拥塞控制，弱在大规模 CDN 分发成本和复杂度。

一条典型直播链路可以这样理解：

```text
主播端 OBS
  摄像头/麦克风
  -> H.264 + AAC 编码
  -> FLV Tag 封装
  -> RTMP 推到服务器

直播服务器
  -> 转 HTTP-FLV：低延迟观看
  -> 转 HLS(TS/fMP4)：大规模 CDN 分发
  -> 转 WebRTC：连麦/超低延迟互动
```

选型直觉：

- **要存文件、能拖进度条**：优先 MP4。
- **要主播推流、生态兼容 OBS**：优先 RTMP + FLV。
- **要普通观众低延迟看直播**：常见 HTTP-FLV。
- **要几百万观众稳定看**：HLS / DASH + CDN。
- **要连麦、会议、低于 500ms**：WebRTC。
- **要安防摄像头拉流和控制**：RTSP/RTP。

---

## 四、系统全景图

把上面 7 个问题映射到 ffmpeg 的模块上，全景大致是这样：

```
                            +--------------------+
                            |   命令行工具入口    |
                            |  ffmpeg / ffprobe  |
                            |       ffplay       |
                            +----------+---------+
                                       |
              +------------------------+------------------------+
              |                                                 |
              v                                                 v
   +----------------------+                          +---------------------+
   |   libavformat        |   <-- 容器 / 协议 -->     |   libavformat        |
   |  封装 / 解封装       |       (mp4/flv/ts/hls/    |   写出新的容器       |
   |  AVFormatContext     |        rtmp/rtsp/http/srt)|                     |
   +----------+-----------+                          +---------+-----------+
              |                                                ^
        AVPacket (压缩字节 + pts/dts + stream_index)            |
              |                                                |
              v                                                |
   +----------------------+                          +---------+-----------+
   |   libavcodec         |   <-- 编码 / 解码 -->     |   libavcodec         |
   |  decode / encode     |     (H.264/H.265/AV1/    |   encode            |
   |  AVCodecContext      |      AAC/Opus/MP3...)    |                     |
   +----------+-----------+                          +---------+-----------+
              |                                                ^
         AVFrame (原始像素或 PCM + pts + format)               |
              |                                                |
              v                                                |
   +----------------------+   +---------------------+   +-----+------------+
   |    libswscale        |   |    libswresample    |   |    libavfilter   |
   |   像素格式 + 缩放      |   |  采样率/格式/声道转换  |   |  水印/裁剪/混音  |
   +----------+-----------+   +---------+-----------+   +-----+------------+
              |                         |                     |
              +-------------+-----------+---------------------+
                            |
                            v
                +---------------------------+
                |  渲染 / 播放 / 重新编码    |
                |  OpenGL / Metal / SDL /   |
                |  AudioTrack / CoreAudio   |
                +---------------------------+

         贯穿全程的基础设施 ↓
         +----------------------------------------+
         |  libavutil                              |
         |  AVRational / AVFrame / mem / log /     |
         |  channel layout / pixel format / time   |
         +----------------------------------------+
         +----------------------------------------+
         |  libavdevice (摄像头 / 麦克风 / 屏幕)    |
         +----------------------------------------+
```

记住几条原则：

- **数据沿 `AVPacket → AVFrame → AVPacket` 流动**。压缩 → 解压缩 → 加工 → 重新压缩。
- **元数据沿 `AVFormatContext → AVStream → AVCodecContext` 分层挂载**。
- **`libavutil` 是地基**，所有模块都依赖它的 `AVRational`、引用计数、像素 / 采样定义。
- **滤镜、缩放、重采样是可选模块**——只在格式不匹配或要做加工时才出现在流水线上。

---

## 五、端到端：一帧画面从 MP4 到屏幕

挑最经典的一条线走一遍——播放 `input.mp4`，画面要显示在窗口里、声音要从喇叭出来。

```
[1] 应用层调用 avformat_open_input("input.mp4")
        |
        v
[2] libavformat 读 ftyp / moov，识别出有 1 路视频（H.264）+ 1 路音频（AAC）
        |
        v
[3] 调用 av_read_frame 拿到一个 AVPacket
    （pkt->stream_index 告诉你这是视频包还是音频包）
        |
        +-------------------- 视频路 ----------------------+
        |                                                  |
        v                                                  v
[4v] 视频解码器（libavcodec）                      [4a] 音频解码器（libavcodec）
     avcodec_send_packet(video_ctx, pkt)              avcodec_send_packet(audio_ctx, pkt)
     avcodec_receive_frame(video_ctx, frame)          avcodec_receive_frame(audio_ctx, frame)
        |                                                  |
        v                                                  v
[5v] AVFrame: format=YUV420P, w=1920, h=1080       [5a] AVFrame: format=FLTP, sr=44100, ch=2
     pts 由 video stream 的 time_base 描述              pts 由 audio stream 的 time_base 描述
        |                                                  |
        v                                                  v
[6v] 渲染管线想要 RGB24（或者 GPU 直接吃 YUV）       [6a] SDL/AudioTrack 想要 S16 packed, 48000Hz
     用 sws_scale 转 YUV420P → RGB24                     用 swr_convert 转 FLTP → S16 + 重采样
        |                                                  |
        v                                                  v
[7v] 提交给 OpenGL / Metal 渲染纹理              [7a] 写进设备 ring buffer，声卡按节奏消费
        |                                                  |
        +--------------- 音视频同步 -----------------------+
                        |
                        v
                以音频时钟为主，视频根据 audio_clock 决定
                是立刻显示、延迟显示，还是丢帧追赶
```

**这条主线串起了 ffmpeg 的几乎所有核心模块**。读其他细节文档时，可以随时回到这张图，问自己"我现在在 [1]-[7] 哪一步"。

---

## 六、5 个必须先建立直觉的关键技术点

这一节只给"是什么 / 为什么需要 / 一句话总结"，**不深入实现**。深入细节看本目录其它文档。

### 6.1 AVPacket vs AVFrame

- **是什么**：`AVPacket` 是压缩字节（编码器/解码器的边界），`AVFrame` 是原始数据（YUV/PCM 这种能直接渲染/听的）。
- **为什么需要**：编码器只认 `AVPacket`，渲染器只认 `AVFrame`。强行混用就 segfault。
- **一句话**：进出解码器是分水岭。进去前都是 packet，出来后都是 frame。

### 6.2 time_base 是一把刻度尺

- **是什么**：流的 `time_base` 是 `AVRational(num, den)`，表示"一个刻度等于多少秒"。`pts * time_base = 实际秒数`。
- **为什么需要**：整数运算精确、跨平台一致、能表达"1001/30000"这种非整数帧率。
- **一句话**：永远不要把 pts 当秒读，要么乘 `av_q2d(time_base)`，要么用 `av_rescale_q` 跨模块换算。

### 6.3 引用计数：unref ≠ free

- **是什么**：`AVPacket` / `AVFrame` 是"信封 + 信件"。`alloc` 买信封，`ref` 加锁，`unref` 倒掉信件留信封，`free` 信封也扔了。
- **为什么需要**：循环处理每秒几十帧，不允许 alloc / free 在循环里抖动。
- **一句话**：循环里只 `unref`，初始化和结束才 `alloc` / `free`。这条规则一旦背反，必出泄漏或 segfault。

### 6.4 Planar vs Packed

- **是什么**：同样的数据，可以**交错存放**（Packed，`L R L R L R`、`R G B R G B`），也可以**分平面存放**（Planar，先全部 L 后全部 R、Y/U/V 三块独立）。
- **为什么需要**：声卡 / 显示器爱 Packed（一路扫过去就能用），编码器爱 Planar（每个分量独立运算更高效）。
- **一句话**：FFmpeg 解 AAC 默认输出 `FLTP`（浮点平面）、解 H.264 默认输出 YUV420P（平面）。如果下游是声卡或简单渲染，几乎一定要先转 Packed。

### 6.5 AVCC vs Annex-B

- **是什么**：H.264 的 NALU 在 MP4 里前面跟 4 字节长度（AVCC），在直播 / RTP / 裸 `.h264` 里前面跟起始码 `00 00 00 01`（Annex-B）。
- **为什么需要**：MP4 已经有索引表能精确定位每个 NALU，不需要起始码；直播流没有索引表，需要靠起始码切边界。
- **一句话**：从 MP4 取出的 H.264 字节，直接喂给 Android MediaCodec / 大多数硬解码器会黑屏——必须用 `h264_mp4toannexb` 这个 bitstream filter 转一次。这是音视频开发最经典的踩坑现场。

---

## 七、新人最容易踩的 6 个坑

| 坑 | 错觉 | 事实 | 避坑 |
|---|---|---|---|
| `linesize` 等于 `width × 字节数` | 一行像素紧挨着排 | 为了 SIMD 对齐，每行末尾常有 padding | 拷贝一定按 `linesize` 走，别按 `width` 算 |
| `pts` 是秒 | `pts = 2` 就是第 2 秒 | `pts` 是刻度，要乘 `time_base` 才是秒 | 跨模块用 `av_rescale_q` |
| 一个音频 packet = 一帧 | `send_packet` 后 `receive_frame` 一次就够 | 一个 AAC packet 可能含多帧，要 while 循环读到 `EAGAIN` | 音频解码必须循环 receive |
| `send_packet` 后立即修改 packet | 数据已经被吃进去了 | 默认浅拷贝（引用计数），底层还在被解码器异步使用 | 永远用 `av_packet_unref` 释放 |
| 把 MP4 里的 H.264 直接喂硬解 | 都是 H.264 应该通用 | MP4 是 AVCC，硬解通常只认 Annex-B | 加 `h264_mp4toannexb` bsf |
| `sws_scale` 处理硬件帧 | 反正都是 `AVFrame` | `sws_scale` 只能处理 CPU 内存里的数据 | 先 `av_hwframe_transfer_data` 下载，或改用硬件滤镜 |

---

## 八、行业现状 / 生态

- **服务端转码**：YouTube / Netflix / B 站等基本都基于 ffmpeg + 自研调度框架做大规模转码集群。商业方案如 AWS MediaConvert、阿里云 / 腾讯云的转码服务底层也是 ffmpeg + 各家定制编码器。
- **客户端播放器**：VLC / mpv / IINA 是开源代表；移动端的 ijkplayer（B 站开源）、ExoPlayer（部分场景）、AVPlayer（iOS 系统）也大量复用 ffmpeg。
- **直播 / 短视频**：抖音、快手、B 站直播的服务端节点几乎都跑 ffmpeg + 自研的流媒体服务（SRS、ZLMediaKit 这些开源 SRS 也基于 ffmpeg）。
- **音视频会议 / RTC**：WebRTC、声网 Agora、即构 ZEGO 这些产品在编码端会用到 ffmpeg / x264 / openh264，但 RTC 的核心传输逻辑（NACK / FEC / 拥塞控制）通常自己写。**ffmpeg 提供的是"编码 + 容器"，不是"实时传输"**。
- **替代品 / 配套**：x264（H.264 编码器，常被 ffmpeg 链入）、x265（H.265）、libfdk_aac（高质量 AAC 编码）、libwebrtc（Google 的 WebRTC 全家桶）、GStreamer（另一个流媒体框架，更模块化但学习曲线更陡）。

---

## 九、应用现实（音视频岗位）

国内招音视频工程师的方向大致是：

- **直播 / 短视频**：抖音、快手、B 站、腾讯视频。重点在推拉流、首屏秒开、卡顿率、ABR 自适应码率。
- **RTC / 视频会议**：腾讯会议、字节飞书会议、阿里钉钉、声网、即构。重点在弱网传输、回声消除、jitter buffer。
- **音视频 SDK / 引擎**：商汤、旷视的图像处理 SDK；网易云信、融云。重点在跨平台 C++、硬件加速。
- **云转码 / CDN**：阿里云、腾讯云、华为云、火山引擎。重点在大规模任务调度 + ffmpeg 命令行调优。
- **多媒体编辑**：剪映、必剪、WonderShare。重点在滤镜、合成、特效、GPU 渲染。

**ffmpeg 的位置**：

- 在直播 / 转码 / 编辑岗位里，ffmpeg 是**核心吃饭技能**——会用 ffmpeg API、能改 ffmpeg 源码、能调编码器参数是硬要求。
- 在 RTC 岗位里，ffmpeg 是**支撑技能**——你要懂它，但每天工作的重点是 WebRTC / 自研 RTC 引擎，ffmpeg 通常只在编码端出现。

所以你 WebRTC 项目和 ffmpeg 笔记并行积累的路径，是合理的：**ffmpeg 是地基，WebRTC 是它上面的一栋楼**。

---

## 十、学习路径建议（结合你目前的位置）

把 ffmpeg 的学习粗略分四阶段：

### 阶段 0：会查、会跑命令

- 知道 `-i` `-c:v` `-c:a` `-b:v` `-vf` `-bsf` 这些常见参数。
- 能用 `ffprobe` 看一个文件的 codec / 分辨率 / 关键帧分布。
- 能用 `ffplay` 验证一段裸 PCM。

### 阶段 1：理解数据结构与生命周期

- AVPacket / AVFrame / AVCodecContext / AVFormatContext 的关系。
- 引用计数的 alloc / ref / unref / free / uninit / close 命名规则。
- `time_base` 的换算。

### 阶段 2：会写一条完整的流水线

- 用 C++ 写一个最简播放器（demux → decode → swscale → 显示 + swresample → 播放）。
- 写一个简单转码器（demux → decode → encode → mux）。
- 写一个 `MP4 → H.264 裸流` 的 mp4toannexb 工具。

### 阶段 3：进入工程化深水区

- 硬件编解码（VideoToolbox / NVENC / QSV）。
- 滤镜图（avfilter）。
- 与渲染层、网络层、音频设备的协作。
- 性能调优、内存控制、跨平台坑。

---

## 十一、重点考点（按面试出现频率粗排）

- AVPacket vs AVFrame 区别、各自字段、生命周期（**几乎必考**）
- PTS / DTS 区别，B 帧为什么导致 DTS ≠ PTS
- I/P/B 帧 + GOP，直播为什么用短 GOP
- YUV 为什么不用 RGB，4:2:0 子采样
- I420 / NV12 / NV21 的区别，Android 摄像头的红蓝反色
- Planar vs Packed，AAC 解码为什么是 FLTP
- MP4 vs H.264 的层级，AVCC vs Annex-B
- SPS / PPS 是什么，每个 IDR 前为什么要注入
- 音视频同步策略，为什么以音频为主时钟
- RTMP / HLS / HTTP-FLV / WebRTC 对比，以及 MP4 / TS / FLV 封装场景
- TCP 队头阻塞 vs UDP，QUIC 为什么用 UDP
- H.264 Profile / Preset / Tune / CRF / CBR / VBR
- 硬解码 vs 软解码，硬件帧如何下载

---

## 十二、当前阶段定位（关键）

**对照 ffmpeg 的 7 个核心问题 + 5 个技术直觉，看你已经覆盖了多少：**

| 主题 | 你的笔记覆盖度 | 状态 |
|---|---|---|
| 数据结构（AVPacket/AVFrame/AVCodecContext） | 4 份文档反复讲，已掌握 | ✅ |
| 引用计数与资源生命周期 | `ffmpeg_resource_lifecycle_notes.md` 整理得很系统 | ✅ |
| 像素格式与内存布局（YUV/NV12/I420/linesize） | `pixel_format_memory_layout_guide.md` 覆盖到位 | ✅ |
| SwsContext 缩放与转换 | `swscontext_lecture.md` 含硬件帧的讨论 | ✅ |
| 音频 PCM / 采样格式 / 重采样 | `cpp音视频开发音频问题与面试指南.md` 1100+ 行，深度足够 | ✅ |
| H.264 / MP4 / NALU / AVCC vs Annex-B | 4 份文档（有重复），知识点齐了 | ✅ |
| 码控（Profile / Preset / Tune / CRF / CBR） | `码率-Profile.md` 完整 | ✅ |
| 硬件编解码 | `hardware_codec_learning_guide.md` 是路径建议，**没动手实践** | ⚠️ |
| 网络协议 / 流媒体 | 两份文档覆盖 TCP/UDP/RTMP/HLS/HTTP-FLV/WebRTC/QUIC | ✅ |
| **time_base 和音视频同步的深入实现** | 笔记里多处提到，但**没有自己写过一个同步逻辑** | ⚠️ |
| **完整端到端流水线（demux→decode→render→mux）** | API 层都懂，**没有从零写一遍组装代码** | ❌ |
| **avfilter 滤镜图** | 笔记里基本没出现 | ❌ |

**结论**：

- 你不是 ffmpeg 新手——**单点知识非常密集**。
- 但你目前的状态是**"零散点很多 + 缺一张地图 + 缺端到端工程实践"**。
- 这正好对应 engineering-overview skill 来源经验里那句"感觉啥都没学到，一直在执行脚本"——零散知识没有被一根主线串起来。

**你现在的位置**：阶段 1.5。理论储备已经超过阶段 1，但没有阶段 2 的"亲手写一条流水线"的肌肉记忆。

---

## 十三、接下来该做什么（一个明确的下一步）

**先回到这份导读，把第 3、4、5、6 节读两遍，确认脑子里有以下这张图，能脱稿画出来：**

```
文件 → 解封装 → AVPacket → 解码 → AVFrame → 加工 → 渲染
                                      ↓
                                   时间戳贯穿所有环节
                                   引用计数管理所有缓冲区
```

**确认建立地图感之后，下一步只做一件具体的事**：

> **笔记重组已完成**（16 份零散笔记 → 01-10 主题文档 + 本导读 + README 索引，原稿归档在 `_archive/`）。所以下一步不再是整理文档，而是**动手写最简播放器**：`demux MP4 → decode H.264 → swscale 转 RGB → SDL 显示`。

把 AVPacket / AVFrame / time_base / 引用计数 / Planar-Packed 从"读过"推进到"骨头里的"，比再读十遍笔记管用。具体待办、验收标准、踩坑记录见 [99-学习进度.md](./99-学习进度.md)——它是这个学习项目的单一真相源，**每次推进先读它**。

---

## 十四、自检题（合上文档你应该能回答）

1. 同样一段 H.264 字节，在 MP4 里和在裸 `.h264` 文件里有什么不同？为什么直接喂硬解码器会黑屏？
2. 解码 AAC 默认输出什么格式？为什么直接送 SDL 播放会有刺耳杂音？要做什么转换？
3. `AVPacket` 和 `AVFrame` 各自存的是什么？编码器和解码器分别接收 / 输出哪种？
4. `linesize` 一定等于 `width * 字节数` 吗？如果按 `width` 拷贝会出现什么现象？
5. `pts` 的单位是秒吗？怎么把它换算成秒？跨模块传递时要怎么处理？
6. `av_packet_unref` 和 `av_packet_free` 的区别？循环处理一万帧应该用哪个？
7. 视频流里 DTS 和 PTS 什么时候相等？什么时候不等？为什么？
8. RTMP / HLS / HTTP-FLV / WebRTC 各自底层用 TCP 还是 UDP？它们和 MP4 / TS / FLV 这些封装格式分别是什么关系？
9. `libswscale` 能处理来自硬件解码（NVDEC / VideoToolbox）的帧吗？为什么？正确做法是什么？
10. ffmpeg 在 WebRTC 项目里扮演什么角色？为什么 WebRTC 不能用 ffmpeg 替代？

能流畅回答这 10 题，说明你已经从"知识点拼图"进入"全景认知"阶段。
