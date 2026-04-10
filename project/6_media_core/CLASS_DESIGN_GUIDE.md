# Media Core 类设计详解（面向初学者）

本文按“整体链路 -> 每个类”的顺序解释 `project/6_media_core` 当前版本的设计初衷、解决的问题、底层逻辑和扩展方向。  
你可以把它当成这套编解码骨架的“架构说明书 + 学习笔记”。

---

## 1. 先建立整体认知：这套代码在做什么？

这套工程做的是一个最小视频转码闭环：

1. 从输入文件里读取压缩包（demux）
2. 解码成原始帧（decode）
3. 转像素格式/分辨率（convert）
4. 编码成新压缩包（encode）
5. 写入输出封装（mux）
6. 最后做 flush，保证尾帧不丢

代码把这条链路拆成“可替换组件”，核心目的是：

- **复用**：以后做 WebRTC，不重写核心编解码逻辑；
- **解耦**：业务层不直接散落 FFmpeg API；
- **可扩展**：后续能加硬解、音频、多线程、动态码控。

---

## 2. 架构分层：为什么要这么拆？

当前分层是：

- **接口层（interfaces）**：只定义“能力契约”，不关心具体实现。
- **实现层（src/ffmpeg_components.cpp）**：用 FFmpeg 实现这些接口。
- **编排层（pipeline）**：像“导演”一样按顺序调用各组件。
- **配置层（config）**：把输入、输出、编码参数集中管理。
- **工厂层（factory）**：集中创建对象，隔离“new 哪个实现”的细节。

这样做的价值：

- 换实现时，编排层不用改（比如从 FFmpegDecoder 换成硬解Decoder）。
- 对 WebRTC 来说，只需替换输入输出端，复用中间的 decode/convert/encode。

---

## 3. 公共基础类

### 3.1 `Status` / `StatusCode`

文件：`include/media_core/common/status.h`

#### 设计初衷

统一错误处理，不让调用方到处写 `if (ret < 0)` 并自己翻译错误。

#### 解决的问题

- C 风格 `int ret` 可读性差；
- 错误来源不统一（参数错误、内部错误、FFmpeg 错误混在一起）；
- 调试时不知道失败点和错误码。

#### 核心逻辑

- `StatusCode` 表示错误类别（参数、内部、找不到、FFmpeg）；
- `Status` 额外携带 `ffmpeg_error` 和可读 `message`；
- `ok()` 用于快速判断是否成功。

#### 对初学者的理解

它相当于“函数返回结果对象”，把“成功/失败 + 失败原因”一次性打包。

---

### 3.2 `VideoTranscodeConfig`

文件：`include/media_core/config/transcode_config.h`

#### 设计初衷

把所有可调参数放在一个结构体里，避免函数参数爆炸和硬编码。

#### 解决的问题

- 输入输出路径写死；
- 编码器名称、码率、分辨率分散在各处；
- 硬解策略不集中。

#### 核心字段含义

- `input_path` / `output_path`：输入输出文件；
- `output_format`：封装格式（如 mp4/flv）；
- `video_encoder_name`：编码器名称（如 libx264）；
- `target_width/height/fps/bitrate`：目标参数；
- `enable_hw_decode + preferred_hw_device`：硬解策略入口。

#### 扩展建议

后续可增加：`gop_size`、`max_b_frames`、`profile`、`preset`、`crf`。

---

## 4. 接口类（抽象层）

这些接口的本质是“能力合同（contract）”。

---

### 4.1 `IDemuxer`

文件：`include/media_core/interfaces/demuxer.h`

#### 设计初衷

把“从容器读取压缩包”的动作独立出来。

#### 主要职责

- 打开输入文件（`Open`）；
- 找到视频流（`VideoStreamIndex`）；
- 逐包读取（`ReadPacket`）；
- 提供流元信息（time base、codecpar、分辨率、帧率）。

#### 为什么 `VideoCodecParameters()` 很重要

解码器初始化常常需要完整 `codecpar`（extradata 等），不仅仅是 `codec_id`。

#### 底层逻辑

内部对应 FFmpeg 的：

- `avformat_open_input`
- `avformat_find_stream_info`
- `av_read_frame`

---

### 4.2 `IVideoDecoder`

文件：`include/media_core/interfaces/video_decoder.h`

#### 设计初衷

把“压缩包 -> 原始帧”的流程封装起来，并预留硬解入口。

#### 主要职责

