# FFmpeg Seek 详解：从拖动进度条到帧精确定位

> **适用方向**：音视频播放器开发、视频编辑器、服务端抽帧/缩略图、直播回放、视频 AI 推理预处理
> **难度分层**：中级（必须掌握）/ 高级（进阶加分）分界线见 §1.6
> **预计阅读**：速记 15 分钟｜全文 45 分钟
> **前置知识**：FFmpeg 解码管线基础（`AVFormatContext` / `AVCodecContext` / `AVPacket` / `AVFrame` 是什么）、PTS/DTS 的概念、time_base 的含义。如果还没概念，先看 [[00-FFmpeg全景导读]] 和 [[01-数据结构与生命周期]]。
> **关联知识**：[[01-数据结构与生命周期]]（`AVIndexEntry`、`AVStream.nb_frames`）、[[05-H264-MP4-NALU]]（MP4 stss Box 与关键帧索引的关系）、[[06-编码参数与码控]]（GOP 结构与 Seek 精度的关系）

---

## 一、全景导读：Seek 在技术版图里的位置

### 1.1 从场景说起

假设你在做这样几个项目：

- **播放器进度条**：用户把进度条拖到 2.5 秒的位置，播放器要立刻从那里开始播。不能从第 0 帧重新解码过来，也不能播到 2.6 秒去——用户的眼睛会感知帧级的偏差。
- **视频编辑器**：用户在时间线上点了一个按钮，要把第 42 帧的画面截出来做封面。你必须精确拿到第 42 帧，而不是第 41 帧也不是第 43 帧。
- **AI 推理管线**：算法同学要求"每秒抽一帧"，你写了个循环 `for (t = 0; t < duration; t += 1.0)`，每次都 Seek 到 `t` 秒然后解码。结果发现——有时候抽到了重复帧，有时候跳过了关键动作。
- **服务端缩略图**：后台需要对每个上传视频生成 10 张均匀分布的缩略图。用户说"这图跟播放器里看到的对不上"——因为你拿到的帧比预期偏了 2 帧。

这些场景有一个共同的核心问题：**你怎么告诉 FFmpeg "我要从这里开始读"？** 答案看起来很简单——Seek。但 Seek 这件事在音视频里，比你想象的复杂得多。

**为什么复杂？** 因为视频不是一本可以随便翻页的书。编码后的视频帧之间有依赖关系——一帧 B 帧可能需要参考它前面的 P 帧和后面的 P 帧才能解码。如果直接跳到某个字节位置开始读，解码器看到的第一帧大概率是无法解码的"孤儿帧"。容器文件格式（MP4/MKV/FLV）的组织方式也各不相同——有的有关键帧索引表（stss Box），有的没有。

### 1.2 它是什么 / 它从哪来（前世今生）

- **一句话定义**：Seek 是将音视频文件/流的读取位置，从当前点移动到指定目标点（用时间、帧号或字节位置表达）的操作。它不是单一 API 调用，而是一整套"定位 → 缓冲区刷新 → 解码到目标帧"的流程。

- **产生背景**：早期播放器（VHS 磁带、CD）的"快进快退"是机械动作——磁头物理移动。数字视频时代，文件可以随机访问，但压缩编码引入了帧间依赖，导致"跳到某字节"不等于"能从这里播"。1990 年代末 MPEG-2 时代，DVD 播放器引入了基于 GOP 的 Seek 策略：跳到最近的关键帧，然后解码到目标帧。FFmpeg 对这一策略做了系统化抽象。

- **发展脉络**（每个阶段解决什么问题）：

| 时间 | 里程碑 | 解决了什么 |
|------|--------|-----------|
| 2003 前 | FFmpeg 早期 `av_seek_frame` | 最基础的按时间戳 Seek，只支持 BACKWARD 到最近关键帧 |
| ~2007 | `avformat_seek_file` 引入 | 提供 `min_ts / max_ts` 范围参数，Seek 精度从"最近关键帧"提升到"指定关键帧区间" |
| ~2012 | `AVSEEK_FLAG_BYTE` 完善 | 支持按字节偏移 Seek，编辑器/分析工具可以直接跳到文件中的任意位置 |
| ~2015 | `AVSEEK_FLAG_FRAME` 加入 | 支持按帧序号 Seek——终于可以不说"2.5 秒"而直接说"第 42 帧" |
| FFmpeg 6.0+ | `avformat_index_get_entry` 等公开 API | `AVStream.index_entries` 字段不再公开暴露，改用封装函数访问关键帧索引，更安全 |

- **今天的位置**：Seek 是播放器和视频编辑器的核心能力。所有主流视频播放器（VLC、mpv、IINA）和服务端转码平台（YouTube、B 站）都在用 FFmpeg 的 Seek 机制。但**很多人只用 `av_seek_frame`，不知道 `avformat_seek_file`、`AVSEEK_FLAG_FRAME` 和 Smart Seek 的存在**——这就是本篇文章要解决的问题。

### 1.3 为什么需要它（技术优势与选型理由）

- **核心解决的问题**：在压缩视频文件中，精确定位到用户指定的时间点或帧号，同时最小化解码开销。

- **相比替代方案，为什么选 FFmpeg Seek 体系**：

| 方案 | 问题 |
|------|------|
| 自己解析 MP4 moov box + 手动定位 | 需要处理几十种容器格式的不同 box 结构；遇到非 MP4 文件（FLV/TS）完全无能为力 |
| 从头解码到目标位置（无 Seek） | 跳转到 10 分钟处需要解码 18000 帧，耗时从 5ms 变成 10 秒 |
| 用第三方播放器 SDK（如 ExoPlayer、AVPlayer） | 不适用于服务端、无头环境；不能用 C/C++ 做自定义管线 |

**FFmpeg Seek 体系的独特优势**：
- **容器无关**：同一套 API 覆盖 MP4、MKV、FLV、TS、AVI 等所有格式
- **精度可调**：从"最近关键帧"到"帧精确"到"字节精确"，按需选择
- **解码器联动**：`avcodec_flush_buffers` 保证 Seek 后解码器状态干净

- **什么场景下不该用它**：
  - 纯直播流（没有索引，Seek 无效）
  - 你只需要从头到尾顺序播放（不需要 Seek）
  - 你用的是硬件解码器的专有 Seek 接口（如 NVDEC 的 CUVID Seek）

### 1.4 大厂如何使用

| 谁在用 / 什么场景 | 怎么用 | 解决什么问题 |
|-----------------|--------|------------|
| YouTube 服务端转码 | `avformat_seek_file` + BACKWARD flag → 顺解到 GOP 边界 → 按 GOP 并行分发给多个 Worker | 把一个长视频按 GOP 切段，多机并行转码，提升吞吐 |
| B 站播放器 (ijkplayer) | `av_seek_frame(FLAG_BYTE)` 回到文件头 → 读到 `moov` box → 解析关键帧索引 → 按 `FLAG_FRAME` Seek | 在线视频拖动进度条时的秒开体验 |
| AI 推理管线（目标检测/分类） | 上传时预建 FrameIndex → 线上 `AVSEEK_FLAG_FRAME` 或 `AVSEEK_FLAG_BYTE` 直接定位 → 解码目标帧 | 替代时间戳 Seek，消除 float 换算带来的帧偏差 |
| 视频编辑器（帧精确截图） | `BuildFullFrameIndex` → 逐帧记录 byte_offset → 线上 `av_seek_frame(FLAG_BYTE, kf_offset)` → 解码到目标帧 | 帧ID 与画面 100% 对应，无漂移 |

### 1.5 关联技术地图

