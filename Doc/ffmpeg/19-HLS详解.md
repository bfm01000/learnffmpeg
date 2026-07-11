# 19 - HLS 详解（面试导向 + 全景）

> 对应导读第 3.7 节"运输问题"。这是 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §6.3 **HLS 那一格的深挖**——专讲"为什么大规模直播分发用 HLS"和"怎么把 HLS 切好、延迟做低"。
> 前置：编码看 [11-H264与H265详解.md](./11-H264与H265详解.md) / [06-编码参数与码控.md](./06-编码参数与码控.md)，TS 封装看 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5，协议大局看 [08](./08-网络协议与流媒体.md) §六。

---

## 一、面试速记卡（先背这个）

**一句话定义**：HLS（HTTP Live Streaming）是 Apple 2009 年提出的**基于 HTTP 的流媒体分发协议**——把连续视频切成一个个小文件（.ts 或 fMP4），用 .m3u8 播放列表告诉播放器"有哪些切片、按什么顺序播"。本质是把"流"变成"下载静态小文件"。

**关键点（爱考）**：

- **角色**：**分发**（服务器 → 海量观众），不是推流（那是 RTMP），不是互动（那是 WebRTC）。
- **底层**：HTTP/HTTPS（端口 80/443），TCP 短连接（每次下载一个切片就断），天然穿透防火墙。
- **延迟**：传统 HLS 10-30 秒（受 切片时长 × 缓冲数 影响），LL-HLS 可降到 2-3 秒。
- **CDN 极度友好**：切片就是普通 HTTP 静态文件，CDN 缓存命中率极高——千万人看同一个切片，请求根本到不了源站。
- **每个 .ts 切片必须自包含**：自带 PAT/PMT + SPS/PPS + IDR 关键帧，任意分片独立可播。FFmpeg 用 `-hls_flags independent_segments` + `-force_key_frames` 保证。
- **多码率自适应（ABR）**：Master Playlist 列出多档码率，播放器根据网络自动切换——卡了就降码率，好了就升回来。

**HLS 分发链路一图**：

```
主播 OBS → RTMP 推流 → 源站(转码+切片) → CDN(缓存 .ts + .m3u8) → 观众播放器(HTTP 拉切片)
                          │
                          ├─ 多码率转码(720p/1080p/4K)
                          ├─ 每个码率独立切片
                          └─ 生成 Master .m3u8
```

---

## 二、面试高频问答（口语化答题模板，开口就能用）

> 这一节是"张嘴就能说"的完整话术，练顺嘴用；文末 §八 是同样问题的**关键词速记版**，临场背要点用。

**Q1：讲一下 HLS 的工作原理？**

> 嗯，HLS 的核心思想就是把"流"拆成"文件"。服务端把直播或点播的视频切成一段一段的小文件，每段大概几秒钟，然后生成一个 .m3u8 播放列表，里面按顺序列出这些切片文件的 URL。
>
> 播放器拿到 .m3u8 之后，就按顺序一个接一个地用 HTTP 去下载这些切片，下载完一个播一个，播完继续下下一个。因为走的是标准 HTTP，所以能穿透任何防火墙，能被 CDN 完美缓存。
>
> 对于多码率场景，还有一个 Master Playlist，它不直接列切片，而是列出"有几个码率可选、每个码率的子 .m3u8 在哪"。播放器先下 Master Playlist，选一个码率，再去下拉对应码率的子 playlist，然后按子 playlist 的列表拉切片。
>
> 一句话收口：**源站切片 → 写 m3u8 → CDN 分发 → 播放器按列表顺序 HTTP 下载切片 → 无缝拼接播放。**

**Q2：HLS 的延迟为什么那么高？怎么降到 3 秒以内？**

