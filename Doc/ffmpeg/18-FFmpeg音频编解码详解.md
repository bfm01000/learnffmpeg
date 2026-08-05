# 18 - FFmpeg 音频编解码详解（AAC / Opus / MP3）

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | 音频编码专题：AAC/Opus/MP3、frame_size、extradata、PTS 和编码 API。 |
| 先背什么 | AAC/Opus 怎么选、nb_samples 怎么填、ADTS/extradata 是什么要能讲。 |
| 深入怎么学 | 结合 04 的 PCM 地基和 24 的同步策略看。 |
| 关联阅读 | 04、24、音频专项 |

---

> **适用方向**：音视频开发、直播推流、播放器开发、WebRTC——任何需要用 FFmpeg 做音频编解码的岗位。
> **难度分层**：中级（必须掌握）/ 高级（进阶加分）分界线见 §1.6
> **预计阅读**：速记 15 分钟｜全文 50 分钟
> **前置知识**：PCM 基础（采样率/位深/通道/Planar vs Packed）。如果还不清楚，先看 [04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md)。
> **关联知识**：[05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)（视频编解码对照）、[08-网络协议与流媒体.md](./08-网络协议与流媒体.md)（RTMP/FLV 里音频怎么封装）、[03-SwsContext-图像缩放与格式转换.md](./03-SwsContext-图像缩放与格式转换.md)（视频版 swscale，对应音频的 swresample）

---

## 一、全景导读：FFmpeg 音频编解码在技术版图里的位置

### 1.1 从场景说起

**场景 1——直播推流**：你用 FFmpeg 从麦克风采集 PCM，要编码成 AAC 推 RTMP。编码器要求 FLTP（float planar），但麦克风给你的是 S16（16-bit signed packed）。不转换的话，AAC 编码器直接报错或输出纯噪声。

**场景 2——播放器开发**：你解封装拿到 AAC 音频包，直接 `fwrite` 存成 `.aac` 文件。系统播放器打开——「无法识别格式」。为什么？因为容器里是 raw AAC，没有 ADTS 帧头。要能播放，要么补头存 ADTS，要么用 FFmpeg 的 ADTS muxer。

**场景 3——WebRTC 音频选型**：项目要求端到端延迟 < 50ms、要抗 10% 丢包。AAC 最低编码延迟也要 ~20ms 一帧，加上网络抖动缓冲区，端到端 60ms 是极限了。技术选型时有人提 Opus——5ms 一帧、内置 FEC、免专利费。选哪个？为什么？

**场景 4——转码**：线上 10 万首 MP3 要统一转成 AAC-LC 192kbps，用 FFmpeg 做。写完脚本一跑——输出文件时长不对、有的多几秒有的少几秒。排查发现 MP3 是 VBR 编码、 decoder 输出的 `nb_samples` 不等于 1152（MP3 标准帧采样数），PTS 重新计算逻辑有 bug。

这四个场景贯穿了 FFmpeg 音频编解码的全部核心知识：**编解码器选型 → 压缩格式的码流结构 → FFmpeg 编解码 API → 编码器输入约束 → 时间戳与帧采样数的关系**。

### 1.2 它是什么 / 它从哪来（前世今生）

- **一句话定义**：FFmpeg 音频编解码层位于「PCM 裸数据」和「压缩音频包」之间——它用 `libavcodec` 提供统一的 send/receive API，封装了 AAC、Opus、MP3、FLAC、Vorbis 等几十种音频编解码器的差异。

- **产生背景**：音频压缩的需求比视频早得多——MP3（MPEG-1 Audio Layer III）1993 年就发布了。但早期的音频编码标准各管各的：MP3 只管音乐、G.711 只管电话、AAC 兼容两者。FFmpeg 的价值在于**用一套统一的 API 屏蔽了所有编解码器的差异**——你不需要知道 AAC 和 Opus 内部算法完全不同，调的都是 `avcodec_send_frame` / `avcodec_receive_packet`。

- **发展脉络**：
  - 1993：**MP3** 发布，掀起数字音乐革命。有损压缩、心理声学模型，10:1 压缩比。
  - 1997：**AAC** 随 MPEG-2 发布，MP3 的继任者。同码率画质/音质更好，引入 ADTS/ADIF 两种码流格式。
  - 2000s：AAC 成为流媒体（HLS/RTMP）和苹果生态（iTunes/iPod）的默认音频格式。
  - 2012：**Opus** 由 IETF 标准化（RFC 6716），融合 Skype 的 SILK（语音）和 Xiph 的 CELT（音乐）双核，专为互联网实时通信设计。
  - 2015+：WebRTC 强制必选 Opus（RFC 7587），AAC 仍是点播/直播的主力，MP3 逐步边缘化但仍大量存于存量内容。
  - 至今：**AAC 是兼容性之王，Opus 是低延迟之王，MP3 是存量之王**。

- **今天的位置**：FFmpeg 的音频编解码器矩阵覆盖了从 8kbps 窄带语音到 510kbps 全频段音乐的完整频谱。原生 AAC 编码器（`aac`）和 Opus 编码器（`libopus`）是两个使用频率最高的音频 codec。

### 1.3 为什么需要它（技术优势与选型理由）

音频编解码要解决的核心问题：**PCM 太大了，不压缩根本没法传**。算一笔账：

```
立体声 48kHz 16bit PCM 一秒 = 48000 × 2(bytes) × 2(channels) = 192 KB/s ≈ 1.5 Mbps
一部 1 小时播客裸 PCM ≈ 192KB × 3600 ≈ 675 MB
```

AAC 192kbps 压缩后同样的内容 ≈ **86 MB**（压缩比 ~8:1），Opus 64kbps 约 **29 MB**（压缩比 ~23:1），而人耳几乎听不出差别。

但选哪个编码器不是只有压缩比一个维度：

| 维度 | AAC | Opus | MP3 |
|------|-----|------|-----|
| 压缩效率（同音质码率） | 基准 | AAC 的 ~70% | AAC 的 ~130% |
| 编码延迟 | ~20-40ms | **2.5-60ms 可配** | ~40-60ms |
| 专利授权 | 需付费（MPEG LA） | **免专利费** | 已过期（2017） |
| 硬件覆盖 | 几乎所有设备 | 中高端设备 | 所有设备 |
| 浏览器支持 | ✅ 全部 | ✅ 全部（WebRTC） | ✅ 全部 |
| 适用场景 | 点播/直播/存档 | **实时通信** | 存量兼容 |

> **一句话选型**：实时通信选 Opus（低延迟 + 抗丢包），点播/直播选 AAC（兼容无敌），存量内容兼容用 MP3。

### 1.4 大厂如何使用

| 谁在用 / 什么场景 | 怎么用 | 解决什么问题 |
|-----------------|--------|------------|
| YouTube | 视频音轨统一转 Opus（WebM）+ AAC（MP4）双轨 | 高版本浏览器走 Opus 省带宽（~30% vs AAC），低版本/老设备回退 AAC |
| Netflix | 5.1 环绕声用 AAC / DD+，立体声用 Opus | 不同音轨配置不同 codec，带宽自适应切换 |
| WebRTC（Chrome/Firefox） | Opus 必选（RFC 7587），fallback 到 PCMU/PCMA | 强制 Opus 保证互通，G.711 给传统电话网关 |
| Apple Music / iTunes | AAC-LC 256kbps VBR（`.m4a`） | 苹果生态原生 AAC 硬解，从 iPod 时代延续至今 |
| 国内直播平台（抖音/快手/B站） | RTMP 推流 AAC-LC 128-192kbps CBR，观众拉流 HLS/FLV 也是 AAC | FLV 只支持 AAC/MP3，CDN 分发链路对 AAC 优化最成熟 |

### 1.5 关联技术地图

```
                        ┌──────────────────────────────┐
                        │   FFmpeg 音频编解码           │
                        │   avcodec_send_frame /        │
                        │   avcodec_receive_packet      │
                        └──────────────────────────────┘
                           │              │
            ┌──────────────┼──────────────┼──────────────┐
            │              │              │              │
            v              v              v              v
       ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
       │  AAC    │   │  Opus   │   │  MP3    │   │  FLAC   │
       │ (有损)  │   │ (有损)  │   │ (有损)  │   │ (无损)  │
       └─────────┘   └─────────┘   └─────────┘   └─────────┘
            │              │
            │              │
            v              v
    ┌──────────────────────────────────┐
    │  编码输入: AVFrame (PCM 裸数据)   │ ← swresample 在这一步之前归一化
    │  解码输出: AVFrame (PCM 裸数据)   │ ← swresample 在这一步之后转换
    └──────────────────────────────────┘
            │
            │ 关联
            v
    ┌──────────────────────────────────┐
    │  PCM / swresample                │ → [04-音频PCM-采样-重采样.md]
    │  Planar vs Packed / 采样率 / 位深 │   (编解码的上下游都依赖它)
    └──────────────────────────────────┘
            │
            │ 关联
            v
    ┌──────────────────────────────────┐
    │  容器层                          │ → [05-H264-MP4-NALU.md]
    │  FLV: AAC sequence header        │   [08-网络协议与流媒体.md]
    │  MP4: esds box                   │   参数集(extradata)由容器提供
    │  TS:  ADTS 内联                  │   码流格式由封装决定
    └──────────────────────────────────┘
```