```
                        ┌──────────────────────────┐
                        │     FFmpeg Seek 体系      │
                        └──────────┬───────────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │                          │                          │
        v                          v                          v
  [容器层]                    [编解码层]                  [时间体系]
  libavformat                libavcodec                 time_base & PTS/DTS
  ─关联─                      ─关联─                     ─关联─
  · AVIndexEntry             · avcodec_flush_buffers    · av_rescale_q
    (关键帧索引，来自            (Seek 后必须 flush，         (秒↔PTS 精准换算，
     MP4 stss / MKV cues)      否则解码器残留旧状态)        用整数避免 float 误差)
  · avformat_seek_file       · I/P/B 帧依赖关系         · AVSEEK_FLAG_FRAME
    (容器层 Seek 入口)          (B 帧导致 DTS≠PTS，        (按帧号 Seek，绕过
  · av_read_frame              影响 Seek 后的解码顺序)    时间戳换算)
    (Seek 后从这里继续读)
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    v                             v
            [GOP 结构]                    [容器元信息]
            ─关联─                        ─关联─
            · I 帧间隔决定                · MP4: stss / stco / stsz Box
              Seek 精度下界               · MKV: Cues / SeekHead
            · 同 GOP 内不用               · FLV: 无原生索引，需逐帧扫描
              重新 Seek                  · TS: 无索引，Seek 很慢
            · Smart Seek
              依赖 GOP 边界判断
```

### 1.6 学习优先级总览

| 层级 | 内容 | 重要度 | 说明 |
|------|------|--------|------|
| 🟢 中级必会 | `av_seek_frame` / `avformat_seek_file` 用法 | 🔥🔥🔥 | 所有需要 Seek 的程序入口点，面试必问 |
| 🟢 中级必会 | `AVSEEK_FLAG_BACKWARD` 的含义和默认行为 | 🔥🔥🔥 | 自己写 Seek 前必须先理解这个 flag |
| 🟢 中级必会 | Seek 后的 `avcodec_flush_buffers` | 🔥🔥🔥 | 忘记 flush 导致花屏/解码失败的坑，99% 的人踩过 |
| 🟢 中级必会 | time_base 与 av_rescale_q 时间戳换算 | 🔥🔥🔥 | 秒→PTS 换算有 float 截断风险，面试常考 |
| 🟢 中级必会 | 关键帧索引 (`AVIndexEntry`) 的作用 | 🔥🔥 | 理解 Seek 为什么不是帧精确的 |
| 🟡 高级加分 | Smart Seek：同 GOP 内不重新 Seek | 🔥🔥 | 降低拖进度条的延迟，播放器面试加分项 |
| 🟡 高级加分 | `AVSEEK_FLAG_FRAME` 按帧号 Seek | 🔥🔥 | FFmpeg 5+ 新增，编辑器和 AI 推理首选 |
| 🟡 高级加分 | `AVSEEK_FLAG_BYTE` 按字节偏移 Seek | 🔥🔥 | 配合 FrameIndex 实现字节级精确寻址 |
| 🟡 高级加分 | FrameIndex 构建（逐帧扫描 + 字节偏移记录） | 🔥🔥 | 生产环境"上传时预建索引 → 线上查表"的标准模式 |
| 🔵 专家深水区 | 不同容器格式的索引结构差异（MP4 stss vs MKV Cues vs FLV 无索引） | 🔥 | 需要深入容器格式 spec |
| 🔵 专家深水区 | 自定义 `AVIOContext` 的 Seek 回调 | 🔥 | 网络流 / 自定义 IO 场景，需要理解 FFmpeg IO 层 |

---

## 二、面试速记（考前 15 分钟扫一遍）

> 这一部分的目标是"背了就能用"。每个问题都是面试高频原题，回答是可直接背诵的口语化表述。

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 出现频率 | 难度 |
|---|------|-----------|---------|------|
| 1 | `av_seek_frame` 和 `avformat_seek_file` 的区别 | 前者是旧 API，后者支持 `min_ts/max_ts` 范围更灵活 | 🔥🔥🔥 | 中级 |
| 2 | `AVSEEK_FLAG_BACKWARD` 做了什么 | Seek 到目标 PTS **之前**最近的关键帧，然后顺解到目标 | 🔥🔥🔥 | 中级 |
| 3 | Seek 后为什么必须 `avcodec_flush_buffers` | 解码器内部缓冲了旧的参考帧和未完成解码的 packet，不 flush 会花屏或解码失败 | 🔥🔥🔥 | 中级 |
| 4 | 秒→PTS 换算的正确方式 | `av_rescale_q` 或用毫秒整数中转，**禁用 float 直除** | 🔥🔥🔥 | 中级 |
| 5 | 为什么 Seek 不是帧精确的 | `AVSEEK_FLAG_BACKWARD` 落到最近关键帧，最多差一个 GOP 长度 | 🔥🔥 | 中级 |
| 6 | Smart Seek 是什么 | 同 GOP 内向前 Seek 时跳过 demux seek 和 flush，直接顺解剩余几帧 | 🔥🔥 | 高级 |
| 7 | `AVSEEK_FLAG_FRAME` 怎么用 | `av_seek_frame(fmt, stream_idx, frame_id, AVSEEK_FLAG_FRAME \| AVSEEK_FLAG_BACKWARD)` | 🔥🔥 | 高级 |
| 8 | FrameIndex 的构建和使用 | 上传时逐帧扫描记录每帧的 byte_offset 和 is_keyframe，线上用 `AVSEEK_FLAG_BYTE` 直接跳到最近关键帧 | 🔥 | 高级 |

### 2.2 面试标准回答

> 每个问题都包含：**面试官考察意图 → 口语化标准回答（可直接背诵）→ 追问预警 → 常见误区提示**。

---

#### Q1：说说 `av_seek_frame` 和 `avformat_seek_file` 的区别，你平时用哪个？

**面试官想听什么：** 考察你是否真的写过 Seek 代码，还是只背了 API 名字。

**🗣️ 标准回答（可背诵）：**

> "我平时主要用 `avformat_seek_file`，因为它的三个 PTS 参数能精确表达我的意图。先看两个函数的签名：
>
> ```
> // av_seek_frame：只有一个 timestamp，你只能说"我要去这里"
> av_seek_frame(fmt, stream_idx, timestamp, flags);
>
> // avformat_seek_file：三个 PTS，你说的是——
> //   "我要去 ts，但别给我比 min_ts 还早的，也别给比 max_ts 还晚的"
> avformat_seek_file(fmt, stream_idx, min_ts, ts, max_ts, flags);
> ```
>
> **为什么这个区别很重要？** 假设视频的关键帧分别在 0s、2s、4s、6s、8s，用户 Seek 到 3.5s：
>
> ```
> 0s        2s        4s        6s        8s
> │         │         │         │         │
> I₀        I₆₀       I₁₂₀      I₁₈₀      I₂₄₀
>                     ▲
>              用户要 3.5s
> ```
>
> `av_seek_frame` 只能传 `3.5s` 对应的 PTS，FFmpeg 内部找 ≤3.5s 的关键帧：落到 **2s**。你没得选。
>
> `avformat_seek_file` 的 `min_ts` 和 `max_ts` 让你可以限定关键帧的合法范围，这在实际工程中有三种典型用法：
>
> **用法一：标准播放器 Seek。** `min_ts=INT64_MIN, ts=3.5s, max_ts=3.5s`。翻译过来是"给我一个 ≤3.5s 的关键帧就行，往前蹿多远无所谓"。行为和 `av_seek_frame` 一样，这是最常见的写法。
>
> **用法二：按 GOP 并行切分。** 假设你要把视频分给 4 个 Worker 并行转码，Worker 1 负责 [2s, 4s)。你必须保证 Worker 1 落到 **2s 的关键帧**，而不是更早的。如果传 `min_ts=INT64_MIN`，FFmpeg 可能落到 0s 的关键帧——Worker 1 就会多做 0s~2s 的无用功，甚至跟 Worker 0 产出重复数据。正确做法是传 `min_ts=2s, ts=2s, max_ts=4s`：**"落的关键帧必须在 [2s, 4s) 区间内，不能跑到 2s 之前。"** 这就是 `min_ts` 的核心价值——你去面试音视频后端，讲清楚这个场景基本就过关了。
>
> **用法三：快速 Seek。** 传一个宽松区间 `min_ts=3s, ts=3.5s, max_ts=4s`，FFmpeg 在 3s~4s 之间找到合适的关键帧就停，选择更多、Seek 更快。适合缩略图/预览等不需要帧精确的场景。
>
> 总结一下：`av_seek_frame` 只能说"我要去哪"，`avformat_seek_file` 能说"我要去哪，而且我不接受比 X 早、比 Y 晚的结果"。这就是 `min_ts`/'精确目标 PTS'的本质区别。
>
> 不过要注意——即便用 `avformat_seek_file`，最后定位到的还是关键帧，不是目标帧本身。真正要拿到目标帧，还得在 Seek 之后顺解。"