> HLS 的默认延迟 = 切片时长 × 播放器缓冲的分片数。比如 6 秒一切片，播放器为防卡顿攒 3 个才开播，从产生到观众看到就过去了 18 秒。
>
> 优化有三个层次：第一层是简单粗暴的——**缩短切片时长**，6 秒变 1 秒。但代价是切片数翻了 6 倍，CDN 回源压力变大、m3u8 文件变大、压缩效率也会因为 GOP 变短而轻微下降。
>
> 第二层是苹果官方的 **LL-HLS（Low-Latency HLS）**。它在 2019 年引入，核心是把一个切片内部再拆成更小的 partial segments（约 200ms 一块），不等整个切片写完，边写边通过 HTTP/2 push 推给播放器，播放器也边下边播——这样延迟能压到 2-3 秒。要求服务器和 CDN 都支持 HTTP/2。
>
> 第三层是换赛道——用 **CMAF + chunked transfer encoding**。CMAF 把 fMP4 的 moof+mdat 做成 chunk，编码器产出一块就发一块，播放器收到一块就播一块，延迟可到 1-2 秒，而且和 DASH 共用一套格式。
>
> 一句话收口：**传统 HLS 靠缩短切片降到 3-6s 延迟但伤 CDN；LL-HLS/CMAF 靠"边切边发"降到 1-3s 不伤 CDN，但要求基础设施更新。**

**Q3：你切 HLS 的时候第二个分片为什么也要带 SPS/PPS/PAT/PMT？**

> 这个问题的核心是"自包含"——任何一个 .ts 分片，不管它是第几个，都必须能被播放器独立解码。
>
> 解码一段视频需要"说明书"：SPS 告诉解码器分辨率是多少、PPS 告诉我编码档次和熵编码模式、PAT/PMT 告诉我这个 TS 流里有几条音视频轨道、音频的 AudioSpecificConfig 告诉我采样率和声道数。这些信息合在一起叫"解码元数据"。
>
> 为什么每个分片都得带？因为观众不一定从第一个分片开始看——拖进度条跳到第 30 分钟，播放器只会请求第 30 分钟对应的那个 .ts，不会去下载前面的文件。如果这个分片里没有 SPS/PPS/PAT/PMT，解码器拿到数据后根本不知道分辨率、不知道编码档次、不知道有几条轨道——**永远黑屏**。CDN 切换、网络重连这些场景也一样——每次都从"当前时刻对应的分片"重新开始拉。
>
> 所以切 HLS 的时候，每个 .ts 分片都必须：① 自带 PAT/PMT；② 第一帧是 IDR 关键帧；③ IDR 之前有 SPS/PPS。FFmpeg 的 `-hls_flags independent_segments` 加上 `-force_key_frames` 就是干这个的。
>
> 补充一个对比：CMAF/fMP4 走了另一条路——把"说明书"单独抽出来叫 init segment，下载一次存着，后面所有 media segment 共用它，不用每个分片都重复带 SPS/PPS，省带宽。但代价是必须保证每个 media segment 开头有 IDR，否则还是切不进去。

**Q4：HLS 的 .m3u8 播放列表长什么样？Master Playlist 和 Media Playlist 有什么区别？**

> .m3u8 就是 UTF-8 编码的纯文本文件，用特定的 tag 来描述播放信息。有两种：
>
> **Media Playlist**（也叫子 playlist）：直接列出切片文件。开头是 `#EXTM3U` 声明这是 m3u8，然后 `#EXT-X-VERSION` 声明版本号，`#EXT-X-TARGETDURATION` 声明切片最大时长。之后每一行 `#EXTINF` 后面跟切片时长，下一行就是切片的 URL。如果是直播，末尾会有 `#EXT-X-ENDLIST` 表示这是完整列表（点播），没有这个 tag 的就是还在直播中，播放器会定时重新拉 m3u8 看有没有新切片。
>
> **Master Playlist**（多码率场景才有）：不直接列切片，而是用 `#EXT-X-STREAM-INF` 列出"这个码率叫什么名、带宽多少、对应的子 .m3u8 在哪"。播放器先读这个，根据当前网速选一个码率，再去拉对应的子 playlist。
>
> 一句话区分：**Master Playlist 是"菜单"（有哪些码率可选），Media Playlist 是"菜谱"（具体哪些切片按什么顺序上）。**