### 1.6 学习优先级总览

| 层级 | 内容 | 重要度 | 说明 |
|------|------|--------|------|
| 🟢 中级必会 | AAC ADTS 头结构、raw AAC vs ADTS、AudioSpecificConfig 解析、补 ADTS 头的代码；AAC/Opus/MP3 选型对比；`avcodec_send_frame/receive_packet` 编解码循环；`frame_size` 与 `nb_samples` 的正确填写；最后一帧 Padding 处理；`AVAudioFifo` 解决输入/输出采样数不匹配 | 🔥🔥🔥 | 日常开发 80% 的音频问题都能用这些解决 |
| 🟡 高级加分 | Opus 双模（SILK/CELT）原理、Opus 帧长与延迟的关系；AAC-LD/ELD 等低延迟子规格的 frame_size 差异（512/480/960）；FFmpeg 音频 parser 用法；VBR 下 PTS/nb_samples 漂移处理；`AVCodecContext.extradata` 的音频版本（AudioSpecificConfig / OpusHead） | 🔥🔥 | 处理复杂场景（WebRTC、VBR 转码、格式自动检测）时用到 |
| 🔵 专家深水区 | HE-AAC v2（SBR+PS 参数集）、Opus FEC 的码流级实现、心理声学模型调参、xHE-AAC/USAC | 🔥 | 需要读 ISO 14496-3 / RFC 6716 原始规范，日常不碰 |

---

## 二、面试速记（考前 15 分钟扫一遍）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 出现频率 | 难度 |
|---|------|-----------|---------|------|
| 1 | 为什么 FLV/MP4 里的 AAC 直接存不能播 | 容器里是 raw AAC（无帧头），播放器需要 ADTS 或 AudioSpecificConfig | 🔥🔥🔥 | 🟢 |
| 2 | ADTS 头 sync word 是什么 | `0xFFF`（12 bit），第 1 字节 `0xFF` 且第 2 字节高 4 位 `0xF` | 🔥🔥🔥 | 🟢 |
| 3 | ADTS 头里采样率怎么存 | `sampling_frequency_index` 4 位查表——3=48000, 4=44100, 8=8000 | 🔥🔥 | 🟢 |
| 4 | AAC 和 Opus 选型对比 | 实时通信（低延迟+抗丢包）→ Opus；点播/直播（兼容性）→ AAC | 🔥🔥🔥 | 🟢 |
| 5 | Opus 为什么延迟比 AAC 低 | 帧长可配 2.5-60ms（AAC 固定 ~20ms+），内置 FEC 免重传等待 | 🔥🔥🔥 | 🟢 |
| 6 | `avcodec_send_frame` 编码时 `nb_samples` 怎么填 | 取自 codec 的 `frame_size`；`frame_size=0` 表示接受任意值（如 Opus 最大 5760） | 🔥🔥 | 🟢 |
| 7 | AAC 的 `frame_size` 是 1024 的含义 | 每帧编码 1024 个采样点。48kHz 下 = 1024/48000 ≈ 21.3ms 音频 | 🔥🔥 | 🟢 |
| 8 | `extradata` 在音频 codec 里是什么 | 编解码初始化参数——AAC 里是 AudioSpecificConfig（2 字节），Opus 里是 OpusHead（19 字节） | 🔥🔥 | 🟢 |
| 9 | 音频编码 PTS 怎么算 | `pts = nb_samples × 帧序号 × time_base.den / time_base.num / 采样率`，或更简单用递增 `pts = frame_index × frame_size`（同 time_base） | 🔥🔥 | 🟡 |
| 10 | FLTP 和 S16 的区别 | FLTP=float planar（AAC 编解码默认），S16=16-bit signed packed（声卡默认）。**AAC 编码器输入必须是 FLTP 或 S16** | 🔥🔥 | 🟢 |

### 2.2 面试标准回答



#### Q1：从 FLV 里拿到 AAC 数据，直接保存能播放吗？为什么？

**面试官想听什么：** 考察你是否理解「容器内的 AAC 是 raw AAC，缺少解码器初始化所需的帧头」。

**🗣️ 标准回答（可背诵）：**

> "不能直接播。FLV 和 MP4 容器里的 AAC 是 raw AAC——每帧纯压缩数据紧挨着排，没有同步标记也没有参数头。解码器初始化需要的两个核心参数——采样率和通道数——不是写在每帧前面，而是存在容器的配置记录里。FLV 里叫 AAC sequence header（即 AudioSpecificConfig），MP4 里在 esds box。
>
> 你把 raw AAC 直接存成 .aac 文件，系统播放器打开——找不到 sync word、不知道采样率、不知道通道数——直接报「无法识别格式」。
>
> 解决方法有两个：一是给每帧补 7 字节 ADTS 头，把 `sampling_frequency_index`、`channel_configuration` 和 `frame_length` 写进去；二是直接用 FFmpeg 的 ADTS muxer：`ffmpeg -i input.flv -c:a copy -f adts output.aac`，muxer 会自动补头。"

**👨‍💻 追问预警：**
> 面试官很可能接着问：「AudioSpecificConfig 是 2 字节的结构，怎么从里面拿到采样率？」
> 应对思路：`byte[0]` 的高 5 位是 `audioObjectType`（AAC-LC=2），低 3 位 + `byte[1]` 高 1 位组成 4 位的 `samplingFrequencyIndex`，`byte[1]` 的中间 4 位是 `channelConfiguration`。

---

#### Q2：AAC 的 ADTS 和 ADIF 区别？为什么 ADIF 基本淘汰了？

**面试官想听什么：** 考察对码流格式本质的理解——不是背定义，是理解 trade-off。

**🗣️ 标准回答（可背诵）：**

> "两者的本质区别是同步信息放哪。ADTS 把参数和帧长放在每一帧的 7 字节头里——sync word `0xFFF` + 采样率索引 + 通道数 + 帧长度。所以解码器能从任意位置切入：扫到 `0xFFF`、读头、初始化解码器、解这一帧。这个设计思路和 H.264 的 Annex-B 起始码 `0x00000001` 是一模一样的——都是让码流自己描述自己。
>
> ADIF 反过来，只在文件最开头放一个全局头，后面全是裸的 raw_data_block。好处是省了每帧 7 字节的重复开销。但它不能随机切入、不能丢文件头——丢了整个文件就废了。这个设计对标的是 H.264 的 AVCC 格式放在 MP4 里的效果——参数只存一次。但实际上有了 MP4/FLV 这样的成熟容器，ADIF 提供的'全局头'功能完全被容器替代了，所以 ADIF 基本消亡了。"

**⚠️ 常见误区：**
> 有人以为 ADTS 头里的 `frame_length` 只包含数据部分——其实它包含 7 字节头本身。写代码时 `frame_length = 7 + raw_data_len`，写错了会导致帧边界错位、解码器丢帧。

---

#### Q3：Opus 是什么？为什么 WebRTC 默认用它而不是 AAC？

**面试官想听什么：** 考察对 WebRTC 音频选型的理解，以及能否说清楚 Opus 相比 AAC 的技术优势。

**🗣️ 标准回答（可背诵）：**