**👨‍💻 追问预警：**
> 面试官很可能接着问：「`min_ts` 传 `INT64_MIN` 是什么意思？」
> 应对思路：`INT64_MIN` 表示"时间戳下界不限制"，等价于说"往前蹿多远都行"。如果传一个具体的值，比如 `target_pts - 1000`，FFmpeg 就不会落到比那更早的关键帧。这个能力在并行切 GOP、视频分段处理时是刚需——每个 Worker 必须严格在自己的时间窗口内工作，否则会跟相邻 Worker 产出重复数据。
>
> 面试官大概率会继续追问：「**那什么情况下必须传一个具体的 min_ts，不能用 INT64_MIN？**」
>
> 这时候你可以甩出四个具体场景，每个一句话：
>
> **1. 多 Worker 并行切 GOP 转码。** 最核心的场景。视频被切成 `[0s,2s)`、`[2s,4s)`、`[4s,6s)`... 每个 Worker 负责一段。Worker 1 如果传 `INT64_MIN`，FFmpeg 可能落到 0s 的关键帧——于是 Worker 1 产出 `[0s,4s)`，跟 Worker 0 的 `[0s,2s)` 重叠。拼接时重复编码，最终文件花屏或时长错误。正确做法：Worker 1 传 `min_ts=pts_of_2s`，锁死下界。
>
> **2. HLS/DASH 分片流内 Seek。** 用户 Seek 到 3.5s，你加载了 `segment-1.ts`（覆盖 `[2s,4s)`）。如果传 `INT64_MIN`，FFmpeg 可能在这个单文件里落到 0s 的索引残留，从分片开头解码白费功夫。传 `min_ts=seg_start_pts` 锁在当前分片的时间窗口内。
>
> **3. 时间戳异常文件兜底。** 某些不规范编码的文件，文件开头残留了一个 PTS=0 的孤儿 I 帧。用户 Seek 到文件中部，`INT64_MIN` 会让 FFmpeg 落到那个 PTS=0 的孤儿帧——追帧追到天荒地老。传 `min_ts=target_pts - 10秒` 就能跳过这些异常点。
>
> **4. `AVSEEK_FLAG_FAST` 快速 Seek。** 不要求帧精确时，给一个宽松区间 `[target-delta, target+delta]`，FFmpeg 选择更多、Seek 更快。传 `INT64_MIN` 区间太大，FFmpeg 反而需要多轮二分查找，并不一定更快。
>
> 这四个场景的共性：**`min_ts` 不是用来"帮助 FFmpeg 找到关键帧"的——FFmpeg 自己会找。`min_ts` 是用来防止 FFmpeg 找到一个"技术上合法但业务上不可接受"的关键帧。** 技术上 ≤ target 的关键帧都合法，但业务上落到邻居 Worker 的地盘里就出 bug。

**⚠️ 常见误区：**
> 很多人以为 `avformat_seek_file` 是"帧精确 Seek"——不是。它只是给了你更细粒度的关键帧选择范围（你可以说"别落到我隔壁 Worker 的地盘里"），但你仍然要顺解才能追到目标帧本身。另一个误区是觉得"`min_ts`/`max_ts` 是给高级用户用的，我永远传 `INT64_MIN` 就行"——如果你只做简单播放器，确实如此；但一旦涉及视频分段处理，不设 `min_ts` 就是在埋雷。

---

#### Q2：`AVSEEK_FLAG_BACKWARD` 到底做了什么？不带这个 flag 会怎样？

**面试官想听什么：** 考察你对 Seek 的底层行为有没有直觉。

**🗣️ 标准回答（可背诵）：**

> "`AVSEEK_FLAG_BACKWARD` 的含义是：Seek 到目标 PTS **之前**最近的关键帧。注意关键词——**之前**，不是之后。
>
> 举个例子：视频的 GOP 间隔是 2 秒，关键帧在 0s、2s、4s、6s... 用户 Seek 到 3.5s。带 `AVSEEK_FLAG_BACKWARD`，你会 Seek 到 2s 的关键帧位置，然后从那里开始顺解——你会依次解码 2s 的 I 帧、2.5s 的 P 帧、3.0s 的 B 帧、3.5s 的 P 帧... 最终拿到 3.5s 的帧。
>
> 如果不带这个 flag，FFmpeg 可能 Seek 到 4s 的关键帧——那在你这次 Seek 的路径上就永远拿不到 3.5s 的帧了。所以**视频播放场景几乎永远带 `AVSEEK_FLAG_BACKWARD`**。
>
> 有一个例外：只往前跳、不往回拖的场景，且你明确知道目标位置恰好是关键帧时，可以不带。但我个人习惯永远带上——代价只是可能多解几帧，但结果一定正确。"

**👨‍💻 追问预警：**
> 面试官可能接着问：「如果视频没有索引（比如 FLV），`AVSEEK_FLAG_BACKWARD` 还能工作吗？」
> 应对思路：能工作，但行为会退化为按字节估算 + 二分查找，精度取决于 FFmpeg 内部是否成功 build index（`avformat_find_stream_info` 时会尝试）。如果完全无索引，Seek 后可能落到任意位置，需要自己兜底。

---

#### Q3：Seek 之后为什么要 `avcodec_flush_buffers`？忘了会怎样？

**面试官想听什么：** 你是不是曾经忘记 flush 然后 debug 了俩小时。

**🗣️ 标准回答（可背诵）：**

> "解码器内部维护了两层缓冲：一个是还没解码完的 packet 队列（`avcodec_send_packet` 送进去了但没产出 frame），一个是解码后的参考帧缓存（DPB，Decoded Picture Buffer），比如 H.264 的 P 帧解码时需要前面 I 帧的重建图像。
>
> 当你 Seek 到一个新位置时，解码器里残留的旧参考帧和旧 packet 指向的是**另一个时间点**的数据。如果不 flush，新位置的数据就会跟旧参考帧混在一起解码——结果通常是花屏、绿屏、或者 `avcodec_receive_frame` 直接返回错误。
>
> `avcodec_flush_buffers` 做的就是清空这两层缓存，让解码器回到初始状态。这是 Seek 流程的标准一步：Seek → flush → 重新送 packet → 解码。忘掉这步的教训，我猜每个做音视频的人都经历过。"

**👨‍💻 追问预警：**
> 面试官可能追问：「flush 之后解码器的参数会丢吗？比如 SPS/PPS？」
> 应对思路：不会丢。`avcodec_flush_buffers` 只清空缓冲数据，不重置 `AVCodecContext` 的参数（`width`、`height`、`extradata` 等）。SPS/PPS 存在 `extradata` 里或是每个 IDR 帧之前都携带，不会丢。但如果你的流只有开头才有 SPS/PPS（比如某些不规范编码的 FLV），Seek 后需要手动重新送一遍 extradata。

---

#### Q4：UI 上的秒数怎么转成 Seek 用的 PTS？