---

## 三、HLS 协议原理

### 3.1 整体架构：把"流"变成"文件"

HLS 的核心洞察：**直播本质上是一串连续的媒体数据，只不过这串数据还在不断生成中**。把已经生成好的那部分，按固定时长切成文件，再写一个文本文件记录这些切片的 URL——播放器就按文本文件的顺序 HTTP 下载。

```
时间轴 →

原始直播流:  ════════════════════════════════════════════════▶
切片后:      [00.ts] [01.ts] [02.ts] [03.ts] [04.ts] [05.ts] ...
m3u8 列表:   #EXTINF:5.0,\n00.ts\n#EXTINF:5.0,\n01.ts\n...

播放器视角:  下载 00.ts → 播放 → 下载 01.ts → 播放 → 下载 02.ts → ...
```

这套架构有三个关键收益：
1. **HTTP 化**：不用 RTMP/RTSP 那些"非标"协议，80/443 端口畅通无阻
2. **CDN 缓存**：切片文件就是静态文件，CDN 天然能缓存
3. **解耦**：源站只负责切片，分发全部甩给 CDN——源站压力极小

### 3.2 .m3u8 播放列表详解

#### Media Playlist（直播，无 ENDLIST）

```m3u8
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:42
#EXTINF:5.200,
segment_42.ts
#EXTINF:5.100,
segment_43.ts
#EXTINF:5.350,
segment_44.ts
```

- **`#EXT-X-TARGETDURATION`**：承诺所有切片时长 ≤ 这个值（用于播放器预估下载间隔），**不是平均值**
- **`#EXT-X-MEDIA-SEQUENCE`**：第一个切片的序号（直播中 m3u8 是滑动窗口，旧切片会被移出列表，这个序号一直递增）
- **`#EXTINF`**：下一个切片的实际时长（秒），紧接着一行是 URL
- **没有 `#EXT-X-ENDLIST`**：告诉播放器"这还不是完整列表，过几秒再来拉一次 m3u8 看看有没有新切片"——**这是区分直播和点播的关键 tag**

#### Media Playlist（点播，有 ENDLIST）

```m3u8
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:0
#EXTINF:5.200,
segment_00.ts
#EXTINF:5.100,
segment_01.ts
...
#EXTINF:3.800,
segment_99.ts
#EXT-X-ENDLIST
```

末尾的 `#EXT-X-ENDLIST` 就是宣告"没了，播完就结束，不用再拉了"。

#### Master Playlist（多码率 ABR）

```m3u8
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS="avc1.4d001e,mp4a.40.2"
360p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720,CODECS="avc1.4d001f,mp4a.40.2"
720p/index.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=5000000,RESOLUTION=1920x1080,CODECS="avc1.4d0028,mp4a.40.2"
1080p/index.m3u8
```

- **BANDWIDTH**：这个码率档的**峰值带宽**（不是平均），播放器用来判断"当前网络够不够格"——面试爱考这个坑
- **RESOLUTION**：分辨率（宽×高）
- **CODECS**：编码格式字符串（`avc1.4d001e` = H.264 Baseline 3.0, `mp4a.40.2` = AAC-LC）

#### 播放器拉流时序（直播场景）

```
时间 T0:  GET master.m3u8     → 拿到 3 个码率档，选 720p
时间 T0:  GET 720p/index.m3u8  → 拿到当前切片列表: seg_0 ~ seg_2
时间 T0:  GET seg_0.ts        → 开始播
时间 T5:  GET seg_1.ts        → seg_0 快播完了，无缝接 seg_1
时间 T10: GET 720p/index.m3u8  → 重新拉 m3u8（直播滑动窗口，旧的下架了新的上来了）
时间 T10: GET seg_3.ts        → ...
```

### 3.3 .ts 分片的内部结构