> "Opus 是 IETF 标准化（RFC 6716）的现代音频编码器，专门为互联网实时通信设计的。WebRTC 把它作为必选 codec（RFC 7587 强制），核心原因有五个：
>
> 第一，**低延迟**。Opus 帧长可配 2.5ms 到 60ms——5ms 帧时端到端延迟可以做到约 22ms。AAC 一帧固定编码 1024 个采样点，48kHz 下一帧就是 ~21ms，还没算上网络缓冲。实时通信要的是 <50ms 的端到端，AAC 在这个门槛上很吃力。
>
> 第二，**双模融合**。Opus 内部有两个编码核：低码率（<32kbps）用 SILK 模式，这个原本是 Skype 的语音编码器，对窄带人声效果极好；高码率用 CELT 模式，来自 Xiph 的音乐编码器。中间还能混合——所以 Opus 从 6kbps 窄带语音到 510kbps 全频段音乐无缝切换，一个 codec 通吃。AAC 没有这种灵活性。
>
> 第三，**内置抗丢包 FEC**。Opus 可以在当前包里附带上一包的低码率备份——如果那包丢了，解码器用 FEC 信息恢复，不需要等重传。AAC 没有这个机制，丢包就只能听天由命。
>
> 第四，**免专利费**。Opus 是 BSD 许可证，完全免费。AAC 需要向 Via Licensing 交专利费。
>
> 第五，**采样率全覆盖**。Opus 支持 8/12/16/24/48kHz，内部统一 48kHz 处理。AAC 同样支持多种采样率但索引表有限。
>
> 但 AAC 也有 Opus 比不了的优势：硬件解码覆盖近乎 100%，文件存储生态完善，所有播放器都认。所以有人说：**Opus 是传输之王，AAC 是存储之王。**"

---

#### Q4：AAC 和 Opus 和 MP3，什么场景选哪个？

**面试官想听什么：** 考察选型能力——能根据不同约束条件做出有理有据的 trade-off。

**🗣️ 标准回答（可背诵）：**

> "核心看三个约束：延迟要求、兼容性范围、是否在乎专利费。
>
> 实时通信（视频通话、连麦互动）首选 Opus——低延迟（<25ms 端到端）、抗丢包 FEC、免专利费，WebRTC 必选 codec。直播推流首选 AAC-LC——CDN 对 AAC 的 FLV/RTMP/HLS 分发链完善到牙齿，所有观众端的播放器都能解。音乐/播客点播也是 AAC——和 H.264 配合装 MP4，生态成熟。如果内容要从零开始生成，新项目选 AAC 不选 MP3。
>
> MP3 现在只在一种场景下主动使用：你手上有存量 MP3 文件、需要兼容非常老的设备（比如 2010 年前的 MP3 播放器、车载老系统）。其他时候——同码率下 AAC 音质更好、延迟更低，MP3 的专利也已经过期了，但音质和技术上已经被 AAC 全面超越。
>
> FLAC 是无损压缩，压缩比约 2:1（PCM 的一半大小），适合音乐存档、母带保存——不是流式传输的选项。
>
> 一句话：实时选 Opus，分发选 AAC，兼容老内容用 MP3，存档用 FLAC。"

---

#### Q5：用 FFmpeg 编码 AAC，AVFrame 的 `nb_samples` 怎么填？为什么有时候不是 1024？

**面试官想听什么：** 考察 FFmpeg 音频编码 API 实战经验。

**🗣️ 标准回答（可背诵）：**

> "`nb_samples` 是 AVFrame 里包含的每声道采样点数，即这个 frame 能存几拍的音频。编码 AAC 时，这个值必须等于 `AVCodecContext.frame_size`——AAC-LC 固定是 1024。编码器每次 consume 1024 个采样点，吐出一个 AVPacket。
>
> 但 `frame_size` 不是所有编码器都固定。Opus 的 `frame_size=0`，意味着接受可变值——你可以每次送 960 个采样点（20ms@48kHz）或 1920 个（40ms），只要不超过最大值 5760。MP3 的 `frame_size=1152`。所以初始化 codec 之后，**一定要读 `codec_ctx->frame_size`**，不要硬编码 1024。
>
> 另外有个关键的接口细节：如果 `frame_size=0`（Opus 就是这种情况），你需要自己决定 `nb_samples`。FFmpeg 提供了一个安全上限：`codec_ctx->max_nb_samples`。Opus 返回 5760，即 120ms@48kHz 的采样点数。
>
> 面试时很容易被追问的一个点是：`avcodec_send_frame` 后，**一帧 PCM 不一定立即吐出一个包**。编码器内部有 lookahead 和 delay——send 一帧后 receive 可能返回 EAGAIN（需要继续送），最后 send NULL 刷出缓存的包。这和视频编码 send/receive 的逻辑完全一致。"

---

#### Q6：音频编码的 PTS 怎么算？和视频有什么不同？

**面试官想听什么：** 考察音频时间戳计算能力。

**🗣️ 标准回答（可背诵）：**

> "音频 PTS 的核心公式比视频简单，但更容易写错——因为音频的 '帧' 有歧义。视频一帧 AVFrame 是一张完整的画面（1080p → 1080 行），PTS 是这张画面应该显示的时刻。音频一帧 AVFrame 不是 '一段完整的音乐'，而是 `nb_samples` 个采样点——每个采样点只是时间轴上的一瞬间。
>
> 计算方式：如果 time_base 是 1/48000（采样率时基），每帧初始 PTS 从 0 开始，递增步长 = `nb_samples`。第 0 帧 PTS=0，第 1 帧 PTS=1024，第 2 帧 PTS=2048...
>
> 如果 time_base 不同，需要用 `av_rescale_q`：
>
> ```
> packet.pts = av_rescale_q(frame_index * frame_size,
>                           (AVRational){1, sample_rate},
>                           codec_ctx->time_base);
> ```
>
> **关键坑**：MP3 VBR 编码时，解码器输出 `nb_samples` 可能不等于 1152（标准帧采样数）。如果用固定步长 `1152` 去算 PTS，而实际解码器某帧只出了 1104 个采样点——PTS 就会漂移，累积导致音画不同步。正确的做法是直接用 `frame->nb_samples` 做累加步长。
>
> **和视频 PTS 的本质区别**：视频一帧 AVFrame 是一张完整的画面（所有行），PTS 是这张画面应该显示的时刻；音频一帧 AVFrame 是 `nb_samples` 个采样点，PTS 是第一个采样点应该被播放的时刻。视频一帧 = 1 个时间点，音频一帧 = 一段连续时间。"

---

### 2.3 一个「串起来」的完整回答模板

> 面试官：「从麦克风采集到 AAC 推流，整个音频链路怎么做的？」

**🗣️ 「总-分-总」3 分钟完整回答：**

> "这条链路分三步——采集归一化、编码、封装推流。
>
> **第一步，采集归一化。** 麦克风通过系统 API——Android 用 AudioRecord、iOS 用 AudioUnit、桌面用 PortAudio——给你 PCM。格式通常是 S16、单声道或立体声、44100Hz 或 48000Hz。但 AAC 编码器要求输入 FLTP（float planar）且采样率匹配。所以采集之后、编码之前，要先用 FFmpeg 的 `SwrContext` 做重采样：把 S16 转 FLTP、采样率统一到目标值、单声道 expand 到立体声（或反过来）。这个转换叫 '归一化'，是所有音频链路的标配。
>
> **第二步，编码。** 用 `avcodec_find_encoder(AV_CODEC_ID_AAC)` 找到编码器，`avcodec_alloc_context3` + `avcodec_open2` 初始化。AAC 的 `frame_size=1024`，所以每次构造 `AVFrame` 时填 `nb_samples=1024`，`format=AV_SAMPLE_FMT_FLTP`，然后 `avcodec_send_frame` + `avcodec_receive_packet` 循环编码。注意编码器有 lookahead——send 最后一帧后要 send NULL 把内部缓冲刷出来。
>
> **第三步，封装推流。** 编码出的 AVPacket 是 raw AAC——没有 ADTS 头。如果是推 RTMP，FLV muxer 会在第一个 Audio Tag 里写入 AAC sequence header（AudioSpecificConfig），后续 Tag 存 raw AAC 帧。时间戳这块要特别注意：AAC 一帧 1024 采样点 = 21.3ms@48kHz，每帧 PTS 递增 1024（在 1/48000 的 time_base 下）。如果用固定的 PTS 步长 1152（MP3 的）——时间全错，播放卡顿。
>
> 您想让我展开讲 SwrContext 的配置，还是 AAC 编码器的具体参数？"

---

## 三、原理深讲（周末花 1 小时吃透）

### 3.1 AAC 码流格式：raw AAC / ADTS / ADIF

#### 3.1.1 🟢 AAC 的三种形态

FFmpeg 处理 AAC 时，你会遇到三种不同的码流形态，搞混了就是播不出来或转封装失败：

| 形态 | 在哪出现 | 有没有帧头 | 参数在哪 |
|------|---------|----------|---------|
| **raw AAC** | FLV/MP4 容器内部 | ❌ 没有同步字、没有帧长 | 容器头的 AudioSpecificConfig |
| **ADTS** | `.aac` 文件、HLS `.ts` 切片 | ✅ 每帧 7 字节头（sync word `0xFFF`） | 每帧头里自带 |
| **ADIF** | （历史遗产） | ⚠️ 文件头有全局参数 | 全局头，已淘汰 |