**面试官想听什么：** 考察你是否理解 time_base 和浮点精度的问题。

**🗣️ 标准回答（可背诵）：**

> "核心原则是：**用整数运算，不要用 float**。
>
> 标准做法两步走。第一步，把秒转成毫秒整数：`int64_t ms = (int64_t)llround(seconds * 1000.0)`。第二步，用 `av_rescale_q` 把毫秒转成流的时间基：`int64_t pts = av_rescale_q(ms, (AVRational){1, 1000}, stream->time_base)`。
>
> 这样全程是整数运算，没有浮点截断。很多人直接写 `(int64_t)(seconds / av_q2d(time_base))`——这是有问题的。`av_q2d` 把分数转成 double，`1/90000` 就是 `0.000011111...`，除法结果有浮点误差，取整后可能偏 1~2 个 PTS 刻度。在 90kHz 时间基下差 1 个刻度就是约 0.01ms，可能刚好让 Seek 定位到上一帧而不是目标帧。
>
> 还有一个更好的方法——如果你知道视频是 CFR（固定帧率），直接用 `av_rescale_q(frame_id, av_inv_q(avg_frame_rate), time_base)` 做帧号到 PTS 的换算，完全不去碰秒数。但 VFR（可变帧率）不行，因为没有固定的帧间隔。"

**👨‍💻 追问预警：**
> 面试官可能追问：「`llround` 为什么要用？不能直接 `(int64_t)(seconds * 1000)` 吗？」
> 应对思路：`(int64_t)(2.3 * 1000)` 在 IEEE 754 下可能是 `2299.999...`，转整数截断后变成 `2299`，差了 1ms。`llround` 会四舍五入到最近的整数，避免这个问题。在对精度敏感的 Seek 边界，差 1ms 意味着差一帧——用户会感知到。

---

#### Q5：为什么 Seek 不能是帧精确的？有没有办法做到帧精确？

**面试官想听什么：** 考察你对 GOP 结构和帧依赖关系的理解。

**🗣️ 标准回答（可背诵）：**

> "Seek 不是帧精确的根本原因是视频编码的帧间依赖——P 帧和 B 帧不能独立解码。你跳到 P 帧的位置，解码器拿到它的第一件事是找参考帧——但参考帧还在前面，你跳过了。所以 Seek 必须落到一个 I 帧（IDR 帧），然后从那里开始解码，一步步追到目标帧。
>
> 那'帧精确'怎么做？三个方案，精度递增：
>
> 方案一：用 `av_seek_frame(FLAG_FRAME)`。FFmpeg 5+ 支持按帧号 Seek，内部自己做了帧号到关键帧的映射。对于大多数 MP4/MKV 文件，这是最简单的帧精确方案。
>
> 方案二：上传时预建 FrameIndex。逐帧扫描一遍视频，记录每帧的 file byte offset 和 is_keyframe 标记。线上要抽第 N 帧时，从索引表里找到它前面最近的关键帧的 byte_offset，用 `AVSEEK_FLAG_BYTE` 直接跳到那个字节位置，然后解码到第 N 帧。这是最精确的方案，字节级定位。
>
> 方案三：如果你能控制编码参数，把 GOP 大小设为 1（全 I 帧）。每一帧都是关键帧，Seek 到哪都是帧精确的。代价是文件体积增大 5~10 倍，只适合特定场景（如视频编辑的中间素材）。
>
> 实际项目中，方案一覆盖 80% 的场景，方案二用在精确度要求极高的场景（AI 抽帧、缩略图服务）。"

**👨‍💻 追问预警：**
> 面试官可能追问：「FrameIndex 什么时候建？建一次开销多大？」
> 应对思路：在视频上传完成后异步建。开销 = 一遍完整解码（`av_read_frame` + `avcodec_send_packet` + `avcodec_receive_frame`），对 1080p H.264 视频大概是 1x~2x 实时速度（取决于 CPU）。对于用户上传的视频，这个开销可以由后台 Worker 承担，用户无感。索引数据本身很小——每帧 20~30 字节，10 分钟 30fps 视频也就约 500KB。

---

#### Q6：什么是 Smart Seek？什么时候能跳过 demux seek？

**面试官想听什么：** 考察你是否想过"不是每次拖动进度条都要重新 Seek"这个优化。

**🗣️ 标准回答（可背诵）：**

> "Smart Seek 的思路是：如果你现在的解码位置和目标位置在同一个 GOP 内，且目标是往前走（不前回退），那你根本不需要重新 demux seek 和 flush。直接从当前位置继续 `av_read_frame` + 解码，跳过中间几帧，追到目标帧就行了。
>
> 举个具体的例子：一个 GOP 有 60 帧（2 秒 @ 30fps），当前你解码到第 10 帧，用户 Seek 到第 45 帧。这时候检查一下——第 10 帧和第 45 帧之间的关键帧索引里没有新的关键帧，说明它们在同一个 GOP 内。你不需要 `avformat_seek_file` 也不需要 `avcodec_flush_buffers`——解码器里已经有这个 GOP 的正确参考帧了——你只需要继续顺序解码，丢弃中间 34 帧，解到第 45 帧就拿到目标了。
>
> 省掉了什么？一次 `avformat_seek_file`（可能需要重新解析 moov/索引）、一次 `avcodec_flush_buffers`（清空 DPB）、重新送 extradata/SPS/PPS 的开销。加起来在服务器上可能从 15ms 降到 1ms。
>
> 判断条件就两个：一是目标在当前之后（不是回退），二是从当前到目标之间没有关键帧。两个条件都满足，就可以走 Smart Seek。这个判断需要依赖关键帧索引——所以 `avformat_find_stream_info` 时 build index 很重要。"

**👨‍💻 追问预警：**
> 面试官可能追问：「如果关键帧索引是空的怎么办？」
> 应对思路：那就退化为安全的做法——每次都走 Classic Seek。Smart Seek 是优化，不是功能依赖。没有索引就保守处理。

---

### 2.3 一个「串起来」的完整回答模板

> **面试官问：「说说你对 FFmpeg Seek 整体流程的理解，从用户拖动进度条到画面出现。」**

"好，我把整个过程拆成五步。

第一步是**时间换算**。用户给的是秒——2.5 秒。我们用 `SecondsToPtsAccurate`：先把秒转成毫秒整数，再用 `av_rescale_q` 把 `{1, 1000}` 的时间基转到流的 `time_base`。这样得到一个精确的 `int64_t target_pts`，没有浮点误差。

第二步是**查关键帧索引**。通过 `avformat_index_get_entry_from_timestamp(stream, target_pts, AVSEEK_FLAG_BACKWARD)`，找到 target_pts 之前最近的关键帧。这个索引是 `avformat_find_stream_info` 时 build 的——对于 MP4，它等价于解析了 stss Box。

第三步是**Seek 决策**。如果是首次 Seek：必须走 Classic Seek。如果是回退：必须走 Classic Seek。如果是向前但跨了 GOP：走 Classic Seek。如果向前且同 GOP 内：走 Smart Seek——跳过 demux seek。

第四步是**执行 Seek**。Classic Seek 的路径：`avformat_seek_file(fmt, stream_idx, INT64_MIN, target_pts, target_pts, AVSEEK_FLAG_BACKWARD)` → `avcodec_flush_buffers(dec)`。Smart Seek 的路径：什么都不做，继续用当前位置。

第五步是**顺解到目标**。`av_read_frame` 循环取 packet → `avcodec_send_packet` → `avcodec_receive_frame`，检查 `frame->pts >= target_pts`。一旦满足，拿到目标帧，退出循环。

这五步做下来，从用户拖进度条到画面更新，在本地文件上通常是 2~10ms。"（有余力再展开 → 可以提一下 `AVSEEK_FLAG_FRAME` 替代前两步的方案，但那是高级话题。）

---