每个 .ts 分片就是一个完整的、独立的 MPEG-TS 文件。了解内部结构有助于理解"为什么每个分片都能独立解码"。

```
.ts 分片 = PAT + PMT + (可选 SIT/其他表) + PES 包流

PAT (Program Association Table) — PID 固定 0x0000
  └─ 列出节目号和 PMT 的 PID 对应关系
     例如: program_number=1 → PMT PID=0x100

PMT (Program Map Table)
  └─ 列出这个节目有哪些流、每个流的 PID 和编码类型
     例如: stream_type=0x1B(H.264) → elementary_PID=0x101
           stream_type=0x0F(AAC)   → elementary_PID=0x102

PES (Packetized Elementary Stream) — 音视频原始数据
  ├─ H.264 PES (PID=0x101) = PES header + 一帧 H.264 数据
  │    ├─ 每帧开头带 PTS/DTS
  │    └─ 关键帧之前带 SPS/PPS（通过 TS 自适应字段或专门的 PES）
  └─ AAC PES   (PID=0x102) = PES header + 多帧 AAC raw 数据
       └─ AAC 数据不含 ADTS 头（PES 头里有时间戳）
```

**关键细节**：PAT/PMT 在每个 .ts 分片里都会重复出现——这就是"自包含"在 TS 层的体现。即使播放器只拿到这一个 .ts，也知道流里有几条轨道、各是什么编码。

### 3.4 自包含切片——HLS 最核心的概念