raw AAC ↔ ADTS 的转换是日常最高频操作：FFmpeg 用 `bsf=aac_adtstoasc` 做 ADTS→raw（剥帧头+提取 extradata），用 `-f adts` muxer 做 raw→ADTS（补帧头）。

#### 3.1.2 🟢 ADTS 头逐 bit 拆解（7 字节无 CRC 版本）

```
字节 0:  syncword 高 8 位 (0xFF)
字节 1:  syncword 低 4 位(0xF) | ID(0) | layer(00) | protection_absent(1=无CRC)
字节 2:  profile(2bit) | sampling_frequency_index(4bit) | private(0) | channel_config 高1位
字节 3:  channel_config 低2位 | original_copy(0) | home(0) | copyright_id(0)
        | copyright_start(0) | frame_length 高2位
字节 4:  frame_length 中8位 (bit 10..3)
字节 5:  frame_length 低3位 | buffer_fullness 高5位
字节 6:  buffer_fullness 低6位 | number_of_raw_data_blocks(00)
        之后是 raw_data_block（AAC 压缩数据）开始
```

**四个最容易写错的字段（已踩过坑的标注）：**

1. **`profile`** = `audioObjectType - 1`。AAC-LC 的 audioObjectType=2，所以 profile 填 **1**。直接填 2 会导致 iOS CoreAudio 拒绝解码。
2. **`sampling_frequency_index`** 是查表索引不是 Hz 值。48000→3，44100→4，8000→8。填原始 Hz 值会导致溢出。
3. **`frame_length`** = 7 + raw 数据长度（包含头本身）。13 位最大 8191。
4. **`buffer_fullness`**：VBR 场景填 `0x7FF`（最大值），CBR 场景按码率计算。99% 转封装场景填 `0x7FF`。

**构造 ADTS 头的位运算代码（能跑的真代码）：**

```c
#include <stdint.h>

/**
 * @param adts    [out] 至少 7 字节
 * @param data_len raw AAC 数据字节数
 * @param profile   audioObjectType-1 (AAC-LC=1)
 * @param freq_idx  sampling_frequency_index (48000→3, 44100→4)
 * @param channels  channel_configuration (1=单声道, 2=立体声)
 */
void adts_header_write(uint8_t adts[7], uint16_t data_len,
                       uint8_t profile, uint8_t freq_idx, uint8_t channels) {
    uint16_t full_len = data_len + 7;  // 注意：包含头本身
    adts[0] = 0xFF;
    adts[1] = 0xF1;  // sync 低4位=1111 | ID=0 | layer=00 | prot=1
    adts[2] = (uint8_t)((profile << 6) | (freq_idx << 2) | (channels >> 2));
    adts[3] = (uint8_t)(((channels & 0x03) << 6) | ((full_len >> 11) & 0x03));
    adts[4] = (uint8_t)((full_len >> 3) & 0xFF);
    adts[5] = (uint8_t)(((full_len & 0x07) << 5) | 0x1F);
    adts[6] = 0xFC;  // buff_fullness 低6位=111111 | blocks=00
}
```

#### 3.1.3 🟢 AudioSpecificConfig：AAC 的 extradata

在 FFmpeg 的 `AVCodecContext` 里，`extradata` 对于 AAC codec 存的就是 AudioSpecificConfig。它最简形式只有 2 字节：

```
extradata[0]:  audioObjectType(5bit) | samplingFrequencyIndex 高3位
               AAC-LC → 0b00010 = 2
extradata[1]:  samplingFrequencyIndex 低1位 | channelConfiguration(4bit) | ...
               stereo → 0b0010 = 2
```

**什么时候需要手动解析 extradata**：从 RTMP 拉流拿到 raw AAC、要转 ADTS 存文件时，采样率索引和通道数就是从 `extradata` 里取的。不需要自己解析也可以——`avcodec_parameters_from_context` 会帮你提取。

**⚠️ 注意**：HE-AAC v1/v2 的 AudioSpecificConfig 不止 2 字节——后面有 SBR 和 PS 的扩展参数。如果 extradata 长度 > 2，用 `ffprobe -show_streams` 查 `profile` 字段能确认是 LC/HE-AACv1/HE-AACv2。

### 3.2 🟢 Opus 详解：为什么它是实时通信的事实标准

#### 3.2.1 Opus 的架构：双核融合

Opus 最核心的设计决策是**一个编码器里放两个引擎**：

```
输入 PCM (48kHz 单声道/立体声)
    │
    ├─ 低码率 (< 32kbps，语音场景): SILK 引擎
    │   - 来源：Skype 的语音编码器
    │   - 技术：LTP(长时预测) + LPC(线性预测编码)
    │   - 特点：窄带/宽带/超宽带语音，对"人声"效果极好
    │
    └─ 高码率 (> 32kbps，音乐/通用场景): CELT 引擎
        - 来源：Xiph.Org 的音乐编码器
        - 技术：MDCT + 频域编码
        - 特点：全频段音乐，低延迟，质量好
```

在中间码率（~16-32kbps），两个引擎可以**混合**——SILK 处理低频、CELT 处理高频。这就是 Opus 能在 6kbps 到 510kbps 全覆盖的原因。

#### 3.2.2 Opus 的帧长与延迟

这是 Opus 相比 AAC 最大的工程优势：

| 参数 | AAC-LC | Opus |
|------|--------|------|
| 编码帧长 | 固定 1024 采样点（~21.3ms@48kHz） | 2.5 / 5 / 10 / 20 / 40 / 60 ms **可配** |
| 最小编码延迟 | ~20ms | **2.5ms**（5ms 是最划算的平衡点） |
| 典型端到端延迟 | 60-100ms | **20-40ms** |
| Lookahead | 0（但编码算法内部有缓冲） | 可配（0-10ms） |

**为什么帧长影响延迟**：编码器必须攒够 `nb_samples` 个采样点才能编码一帧。5ms 帧 = 240 个采样点就够了，20ms 帧要等 960 个。在采集端——麦克风每 10ms 吐一次数据，编码器如果是 20ms 帧长，第一帧要等第二次采集才能开始编，这就是最基础的 "编码延迟"。

**Opus 在 WebRTC 里的典型配置：** 20ms 帧长（和标准语音帧率对齐）、关闭 FEC（除非网络很差）、可变码率（VBR）。这个配置延迟约 25ms 编码 + 50ms 网络缓冲 = 端到端 ~75ms。如果开 10ms 帧、FEC、网络好——端到端可以压到 30ms 以内。

#### 3.2.3 Opus 的 FFmpeg 使用

```c
// 找到 Opus 编码器（需要 libopus）
AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
AVCodecContext *ctx = avcodec_alloc_context3(codec);

ctx->sample_rate    = 48000;   // Opus 只支持 48000 内部处理
ctx->sample_fmt     = AV_SAMPLE_FMT_FLT;  // Opus 编码输入是 FLT packed
ctx->channel_layout = AV_CH_LAYOUT_STEREO;
ctx->bit_rate       = 64000;   // 64kbps 立体声（音乐质量）

avcodec_open2(ctx, codec, NULL);

// ⚡ 关键：frame_size = 0 意味着可变帧长
// 自己决定每次送多少采样点（120/240/480/960/1920/2880/5760）
int nb_samples = 960;  // 20ms @ 48kHz

AVFrame *frame = av_frame_alloc();
frame->nb_samples     = nb_samples;
frame->format         = ctx->sample_fmt;
frame->channel_layout = ctx->channel_layout;
av_frame_get_buffer(frame, 0);  // 分配 PCM buffer

// 填充 PCM 数据到 frame->data[0]（packed, 左右交错）
// ...

// 编码
avcodec_send_frame(ctx, frame);
while (avcodec_receive_packet(ctx, pkt) == 0) {
    // pkt 里是压缩后的 Opus 数据
    // 可以封装到 Ogg/WebM/MP4 或通过 RTP 发送
    av_packet_unref(pkt);
}
```

**⚠️ Opus 编码的两个关键约束：**

1. **采样率必须是 48000**——Opus 内部统一 48kHz 处理。如果你采集的是 44100，编码前必须用 swresample 转到 48kHz。
2. **编码器输入是 FLT（float packed），不是 FLTP**。和 AAC 不一样——AAC 输入是 FLTP（planar），Opus 输入是 FLT（packed）。搞混了也是噪声。

#### 3.2.4 Opus vs AAC：一段量化对比总结