## 三、原理深讲（周末花 1 小时吃透）

> 这一部分是留给"有时间想深入搞懂"的时刻。每一节独立可读，不必顺序通读。

### 3.1 核心原理与机制

#### 3.1.1 🟢 中级：Seek 为什么必须落到关键帧

视频编码的核心是**帧间预测**——P 帧和 B 帧只存储了和参考帧的"差异"，不存储完整画面。解码时，解码器手里必须有前面 I 帧（或 P 帧）的重建图像，才能"还原"当前帧。

```
 GOP #0                               GOP #1
 ┌─────────────────────────────────┐  ┌───────────────────...
 │ I₀  B₁  B₂  P₃  B₄  B₅  P₆ ... │  │ I₆₀  B₆₁  P₆₂ ...
 └─────────────────────────────────┘  └───────────────────...
   ▲                                  ▲
   │ 可以独立解码                      │ 可以独立解码
   其他帧依赖前序参考帧                 其他帧依赖前序参考帧
```

如果你直接 Seek 到 P₃ 的位置，解码器拿到的第一个 packet 就是 P₃ 的压缩数据。它尝试解码，发现参考帧列表里没有 I₀ 的重建图像——解码失败或者花屏。

所以 Seek 的策略是：**退到最近的关键帧，从那里开始建立解码上下文，然后顺向解码到目标帧。** 这就是 `AVSEEK_FLAG_BACKWARD` 的物理含义。

```
用户要第 45 帧 (P₄₅)
       │
       │  Seek 落到...
       v
   I₀ → P₃ → ... → P₄₂ → P₄₅  ← 从 I₀ 开始，一个个解过去，追到 P₄₅
   ▲                    ▲
   │                    │
关键帧(GOP起点)      目标帧(丢弃中间的 P₃..P₄₂)
```

#### 3.1.2 🟡 高级：GOP 结构与 Seek 性能的定量关系

GOP（Group of Pictures）是两个 I 帧之间的帧序列。GOP 大小直接影响 Seek 的性能：

| GOP 大小 | Seek 精度 | 最坏解码开销（追到目标帧） | 文件体积 |
|----------|----------|--------------------------|---------|
| 1（全 I） | 帧精确 | 0 帧 | 很大（5~10x） |
| 30（1秒 @ 30fps） | ≈1 秒 | 最多 29 帧 | 标准 |
| 60（2秒 @ 30fps） | ≈2 秒 | 最多 59 帧 | 较小 |
| 300（10秒 @ 30fps） | ≈10 秒 | 最多 299 帧 | 更小 |
| 600（20秒） | ≈20 秒 | 最多 599 帧 | 最小 |

**这意味着一个权衡**：GOP 越大 → 压缩效率越高（文件越小），但 Seek 精度越低、追帧开销越大。

实际的 GOP 还要考虑**开放 GOP vs 封闭 GOP**：
- **封闭 GOP**：GOP 内的帧不参考前一个 GOP 的帧。Seek 严格落到 I 帧即可。
- **开放 GOP**：GOP 开头的一些 B 帧可能参考前一个 GOP 的最后 P 帧。这种情况下 Seek 到一个 I 帧，本 GOP 前几个 B 帧仍然无法解码。FFmpeg 用 `AVINDEX_KEYFRAME` 标记的是 IDR 帧（封闭 GOP 的 I 帧），避免这种问题。

#### 3.1.3 🔵 专家：不同容器的索引机制差异

```
MP4/MOV:
  moov → trak → stbl
    ├── stss (Sync Sample Box)     ← 哪些 sample 是关键帧
    ├── stco / co64 (Chunk Offset) ← 每个 chunk 的文件字节偏移
    ├── stsc (Sample-to-Chunk)     ← 每个 chunk 有几个 sample
    └── stsz (Sample Size)         ← 每个 sample 的字节大小
  结论: ★★★★★ 索引完美，Seek 快且精确

MKV/WebM:
  SeekHead → Cues
    └── CuePoint { Timecode → Cluster 字节位置 }
  结论: ★★★★ 索引完善，Seek 表现好

FLV:
  无原生关键帧索引。
  FFmpeg 在 avformat_find_stream_info 时会自动扫描建索引，
  但对于大文件可能不完整。
  结论: ★★ Seek 可能慢且不精确

TS (MPEG-TS):
  无原生索引，纯靠 FFmpeg 扫描建索引。
  大文件 Seek 非常慢。
  结论: ★ 尽量转封装成 MP4 再用
```

### 3.2 关键数据结构 / API / 参数详解

#### 3.2.1 Seek 相关 API

| 名称 | 作用 | 注意事项 |
|------|------|---------|
| `av_seek_frame(fmt, stream_idx, timestamp, flags)` | 基础 Seek：按时间戳或字节或帧号定位 | ⚠️ `timestamp` 的单位取决于 `flags`：默认是 `time_base` 单位；`FLAG_BYTE` 时是字节偏移；`FLAG_FRAME` 时是帧序号。别混用。 |
| `avformat_seek_file(fmt, stream_idx, min_ts, ts, max_ts, flags)` | 增强 Seek：限定关键帧的合法 PTS 区间 | ⚠️ `min_ts` = "最早接受的关键帧 PTS"（传 `INT64_MIN` 表示不限制），`ts` = 你的精确目标，`max_ts` = "最晚接受的关键帧 PTS"。三者永远是 `time_base` 单位，不受 flag 影响。并行切 GOP 时 `min_ts` 是关键——防止落到相邻 Worker 的区间里。 |
| `avcodec_flush_buffers(ctx)` | 清空解码器缓冲 | ⚠️ Seek 后**必须调用**。不会丢失 `extradata`（SPS/PPS），但会丢失尚未产出的 frame。 |
| `avformat_index_get_entry(stream, idx)` | FFmpeg 6.0+：获取指定位置的索引条目 | ⚠️ FFmpeg 5.x 及之前用 `stream->index_entries[idx]` 直接访问。6.0 起该字段不公开，必须用此函数。 |
| `avformat_index_get_entries_count(stream)` | 获取索引条目总数 | ⚠️ 返回 -1 表示无索引 |
| `avformat_index_get_entry_from_timestamp(stream, ts, flags)` | 按时间戳查找最近的索引条目 | ⚠️ `flags` 用 `AVSEEK_FLAG_BACKWARD` 表示找 ≤ ts 的最近条目；用 `AVSEEK_FLAG_ANY` 找最近的（可能 > ts） |

#### 3.2.2 Seek Flag 详解

| Flag | 值 | 含义 | 使用场景 |
|------|---|------|---------|
| `AVSEEK_FLAG_BACKWARD` | 1 | Seek 到 ≤ target 的最近关键帧 | 播放器拖动进度条（最常用） |
| `AVSEEK_FLAG_BYTE` | 2 | timestamp 参数解释为字节偏移 | 编辑器精确定位、FrameIndex 方案 |
| `AVSEEK_FLAG_ANY` | 4 | Seek 到任意帧（不强制关键帧），可能返回非关键帧的 packet | 仅用于特殊场景，不推荐常规使用 |
| `AVSEEK_FLAG_FRAME` | 8 | timestamp 参数解释为帧序号 | FFmpeg 5+，配合 `av_seek_frame` 使用 |
| `AVSEEK_FLAG_FAST` | 16 | 启发式快速 Seek（牺牲精度换速度） | 只用于预览场景 |

**常用组合**：