> 这是面试里 HLS 部分**最高频的追问**，对应 Q3（上面的问答）和 [self-check Q17](../project/WebRTC/03-音视频基础self-check.md#q17-什么是封装格式的自包含和非自包含为什么-hls-切片要自包含)。

#### 定义

**自包含（self-contained）**：单个文件/分片自带全部解码必需的元数据，不依赖任何前置文件即可独立解码。

解码需要哪些"说明书"？

| 元数据 | 层 | 内容 | 没有它会怎样 |
|---|---|---|---|
| SPS | H.264 视频 | 分辨率、profile/level、参考帧数 | 解码器不知道画面多大，直接失败 |
| PPS | H.264 视频 | 熵编码模式（CABAC/CAVLC）、量化参数初值 | 不知道怎么解析 slice 数据 |
| PAT | MPEG-TS 容器 | 节目号和 PMT PID 的映射 | 不知道 PMT 在哪、节目有几路流 |
| PMT | MPEG-TS 容器 | 每路流的 PID 和编码类型 | 不知道流里有几条音视频轨道 |
| AudioSpecificConfig | AAC 音频 | 采样率、声道数、profile | 音频解码器不知道输出格式 |
| IDR 关键帧 | H.264 视频 | 不依赖任何帧的完整图像 | 没办法开始解码（参考帧全都不在） |

**非自包含**：缺少上面任意一项。例如普通 MP4 的非首个 fragment——SPS/PPS 只在文件头的 moov box 里，后面的 fragment 自己不带；播放从中间开始时解码器不知道在哪找。

#### 为什么 HLS 必须自包含？

三个根本原因：

1. **随机切入（seek）**：用户拖进度条，只请求目标位置的 .ts，不会下载前面的分片
2. **CDN 多节点**：同一个播放会话中，不同分片可能命中不同 CDN 节点——每个 CDN 节点都独立提供文件，没有"上下文"
3. **网络中断重连**：断了再连，播放器从"断点对应的分片"继续拉，不可能回溯已播过的分片

核心原则一句话：**播放器每一次发起 HTTP GET 都是独立的、无状态的，分片之间在服务端没有任何关联。**

#### FFmpeg 怎么保证？

```bash
ffmpeg -i input.mp4 \
  -c:v libx264 -c:a aac \
  -force_key_frames "expr:gte(t,n_forced*5)" \
  -hls_flags independent_segments \
  -hls_time 5 \
  -hls_segment_filename "seg_%03d.ts" \
  output.m3u8
```

两个关键参数：

- **`-force_key_frames "expr:gte(t,n_forced*5)"`**：强制每 5 秒（和切片时长对齐）产出一个 IDR 关键帧，保证每个分片的第一帧就是 IDR。`n_forced` 是已产出的强制关键帧计数，`gte(t, n_forced*5)` 的含义是"当前时间 t ≥ 该产下一个关键帧的时刻时，强制出 IDR"。
- **`-hls_flags independent_segments`**：告诉 muxer"每个分片都要独立可播"。具体做的事包括——在每个分片里重新插入 PAT/PMT、确保 IDR 之前有 SPS/PPS、音频流开头有 AudioSpecificConfig。

#### 和 CMAF/fMP4 的对比

CMAF/fMP4 走了另一条思路——把"说明书"和"数据"分离：

```
传统 HLS (.ts):
  每个分片 = 说明书(PAT/PMT/SPS/PPS) + 数据(IDR + P/B帧 + 音频)
  代价: 每个分片都要重复传说明书，浪费几百字节/分片

CMAF/fMP4:
  init segment (下载一次) = 说明书(moov box = SPS/PPS + 音频配置)
  media segment 1         = 数据(moof + mdat, 开头是 IDR)
  media segment 2         = 数据(moof + mdat, 开头是 IDR)
  ...
  代价: 必须先下载 init segment，否则所有 media segment 都解不了
       每个 media segment 开头仍必须是 IDR
```

**选型口诀**：传统点播/HLS 兼容性优先用 TS 切片；追求低延迟 + 省带宽用 CMAF/fMP4。

---

## 四、FFmpeg 切 HLS 实战

### 4.1 基础用法（最简版）

```bash
ffmpeg -i input.mp4 -c:v copy -c:a copy \
  -hls_time 5 \
  -hls_list_size 0 \
  -hls_segment_filename "seg_%03d.ts" \
  output.m3u8
```

| 参数 | 含义 |
|---|---|
| `-hls_time 5` | 每个切片目标 5 秒（不是绝对的——会在最近的 IDR 处切，所以实际可能 ±1 秒） |
| `-hls_list_size 0` | 0 = 保留所有切片在 m3u8 里（点播）；直播设 > 0（如 5）表示滑动窗口最多保留 5 个 |
| `-hls_segment_filename` | 切片文件命名模板 |

**注意**：`-c:v copy` 不重新编码，切片位置跟随原始流的 GOP 结构。如果原始流的关键帧间隔是 10 秒，那切片时长会漂到 10 秒左右。想要精确时长控制，必须重新编码 + `-force_key_frames`。

### 4.2 保证自包含的完整命令（生产推荐）

```bash
ffmpeg -i input.mp4 \
  -c:v libx264 -preset fast -crf 23 \
  -c:a aac -b:a 128k \
  -force_key_frames "expr:gte(t,n_forced*5)" \
  -sc_threshold 0 \
  -hls_time 5 \
  -hls_flags independent_segments \
  -hls_segment_type mpegts \
  -hls_list_size 0 \
  -hls_segment_filename "seg_%03d.ts" \
  output.m3u8
```

参数解释：

| 参数 | 为什么 |
|---|---|
| `-force_key_frames "expr:gte(t,n_forced*5)"` | 强制每 5 秒一个 IDR，和切片时长对齐——**这是保证自包含的核心** |
| `-sc_threshold 0` | 关闭场景切换检测（不因画面突变而出 IDR，完全由 force_key_frames 控制节奏） |
| `-hls_flags independent_segments` | 每个分片独立可播——重插 PAT/PMT、确保 SPS/PPS 在 IDR 前 |
| `-hls_segment_type mpegts` | 显式声明用 TS 容器（默认就是 mpegts，fmp4 是 CMAF 模式） |

### 4.3 多码率自适应（ABR）

```bash
# 三步走：先转多码率 → 再各切 HLS → 再合 Master Playlist

# 步骤 1：转码出多档码率（这里用 filter 分三路，也可以单独跑三次 ffmpeg）
ffmpeg -i input.mp4 \
  -filter_complex "[0:v]split=3[v1][v2][v3]; \
    [v1]scale=640:360[v360]; \
    [v2]scale=1280:720[v720]; \
    [v3]scale=1920:1080[v1080]" \
  -map "[v360]"  -c:v:0 libx264 -b:v:0  800k -maxrate:v:0  856k -bufsize:v:0 1200k \
  -map "[v720]"  -c:v:1 libx264 -b:v:1 2000k -maxrate:v:1 2140k -bufsize:v:1 3000k \
  -map "[v1080]" -c:v:2 libx264 -b:v:2 5000k -maxrate:v:2 5350k -bufsize:v:2 7500k \
  -map 0:a -c:a aac -b:a 128k \
  -force_key_frames "expr:gte(t,n_forced*5)" \
  -sc_threshold 0 \
  -hls_flags independent_segments \
  -hls_time 5 \
  -var_stream_map "v:0,a:0 v:1,a:0 v:2,a:0" \
  -master_pl_name master.m3u8 \
  -hls_segment_filename "v%v/seg_%03d.ts" \
  v%v/index.m3u8
```

- **`-var_stream_map`**：定义"哪些视频流 + 哪些音频流"组合成一个 variant stream
- **`%v`**：variant stream 索引（0, 1, 2…），用在文件名和目录名里
- **`-master_pl_name`**：自动生成 Master Playlist

**关键坑**：ABR 场景每个码率的关键帧必须**时间对齐**（同一个 GOP 边界），否则切码率时画面会跳。`-force_key_frames` 对所有码率用同一个表达式就自然对齐了。

---

## 五、HLS 延迟优化

### 5.1 延迟来源分析

```
端到端延迟 = 编码器缓冲 + 切片等待 + 上传 CDN + 播放器缓冲

                       切片等待(切片时长)
 原始帧产生 ──────────────────────────────▶ .ts 文件写完
                      │                    │
                   GOP 要凑够             上传 CDN
                   IDR 才切                      │
                                           CDN 边缘节点缓存
                                                  │
                                           播放器下载 + Jitter Buffer(攒 3 个切片)
                                                  │
                                           观众看到画面
```

**大头**：
1. **切片时长**：必须等一个完整切片写完才能上传，6 秒切片 = 6 秒等待
2. **播放器缓冲**：为防止卡顿，通常攒 3 个切片才开播，6 秒 × 3 = 18 秒
3. **编码器 lookahead**：如果开了 B 帧或 lookahead，编码器内部也有一帧到几帧的延迟

### 5.2 传统优化手段

| 手段 | 效果 | 代价 |
|---|---|---|
| 缩短切片时长（6s → 2s）| 延迟压到 6-8 秒 | 切片数 ×3，m3u8 变大，CDN 回源压力增大 |
| 关闭 B 帧（`-bf 0`）| 编码器缓冲降为 0 | 压缩效率损失 10-15% |
| `-tune zerolatency` | 编码器全线零缓冲 | 画质下降（关闭了 lookahead 和 B 帧） |
| 播放器缓冲减为 2 个 | 省一个切片时长 | 网络抖动时容易卡顿 |

**极限组合**（延迟优先，牺牲画质和 CDN 效率）：

```bash
ffmpeg -i input -c:v libx264 -tune zerolatency -bf 0 \
  -force_key_frames "expr:gte(t,n_forced*1)" \
  -hls_time 1 -hls_flags independent_segments ...
```

1 秒切片 + zerolatency ≈ 端到端 3-4 秒延迟。

### 5.3 LL-HLS（Low-Latency HLS）

Apple 2019 年引入，核心思路：**不等整个切片写完就开始分发**。

```
传统 HLS:  编码整个切片 → .ts 写完 → CDN 缓存 → 播放器下载 → 播
LL-HLS:    编码产出 partial segment(200ms) → HTTP/2 push → 播放器边下边播
           ↑                                               ↑
           不等整个切片写完                                 不等全部下载完
```

关键机制：
- **`#EXT-X-PART-INF`**：声明 partial segment 的最大时长
- **`#EXT-X-PART`**：标记一个 partial segment
- **`#EXT-X-PRELOAD-HINT`**：预告"下一个 partial segment 马上要来"，播放器提前发起请求（HTTP/2 server push 或阻塞式 GET）
- **`#EXT-X-SERVER-CONTROL`**：`CAN-BLOCK-RELOAD=YES`——播放器请求 m3u8 时告诉服务器"没新数据别急着回，挂住等到有新切片再回"（长轮询），避免空转轮询

LL-HLS m3u8 示例：

```m3u8
#EXTM3U
#EXT-X-VERSION:9
#EXT-X-TARGETDURATION:4
#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,PART-HOLD-BACK=1.000
#EXT-X-PART-INF:PART-TARGET=0.200
#EXT-X-MEDIA-SEQUENCE:100
#EXTINF:4.000,
segment_100.ts
#EXT-X-PART:DURATION=0.200,URI="seg_101_part_0.ts"
#EXT-X-PART:DURATION=0.200,URI="seg_101_part_1.ts"
#EXT-X-PART:DURATION=0.200,URI="seg_101_part_2.ts"
#EXT-X-PRELOAD-HINT:TYPE=PART,URI="seg_101_part_3.ts"
```

**现状**：LL-HLS 需要服务器（nginx-rtmp-module / wowza / shaka packager）、CDN（支持 HTTP/2 push 和 blocking reload）都升级，覆盖率还在爬坡。国内主流 CDN 目前支持一般。

### 5.4 LL-HLS vs CMAF chunked transfer

| 维度 | LL-HLS | CMAF chunked transfer |
|---|---|---|
| 标准制定 | Apple | MPEG（ISO） |
| 播放列表 | .m3u8 | .mpd（DASH）+ 也可以用 .m3u8 |
| 切片格式 | TS（传统）或 fMP4 | fMP4（强制） |
| 延迟 | 2-3 秒 | 1-2 秒 |
| CDN 要求 | HTTP/2 push + blocking reload | HTTP/1.1 chunked encoding 就够了（更易部署） |
| 生态 | Apple 生态原生支持 | 跨平台、播放器有 hls.js / dash.js / shaka-player |

---

## 六、HLS vs 其他协议的工程权衡

| 维度 | HLS | RTMP | HTTP-FLV | WebRTC |
|---|---|---|---|---|
| 延迟 | 10-30s（LL-HLS 2-3s）| 1-3s | 1-3s | < 500ms |
| 穿透防火墙 | ★★★★★（80/443）| ★★☆☆☆（1935 常被封）| ★★★★★ | ★★★☆☆（需 STUN/TURN） |
| CDN 友好 | ★★★★★（HTTP 静态文件）| ★★☆☆☆（长连接）| ★★★☆☆ | ★☆☆☆☆ |
| 大规模分发 | ★★★★★ | ★☆☆☆☆ | ★★★★☆ | ★★☆☆☆ |
| 协议复杂度 | ★★☆☆☆ | ★★★☆☆ | ★★★☆☆ | ★★★★★ |
| 苹果生态 | ★★★★★（原生）| ☆☆☆☆☆（不支持）| ★★★★☆（hls.js）| ★★★★☆ |

**一句话选型**：
- **十万+并发直播分发** → HLS（CDN 帮你扛，源站几乎无压力）
- **国内低延迟直播观流** → HTTP-FLV（B 站方案，延迟低 + 穿透好）
- **主播推流** → RTMP（生态最成熟）
- **互动/连麦/会议** → WebRTC

**常见的混合架构**：

```
主播 → RTMP 推流 → 源站转码 → ┬→ HLS  切片 → CDN → 大多数观众（延迟容忍）
                               └→ HTTP-FLV   → 追求低延迟的观众（国内）
                               └→ WebRTC     → 连麦互动用户
```

---

## 七、常见坑与排查

| 坑 | 现象 | 原因 | 解决 |
|---|---|---|---|
| **第二个分片起新观众黑屏** | 从头播正常，跳到中间黑屏 | 分片不自包含——缺 SPS/PPS 或第一帧不是 IDR | 加 `-hls_flags independent_segments` + `-force_key_frames` |
| **切片时长不稳定** | 设了 5 秒，实际 3~10 秒波动 | `-c:v copy` 模式下跟随原始流 GOP，关键帧不规律 | 重新编码 + `-force_key_frames`；或接受 copy 模式下的波动 |
| **m3u8 越来越大** | 直播跑几小时后 m3u8 几 MB | 没有设 `-hls_list_size`，所有历史切片都在列表里 | 设 `-hls_list_size 5`（只保留最近 5 个） |
| **音视频不同步** | 播一会嘴型对不上 | TS 切片的 PTS 起始值没有重置，连续播放时累积误差 | 加 `-output_ts_offset` 重置每个分片的起始 PTS |
| **切换码率时画面跳一下** | ABR 场景切档时卡顿 | 不同码率的关键帧时间没对齐 | 所有码率用同一个 `-force_key_frames` 表达式（帧率对齐的话 GOP 自然对齐） |
| **AAC 编码无声音** | .ts 视频正常但没声音 | AAC raw 缺 ADTS 头且 PES 里 AudioSpecificConfig 异常 | 确保 `-hls_flags independent_segments` 打开（它会在音频流加 AudioSpecificConfig） |
| **CDN 回源爆炸** | 万人直播源站 CPU/带宽跑满 | 切片太短（1s）导致文件数暴增，CDN 缓存命中率下降 | 切片时长换回 4-6 秒；或用 LL-HLS 让 CDN 缓存仍按整切片粒度 |
| **m3u8 跨域问题** | 网页播放器报 CORS 错误 | .m3u8 和 .ts 没有 CORS 头 | CDN/服务器加 `Access-Control-Allow-Origin: *` |

---

## 八、面试关键词速记表（临场背要点）

| 关键词 | 一句话 |
|---|---|
| HLS 本质 | HTTP 短连接 + 切片文件 + m3u8 播放列表 |
| 延迟公式 | 切片时长 × 播放器缓冲数 |
| 自包含 | 每个 .ts 自带 PAT/PMT/SPS/PPS + 开头 IDR，独立可播 |
| independent_segments | FFmpeg flag，保证分片自包含 |
| force_key_frames | 强制对齐 IDR 到切片边界 |
| LL-HLS | partial segment 200ms 边切边发，延迟 2-3s |
| Master vs Media Playlist | Master = 菜单（有哪些码率），Media = 菜谱（具体切片列表） |
| ENDLIST | 有 = 点播（播完结束），无 = 直播（持续拉 m3u8） |
| HLS vs DASH | 同类协议，HLS 苹果生态原生，DASH 编码无关跨平台 |
| 切片格式选型 | TS = 兼容性优先，fMP4 = 低延迟 + 省带宽 + DASH 复用 |
| ABR 关键帧对齐 | 所有码率必须同一 GOP 边界切，否则切码率跳动 |
| CDN 为什么友好 | 切片就是 HTTP 静态文件，CDN 天然缓存 |

---

## 九、延伸阅读

- [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §6.3 — HLS 在协议全景中的定位
- [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5 — MPEG-TS 188 字节包、PCR 原理
- [12-RTMP推流详解.md](./12-RTMP推流详解.md) — RTMP 推流端（和 HLS 组成"推流→分发"的常见组合）
- [project/WebRTC/03-音视频基础self-check.md](../project/WebRTC/03-音视频基础self-check.md#q17) — Q17 自包含概念自检
- [Apple HLS Specification](https://developer.apple.com/documentation/http-live-streaming) — 官方协议规范
- [RFC 8216](https://datatracker.ietf.org/doc/html/rfc8216) — HLS 的 IETF 标准化文档