| 维度 | AAC-LC | Opus |
|------|--------|------|
| 标准 | ISO 14496-3 | IETF RFC 6716 |
| 帧长 | 1024（固定） | 120/240/480/960/1920/2880/5760 |
| FFmpeg frame_size | **1024** | **0**（可变） |
| 编码输入格式 | FLTP | FLT |
| 编码延迟（20ms 帧） | ~21ms | ~20ms（可压低到 5ms） |
| 最低码率（可懂语音） | ~16kbps | **6kbps** |
| 最高码率 | ~512kbps | 510kbps |
| 内置抗丢包 | ❌ | ✅ FEC |
| 专利费 | 需要 | **免费** |
| 硬件解码 | ✅ 几乎所有设备 | ⚠️ 中高端设备 |
| 浏览器 WebRTC | ⚠️ 可选 | ✅ 必选 |
| 文件/流媒体生态 | ✅ 极成熟 | ⚠️ 发展中 |

#### 3.2.5 Opus 码流结构：TOC 字节与帧打包（面试常问）

**一帧 Opus 数据 = TOC（1 字节）+ 压缩音频数据**。TOC（Table of Contents）是 Opus 编解码器标准（RFC 6716）定义的帧自描述头，`opus_encode()` 产出的第一个字节就是 TOC——它是编解码层的东西，不是 RTP/容器额外加的。

##### TOC 字节位结构（1 字节）

```
 0  1  2  3  4  5  6  7
┌──┬──┬──┬──┬──┬──┬──┬──┐
│     config      │ s│ c│
└──┴──┴──┴──┴──┴──┴──┴──┘
  5 bit (bit0-4)   1b   2b (bit6-7)
```

| 字段 | 位宽 | 含义 | 取值 |
|---|---|---|---|
| **config** | bit 0-4（5 bit）| 编码模式 + 音频带宽 + 帧长 | 0-31 共 32 种组合 |
| **s** | bit 5 | stereo 立体声标志 | 0=单声道, 1=立体声 |
| **c** | bit 6-7（2 bit）| 帧数编码 | 0=1帧, 1=2帧, 2=3帧, 3=≥4帧（实际帧数 = c+1）|

**config 字段映射表（5 bit → 32 种组合）**：

| config 范围 | 编码模式 | 音频带宽 | 可选帧长 |
|---|---|---|---|
| 0-3 | SILK（语音优化）| NB 窄带 8kHz | 10/20/40/60ms |
| 4-7 | SILK | MB 中带 12kHz | 10/20/40/60ms |
| 8-11 | SILK | WB 宽带 16kHz | 10/20/40/60ms |
| 12-15 | **Hybrid**（SILK+CELT 混合）| SWB 超宽带 24kHz | 10/20ms |
| 16-19 | Hybrid | FB 全带 48kHz | 10/20ms |
| 20-23 | CELT（音乐优化）| SWB | 2.5/5/10/20ms |
| 24-27 | CELT | FB | 2.5/5/10/20ms |
| 28-31 | 预留 | — | — |

**举例**：

```
TOC = 0x4C = 01001100
 config=01001=9  s=1  c=00=1帧
 → config=9 → SILK 模式, WB 宽带(16kHz), 20ms 帧
 → s=1     → 立体声
 → c=0     → 单个 Opus 帧
```

##### 帧打包：一个 TOC 可以管多个帧

**一个 TOC + c 字段** 描述一组帧——同一个 TOC 下所有帧的编码模式、带宽、立体声标志、帧长都相同，因此字节数也相同。

**常见情况（1 个 TOC，管所有帧）**：

```
[RTP/容器载荷] [TOC c=2 1B] [Frame0] [Frame1] [Frame2]
              └── 3 帧均分剩余字节，全用同一模式
```

WebRTC 默认 20ms 帧、同一编码模式不变 → 几乎永远只用 1 个 TOC。

**不常见（多个 TOC，帧间切换编码模式）**：

```
[TOC1 c=0][Frame0][TOC2 c=1][Frame1][Frame2]
    │         │        │         └── CELT FB 10ms, c=1→2 帧
    │         │        └── SILK WB 20ms, c=0→1 帧
    │         └── 模式改变，需要新 TOC
    └── TOC 之间没有分隔符——读完上一组帧后指针自然落在下一个 TOC
```

##### c=0/1/2 时怎么算边界？——均分

Opus 协议强约束：同一个 TOC 下所有帧**时长相同**。时长相同 + 编码模式相同 → 每帧字节数相同。所以接收端直接均分：

```
c=1（2帧）: 每帧字节数 = (payload 总字节 - 1B TOC) / 2
c=2（3帧）: 每帧字节数 = (payload 总字节 - 1B TOC) / 3
```

不需要每帧加 2 字节长度前缀——这是 Opus 比 H.264 STAP-A 更省头的关键。

**c=3（≥4 帧）需要额外长度字节辅助**，但在 WebRTC 里极其罕见（20ms×4=80ms 音频在一个包，延迟太大）。

##### 接收端解析伪代码

```
ptr = payload 起始位置
while (ptr < 包末尾) {
    TOC    = *ptr++;           // 读 TOC
    config = TOC & 0x1F;       // 低 5 位
    s      = (TOC >> 5) & 1;
    c_val  = (TOC >> 6) & 3;
    帧数   = (c_val == 3) ? 额外解析 : c_val + 1;
    帧大小  = 查表(config) + s 修正;
    for (i = 0; i < 帧数; i++) {
        解码(ptr, 帧大小);
        ptr += 帧大小;
    }
    // ptr 要么指向新 TOC, 要么到达包末尾
}
```

##### DTX 静音不传 + 带内 FEC

**DTX（Discontinuous Transmission）**：说话间歇停止编码不发帧，恢复时第一个帧带特殊标记。

```
发帧: ████████████░░░░░░░░░░░░████████████
               ↑ 静音段,不发帧  ↑ 恢复,标记首帧
```

**带内 FEC（In-band Forward Error Correction）**：可在当前帧后附带前一帧的低码率副本，前一帧丢了也能还原出可接受的语音质量：

```
RTP 包 N+1: [当前帧 N+1] [FEC: 帧 N 的低码率副本]
                                ↑
                           帧 N 丢了就从这恢复
```

FEC 不是每帧都带——由编码器根据预期丢包率决定。

> **Opus 码流结构的 RTP 传输细节（RFC 7587）见 [06-M4-RTP传输模块.md](../../project/WebRTC/06-M4-RTP传输模块.md) §2.4**——包含多帧合包在 RTP 里的具体布局、Marker 位在 DTX 恢复时的语义、以及 Opus vs H.264 打包方式的全面对比。

### 3.3 MP3 和 FLAC 速览

#### 3.3.1 MP3（MPEG-1 Audio Layer III）

MP3 在 FFmpeg 里的 codec ID 是 `AV_CODEC_ID_MP3`。关键参数：

- `frame_size = 1152`（MPEG-1）或 576（MPEG-2/2.5）
- 编码输入：S16P（planar）或 FLTP
- 支持 VBR/CBR/ABR 全部三种码控
- 解码可以 detect 码流格式（FFmpeg 的 mp3 decoder 很强，支持自动检测）

**MP3 在今天唯一的使用理由**：存量兼容。如果你手上的音源是 MP3、目标播放器可能非常老（比如 2010 年前的车载系统），用 MP3 直接封装。但如果是新生成的内容，用 AAC——同码率音质更好、延迟更低、压缩效率更高。

#### 3.3.2 FLAC（Free Lossless Audio Codec）

FLAC 是无损压缩，codec ID = `AV_CODEC_ID_FLAC`。压缩比约 2:1（PCM 的一半），解码后和原始 PCM 逐 bit 一致。

- `frame_size = 0`（可变）
- 编码输入：S16 或 S32
- 适用场景：音乐存档、母带保存、需要多次编解码的中间格式
- **不是流式格式**——带宽要求高（~700kbps CD 音质）、编码延迟大

### 3.4 🟢 FFmpeg 音频编码完整 API 流程

这一段是实际写代码时必须掌握的模板。

#### 3.4.1 编码流程