```c
// 用法一：标准播放器 Seek（最常用）
// min_ts=INT64_MIN → "多早都行，我不限制"
avformat_seek_file(fmt, video_stream_idx,
                   INT64_MIN, target_pts, target_pts,
                   AVSEEK_FLAG_BACKWARD);

// 用法二：按 GOP 并行切分（min_ts 的核心价值）
// Worker 1 负责 [2s, 4s)，必须保证落到 2s 的关键帧，不能跑到 0s 去
avformat_seek_file(fmt, video_stream_idx,
                   pts_of_2s,   // min_ts: 最早接受 2s 处的关键帧
                   pts_of_2s,   // ts:     目标就是 2s
                   pts_of_4s,   // max_ts: 最晚接受 4s 处的关键帧
                   AVSEEK_FLAG_BACKWARD);

// 用法三：快速 Seek（牺牲精度换速度）
// 给 ±delta 的宽松区间，FFmpeg 选择更多 → Seek 更快
avformat_seek_file(fmt, video_stream_idx,
                   target_pts - delta, target_pts, target_pts + delta,
                   AVSEEK_FLAG_BACKWARD);

// 按帧号精确 Seek（FFmpeg 5+）
av_seek_frame(fmt, video_stream_idx, frame_id,
              AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD);

// 按字节偏移 Seek
av_seek_frame(fmt, video_stream_idx, byte_offset,
              AVSEEK_FLAG_BYTE);

// ⚠️ 错误用法：混用 FLAG_BYTE 和 avformat_seek_file
// avformat_seek_file 的 min_ts/ts/max_ts 参数永远是 time_base 单位！
// FLAG_BYTE 只对 av_seek_frame 的单 timestamp 参数生效。
```

#### 3.2.2.1 为什么需要 `min_ts` / `max_ts`？——五种打破直觉的场景

> 一个自然的疑问：**"落到目标之前最近的关键帧不就行了，为什么还要用 `min_ts` 和 `max_ts` 限制范围？传 `INT64_MIN` 不香吗？"** 
>
> 对于简单播放器来说，你说得对。但以下五种工程场景，`INT64_MIN` 会直接导致数据错误或性能灾难。

---

**场景 A：多 Worker 并行转码（`min_ts` 的核心价值）**

这是 `min_ts` 最重要的使用场景。把一个长视频按 GOP 边界切成 N 段，分给 N 个 Worker 各自转码，最后拼回一个文件。关键约束是：**相邻 Worker 之间不能有重叠，也不能有间隙**。

```
整个视频: ═══════════════════════════════════════════════
           0s         2s         4s         6s         8s
           │          │          │          │          │
           I₀         I₆₀        I₁₂₀       I₁₈₀       I₂₄₀

切分方案:  Worker 0   Worker 1   Worker 2   Worker 3
           [0s, 2s)   [2s, 4s)   [4s, 6s)   [6s, 8s)
```

Worker 1 的代码可能写成：

```c
// ❌ 直觉写法：传 INT64_MIN
int64_t target = pts_of_2s;
avformat_seek_file(fmt, stream_idx,
                   INT64_MIN, target, target,   // min_ts 不限制
                   AVSEEK_FLAG_BACKWARD);
```

**问题**：`pts_of_2s` 之前最近的关键帧是 `I₆₀`（恰好就在 2s），所以这次能正确落到 2s。**但**——如果因编码参数微调或其他原因，`I₆₀` 实际的 PTS 偏了一点点（比如 1.98s），而更前面的 `I₀`（0s）也是 ≤ target 的关键帧。FFmpeg 的二分查找在 `INT64_MIN` 的无约束下，**可能落到 0s 的 I₀**。

后果：Worker 1 从 0s 开始解，产出的数据跟 Worker 0 有重叠。拼接时 Worker 0 的 [0s,2s) 和 Worker 1 的 [0s,4s) 就会在同一段上重复编码，最终文件花屏或时长错误。

```c
// ✅ 正确写法：用 min_ts 锁死下界
avformat_seek_file(fmt, stream_idx,
                   pts_of_2s,      // min_ts: "最早只能落到 2s，不许再往前"
                   pts_of_2s,      // ts:     目标 2s
                   pts_of_4s,      // max_ts: "最晚 4s，不能跑到下一个 Worker 的地盘"
                   AVSEEK_FLAG_BACKWARD);
```

这样即使 I₆₀ 偏到了 1.98s（< min_ts），FFmpeg 也会跳过它，落到 [2s, 4s) 区间内的下一个关键帧。**每个 Worker 严格在自己的时间窗口内工作，边界干净**。

---

**场景 B：HLS / DASH 分片流（`min_ts` 避免跨 segment）**

HLS 视频被切成了独立的 `.ts` 分片文件，每个分片只覆盖一段固定时间：

```
segment-0.ts   segment-1.ts   segment-2.ts   segment-3.ts
[0s - 2s]      [2s - 4s]      [4s - 6s]      [6s - 8s]
```

用户 Seek 到 3.5s，你的程序选择了 `segment-1.ts`（覆盖 [2s, 4s)）。如果用 `av_seek_frame(segment-1.ts, 3.5s, BACKWARD)`，FFmpeg 在这个**单文件**里找到 ≤3.5s 的最近关键帧——可能落到 segment-1.ts 的开头 2s 处，也可能落在文件的最开头 0s 处（如果 keyframe 索引跨文件未清理）。

传 `min_ts = seg_start_pts` 就能把这个 Seek 锁定在当前 segment 的时间窗口内，不会越界。

---

**场景 C：时间戳异常的文件（`min_ts` 兜底）**

某些不规范编码的文件，开头几帧的 PTS 异常——比如 PTS=0 有一个很靠后的 GOP 残留入口。当用户 Seek 到文件中部时：

```
PTS:  0         0         ...   5000      6000      7000
      │         │                │         │         │
    孤儿I帧   正常开头           正常中部关键帧
    (编码器bug遗留)
```

如果你传 `INT64_MIN`，FFmpeg 可能落到 PTS=0 的那个孤儿 I 帧——它虽然 ≤ target，但距离目标十万八千里，解码追帧追到天荒地老。传一个合理的 `min_ts`，比如 `target_pts - 10秒`，就能跳过这些异常点。

---

**场景 D：`AVSEEK_FLAG_FAST` 的快速启发式 Seek（`min_ts` 和 `max_ts` 同时宽松）**

当你不要求帧精确、只想要"大概靠这个时间点"的画面时（如缩略图预览），可以把区间放宽：

```c
avformat_seek_file(fmt, stream_idx,
                   target_pts - delta,   // min_ts: 宽松下界
                   target_pts,           // ts:     目标
                   target_pts + delta,   // max_ts: 宽松上界
                   AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FAST);
```

FFmpeg 可以在 [target-delta, target+delta] 之间选任意一个关键帧——选择更多，seek 更快。如果传 `INT64_MIN`，区间变成全文件范围，**表面上选择最多，但实际上 FFmpeg 反而需要多轮二分查找才能找到合适的**，并不一定更快。

---

**场景 E：多流同步 Seek（隐式生效）**

当文件有视频 + 音频两路流时，`avformat_seek_file` 的 `min_ts`/`max_ts` 不仅为视频流服务——FFmpeg 内部会用它们去定位音频流的关键帧（或 seek point），确保 Seek 后音视频是同步的。传 `INT64_MIN` 时，音频流的回退距离可能远大于必要，导致 Seek 后第一段音频有可闻的静音或错位。

---

**总结**：

| 你传的 `min_ts` | 含义 | 什么时候用 |
|:--|:--|:--|
| `INT64_MIN` | "多早都行" | 简单播放器，不关心边界 |
| `target_pts` | "不得早于目标本身" | 并行切 GOP，锁定 Worker 边界 |
| `target - K` | "可以往前找，但别找太远" | 兜底时间戳异常；HLS 分片内 Seek |
| `target - 大delta` | 配合 `max_ts` 给宽松区间 | 快速预览 Seek |

#### 3.2.3 关键数据结构