- 初始化解码器（`Open`）；
- 送入压缩包（`SendPacket`）；
- 取出解码帧（`ReceiveFrame`）；
- 输入结束时 flush（`SendEof`）。

#### 为什么 `ReceiveFrame` 要有 `again/eof`

FFmpeg 的解码是异步队列模型：

- `EAGAIN`：当前拿不到帧，但不是错误；
- `EOF`：解码器已经完全排空。

用 `again/eof` 显式表达状态，能避免调用层误判。

---

### 4.3 `IFrameConverter`

文件：`include/media_core/interfaces/frame_converter.h`

#### 设计初衷

把“像素格式/尺寸转换”单独抽出来，不和编码器耦合。

#### 主要职责

- 初始化转换参数（输入上下文、输出宽高、输出像素格式）；
- 把 `in_frame` 转成 `out_frame`。

#### 底层逻辑

内部使用 `libswscale`：

- `sws_getCachedContext`
- `sws_scale`

#### 为什么要单独一个类

因为在 WebRTC 或推流场景，常常只改转换策略，不改编解码器本身。

---

### 4.4 `IVideoEncoder`

文件：`include/media_core/interfaces/video_encoder.h`

#### 设计初衷

把“原始帧 -> 压缩包”独立出来，方便更换编码器实现。

#### 主要职责

- 按配置初始化编码器（`Open`）；
- 送帧编码（`SendFrame`）；
- 取编码包（`ReceivePacket`）；
- 结束时 flush（`SendEof`）。

#### 关键概念

- `time_base`：编码时间基；
- `frame_rate`：目标帧率；
- `bitrate`：码率。

#### 对 WebRTC 的意义

将来可保留同样接口，底层换成“支持实时码控/强制IDR”的实现。

---

### 4.5 `IMuxer`

文件：`include/media_core/interfaces/muxer.h`

#### 设计初衷

把“写封装文件”独立，避免编码器直接碰容器逻辑。

#### 主要职责

- 打开输出目标（文件/容器）；
- 从编码器创建输出流（`AddVideoStreamFromEncoder`）；
- 写 header / packet / trailer。

#### 底层关键点

- 写包前要做时间戳重标定 `av_packet_rescale_ts`；
- 不写 trailer 可能导致文件尾索引不完整。

---

### 4.6 `ICapabilityProbe`

文件：`include/media_core/interfaces/capability_probe.h`

#### 设计初衷

把“硬件能力探测”从解码流程里剥离出来。

#### 作用

在真正解码前，先知道某个 codec 支持哪些硬件设备类型（如 videotoolbox）。

#### 价值

便于做“按平台优先级选择 + 失败回退软解”的策略引擎。

---

### 4.7 `CodecFactory`

文件：`include/media_core/factory/codec_factory.h`

#### 设计初衷

把对象创建集中管理，避免上层知道具体类名。

#### 作用

- `CreateDemuxer()` / `CreateVideoDecoder()` ...
- 上层只依赖接口，不依赖实现类。

#### 好处

后续切换实现（例如 `FfmpegVideoDecoder -> HardwareVideoDecoder`）时，改工厂即可。

---

### 4.8 `TranscodePipeline`

文件：`include/media_core/pipeline/transcode_pipeline.h`

#### 设计初衷

把“业务编排”集中在一个地方，形成可复用工作流。

#### 职责边界

- 负责流程顺序和状态推进；
- 不直接处理底层 FFmpeg 细节（细节在实现类里）。

---

## 5. 具体实现类（FFmpeg 适配层）

文件：`src/ffmpeg_components.cpp`

---

### 5.1 `FfmpegDemuxer`

#### 目的

读取输入容器并找到视频流。

#### 关键行为

- `Open`：打开文件 + 读取流信息 + 定位视频流；
- `ReadPacket`：逐包读取并支持 EOF 标志；
- 提供视频流的 `codecpar/time_base/fps` 等元信息。

#### 设计价值

把容器层和解码层隔离开，便于将来换输入源（例如 RTP 或自定义 source）。

---

### 5.2 `FfmpegVideoDecoder`

#### 目的

完成视频包解码。

#### 关键行为

- 根据 `codec_id` 查找 decoder；
- `codecpar -> AVCodecContext`；
- `SendPacket/ReceiveFrame` 队列式解码；
- `SendEof` 触发解码器排空。

#### 当前与未来

当前 `enable_hw_decode/preferred_hw_device` 先作为保留参数；后续可在这里接入硬解设备上下文。

---