```c
// ============ 1. 打开编码器 ============
// 注意：FFmpeg 内置 aac 编码器 vs libfdk_aac（需外部编译）
// 内置 aac: 质量好、免费，推荐日常使用
// libfdk_aac: 质量最好，但 GPL 不兼容，商业项目慎用
const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
AVCodecContext *ctx = avcodec_alloc_context3(codec);

ctx->sample_rate    = 48000;
ctx->sample_fmt     = AV_SAMPLE_FMT_FLTP;  // AAC 要求 planar float
ctx->channel_layout = AV_CH_LAYOUT_STEREO;
ctx->bit_rate       = 128000;              // 128kbps
ctx->time_base      = (AVRational){1, 48000};

// ⚡ 必须检查：不是所有编码器都支持你设的参数
if (avcodec_open2(ctx, codec, NULL) < 0) { /* error */ }

// 记下 frame_size——之后每次送帧的 nb_samples 就是这个值
int frame_size = ctx->frame_size;  // AAC-LC=1024, MP3=1152

// ============ 2. 分配 AVFrame ============
AVFrame *frame = av_frame_alloc();
frame->nb_samples     = frame_size;
frame->format         = ctx->sample_fmt;
frame->channel_layout = ctx->channel_layout;
av_frame_get_buffer(frame, 0);  // 分配 data[0]..data[N] 的 PCM buffer

// ============ 3. 编码循环 ============
int64_t pts = 0;
while (has_more_pcm) {
    // 填充 PCM 数据到 frame->data[0..N]
    // 对于 FLTP stereo: data[0]=左声道, data[1]=右声道
    fill_planar_float(frame->data[0], frame->data[1], frame_size);

    frame->pts = pts;
    pts += frame_size;  // 每帧 PTS 递增 frame_size（在 1/sample_rate 时基下）

    avcodec_send_frame(ctx, frame);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        // pkt.data: 编码后的压缩数据（raw AAC）
        // pkt.pts / pkt.dts: 已由编码器设置
        write_or_mux(pkt);
        av_packet_unref(pkt);
    }
}

// ============ 4. 刷出残留帧 ============
avcodec_send_frame(ctx, NULL);  // NULL 表示 EOF
while (avcodec_receive_packet(ctx, pkt) == 0) {
    write_or_mux(pkt);
    av_packet_unref(pkt);
}
```

#### 3.4.1.1 🟢 `nb_samples` 到底该填多少？——从 frame_size 说起

这是音频编码最容易写错的入口参数。核心规则只有一条：

> **`nb_samples` 必须严格匹配编码器上下文的 `codec_ctx->frame_size`。** 编码器在 `avcodec_open2` 之后自动计算并填充这个值——不要硬编码 1024，要读 `ctx->frame_size`。

```c
// ✅ 正确：打开编码器后读取 frame_size
avcodec_open2(ctx, codec, NULL);
frame->nb_samples = ctx->frame_size;  // 编码器告诉你它要多少
frame->format     = ctx->sample_fmt;
frame->ch_layout  = ctx->ch_layout;
av_frame_get_buffer(frame, 0);        // 按以上三个参数分配 PCM buffer

// ❌ 错误：硬编码
frame->nb_samples = 1024;  // 如果编码器期望 960 或 512，send_frame 直接报错
```

#### 3.4.1.2 🟡 为什么 frame_size 不总是 1024？

你可能在很多资料中看到"AAC 一帧是 1024 个采样"——这确实是 AAC-LC（Low Complexity）最常见的配置，但它**不是铁律**。不同 AAC 子规格定义的帧大小本身就不同：

| AAC 子规格 | frame_size | 时长 @48kHz | 场景 |
|-----------|-----------|------------|------|
| **AAC-LC**（标准） | **1024** | ~21.3ms | 点播/直播/存档——最常见 |
| **AAC-LC**（广播） | **960** | 20.0ms | 数字广播/电视，要求 20ms 整数帧长对齐视频帧 |
| **AAC-LD**（低延迟） | **512** 或 480 | ~10.7ms / 10ms | 视频会议、双向通话 |
| **AAC-ELD**（超低延迟） | **512** 或 480 | ~10.7ms / 10ms | WebRTC 音频备选、专业通话设备 |
| **HE-AAC / HE-AAC v2** | **2048**（内部） | ~42.6ms | 低码率流媒体（实际每帧仍编码 1024，但 SBR 需要双倍窗口） |

**为什么广播用 960？** 因为 960 个采样 @48kHz = 正好 20.0ms——和 50fps 视频的帧间隔（20ms）完美对齐，不会出现音频帧跨越两个视频帧的相位偏移。这是 MPEG 专门为电视广播定义的对齐值。

**为什么低延迟用 512/480？** 帧长越短、编码器攒够一帧数据需要的时间越短、编码延迟越低。1024 采样 = 21.3ms 的先天延迟，512 采样 = 10.7ms，直接砍半。

**实践中怎么确认**：打开编码器后打印 `ctx->frame_size`，不要猜。不同 FFmpeg 版本、不同编码器后端（内置 `aac` vs `libfdk_aac`）对同一 `AV_CODEC_ID_AAC` 返回的 `frame_size` 可能不同——尤其是在设置了 `profile` 为 `FF_PROFILE_AAC_LOW`（LC）vs `FF_PROFILE_AAC_LD`（LD）之后。

#### 3.4.1.3 🟢 最后一帧（残帧）怎么处理？

音频流的总采样数很难刚好被 `frame_size` 整除。末尾可能只剩 300 个采样。两种处理方式：

**方式 A：直接送入不足一帧（部分编码器支持）**——把 `frame->nb_samples` 设为实际剩余数（如 300），然后 `avcodec_send_frame`。但**很多编码器（尤其是 libfdk_aac）不接受小于 frame_size 的帧**，会直接返回 `AVERROR(EINVAL)`。

**方式 B：补零 Padding（最稳妥）**——依然填满 `frame_size`（如 1024），把有效数据放前 300 个采样，后面 724 个采样全部清零。送入后立即 send NULL 刷出残留帧：

```c
// 剩余 300 个采样时：补零送最后一帧
frame->nb_samples = frame_size;  // 依然是 1024
// 前 300 个有效采样已经填在 data 里
memset(frame->data[0] + 300 * sizeof(float), 0,
       (frame_size - 300) * sizeof(float));  // 后面清零
avcodec_send_frame(ctx, frame);   // 送入满帧
avcodec_send_frame(ctx, NULL);    // 立即刷出
```

**方式 B 是商业代码的标配做法**——兼容所有编码器，逻辑简单，零样本不会被解码器发出声音（幅度为零的采样 = 静音）。

#### 3.4.2 解码流程

```c
// 解码是编码的镜像
const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
AVCodecContext *ctx = avcodec_alloc_context3(codec);
avcodec_open2(ctx, codec, NULL);

AVFrame *frame = av_frame_alloc();

while (has_more_packets) {
    avcodec_send_packet(ctx, pkt);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        // frame->data[0..N] : 解码后的 PCM（AAC 默认 FLTP）
        // frame->nb_samples : 实际采样点数（通常是 1024）
        // frame->sample_rate: 音频流的采样率
        // frame->pts: 此帧第一个采样点的 PTS（解码器已计算好）
        process_pcm(frame);
        av_frame_unref(frame);
    }
}

// 刷出残留
avcodec_send_packet(ctx, NULL);
while (avcodec_receive_frame(ctx, frame) == 0) { /* ... */ }
```

#### 3.4.3 🟡 编码器和解码器的 frame_size 为什么不一致

**编码器** `ctx->frame_size` 告诉你要送多少采样点进去（AAC-LC=1024, MP3=1152）。这是压缩算法决定的——AAC 一帧编码 1024 个采样点才能做 MDCT 变换。

**解码器** `ctx->frame_size` 一般也是 1024（AAC）/ 1152（MP3）。但 VBR 场景下 `frame->nb_samples` 可能不等于 `frame_size`——因为编码器可能填充了 padding（使总帧数对齐）。这就是前面说的 PTS 漂移的根因——不要用 `ctx->frame_size` 做 PTS 步长，用 `frame->nb_samples`。

#### 3.4.4 🟡 核心痛点：输入源与编码器 nb_samples 不匹配怎么办？——AVAudioFifo

这是初学者最容易踩的坑，也是最体现工程能力的知识点。

**问题场景**：你从 MP3 解码出来的 AVFrame，每帧 `nb_samples=1152`。但 AAC 编码器要求 `frame_size=1024`。直接把 1152 个采样的 AVFrame 丢给 AAC 编码器——`avcodec_send_frame` 直接返回 `AVERROR(EINVAL)`（参数非法）。

**更普遍的表述**：在任何"解码后重新编码"（transcoding）或"采集后编码"的场景中，输入源每次给你的采样数（麦克风采集 buffer 大小、MP3/Opus 解码输出帧长）**几乎永远不等于**目标编码器的 `frame_size`。

**解决方案：`AVAudioFifo`（音频先进先出队列）**

在输入源和编码器之间插入一个音频缓冲区——就像生产流水线上的"蓄水池"：

```
输入源                     AVAudioFifo                   编码器
(任意 nb_samples)    →    蓄水池缓冲    →        (固定 frame_size)
MP3 解码: 1152               │                    AAC 编码: 1024
Opus 解码: 960              │                    每次取出 1024
麦克风采集: 480             │                    不够就等着
```