| 名称 | 关键字段 | 说明 |
|------|---------|------|
| `AVIndexEntry` | `timestamp`（PTS）、`pos`（字节偏移）、`flags`（含 `AVINDEX_KEYFRAME`）、`min_distance`（到下一关键帧的距离） | 一个索引条目代表容器中的一个 seek point，通常是关键帧 |
| `AVStream.nb_frames` | 容器声明的总帧数 | ⚠️ 可能为 0（如 FLV/TS 无此元信息），不可完全信赖 |
| `AVStream.avg_frame_rate` | 平均帧率 | 用于帧号↔时间戳的换算。CFR 可靠，VFR 不适用 |
| `AVStream.time_base` | 流的时间基 | 所有 PTS 的单位。⚠️ 不同流有不同 time_base，不能混用 |
| `AVFormatContext.duration` | 文件总时长（`AV_TIME_BASE` 单位，即微秒） | `duration / AV_TIME_BASE` = 秒 |

### 3.3 典型工作流 / 端到端流程

#### 🟢 中级：标准 Seek → 解码流程

```
[1] 用户输入秒数 (2.5s)
        │
        v
[2] 秒 → PTS: int64_t target_pts = av_rescale_q(ms, {1,1000}, stream->time_base);
        │
        v
[3] 查关键帧: AVIndexEntry* e = avformat_index_get_entry_from_timestamp(
                                    stream, target_pts, AVSEEK_FLAG_BACKWARD);
    输出: 最近关键帧的 PTS 和字节位置（用于日志/调试）
        │
        v
[4] avformat_seek_file(fmt, stream_idx, INT64_MIN, target_pts, target_pts,
                        AVSEEK_FLAG_BACKWARD)
    效果: 文件读指针跳转到目标 PTS 之前最近的关键帧
        │
        v
[5] avcodec_flush_buffers(dec)
    效果: 清空解码器的 DPB 和未完成 packet 队列
        │
        v
[6] while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != video_idx) continue;
        avcodec_send_packet(dec, pkt);
        while (avcodec_receive_frame(dec, frame) >= 0) {
            if (frame->pts >= target_pts) {
                // ★ 拿到目标帧，退出循环
                goto done;
            }
            av_frame_unref(frame);  // 丢弃追赶路径上的中间帧
        }
    }
        │
        v
[7] done: 使用目标帧（展示、编码、存图...）
```

**完整代码示例**（可直接编译运行，配套项目 `10_FrameAccurate_Seek_Demo`）：

```c
// 完整流程：UI 秒 → PTS → Seek → 解码到目标帧
// 来自 project/10_FrameAccurate_Seek_Demo/main.cpp

// ✅ 大厂推荐：整数 rescale，避免 float 边界误差
static int64_t SecondsToPtsAccurate(double seconds, AVRational timeBase) {
    const int64_t ms = static_cast<int64_t>(llround(seconds * 1000.0));
    return av_rescale_q(ms, AVRational{1, 1000}, timeBase);
}

// Classic Seek：avformat_seek_file + flush
static int ClassicSeek(AVFormatContext *fmt, AVCodecContext *dec,
                       AVStream *stream, int64_t targetPts) {
    const int ret = avformat_seek_file(
        fmt, stream->index, INT64_MIN, targetPts, targetPts,
        AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return ret;
    avcodec_flush_buffers(dec);
    return 0;
}

// 顺解码直到 frame->pts >= targetPts
static SeekResult DecodeUntilPts(AVFormatContext *fmt, AVCodecContext *dec,
                                 int videoStreamIndex, int64_t targetPts,
                                 AVFrame *outFrame) {
    SeekResult result;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(dec, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(dec, frame) >= 0) {
            result.decodedFrames++;
            if (frame->pts != AV_NOPTS_VALUE && frame->pts >= targetPts) {
                av_frame_ref(outFrame, frame);
                result.ok = true;
                result.framePts = frame->pts;
                goto done;
            }
            result.droppedFrames++;
            av_frame_unref(frame);
        }
    }
done:
    av_packet_free(&packet);
    av_frame_free(&frame);
    return result;
}
```

#### 🟡 高级：FrameIndex + BYTE Seek 精确抽帧流程

```
[前置] 上传时异步构建完整索引
        │
        v
BuildFullFrameIndex(fmt, dec, stream, video_idx, keyframes):
  ┌─ av_seek_frame(fmt, video_idx, 0, AVSEEK_FLAG_BYTE)  // 回到文件头
  ├─ avcodec_flush_buffers(dec)
  ├─ while (av_read_frame + decode) {
  │    记录每帧: frame_id, pts, byte_offset, size_bytes,
  │              is_keyframe, gop_index, frame_in_gop
  │  }
  └─ 返回 FrameIndex (vector<FrameSampleInfo>)
        │
        v
[线上] 抽第 N 帧:
  ┌─ 查 FrameIndex[N]: 找到 is_keyframe、byte_offset
  ├─ 向前找到最近关键帧: kf_id = N; while (!index[kf_id].is_keyframe) kf_id--;
  ├─ av_seek_frame(fmt, video_idx, index[kf_id].byte_offset, AVSEEK_FLAG_BYTE)
  ├─ avcodec_flush_buffers(dec)
  └─ 解码从 kf_id 追到 N: 跟标准流程一样的 av_read_frame + decode 循环
```

#### 🟡 高级：Smart Seek 决策流程

```
当前解码位置: current_pts
用户 Seek 到: target_pts

决策树:
  if (is_first_seek || current_pts == AV_NOPTS_VALUE)
      → Classic Seek（首次，必须建立基线）
  else if (target_pts < current_pts)
      → Classic Seek（回退，解码器不能倒放）
  else if (HasKeyframeBetween(keyframes, current_pts, target_pts))
      → Classic Seek（跨 GOP，解码器基线不连续）
  else
      → Smart Seek（同 GOP 向前，复用当前解码器状态）
```

**代码实现**：

```c
// 判断两个 PTS 之间是否存在关键帧
static bool HasKeyframeBetween(const std::vector<KeyframeEntry>& keyframes,
                               int64_t lo_pts, int64_t hi_pts) {
    for (const auto& kf : keyframes) {
        if (kf.pts > lo_pts && kf.pts < hi_pts) return true;
    }
    return false;
}

static SeekDecision MakeSeekDecision(const std::vector<KeyframeEntry>& keyframes,
                                     int64_t current_pts, int64_t target_pts,
                                     bool is_first_seek) {
    if (is_first_seek || current_pts == AV_NOPTS_VALUE)
        return {true, "首次 Seek，需要建立解码器基线"};
    if (target_pts < current_pts)
        return {true, "目标在当前解码位置之前，解码器不能倒放"};
    if (HasKeyframeBetween(keyframes, current_pts, target_pts))
        return {true, "跨 GOP，中间有关键帧"};
    return {false, "同 GOP 内向前 → Smart Seek"};
}
```

### 3.4 性能上的坑与避坑指南

**场景 1：用户快速拖动进度条（高频 Seek）**

- **常见的做法**：每次 `onSeekBarChanged` 回调都执行完整的 `avformat_seek_file + flush + 解码`。
- **坑在哪里**：用户从 0s 拖到 10s 的过程中，可能触发 50 次回调。每次都做完整的 demux seek——对本地文件还行（~5ms/次），但对网络流（HTTP Range 请求）就是灾难，50 次 HTTP 请求把带宽和延迟都打满了。
- **正确做法**：
  1. 拖动过程中只更新 UI，不发起 Seek
  2. 用户松手（`onSeekBarReleased`）时才发起一次真正的 Seek
  3. 如果用户松手后又立即拖动，取消上一次正在进行的 Seek（用 abort 标志或 `avformat_interrupt_cb`）
  4. 效果：从 50 次 HTTP seek → 1 次，延迟从 ~2s 降到 ~100ms

**场景 2：循环抽帧（每秒抽一帧）**