### 5.3 `FfmpegFrameConverter`

#### 目的

把解码后的帧变成编码器想要的格式（比如 YUV420P）。

#### 关键行为

- 为输出帧分配 buffer；
- `sws_getCachedContext` 复用转换上下文；
- `sws_scale` 执行像素变换与缩放。

#### 常见坑

忘记 `av_frame_make_writable` 或 buffer 分配失败时继续写，容易崩溃。

---

### 5.4 `FfmpegVideoEncoder`

#### 目的

把原始帧编码成目标码流。

#### 关键行为

- 按编码器名打开 encoder（如 `libx264`）；
- 配置宽高、时基、帧率、像素格式、码率；
- 发送帧并提取压缩包；
- `SendEof` 取尾包。

#### 设计意义

把编码参数集中到一处，后续做“实时改码率/分辨率”有明确入口。

---

### 5.5 `FfmpegMuxer`

#### 目的

把编码包写成完整的输出容器文件。

#### 关键行为

- `Open` 打开输出上下文和文件句柄；
- `AddVideoStreamFromEncoder` 从 encoder 复制 codecpar；
- `WriteHeader/WritePacket/WriteTrailer` 完成封装生命周期；
- 析构时确保关闭 IO、释放 format context。

#### 关键底层点

`WritePacket` 内部做了 `av_packet_rescale_ts`，这是保证时间戳正确的核心步骤。

---

### 5.6 `FfmpegCapabilityProbe`

#### 目的

查询某个 decoder 支持哪些硬件设备类型。

#### 关键行为

使用 `avcodec_get_hw_config` 枚举支持项，再转成字符串列表输出。

#### 为什么单独放类

策略层（比如“优先 videotoolbox，再 fallback 软件解码”）可以独立演进，不污染解码主路径。

---

## 6. 编排层：`TranscodePipeline::Run` 的底层逻辑

文件：`src/transcode_pipeline.cpp`

可以把它理解成一台“状态机”：

1. 创建组件（demuxer/decoder/converter/encoder/muxer）
2. 打开输入，初始化解码器
3. 决定输出参数（宽高、fps、time base）
4. 初始化转换器、编码器、muxer
5. 写封装 header
6. 主循环：
  - 读 packet
  - 只处理视频 packet
  - 解码到 frame
  - 转换 frame
  - 计算/重映射 pts
  - 编码
  - 写 packet
7. flush decoder（取残留 frame）
8. flush encoder（取残留 packet）
9. 写 trailer

### 为什么要“两次 flush”

- flush decoder：解决“输入包喂完但解码器内部还有帧”
- flush encoder：解决“帧喂完但编码器内部还有包”

少任意一次，尾部数据都可能丢。

---

## 7. Demo 类：`transcode_demo.cpp`

#### 目的

提供最小可运行入口，验证整个 pipeline 设计是否可用。

#### 当前行为

- 从命令行读取输入输出路径；
- 构造 `VideoTranscodeConfig`；
- 调 `TranscodePipeline::Run`；
- 打印成功或失败信息。

#### 价值

它是“架构正确性”的冒烟测试，不是最终业务入口。

---

## 8. 为什么这套设计适合未来 WebRTC 复用？

核心原因是职责边界清晰：

- WebRTC 需要的是“实时输入 + 编码输出 + 时间戳一致性”；
- 你现在已经把“编解码核心”独立成可复用模块；
- 将来仅需替换：
  - 输入端：文件读包 -> 摄像头/网络帧
  - 输出端：文件封装 -> RTP 包输出

中间的 `Decoder/Converter/Encoder` 可以继续复用。

---

## 9. 给初学者的理解口诀

- **Demuxer**：拆箱（把容器里的包拿出来）
- **Decoder**：解压（包 -> 原始帧）
- **Converter**：转格式（让帧适配编码器）
- **Encoder**：再压缩（帧 -> 包）
- **Muxer**：装箱（把包写成目标文件）
- **Pipeline**：总导演（安排谁先干、何时 flush）

---

## 10. 当前版本的边界（你接下来该做什么）

当前骨架是 Phase 1 的“可编译、可理解、可扩展”版本。下一步建议：

1. 在 `FfmpegVideoDecoder` 中实现真正硬解设备上下文创建和回退；
2. 增加统一日志接口（分级日志）；
3. 增加运行期参数更新接口（码率、关键帧请求），为 WebRTC 做准备；
4. 补单元测试（至少覆盖：错误路径、flush 路径、时间戳映射）。