**完整代码模板：**

```c
#include <libavutil/audio_fifo.h>

// ============ 1. 创建 AVAudioFifo ============
// 参数: 采样格式、声道数、初始容量（设为 0 自动分配）
AVAudioFifo *fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, 0);
if (!fifo) { /* error */ }

// ============ 2. 输入循环：往 FIFO 里存 ============
AVFrame *input_frame = /* 解码出来的或采集到的，nb_samples 任意 */;
// 把输入 AVFrame 的音频数据写入 FIFO
// ⚡ 注意：只复制数据，不复制 frame 本身的元数据
int ret = av_audio_fifo_write(fifo, (void **)input_frame->data,
                               input_frame->nb_samples);
if (ret < 0) { /* error */ }

// ============ 3. 编码循环：从 FIFO 取出来、凑满 1024 送编码器 ============
AVFrame *enc_frame = av_frame_alloc();
enc_frame->nb_samples     = ctx->frame_size;  // 1024
enc_frame->format         = ctx->sample_fmt;
enc_frame->channel_layout = ctx->channel_layout;
av_frame_get_buffer(enc_frame, 0);

int64_t pts = 0;
while (av_audio_fifo_size(fifo) >= ctx->frame_size) {
    // FIFO 里够 1024 了，取出来
    av_audio_fifo_read(fifo, (void **)enc_frame->data, ctx->frame_size);

    enc_frame->pts = pts;
    pts += ctx->frame_size;

    avcodec_send_frame(ctx, enc_frame);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        write_or_mux(pkt);
        av_packet_unref(pkt);
    }
}
// 循环结束后 FIFO 里可能还剩下不足 frame_size 的采样（末尾残帧）

// ============ 4. 处理末尾残帧（Padding） ============
int remaining = av_audio_fifo_size(fifo);
if (remaining > 0) {
    // 读出剩余的采样（不足一帧）
    av_audio_fifo_read(fifo, (void **)enc_frame->data, remaining);

    // 把剩余部分清零（静音 padding）
    for (int ch = 0; ch < 2; ch++) {
        memset(enc_frame->data[ch] + remaining * sizeof(float),
               0,
               (ctx->frame_size - remaining) * sizeof(float));
    }

    enc_frame->pts = pts;
    avcodec_send_frame(ctx, enc_frame);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        write_or_mux(pkt);
        av_packet_unref(pkt);
    }
}

// ============ 5. 刷编码器 ============
avcodec_send_frame(ctx, NULL);
while (avcodec_receive_packet(ctx, pkt) == 0) {
    write_or_mux(pkt);
    av_packet_unref(pkt);
}

// ============ 6. 清理 ============
av_audio_fifo_free(fifo);
av_frame_free(&enc_frame);
```

**关键 API 对照**：

| AVAudioFifo API | 作用 | 注意事项 |
|-----------------|------|---------|
| `av_audio_fifo_alloc(fmt, channels, nb_samples)` | 创建 FIFO | `nb_samples` 是初始容量，可设 0 自动分配 |
| `av_audio_fifo_write(fifo, data, nb_samples)` | 写入数据 | `data` 是 `void **` 指向各声道 buffer 的指针数组 |
| `av_audio_fifo_read(fifo, data, nb_samples)` | 读出数据 | 读出的数据从 FIFO 中**移除** |
| `av_audio_fifo_size(fifo)` | 当前 FIFO 中缓存的采样数 | 用于判断"够不够一帧" |
| `av_audio_fifo_free(fifo)` | 释放 FIFO | 同时也释放内部缓存的所有数据 |

**⚠️ 两个容易写错的细节**：

1. **不要直接传 frame->data 给 FIFO**。`av_audio_fifo_write` 的第二个参数是 `void **`——如果你的 frame 是 FLTP（planar），`frame->data` 本身就是 `uint8_t *data[8]`，直接强转 `(void **)frame->data` 是对的。但如果 frame 是 FLT（packed），数据全在 `data[0]` 里——此时要构造一个指针数组指向 `data[0]` 的各个声道偏移位置。

2. **FIFO 满了会动态扩容**——不用担心初始容量设小了，`av_audio_fifo_write` 内部会自动调用 `av_audio_fifo_realloc` 扩容。但要注意内存碎片：如果处理超长音频流（几小时），建议在 FIFO 大小超过一定阈值后主动 `av_audio_fifo_drain` 清理旧数据。

### 3.5 关键 API / 参数速查

| API / 参数 | 作用 | 注意事项 |
|-----------|------|---------|
| `codec_ctx->frame_size` | 编码器期望的每帧采样点数 | AAC-LC=1024, AAC-LD=512/480, MP3=1152, Opus=0（可变）, FLAC=0。**不要硬编码，读 `ctx->frame_size`** |
| `codec_ctx->sample_fmt` | 编码器支持的输入/输出格式 | 设置后用 `avcodec_open2` 验证——不支持的格式会被拒绝 |
| `codec_ctx->extradata` | 解码初始化参数 | AAC→AudioSpecificConfig（2 字节），Opus→OpusHead（19 字节），MP3→可能为 NULL（解码器自行探测） |
| `codec_ctx->bit_rate` | 目标码率 | AAC CBR/ABR 场景必须设为有效值；Opus 可选，不设则按 `nb_samples` 和复杂度自动选 |
| `frame->nb_samples` | 当前帧包含的采样点数 | 编码时设置为 `ctx->frame_size`；解码时由解码器填充；**送错值会直接 `EINVAL`** |
| `frame->pts` | 该帧第一个采样点的 PTS | 编码时自己设置递增；解码时由解码器自动填充 |
| `av_audio_fifo_alloc/write/read/size` | 音频 FIFO：缓冲输入源和编码器之间的采样数不匹配 | ⚠️ 转码和采集-编码场景的必备中间层——输入 `nb_samples` ≠ 编码器 `frame_size` 时用它 |
| `avcodec_parameters_from_context()` | 把 codec ctx 的参数导出到 AVCodecParameters | 封装（muxing）时用——把编码器的 extradata 复制到流的 codecpar |
| `av_parser_parse2()` | 从无头字节流中分割音频帧 | raw AAC 或 ADTS 字节流的分帧——parser 自动检测 frame 边界，返回完整的 frame |

### 3.6 🟡 典型工作流：FLV AAC → .aac 文件（完整代码）

```c
// 从 FLV 拉 AAC 流，补 ADTS 头存成 .aac 文件
// 伪代码，仅示意流程

// 1. 读取 AudioSpecificConfig
uint8_t *extradata = audio_stream->codecpar->extradata;
int      extradata_size = audio_stream->codecpar->extradata_size;

// 2. 解析 AudioSpecificConfig 得到 ADTS 参数
uint8_t audioObjectType = (extradata[0] >> 3) & 0x1F;        // 5 bit, AAC-LC=2
uint8_t freq_idx = ((extradata[0] & 0x07) << 1)               // 4 bit
                 | ((extradata[1] >> 7) & 0x01);
uint8_t channels = (extradata[1] >> 3) & 0x0F;                // 4 bit
uint8_t profile = audioObjectType - 1;                        // ⚡ 减一！

// 3. 循环读包 + 补头 + 写入
AVPacket pkt;
while (av_read_frame(fmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index != audio_idx) { av_packet_unref(&pkt); continue; }

    uint8_t adts[7];
    adts_header_write(adts, pkt.size, profile, freq_idx, channels);
    fwrite(adts, 1, 7, fp);
    fwrite(pkt.data, 1, pkt.size, fp);

    av_packet_unref(&pkt);
}
```

### 3.7 性能上的坑与避坑指南

**场景 1：AAC 编码输出后直接喂给解码器——解码器报错**

- 常见做法：`avcodec_receive_packet` 出来的 AAC 数据直接 `fwrite` 成文件，然后 `ffplay` 播——一切正常。但自己写代码 `avcodec_send_packet` 给解码器——报 `invalid data`。
- 坑在哪里：编码器输出是 raw AAC（无 ADTS 头），而 `avcodec_send_packet` 给解码器时，解码器需要 `extradata` 已经设置好 AudioSpecificConfig。如果创建解码器时没有设置 `extradata`，它就不知道采样率和 codec profile。
- 正确做法：解码器 `avcodec_open2` 之前，从编码器的 `extradata` 复制到解码器：`avcodec_parameters_to_context(dec_ctx, enc_par)`。

**场景 2：Opus 编码后拿去 FLV 封装——muxer 拒绝**