- **常见的做法**：
  ```c
  for (double t = 0; t < duration; t += 1.0) {
      avformat_seek_file(fmt, ..., SecondsToPtsFloat(t, tb), ...);  // ❌
      avcodec_flush_buffers(dec);                                    // ❌
      DecodeUntilPts(...);
      save_frame(...);
  }
  ```

- **坑在哪里**：
  1. **每次 Seek 只用 float 换算秒数**：`SecondsToPtsFloat` 用 `seconds / av_q2d(tb)` 取整，浮点误差累积导致 `t=5.0` 算出来的 PTS 和 `t=5.0` 应该是的 PTS 不同。随着 t 变大，误差可能偏 2~3 个 PTS 刻度。
  2. **每次都 flush + Seek**：即使相邻两次采样在同一个 GOP 内，也重新做了 demux seek。对于 30fps、GOP=60 的视频，连续 60 次采样只需要 1 次真正的 Seek！
  3. **每次 Seek 后用 float 秒数算 PTS，跟上次的 actual PTS 对不上**：`t += 1.0` 是数学上的加 1 秒，但实际帧的 PTS 不正好是整数秒。第 30 帧的 PTS 可能是 `1.001s`，你 Seek 到 `1.000s` 就会每次都拿到同一帧。

- **正确做法**：
  ```c
  // ✅ 用 av_rescale_q 精确换算
  int64_t pts = av_rescale_q((int64_t)(t * 1000), {1,1000}, time_base);
  
  // ✅ Smart Seek：检查是否可以跳过 demux seek
  SeekDecision d = MakeSeekDecision(keyframes, current_pts, pts, is_first);
  if (d.need_classic_seek) {
      avformat_seek_file(...);
      avcodec_flush_buffers(dec);
  }
  DecodeUntilPts(...);
  
  // ✅ 用实际拿到帧的 PTS 作为下一次的起点
  current_pts = actual_frame_pts;
  // ✅ 用 current_pts 反算下一次的 target_pts
  // 而不是 t += 1.0 继续数学累加
  ```

- **效果**：10 分钟视频，每秒抽一帧（600 次），Smart Seek 命中率通常在 80~95%（取决于 GOP 大小），总耗时从 ~3s 降到 ~0.3s。

**场景 3：Seek 到文件末尾附近**

- **常见的做法**：直接 Seek 到最后几秒，然后 `av_read_frame` 解码。
- **坑在哪里**：如果视频最后一帧离文件末尾很近，`avformat_seek_file` 可能 Seek 到最后一个关键帧，但之后 `av_read_frame` 读不到足够的 packet 来解码目标帧。`avcodec_receive_frame` 返回 `AVERROR_EOF`，你以为解码完了——但实际上还有几帧没解出来（它们在解码器 DPB 里排队）。
- **正确做法**：Seek 到末尾区域时，解码到最后要 drain decoder——`avcodec_send_packet(dec, NULL)` 然后继续 `avcodec_receive_frame`，把 DPB 里排队的帧都取出来。

### 3.5 与竞品 / 替代方案的对比

| 维度 | FFmpeg Seek | GStreamer Seek | 自解析 MP4 + 手动定位 |
|------|------------|---------------|---------------------|
| 容器覆盖 | 所有常见格式 | 依赖插件，覆盖不如 FFmpeg 广 | 只支持 MP4 |
| Seek API 灵活度 | 4 种 flag × 2 个入口函数 | 基于事件，Seek 后等 `SEGMENT_DONE` 消息 | 完全自定义 |
| 帧精确 Seek | `FLAG_FRAME`（5.0+）/ FrameIndex + `FLAG_BYTE` | 需手动管理 segment | 可以做到最精确（你控制一切） |
| 学习曲线 | 中等，需要理解 time_base/PTS/GOP | 较高，事件驱动心智模型 | 高，需要理解 ISO BMFF spec |
| 社区/文档 | 极丰富 | 较多 | 无 |
| 适用场景 | 通用音视频开发 | GStreamer 管线项目 | 对 MP4 有极端定制需求的项目 |
| 选型建议 | **默认首选**，除非你已经深度绑定 GStreamer | 如果整个项目已经用 GStreamer 构建管线 | 自己解析 MP4 的 stss/stco/stsz Box 通常不划算 |

### 3.6 延伸阅读与进阶方向

- [[00-FFmpeg全景导读]] — 理解 Seek 在整个 FFmpeg 体系中的位置
- [[01-数据结构与生命周期]] — 深入 `AVIndexEntry`、`AVStream`、`AVFormatContext` 的字段含义
- [[05-H264-MP4-NALU]] — 理解 MP4 stss Box 与 `AVINDEX_KEYFRAME` 的对应关系
- [[06-编码参数与码控]] — 理解 GOP 大小如何影响 Seek 精度和性能
- **FFmpeg 官方文档**：`doc/APIChanges` 查看 `avformat_index_get_entry` 从哪个版本引入
- **源代码**：`libavformat/utils.c` 中的 `seek_frame_internal` 和 `avformat_seek_file` 实现，理解内部二分查找逻辑
- **配套 Demo**：
  - `project/10_FrameAccurate_Seek_Demo/` — Classic Seek + Smart Seek 完整流程
  - `project/11_FrameIndex_Extraction_Demo/` — FrameIndex 构建 + FLAG_FRAME + FLAG_BYTE + Smart Seek 决策引擎 + 精度对比
- **前沿方向**：AV1 编码的开放 GOP 对 Seek 行为的影响；WebCodecs API 中 `VideoDecoder` 的 seek 模型与 FFmpeg 的差异

---

## 四、自检题（合上文档能回答吗？）

1. `av_seek_frame` 和 `avformat_seek_file` 的参数有什么本质区别？为什么推荐后者？
2. 用户输入 2.5 秒，你拿到一个 `time_base={1,90000}` 的视频流。写出从秒到 PTS 的正确换算代码，并说明为什么不能用 `(int64_t)(2.5 / av_q2d(time_base))`。
3. Seek 后不调用 `avcodec_flush_buffers` 会发生什么？为什么？
4. 为什么 `AVSEEK_FLAG_BACKWARD` 是"落到之前最近的关键帧"而不是"落到目标帧本身"？如果用 `AVSEEK_FLAG_ANY` 会有什么风险？
5. Smart Seek 的触发条件是什么？在什么情况下它比 Classic Seek 能快 10 倍以上？
6. `AVSEEK_FLAG_FRAME` 和 FrameIndex + `AVSEEK_FLAG_BYTE` 两种帧精确方案各有什么优劣？什么时候用哪个？
7. 如果你的视频是 FLV 格式，Seek 行为会和 MP4 有什么不同？为什么？
8. 循环抽帧（每秒抽一帧）时，为什么不能用 `t += 1.0` 算下一次的目标时间？应该怎么累积？
9. 对于 VFR（可变帧率）视频，用 `avg_frame_rate` 做帧号→时间戳换算为什么不可靠？应该用什么替代方案？
10. 你在 `av_read_frame` 循环里丢弃中间帧时，有没有可能把目标帧也提前丢弃了？如何正确判断"这一帧就是我要的"？

能流畅回答 **8/10** 以上，说明已经掌握 FFmpeg Seek 的全景。

---

## 🎯 一句话总结

> Seek 不是单一 API 调用，而是"定位关键帧 → 刷新解码器 → 顺向追帧"的三部曲；帧精确的关键不在于 Seek 本身，而在于用帧索引（FLAG_FRAME 或 FrameIndex）绕开时间戳换算。

## 🔗 关联文档

- [[00-FFmpeg全景导读]] — FFmpeg 整体架构与 Seek 在其中的位置
- [[01-数据结构与生命周期]] — `AVIndexEntry`、`AVStream` 等核心数据结构的深入讲解
- [[05-H264-MP4-NALU]] — MP4 容器结构，stss Box 与关键帧索引的对应关系
- [[06-编码参数与码控]] — GOP 大小、I 帧间隔对 Seek 性能的影响