- 常见做法：用 Opus 编码因为它在 WebRTC 里表现好，想着也用它推 RTMP。
- 坑在哪里：**FLV 容器不支持 Opus**。FLV 的 Audio Tag 只定义了 MP3、AAC-LC、PCM、Speex。推 RTMP 只能用 AAC 或 Speex（后者已淘汰）。
- 正确做法：推 RTMP 用 AAC；如果需要 Opus 的低延迟特性，用 WebRTC 的 RTP 直接封装。

**场景 3：AAC VBR 编码后 PTS 漂移**

- 常见做法：每帧 PTS `+= 1024`，固定步长。编码器是 VBR 模式，有的帧实际 `nb_samples` 不等于 1024。
- 坑在哪里：编码器在 `avcodec_receive_packet` 时，某些帧可能因为内部 lookahead 或 padding 而输出更多或更少采样点。用固定 1024 步长累积十几个帧后，PTS 漂移可能超过 10ms——人耳能感知到音画不同步。
- 正确做法：**不要用固定步长**，而是从编码器吐出的 packet 上取真实的 duration，逐帧累加。以下两种方案任选其一。

**方案 A：用 `pkt.duration` 驱动 PTS（最推荐，编码器替你算好了）**

编码器在吐出每个 AVPacket 时，会在 `pkt.duration` 里填好"这一包音频实际对应多少采样点"（以 `time_base` 为单位）。你只需要维护一个累加器：

```c
int64_t next_pts = 0;
AVPacket *pkt = av_packet_alloc();

while (has_more_input) {
    avcodec_send_frame(ctx, frame);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        // ⚡ 关键：由编码器决定 PTS，不要自己 += 1024
        pkt->pts = next_pts;
        next_pts += pkt->duration;  // duration 由编码器设置，反映真实采样点数
        av_interleaved_write_frame(mux_ctx, pkt);
        av_packet_unref(pkt);
    }
}
```

**为什么这个方案最安全**：编码器内部知道每个包的实际采样点数——包括 padding、lookahead 带来的增减。你把 PTS 计算权交给它，就不需要关心 VBR/CBR、AAC/Opus/MP3 的差异。适用于**所有编码器、所有码控模式**。

**方案 B：用 `frame->nb_samples` 自己做 `av_rescale_q`（适用于需要显式控制 PTS 的场景）**

如果你想在送编码器之前就确定 PTS（比如时间戳需要和视频帧对齐），用自己的时基重算：

```c
int64_t next_pts = 0;
// 如果目标 time_base 是 1/48000（采样率时基）
AVRational tb = (AVRational){1, 48000};

while (has_more_input) {
    // frame->nb_samples 每帧可能不同（VBR / 末帧 padding）
    frame->pts = next_pts;
    next_pts += frame->nb_samples;  // 在 1/48000 时基下，步长 = 采样点数

    avcodec_send_frame(ctx, frame);
    while (avcodec_receive_packet(ctx, pkt) == 0) {
        // pkt->pts 由编码器从 frame->pts 继承，无需手动设置
        av_interleaved_write_frame(mux_ctx, pkt);
        av_packet_unref(pkt);
    }
}
```

如果目标 time_base 不是 `1/sample_rate`（比如封装器用的是 `1/90000` 或 `1/1000`），用 `av_rescale_q` 换算：

```c
AVRational src_tb = (AVRational){1, ctx->sample_rate};   // 输入时基
AVRational dst_tb = mux_stream->time_base;                // 输出时基（容器）

frame->pts = av_rescale_q(sample_count, src_tb, dst_tb);
sample_count += frame->nb_samples;
```

**⚠️ 方案 B 要注意两点**：
1. `frame->nb_samples` 是每帧的实际采样数——如果是按"固定 frame_size + Padding 末帧"编码的，大部分帧的 `nb_samples` 就是 `frame_size`（1024），只有最后一帧可能不同。
2. 如果用 `AVAudioFifo` 喂编码器（§3.4.4），从 FIFO 读出来的 `enc_frame->nb_samples` 永远是 `ctx->frame_size`（1024），所以这个场景下 PTS 固定步长其实是安全的。**真正需要警惕固定步长的，是解码输出的帧长不一致（如 VBR MP3、Opus）或采集端帧长不一致的场景。**

**场景 4：MP3 解码输出 1152 采样直接送 AAC 编码器——EINVAL**

- 常见做法：MP3 解码后拿到 `AVFrame`，`frame->nb_samples=1152`，直接 `avcodec_send_frame(aac_ctx, frame)`。
- 坑在哪里：AAC-LC 编码器要求 `frame_size=1024`，你送了 1152。`avcodec_send_frame` 返回 `AVERROR(EINVAL)`——参数非法。更隐蔽的是，如果只是偶尔发生（比如 MP3 VBR 某些帧恰好是 1152），错误在运行时随机出现，很难定位。
- 正确做法：在 MP3 解码器和 AAC 编码器之间插入 `AVAudioFifo`（见 §3.4.4）。解码出来的任意大小帧先写入 FIFO，攒够 1024 后取出送编码器，末尾不够的 Padding 补零。这个模式**适用于所有"不同帧长编解码器串联"的场景**——不仅是 MP3→AAC，还包括 Opus→AAC、采集→编码等。

### 3.8 延伸阅读与进阶方向

- **本仓库**：[04-音频PCM-采样-重采样.md](./04-音频PCM-采样-重采样.md) — swresample 完整使用指南（编码前/解码后都依赖它）；[08-网络协议与流媒体.md](./08-网络协议与流媒体.md) — RTMP/FLV 里音频的封装细节
- **官方资源**：FFmpeg 源码 `libavcodec/aacenc.c`（AAC 编码器实现）、`libavcodec/opusenc.c`（Opus 编码器封装）、`libavcodec/aac_parser.c`（ADTS 帧边界检测）；ISO 14496-3（AAC 规范）；RFC 6716（Opus 规范）
- **前沿方向**：xHE-AAC（USAC 编码核）、Opus 2.0（正在 IETF 标准化，AI 辅助的心理声学模型）

---

## 四、自检题（合上文档能回答吗？）

1. 不看文档，写出 7 字节 ADTS 头的结构——sync word 在哪几位？`frame_length` 占几位、跨哪几个字节？
2. AAC-LC 的 `audioObjectType=2`，在 ADTS 头的 `profile` 字段填几？为什么？
3. AAC-LC 的 `frame_size` 一定是 1024 吗？什么情况下是 960、512、480？分别对应什么场景？
4. Opus 编码器的 `frame_size=0` 是什么意思？你每次编码应该送多少 `nb_samples`？
5. AAC 编码输入是 FLTP，Opus 编码输入是 FLT——这是为什么？（提示：从"编码器内部如何处理多声道"来想）
6. MP3 解码输出每帧 1152 采样，AAC 编码器要求 1024——你直接在中间传会怎样？正确的解决方案是什么？
7. `AVAudioFifo` 的三个核心操作是什么（alloc / write / read）？什么时候用它？
8. 音频流末尾只剩 300 个采样，但编码器要求 1024。两种处理方式分别是什么？哪种更稳妥、为什么？
9. 从 FLV 提取 AAC 存成 `.aac` 文件——不补 ADTS 头直接存的后果是什么？为什么 FLV 里不需要 ADTS 头？
10. WebRTC 为什么强制 Opus 必选？如果换成 AAC-LC，在延迟和丢包上会有什么问题？
11. AAC 编码时 `frame->pts` 每帧递增 1024，在 1/48000 的 time_base 下——这个帧的时长是多少毫秒？如果 `nb_samples=960` 呢？
12. `avcodec_send_frame` 编码最后为什么要 `send_frame(NULL)`？不调的话会丢什么？
13. FLV 容器为什么不能用 Opus 封装？如果一定要用 Opus 推直播，该换什么协议？
14. AAC/Opus/MP3/FLAC——分别说说什么时候选哪个。

能流畅回答 **12/14** 以上，说明已经掌握 FFmpeg 音频编解码全景。

---

## 🎯 一句话总结

> AAC 是音频界的 H.264——兼容无敌；Opus 是音频界的 AV1——技术领先但生态在建；FFmpeg 用统一的 send/receive API 屏蔽了它们的全部差异。

## 🔗 关联文档

- [[04-音频PCM-采样-重采样.md]] — 编解码上下游的 PCM 格式转换（FLTP↔S16↔FLT）+ swresample 完整指南
- [[05-H264-MP4-NALU.md]] — 视频编解码对照阅读（Annex-B vs AVCC = ADTS vs raw AAC）
- [[08-网络协议与流媒体.md]] — RTMP/FLV 里音频的封装位置（AAC sequence header 在哪）
- [[11-H264与H265详解.md]] — 音频版 SPS/PPS = AudioSpecificConfig = extradata 的对照理解


