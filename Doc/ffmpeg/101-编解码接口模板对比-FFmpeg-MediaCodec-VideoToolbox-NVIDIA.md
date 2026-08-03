# 101 - 编解码接口模板对比：FFmpeg / MediaCodec / VideoToolbox / NVIDIA

> **目标读者**：被面试官追问"你用过哪些编解码 API？分别调了哪些接口？生命周期怎么管？"的中高级音视频开发。
> **目标效果**：读完能**张嘴就把四套接口的画出来**——每条链路从创建到销毁，每一步调什么、传什么、坑在哪。
> **前置阅读**：[07-硬件编解码.md](./07-硬件编解码.md)（硬件帧通用底座）、[05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md)（Annex-B / AVCC）、[06-编码参数与码控.md](./06-编码参数与码控.md)（GOP / 码控 / 低延迟旋钮）。
> **姊妹篇**：各平台深层专题在 [13](./13-NVIDIA硬件编解码.md)、[14](./14-Android硬件编解码.md)、[15](./15-iOS硬件编解码.md)、[16](./16-硬件编解码高级专题.md)。

---

## 〇、面试速答：四套接口一句话总结

> 先上一张"张嘴就能说"的总表。面试官问"你用过哪些编解码 API"，先把这四条背出来。

| 框架 | 一句话 | 驱动模型 | 核心对象 |
|---|---|---|---|
| **FFmpeg (libavcodec)** | 软件编解码的事实标准，一切硬件加速也走它封装 | 同步（send/receive 模式） | `AVCodecContext` |
| **NVIDIA Video Codec SDK** | 桌面/服务端硬编硬解首选，直接驱 NVENC/NVDEC | 异步（注册回调 → 排队 → 回调吐帧） | `NV_ENC_INPUT_PTR` / `CUvideodecoder` |
| **Android MediaCodec** | Android 统一门面，同一套 API 屏蔽硬解/软解 | 异步（`setCallback`）+ 同步（`dequeue`）双模 | `MediaCodec` |
| **Apple VideoToolbox** | Apple 平台中层 API，直驱 Media Engine | 异步（创建时注册回调 → 回调吐帧） | `VTCompressionSession` / `VTDecompressionSession` |

再给一句万能开场白，面试官听完就知道你懂行：

> "编解码框架说到底就是**一个状态机 + 两条缓冲区队列**。创建会话配参数 → 喂输入 → 收输出 → 销毁。四套 API 只是把这套模式翻译成了各自平台的方言——FFmpeg 是把编解码器抽象成 `AVCodecContext`、用 send/receive 包了一层；NVIDIA 是 `CreateEncoder` 拿句柄、`EncodePicture` 推帧、回调收包；Android 是 `MediaCodec` 配好之后 `queueInputBuffer` / `dequeueOutputBuffer`；Apple 是 `VTCompressionSessionEncodeFrame` 推帧、回调收 `CMSampleBuffer`。核心生命周期完全一一对应。"

---

## 一、先建立心智模型：所有编解码器的共同状态机

在看具体代码前，先把底层共通的状态机讲清楚。**你把这个图画给面试官就已经赢了一半。**

```
                        ┌─────────────────────────┐
                        │      IDLE (未初始化)       │
                        └───────────┬─────────────┘
                                    │ create / open / init
                                    ▼
                        ┌─────────────────────────┐
                        │   CONFIGURED (已配置)     │
                        │   设置分辨率/码率/GOP/像素格式 │
                        └───────────┬─────────────┘
                                    │ start / begin
                                    ▼
              ┌─────────────────────────────────────────┐
              │            RUNNING (运行中)               │
              │                                         │
              │   输入队列 ◄── 喂原始帧                   │
              │      │                                  │
              │      ▼                                  │
              │   [ 编解码引擎 ]                          │
              │      │                                  │
              │      ▼                                  │
              │   输出队列 ──► 取编码包 / 解码帧           │
              │                                         │
              │   异步变体：回调代替轮询                   │
              └───────────────┬─────────────────────────┘
                              │ drain / flush / stop
                              ▼
                        ┌─────────────────────────┐
                        │     DRAINING (排空中)     │
                        │  逼出流水线里剩余的输出     │
                        └───────────┬─────────────┘
                                    │ EOS / 输出耗尽
                                    ▼
                        ┌─────────────────────────┐
                        │      END (已停止)         │
                        └───────────┬─────────────┘
                                    │ destroy / release / free
                                    ▼
                        ┌─────────────────────────┐
                        │    RELEASED (已释放)      │
                        └─────────────────────────┘
```

**面试核心话术**：

> "所有编解码框架都遵循同一个状态机：IDLE → CONFIGURED → RUNNING → DRAINING → END → RELEASED。关键差异在两点：一是 RUNNING 阶段是同步拉取还是异步回调；二是 flush/drain 阶段怎么做——FFmpeg 发 null packet、MediaCodec 发 BUFFER_FLAG_END_OF_STREAM、VideoToolbox 调 CompleteFrames、NVIDIA 调 EndSequence。但无论哪家，**必须在销毁前 drain**，否则流水线里最后几帧会丢。"

---

## 二、FFmpeg (libavcodec)：软件编解码 + 硬件加速封装

### 2.1 核心对象与生命周期

```
   avcodec_find_encoder / avcodec_find_decoder    ← 找编解码器
   avcodec_alloc_context3                          ← 分配 context
   av_opt_set / avcodec_open2                      ← 配置 + 打开
                    │
   [编码] avcodec_send_frame   ──► [内部流水线] ──► avcodec_receive_packet
   [解码] avcodec_send_packet  ──► [内部流水线] ──► avcodec_receive_frame
                    │
   avcodec_send_frame(ctx, NULL)                   ← flush (逼出剩余包)
                    │
   avcodec_free_context                            ← 释放
```

### 2.2 解码器完整调用模板

```c
// ==================== 第一步：查找 + 分配 ====================
const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
// 或者硬件解码器：avcodec_find_decoder_by_name("h264_cuvid")

AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);

// ==================== 第二步：配置 ====================
// 从 demuxer 拿到 extradata（SPS/PPS/CSD）
codec_ctx->extradata = extradata_buf;
codec_ctx->extradata_size = extradata_size;

// 【硬件解码额外步骤】创建硬件设备上下文
// AVBufferRef *hw_device_ctx;
// av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, ...);
// codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

avcodec_open2(codec_ctx, codec, NULL);  // ← 这里才真正初始化

// ==================== 第三步：解码循环（send/receive 模式）====================
AVPacket *pkt = av_packet_alloc();
AVFrame *frame = av_frame_alloc();

while (/* 有数据 */) {
    // 3a. 从 demuxer 读一个 packet
    av_read_frame(fmt_ctx, pkt);

    // 3b. send：送入编码数据
    int ret = avcodec_send_packet(codec_ctx, pkt);
    // ret == 0              → 成功送入
    // ret == AVERROR(EAGAIN) → 内部缓冲区满，先 receive 再 send
    // ret == AVERROR_EOF     → 已 flush 过

    // 3c. receive：取出解码帧（一个 packet 可能产出多个 frame）
    while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        // ret == 0           → 拿到一帧
        // ret == AVERROR(EAGAIN) → 还需要更多输入
        // ret == AVERROR_EOF → 解码器已排空

        if (ret == 0) {
            // frame->data[0] / frame->data[1] / frame->data[2]
            // frame->width / frame->height / frame->format
            // 【硬件帧】hw_frames_ctx 不为空时，data[] 是设备指针不是内存地址
            process_frame(frame);
            av_frame_unref(frame);
        }
    }
    av_packet_unref(pkt);
}

// ==================== 第四步：Flush（逼出解码器内部缓冲）====================
avcodec_send_packet(codec_ctx, NULL);  // ← 传 NULL = flush 信号
while (1) {
    ret = avcodec_receive_frame(codec_ctx, frame);
    if (ret == AVERROR_EOF) break;
    process_frame(frame);
    av_frame_unref(frame);
}

// ==================== 第五步：释放 ====================
av_frame_free(&frame);
av_packet_free(&pkt);
avcodec_free_context(&codec_ctx);
```

### 2.3 编码器完整调用模板

```c
// ==================== 第一步：查找 + 分配 ====================
const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
// 或者硬件编码器：avcodec_find_encoder_by_name("h264_nvenc")

AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);

// ==================== 第二步：配置编码参数 ====================
codec_ctx->width     = 1920;
codec_ctx->height    = 1080;
codec_ctx->time_base = (AVRational){1, 30};  // 30fps
codec_ctx->framerate = (AVRational){30, 1};
codec_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;   // 或 AV_PIX_FMT_CUDA
codec_ctx->bit_rate  = 4000000;               // 4 Mbps
codec_ctx->gop_size  = 60;                    // GOP = 2 秒
codec_ctx->max_b_frames = 0;                  // 低延迟关 B 帧

// 更多参数走 av_opt_set（profile、preset、码控模式等）
av_opt_set(codec_ctx->priv_data, "preset", "p4", 0);    // NVENC: p1-p7
av_opt_set(codec_ctx->priv_data, "profile", "high", 0);
av_opt_set(codec_ctx->priv_data, "rc", "vbr", 0);       // CBR / VBR

avcodec_open2(codec_ctx, codec, NULL);

// ==================== 第三步：编码循环 ====================
AVFrame *frame = av_frame_alloc();
frame->width  = 1920;
frame->height = 1080;
frame->format = codec_ctx->pix_fmt;
av_frame_get_buffer(frame, 0);

AVPacket *pkt = av_packet_alloc();

for (int i = 0; i < total_frames; i++) {
    frame->pts = i;

    // 填充 frame->data[]（从采集源拿 YUV 数据）
    fill_frame_data(frame, src_buffer);

    // send
    ret = avcodec_send_frame(codec_ctx, frame);
    // ret == AVERROR(EAGAIN) → 内部缓冲满，先 receive

    // receive
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        // ret == AVERROR(EAGAIN) → 还没产出，继续 send
        if (ret == 0) {
            // pkt->data / pkt->size → 编码后的 H.264/HEVC 数据
            // pkt->pts / pkt->dts → 时间戳
            // pkt->flags & AV_PKT_FLAG_KEY → 是否关键帧
            write_packet(pkt);
            av_packet_unref(pkt);
        }
    }
}

// ==================== 第四步：Flush ====================
avcodec_send_frame(codec_ctx, NULL);  // NULL frame = drain
while (1) {
    ret = avcodec_receive_packet(codec_ctx, pkt);
    if (ret == AVERROR_EOF) break;
    write_packet(pkt);
    av_packet_unref(pkt);
}

// ==================== 第五步：释放 ====================
av_frame_free(&frame);
av_packet_free(&pkt);
avcodec_free_context(&codec_ctx);
```

### 2.4 FFmpeg 面试核心话术

> **Q：avcodec_send_packet / avcodec_receive_frame 的返回值怎么处理？**
>
> 这是 FFmpeg 3.1 引入的 send/receive 模型，核心是**解耦输入和输出**——不再假设一个 packet 换一个 frame。send 可能有三种返回：0（接受了）、EAGAIN（内部缓冲满，先 receive 清出空间再 send）、EOF（已经 flush 过不能再 send）。receive 也是三种：0（拿到帧）、EAGAIN（还需要更多输入）、EOF（解码器空了）。**写循环的正确姿势是 send 一次、receive 一个 while 直到 EAGAIN**——因为 H.264 一个 packet 可能解出多个 frame（有 B 帧重排序时），也可能多个 packet 才拼出一个 frame。

> **Q：硬件编解码的 send/receive 有什么不同？**
>
> send/receive 这层封装**对上层完全一致**——区别仅仅发生在 open 阶段：软编 `avcodec_find_decoder` 不加后缀，硬编加（`h264_cuvid`、`h264_nvenc`、`h264_videotoolbox`）；然后需要创建 `hw_device_ctx` 并 attach 到 `codec_ctx->hw_device_ctx`。send/receive 循环里的帧是 AV_PIX_FMT_CUDA / AV_PIX_FMT_VAAPI 这类硬件格式，data[] 指针是设备指针，**不能直接 memcpy**——要用 `av_hwframe_transfer_data` 拷回系统内存，或者直接交给下一个硬件滤镜/编码器做零拷贝。

> **Q：不调 avcodec_send_packet(ctx, NULL) 会发生什么？**
>
> 最后几帧丢数据。编解码器内部有缓冲（lookahead、B 帧重排序），你不 flush 它不会主动吐出来。send NULL 就是告诉它"没有更多输入了，把你肚子里剩下的都排出来"。

---

## 三、NVIDIA Video Codec SDK：NVENC 编码 / NVDEC 解码

> 这是**直接驱动 NVENC/NVDEC 硬件的底层 SDK**，不是 FFmpeg 的 wrapper。面试考这个说明你在面云转码/云游戏/DeepStream 岗。

### 3.1 NVENC 编码器完整调用模板

```
   NvEncodeAPICreateInstance                    ← 拿 NVENC 接口
   NvEncOpenEncodeSessionEx                     ← 选设备、开编码会话
   NvEncInitializeEncoder                       ← 创建编码器实例、配 preset/GUID
   NvEncCreateInputBuffer / NvEncCreateBitstreamBuffer  ← 分配输入/输出 buffer
                     │
   ┌─ 编码循环 ─────────────────────────────────────
   │  NvEncLockInputBuffer    ← 锁输入 buffer、填 YUV
   │  NvEncEncodePicture      ← 推入编码流水线
   │  NvEncLockBitstream      ← 锁输出 buffer、取编码包
   │  NvEncUnlockBitstream    ← 解锁、发走
   │  NvEncUnlockInputBuffer  ← 解锁输入 buffer、复用
   └────────────────────────────────────────────────
                     │
   NvEncSendEOS / NvEncEndSequence                ← 结束序列
   NvEncDestroyBitstreamBuffer / NvEncDestroyInputBuffer
   NvEncDestroyEncoder                            ← 销毁
```

**详细步骤拆解**：

```c
// ==================== 第一步：创建 API 实例 ====================
NV_ENCODE_API_FUNCTION_LIST nvEnc = {0};
nvEnc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
NvEncodeAPICreateInstance(&nvEnc);
// 拿到函数指针表：nvEnc.nvEncOpenEncodeSession, nvEnc.nvEncEncodePicture ...

// ==================== 第二步：打开编码会话 ====================
NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = {0};
session_params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;  // 用 CUDA 上下文
session_params.device     = cuda_context;
session_params.apiVersion = NVENCAPI_VERSION;

void *encoder = NULL;
nvEnc.nvEncOpenEncodeSessionEx(&session_params, &encoder);

// ==================== 第三步：初始化编码器参数 ====================
NV_ENC_INITIALIZE_PARAMS init_params = {0};
init_params.version              = NV_ENC_INITIALIZE_PARAMS_VER;
init_params.encodeGUID           = NV_ENC_CODEC_H264_GUID;  // 或 HEVC / AV1
init_params.presetGUID           = NV_ENC_PRESET_P4_GUID;   // P1~P7
init_params.encodeWidth          = 1920;
init_params.encodeHeight         = 1080;
init_params.darWidth             = 1920;
init_params.darHeight            = 1080;
init_params.frameRateNum         = 30;
init_params.frameRateDen         = 1;
init_params.enableEncodeAsync    = 1;   // 异步模式
init_params.enablePTD            = 1;   // Picture Type Decision
init_params.maxEncodeWidth       = 1920;
init_params.maxEncodeHeight      = 1080;

// 码控配置
NV_ENC_CONFIG config = {0};
config.version = NV_ENC_CONFIG_VER;
nvEnc.nvEncGetEncoderCaps(encoder, init_params.encodeGUID, &caps, ...);
// 填充 config.rcParams:
//   config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR / VBR / CBR_LOWDELAY_HQ / CONSTQP
//   config.rcParams.averageBitRate  = 4000000;
//   config.rcParams.maxBitRate      = 4000000;
//   config.rcParams.vbvBufferSize   = ...;
//   config.encodeCodecConfig.h264Config.repeatSPSPPS = 1;  // 每个 IDR 前带 SPS/PPS
//   config.encodeCodecConfig.h264Config.idrPeriod     = 60; // GOP

init_params.encodeConfig = &config;
nvEnc.nvEncInitializeEncoder(encoder, &init_params);

// ==================== 第四步：分配 Buffer ====================
// 4a. 输入 Buffer（放原始帧）
NV_ENC_CREATE_INPUT_BUFFER ib_create = {0};
// ... 分配多块做轮转 ...
nvEnc.nvEncCreateInputBuffer(encoder, &ib_create);

// 4b. 输出 Bitstream Buffer（放编码包）
NV_ENC_CREATE_BITSTREAM_BUFFER ob_create = {0};
nvEnc.nvEncCreateBitstreamBuffer(encoder, &ob_create);

// ==================== 第五步：编码循环 ====================
for (int i = 0; i < total_frames; i++) {
    NV_ENC_PIC_PARAMS pic = {0};
    pic.version       = NV_ENC_PIC_PARAMS_VER;
    pic.inputWidth    = 1920;
    pic.inputHeight   = 1080;
    pic.inputPitch    = pitch;
    pic.inputBuffer   = input_bufs[cur_buf].handle;
    pic.inputTimeStamp = i * (1000 / 30);  // 毫秒
    pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pic.bufferFmt     = NV_ENC_BUFFER_FORMAT_NV12;  // NVENC 吃 NV12

    // 强制关键帧
    if (need_keyframe) pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;

    // 5a. Lock 输入 buffer，填 YUV 数据
    NV_ENC_LOCK_INPUT_BUFFER lock_ib = {0};
    lock_ib.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lock_ib.inputBuffer = input_bufs[cur_buf].handle;
    nvEnc.nvEncLockInputBuffer(encoder, &lock_ib);
    // lock_ib.bufferDataPtr → 填 NV12 数据
    nvEnc.nvEncUnlockInputBuffer(encoder, input_bufs[cur_buf].handle);

    // 5b. 送入编码
    nvEnc.nvEncEncodePicture(encoder, &pic);

    // 5c. 取输出（异步模式要等 completion event）
    NV_ENC_LOCK_BITSTREAM lock_ob = {0};
    lock_ob.version          = NV_ENC_LOCK_BITSTREAM_VER;
    lock_ob.outputBitstream  = output_bufs[cur_out].handle;
    lock_ob.doNotWait        = 0;
    nvEnc.nvEncLockBitstream(encoder, &lock_ob);

    // lock_ob.bitstreamBufferPtr → 编码后的 H.264 数据
    // lock_ob.bitstreamSizeInBytes
    // lock_ob.pictureType → NV_ENC_PIC_TYPE_IDR / I / P / B
    // lock_ob.frameAvgQP → 质量参考
    send_to_network(lock_ob.bitstreamBufferPtr, lock_ob.bitstreamSizeInBytes);

    nvEnc.nvEncUnlockBitstream(encoder, output_bufs[cur_out].handle);
}

// ==================== 第六步：End + 清场 ====================
NV_ENC_PIC_PARAMS eos = {0};
eos.version         = NV_ENC_PIC_PARAMS_VER;
eos.encodePicFlags  = NV_ENC_PIC_FLAG_EOS;  // End Of Stream
nvEnc.nvEncEncodePicture(encoder, &eos);     // 逼出最后一组包

nvEnc.nvEncDestroyEncoder(encoder);
```

### 3.2 NVDEC 解码器完整调用模板

```c
// ==================== 第一步：创建 CUVID 解码器 ====================
// 方式一：CUDA Driver API（cuvidCreateDecoder）
CUVIDDECODECREATEINFO dec_create = {0};
dec_create.ulWidth          = 1920;
dec_create.ulHeight         = 1080;
dec_create.ulNumDecodeSurfaces = 8;          // 解码表面池大小
dec_create.CodecType        = cudaVideoCodec_H264;
dec_create.ChromaFormat     = cudaVideoChromaFormat_420;
dec_create.ulTargetWidth    = 1920;
dec_create.ulTargetHeight   = 1080;
dec_create.OutputFormat     = cudaVideoSurfaceFormat_NV12;  // 或直接输出到 CUDA array
dec_create.DeinterlaceMode  = cudaVideoDeinterlaceMode_Weave;
dec_create.ulNumOutputSurfaces = 4;          // 输出表面数

CUvideodecoder decoder;
cuvidCreateDecoder(&decoder, &dec_create);

// ==================== 第二步：解码循环 ====================
CUVIDSOURCEDATAPACKET pkt = {0};
for (/* 每个 NALU / packet */) {
    pkt.payload      = nal_data;        // H.264 裸 NALU 或 Annex-B
    pkt.payload_size = nal_size;
    pkt.flags        = CUVID_PKT_TIMESTAMP;
    pkt.timestamp    = pts;

    if (is_keyframe && first_pkt_of_keyframe) {
        pkt.flags |= CUVID_PKT_ENDOFSTREAM;  // 或 CUVID_PKT_NOTIFY_EOS
    }

    // cuvidParseVideoData 做码流解析（去起始码、分类 slice type）
    cuvidParseVideoData(parser, &pkt);

    // 取解码后的帧
    CUVIDPARSERDISPINFO disp_info;
    cuvidMapVideoFrame(decoder, disp_info.picture_index, &mapped_ptr, &pitch, &params);
    // params 里：pitch、NV12 各平面偏移
    // 直接用 CUDA kernel 处理、或者 cuMemcpyDtoH 拷回 CPU
    process_decoded_frame(mapped_ptr, pitch, params);
    cuvidUnmapVideoFrame(decoder, mapped_ptr);
}

// ==================== 第三步：销毁 ====================
cuvidDestroyDecoder(decoder);
cuvidDestroyVideoParser(parser);
```

### 3.3 NVIDIA 面试核心话术

> **Q：NVENC 的异步编码机制是怎样的？为什么要异步？**
>
> NVENC 硬件有自己的 PCIe 命令队列。同步模式下，`nvEncEncodePicture` 调用阻塞直到硬件编码完成。异步模式下（`enableEncodeAsync=1`），调用立刻返回，硬件在后台编——你在每帧编码前注册一个 CUDA event，之后可以一边等 event 一边做别的事（比如并行处理下一帧）。做实时转码/云游戏就是靠这个把 PCIe 往返延迟盖掉：第 N 帧在硬件里编着，CPU 上已经在准备第 N+1 帧了。

> **Q：NVENC 的 preset（P1~P7）和内部分辨率（lookahead）的关系？**
>
> P1 是纯帧内编码速度最快但压缩率最差，P7 是最大 lookahead + 最强搜索质量最好。P5 及以上会开 lookahead——就是编码器提前看 32/40 帧的画面来决定场景切换点、自适应 GOP 结构、AQ（自适应量化）参数。代价是延迟：lookahead depth 帧必须缓冲。所以低延迟场景（RTC/云游戏）用 P1~P4 + LowLatency tuning，离线转码才上 P6/P7。这个经常被追问，因为它体现在接口参数上就是 `NV_ENC_PRESET_P1_GUID`~`NV_ENC_PRESET_P7_GUID` 之间画质的阶梯差。

> **Q：NVDEC 解码输出在 GPU 显存，怎么零拷贝传给编码/渲染？**
>
> NVDEC 解码输出在 `CUdeviceptr`（显存指针）。三个去向全是零拷贝：① 直接送给 NVENC 编码（不经过 CPU，全 GPU 链路）；② 在 CUDA kernel 里处理（scale/格式转换/画质增强），结果留在显存；③ 通过 CUDA-OpenGL/D3D interop 直接当纹理渲染，一步都不回 CPU。这整条流水线是**解码→CUDA 处理→编码→网络发送**全在 GPU 侧的底气。

---

## 四、Android MediaCodec：移动端硬编硬解

> MediaCodec 是 Android 的统一门面。**核心要讲清三件事**：状态机、缓冲区模型、同步 vs 异步。

### 4.1 状态机（最重要，面试必问）

```
  createByCodecName / createDecoderByType / createEncoderByType
       │
       ▼
  ┌──────────┐   configure(format, surface, crypto, flags)    ┌──────────────┐
  │ Stopped  │────────────────────────────────────────────────►│ Configured   │
  └──────────┘                                                └──────┬───────┘
       ▲                                                    start() │
       │   reset()          ┌───────────────────────────────────────┘
       │◄───────────────────┘          │
       │                               ▼
       │                    ┌──────────────────┐
       │                    │    Running       │ ← flush() ──► 可短暂回到它自己
       │                    │ (编解码循环!)     │
       │                    └────────┬─────────┘
       │                             │
       │        queueInputBuffer(     │  stop()
       │          BUFFER_FLAG_        │
       │          END_OF_STREAM)      ▼
       │                    ┌──────────────────┐
       │                    │  End-of-Stream   │
       │                    │  (仍在 Running)  │
       │                    └────────┬─────────┘
       │                             │
       │                             │ stop() / reset()
       │                             ▼
       └─────────────────────────────────────────────────
                         release()
                              │
                              ▼
                        ┌──────────┐
                        │ Released │
                        └──────────┘
```

**面试一句话**：

> "MediaCodec 的状态是单向流转的：Stopped → Configured → Running → End-of-Stream → Stopped。**只能在 Configured 状态改参数、只能在 Running 状态编解码、drain 靠发 BUFFER_FLAG_END_OF_STREAM 标记**。面试官最喜欢追问的是：如果你在 Running 状态直接 release 会发生什么——官方说行为未定义，实际上多数设备会崩。正确顺序永远是先 stop、再 release。"

### 4.2 同步模式（dequeue 轮询）

```java
// ==================== 第一步：创建 ====================
MediaCodec codec = MediaCodec.createDecoderByType("video/avc");
// 编码：MediaCodec.createEncoderByType("video/avc");
// 或者指定 codec 名字：MediaCodec.createByCodecName("OMX.qcom.video.encoder.avc");

// ==================== 第二步：配置 ====================
MediaFormat format = MediaFormat.createVideoFormat("video/avc", 1920, 1080);

// 解码额外参数：CSD-0 (SPS) / CSD-1 (PPS)
format.setByteBuffer("csd-0", spsBuffer);
format.setByteBuffer("csd-1", ppsBuffer);

// 编码额外参数
format.setInteger(MediaFormat.KEY_BIT_RATE,          4000000);
format.setInteger(MediaFormat.KEY_FRAME_RATE,        30);
format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL,  2);   // I 帧间隔（秒）
format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar);  // NV12
// COLOR_FormatSurface 走零拷贝：format.setInteger(KEY_COLOR_FORMAT, COLOR_FormatSurface);

codec.configure(format, null, null, 0);  // surface, crypto, flags
codec.start();

// ==================== 第三步：解码循环（同步模式）====================
MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

while (/* 有输入数据 */) {
    // 3a. 从编解码器要一个空输入 buffer 的 index
    int inIdx = codec.dequeueInputBuffer(TIMEOUT_US);
    if (inIdx >= 0) {
        ByteBuffer inBuf = codec.getInputBuffer(inIdx);  // API 21+
        // 或 codec.getInputBuffers()[inIdx]（API 21 以前）
        inBuf.clear();
        inBuf.put(inputData, 0, inputSize);              // 填数据
        codec.queueInputBuffer(inIdx, 0, inputSize, pts, flags);
        // flags: 0（普通帧）或 BUFFER_FLAG_END_OF_STREAM（最后一帧）
    }

    // 3b. 从编解码器要输出
    int outIdx = codec.dequeueOutputBuffer(info, TIMEOUT_US);
    if (outIdx >= 0) {
        ByteBuffer outBuf = codec.getOutputBuffer(outIdx);
        // outBuf → 解码帧（YUV）或编码包（H.264 bitstream）
        // info.size / info.presentationTimeUs / info.flags

        process_output(outBuf, info);

        codec.releaseOutputBuffer(outIdx, true);  // render=true（Surface 模式才有效）
        // ↑ 必须 release！Buffer 总数固定，不 release 池子耗尽会堵死

        if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
            break;  // 解码结束
        }
    }
}

// ==================== 第四步：停止 + 释放 ====================
codec.stop();
codec.release();
```

### 4.3 异步模式（setCallback，推荐生产使用）

```java
// ==================== 创建 + 配置（同同步模式）====================
MediaCodec codec = MediaCodec.createDecoderByType("video/avc");

// ⚠️ setCallback 必须在 configure 之前调用
codec.setCallback(new MediaCodec.Callback() {
    @Override
    public void onInputBufferAvailable(MediaCodec codec, int index) {
        // 系统通知：有一个空输入 buffer 可用了
        ByteBuffer inBuf = codec.getInputBuffer(index);
        if (needFeedData()) {
            inBuf.put(data, offset, size);
            codec.queueInputBuffer(index, 0, size, pts, 0);
        } else {
            // EOS：最后一帧
            codec.queueInputBuffer(index, 0, 0, 0,
                MediaCodec.BUFFER_FLAG_END_OF_STREAM);
        }
    }

    @Override
    public void onOutputBufferAvailable(MediaCodec codec, int index, BufferInfo info) {
        // 系统通知：有一个输出 buffer 可用了
        ByteBuffer outBuf = codec.getOutputBuffer(index);
        process_output(outBuf, info);
        codec.releaseOutputBuffer(index, true);

        if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
            // 处理 EOS，停止 codec
        }
    }

    @Override
    public void onError(MediaCodec codec, MediaCodec.CodecException e) {
        // 编解码错误
    }

    @Override
    public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
        // 解码器：CSD 信息更新（分辨率和 color format 可能在此时才确定）
        // 编码器：收到 "output format changed" 后才拿到 csd-0/csd-1
    }
});

codec.configure(format, null, null, 0);
codec.start();  // ← start 后回调才开始火
```

### 4.4 Surface 零拷贝模式

```java
// ==================== 解码 → Surface（零拷贝渲染）====================
// 不需要 ByteBuffer 操作，输出直接进 Surface
Surface outputSurface = surfaceView.getHolder().getSurface();
// 或 TextureView 的 SurfaceTexture → Surface

codec.configure(format, outputSurface, null, 0);  // 第二个参数 = Surface
codec.start();

// 解码循环里不需要 dequeueOutputBuffer！SDK 自动把解码帧推到 Surface
// 只需要喂输入
while (/* ... */) {
    int inIdx = codec.dequeueInputBuffer(TIMEOUT_US);
    ByteBuffer inBuf = codec.getInputBuffer(inIdx);
    inBuf.put(data, 0, size);
    codec.queueInputBuffer(inIdx, 0, size, pts, 0);
}

// ==================== 编码 ← Surface（零拷贝输入）====================
codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
Surface inputSurface = codec.createInputSurface();  // 来自 OpenGL/Metal/Camera
codec.start();

// 通过 OpenGL 或 Camera2 往 inputSurface 画帧
// GLES: eglCreateWindowSurface → inputSurface
// Camera2: captureRequest.addTarget(inputSurface)

// 异步回调收编码结果
codec.setCallback(new MediaCodec.Callback() {
    @Override
    public void onOutputBufferAvailable(MediaCodec codec, int idx, BufferInfo info) {
        ByteBuffer outBuf = codec.getOutputBuffer(idx);
        send_to_network(outBuf, info.size);  // 编码后的码流直接发
        codec.releaseOutputBuffer(idx, false);
    }
});
```

### 4.5 NDK AMediaCodec（C 层接口，面试加分项）

```c
// 创建
AMediaCodec *codec = AMediaCodec_createDecoderByType("video/avc");

// 配置
AMediaFormat *format = AMediaFormat_new();
AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH,  1920);
AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 1080);

AMediaCodec_configure(codec, format, NULL, NULL, 0);
AMediaCodec_start(codec);

// 同步解码循环
ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, TIMEOUT_US);
size_t  inSize;
uint8_t *inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &inSize);
memcpy(inBuf, data, data_size);
AMediaCodec_queueInputBuffer(codec, inIdx, 0, data_size, pts, 0);

AMediaCodecBufferInfo info;
ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, TIMEOUT_US);
size_t  outSize;
uint8_t *outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
// 处理 outBuf...
AMediaCodec_releaseOutputBuffer(codec, outIdx, false);

// 释放
AMediaCodec_stop(codec);
AMediaCodec_delete(codec);
AMediaFormat_delete(format);
```

### 4.6 MediaCodec 面试核心话术

> **Q：MediaCodec 同步和异步的区别？什么时候用哪个？**
>
> 同步模式你主动 dequeue 带 timeout 轮询，好理解、快速上手，但容易写出忙等或错过事件。异步模式 `setCallback` 注册回调，系统在 buffer 可用时通知你，没有忙等、线程模型干净。**生产环境一律异步**。关键的坑：`setCallback` 必须在 `configure` 之前调，configure 之后才 set 没用。

> **Q：CSD (Codec-Specific Data) 是什么？解码器为什么需要它？**
>
> CSD 是解码器初始化时必须知道的"元数据"——H.264 就是 SPS 和 PPS（`csd-0` = SPS, `csd-1` = PPS），没有它解码器不知道怎么解。在 MediaCodec 里通过 `MediaFormat` 的 `csd-0` / `csd-1` 键传入。面试官会追问：**推流场景下 csd 从哪来**——从 RTMP/FLV 的 `avcC` (AVCDecoderConfigurationRecord) 里解析出来，或者从 MP4 的 `avcC` box 里拿。MediaCodec 接受 Annex-B 格式的 SPS/PPS 裸 NALU（不带起始码、直接就是 byte 数组）。

> **Q：ByteBuffer 模式和 Surface 模式的核心区别？**
>
> ByteBuffer 模式：数据走 CPU 路径，你能直接读/写 buffer 里的字节。优点是灵活——能做帧级处理、分析、存文件；缺点是要拷数据，4K 60fps 下 CPU 和内存带宽吃不消。Surface 模式：数据走 GPU 零拷贝路径，硬件直接写 Surface，完全不经过应用层 CPU。**解码输出 Surface 直接上屏**、**编码输入 Surface 来自 Camera/GL，不用拷**。代价是你拿不到裸字节——如果要做画质分析、截图就还得掉回 ByteBuffer。

---

## 五、Apple VideoToolbox

> VideoToolbox 是 Apple 的中层编解码 API。它的设计哲学是 CoreFoundation 风格 + 异步回调，和 MediaCodec 一脉相承但细节不同。

### 5.1 编码器完整调用模板

```objc
// ==================== 第一步：创建压缩会话 ====================
VTCompressionSessionRef session = NULL;

// 1a. 指定编码器
CFDictionaryRef encoderSpec = NULL;
// 强制硬件编码：
// const void *keys[]   = { kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder };
// const void *values[] = { kCFBooleanTrue };
// encoderSpec = CFDictionaryCreate(NULL, keys, values, 1, NULL, NULL);

// 1b. 创建会话
OSStatus status = VTCompressionSessionCreate(
    NULL,                              // allocator
    1920, 1080,                        // width, height
    kCMVideoCodecType_H264,            // 编码类型 (H.264 / HEVC / ProRes)
    encoderSpec,                       // 编码器属性（指定硬件/软件）
    NULL,                              // 源像素 buffer 属性（nullable）
    NULL,                              // allocator for compressed data
    &compressionOutputCallback,        // ★ 输出回调
    (__bridge void *)self,             // 回调 context（传 self 进去）
    &session
);

// ==================== 第二步：配置编码属性 ====================
// 码率
int bitrate = 4000000;
VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate,
                     (__bridge CFNumberRef)@(bitrate));
// 码控上限（CBR 时可配等同 AverageBitRate）
VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits,
                     (__bridge CFArrayRef)@[@(bitrate * 1.5 / 8), @(1)]);

// 帧率
VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate,
                     (__bridge CFNumberRef)@(30));

// GOP：关键帧间隔
VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval,
                     (__bridge CFNumberRef)@(60));  // 2 秒 @ 30fps

// 低延迟开关（实时通信必设）
VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime,
                     kCFBooleanTrue);
VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering,
                     kCFBooleanFalse);  // 关 B 帧

// Profile & Level
VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel,
                     kVTProfileLevel_H264_High_AutoLevel);

// 像素格式
VTSessionSetProperty(session, kVTCompressionPropertyKey_PixelFormat,
                     (__bridge CFNumberRef)@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange));
                     // NV12 (BiPlanar) 或 kCVPixelFormatType_420YpCbCr8Planar (I420)

// ==================== 第三步：开始编码 ====================
VTCompressionSessionPrepareToEncodeFrames(session);

// 编码循环
for (int i = 0; i < total_frames; i++) {
    CVPixelBufferRef pixelBuffer = /* 从采集源拿帧 */;

    CMTime pts = CMTimeMake(i, 30);  // 第 i 帧，30fps
    CMTime dur = CMTimeMake(1, 30);  // 每帧时长

    // 强制关键帧的属性字典
    CFDictionaryRef frameProps = NULL;
    if (need_keyframe) {
        const void *keys[]   = { kVTEncodeFrameOptionKey_ForceKeyFrame };
        const void *values[] = { kCFBooleanTrue };
        frameProps = CFDictionaryCreate(NULL, keys, values, 1, NULL, NULL);
    }

    // ★ 送入编码（异步：返回不代表编完）
    status = VTCompressionSessionEncodeFrame(
        session,
        pixelBuffer,
        pts,
        dur,
        frameProps,  // 帧级属性（强制关键帧等），没有传 NULL
        NULL,         // sourceFrameRefCon（可在回调里用做标识）
        NULL          // infoFlagsOut
    );

    if (frameProps) CFRelease(frameProps);
}

// ==================== 第四步：Flush（逼出水管里剩余的帧）====================
VTCompressionSessionCompleteFrames(session);  // ← 阻塞直到编码器排空

// ==================== 第五步：销毁 ====================
VTCompressionSessionInvalidate(session);  // 异步停止
CFRelease(session);

// ==================== 输出回调（创建时注册的）====================
void compressionOutputCallback(
    void *outputCallbackRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTEncodeInfoFlags infoFlags,
    CMSampleBufferRef sampleBuffer)
{
    if (status != noErr) return;

    // sampleBuffer 包含编码后的 H.264/HEVC 数据

    // 1. 检查是否关键帧
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true);
    CFDictionaryRef attach  = CFArrayGetValueAtIndex(attachments, 0);
    bool isKeyFrame = !CFDictionaryContainsKey(attach, kCMSampleAttachmentKey_NotSync);
    // 或 kCMSampleAttachmentKey_DependsOnOthers == false

    // 2. 取 SPS/PPS（从 format description 里，不在数据里！）
    CMVideoFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    if (isKeyFrame) {
        // H.264: 取 SPS (index 0) + PPS (index 1)
        // HEVC: 取 VPS (0) + SPS (1) + PPS (2)
        const uint8_t *sps, *pps;
        size_t spsSize, ppsSize;
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(formatDesc, 0, &sps, &spsSize, NULL, NULL);
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(formatDesc, 1, &pps, &ppsSize, NULL, NULL);
        // 手动在每个 IDR 前注入 SPS/PPS（Annex-B 起始码分隔）
    }

    // 3. 取编码数据
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    size_t totalLength;
    char *dataPointer;
    CMBlockBufferGetDataPointer(blockBuffer, 0, NULL, &totalLength, &dataPointer);
    // dataPointer → AVCC 格式的 H.264 码流（4 字节长度前缀）
    // 如果要推 RTP，需要转 Annex-B（长度前缀 → 起始码 00 00 00 01）
}
```

### 5.2 解码器完整调用模板

```objc
// ==================== 第一步：创建解压会话 ====================
VTDecompressionSessionRef session = NULL;

// 1a. SPS/PPS → CMVideoFormatDescription
CMVideoFormatDescriptionRef formatDesc = NULL;
const uint8_t *parameterSets[2] = { spsData, ppsData };
size_t parameterSetSizes[2] = { spsSize, ppsSize };
CMVideoFormatDescriptionCreateFromH264ParameterSets(
    NULL, 2, parameterSets, parameterSetSizes, 4,  // NALULengthHeaderSize = 4
    &formatDesc
);

// 1b. 输出目标属性（解码后的像素格式、宽高等）
CFMutableDictionaryRef destAttrs = CFDictionaryCreateMutable(...);
// 解码后输出 NV12
CFNumberRef pxFmt = CFNumberCreate(NULL, kCFNumberIntType,
    &(int){kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange});
CFDictionarySetValue(destAttrs, kCVPixelBufferPixelFormatTypeKey, pxFmt);
// 解码后宽高
CFDictionarySetValue(destAttrs, kCVPixelBufferWidthKey,  (__bridge CFNumberRef)@(1920));
CFDictionarySetValue(destAttrs, kCVPixelBufferHeightKey, (__bridge CFNumberRef)@(1080));
// IOSurface 后端（零拷贝给 Metal）
CFDictionarySetValue(destAttrs, kCVPixelBufferIOSurfacePropertiesKey,
                     (__bridge CFDictionaryRef)@{});

// 1c. 创建会话
VTDecompressionSessionCreate(
    NULL,
    formatDesc,                            // format description (含 SPS/PPS)
    NULL,                                  // 解码器属性（nullable，可强制硬件）
    destAttrs,                             // 输出像素 buffer 属性
    &decompressionOutputCallback,          // ★ 输出回调
    (__bridge void *)self
);

// ==================== 第二步：解码循环 ====================
// VideoToolbox 解码吃的是 AVCC 格式的 CMSampleBuffer
// 所以要先构造 CMSampleBuffer

for (/* 每个 NALU / packet */) {
    // 2a. 构造 CMBlockBuffer（编码数据）
    CMBlockBufferRef blockBuffer = NULL;
    CMBlockBufferCreateWithMemoryBlock(NULL, naluData, naluSize,
        NULL, NULL, 0, naluSize, 0, &blockBuffer);

    // 2b. 构造 CMSampleBuffer
    CMSampleBufferRef sampleBuffer = NULL;
    CMSampleTimingInfo timing = { CMTimeMake(pts, timescale), CMTimeMake(duration, timescale), ... };
    size_t sampleSizes[] = { naluSize };

    CMSampleBufferCreateReady(
        NULL, blockBuffer, formatDesc, 1, 1, &timing, 1, sampleSizes, &sampleBuffer);

    // 2c. 送入解码
    VTDecodeFrameFlags flags = 0;
    VTDecodeInfoFlags   infoFlags;
    VTDecompressionSessionDecodeFrame(
        session, sampleBuffer, flags,
        NULL,     // sourceFrameRefCon
        &infoFlags
    );

    CFRelease(sampleBuffer);
    CFRelease(blockBuffer);
}

// ==================== 第三步：Flush + 销毁 ====================
VTDecompressionSessionWaitForAsynchronousFrames(session);  // 等异步帧完成
VTDecompressionSessionInvalidate(session);
CFRelease(session);

// ==================== 输出回调 ====================
void decompressionOutputCallback(
    void *decompressionOutputRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVImageBufferRef imageBuffer,   // ← CVPixelBuffer = 解码后的帧
    CMTime presentationTimeStamp,
    CMTime presentationDuration)
{
    if (status != noErr) return;

    // imageBuffer 就是 CVPixelBuffer，可直接给 Metal 当纹理、做零拷贝
    // CVPixelBufferGetPlaneCount(imageBuffer) → 2 (NV12)
    // CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0) → Y 平面
    // CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1) → UV 平面
    // CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0) → stride

    // 直接上屏（用 AVSampleBufferDisplayLayer 或 Metal）
    render_pixel_buffer(imageBuffer, presentationTimeStamp);
}
```

### 5.3 VideoToolbox 面试核心话术

> **Q：VTCompressionSessionEncodeFrame 是同步的还是异步的？**
>
> 异步。`EncodeFrame` 调用立刻返回，不代表帧已编码完。编码结果通过创建会话时注册的 `VTCompressionOutputCallback` 异步回调给你。所以收尾时必须调 `VTCompressionSessionCompleteFrames`——这是**阻塞的 flush**，会把流水线里还没吐出来的帧全部逼出来、通过回调交给你之后再返回。不调就丢最后几帧。

> **Q：VideoToolbox 吐的比特流格式是什么？跟 MediaCodec 有什么区别？**
>
> VideoToolbox 输出 AVCC 格式——每个 NALU 前是 4 字节大端长度前缀。MediaCodec 输入和输出都是 Annex-B——`00 00 00 01` 起始码分隔。**这是跨平台最容易翻车的差异**：iOS 推 RTP 必须把 AVCC 转 Annex-B（长度前缀→起始码），而且 SPS/PPS 在 VideoToolbox 里不在数据流里，要单独从 `CMVideoFormatDescriptionGetH264ParameterSetAtIndex` 取出来手动注入。不转的结果是对端解码器不认识格式、要么只有声音没有画面、要么首帧后全是马赛克。

> **Q：怎么在 iOS 上零拷贝解码 → Metal 渲染？**
>
> 建 `VTDecompressionSession` 时在 destAttrs 里加 `kCVPixelBufferIOSurfacePropertiesKey`，解码输出的 `CVPixelBuffer` 就会以后端为 IOSurface——一个内核管理的共享内存。然后用 `CVMetalTextureCache`，把 NV12 的 Y 平面 alias 成 r8 格式的 `MTLTexture`、UV 平面 alias 成 rg8 格式的，在 fragment shader 里做 YUV→RGB 转换。**整个过程数据都在 GPU 侧，一步 CPU 拷贝都没有**。同一条 IOSurface 可以同时被 VideoToolbox 写、Metal 读、甚至被 VTCompressionSession 再编码——拍照后直接硬编码零拷贝就是这个原理。

---

## 六、四套接口横向对比

### 6.1 生命周期对照表（面试画在纸上就是满分）

| 阶段 | FFmpeg | NVIDIA NVENC | Android MediaCodec | Apple VideoToolbox |
|---|---|---|---|---|
| **找编解码器** | `avcodec_find_decoder` | `NvEncodeAPICreateInstance` | `MediaCodec.createDecoderByType` | 用 `kCMVideoCodecType_*` 常量 |
| **创建对象** | `avcodec_alloc_context3` | `NvEncOpenEncodeSessionEx` | `MediaCodec.createDecoderByType` | `VTCompressionSessionCreate` |
| **配置** | `av_opt_set` + `avcodec_open2` | 填 `NV_ENC_INITIALIZE_PARAMS` + `NvEncInitializeEncoder` | `codec.configure(format, ...)` | `VTSessionSetProperty` × N |
| **启动** | `avcodec_open2` 即启动 | `NvEncInitializeEncoder` 即启动 | `codec.start()` | `VTCompressionSessionPrepareToEncodeFrames` |
| **输入** | `avcodec_send_packet`(解码) / `avcodec_send_frame`(编码) | `NvEncEncodePicture` | `queueInputBuffer` | `VTCompressionSessionEncodeFrame`(编码) / `VTDecompressionSessionDecodeFrame`(解码) |
| **输出** | `avcodec_receive_frame`(解码) / `avcodec_receive_packet`(编码) | `NvEncLockBitstream` | `dequeueOutputBuffer` | 回调函数 |
| **Flush** | `avcodec_send_packet(ctx, NULL)` | `NvEncEncodePicture` + `NV_ENC_PIC_FLAG_EOS` | `queueInputBuffer` + `BUFFER_FLAG_END_OF_STREAM` | `VTCompressionSessionCompleteFrames`(阻塞) |
| **停止** | `avcodec_free_context` 一并做 | `NvEncDestroyEncoder` | `codec.stop()` | `VTCompressionSessionInvalidate` |
| **释放** | `avcodec_free_context` | `NvEncDestroyEncoder` | `codec.release()` | `CFRelease(session)` |

### 6.2 关键设计差异

| 维度 | FFmpeg | NVIDIA | MediaCodec | VideoToolbox |
|---|---|---|---|---|
| **参数传递** | `av_opt_set` / 直接写 struct | 填 struct + `NvEncInitializeEncoder` | `MediaFormat` key-value | `VTSessionSetProperty` key-value |
| **SPS/PPS** | `extradata` (自动从 demuxer 拿) | 内联在比特流 | `csd-0` / `csd-1` (MediaFormat) | 在 `CMVideoFormatDescription` 里（数据外） |
| **比特流格式** | Annex-B（默认），可配 AVCC | Annex-B | Annex-B（输入和输出都是） | AVCC（4 字节长度前缀） |
| **Buffer 模型** | send/receive（上层不管 buffer 池） | Lock/Unlock 显式管理 buffer 池 | 队列池（dequeue input → fill → queue → dequeue output → release） | PixelBuffer 池（系统管理） |
| **零拷贝路径** | `hw_frames_ctx` + HW accel | CUDA interop / `cuMemcpyDtoD` | Surface 模式 | IOSurface + `CVMetalTextureCache` |
| **异步支持** | 同步（可在独立线程模拟异步） | CUDA event（原生异步） | `setCallback`（原生异步） | 回调（原生异步） |
| **错误处理** | 负返回值（`AVERROR(x)`） | `NV_ENC_ERR_*` 错误码 | `CodecException` / 负返回 | `OSStatus != noErr` |

### 6.3 沟通用总表（一句话说清楚每个框架的"模板"）

> 面试官问"你用过哪些编解码 API"，回答这个：

| 框架 | 编码模板（一句话） | 解码模板（一句话） |
|---|---|---|
| **FFmpeg** | `avcodec_find_encoder` → `alloc/av_opt_set/open2` → `send_frame`/`receive_packet` 循环 → `send_frame(NULL)` → `free_context` | `avcodec_find_decoder` → `alloc/open2` → `send_packet`/`receive_frame` 循环 → `send_packet(NULL)` → `free_context` |
| **NVIDIA** | `CreateInstance` → `OpenEncodeSession` → `InitializeEncoder` → Lock/填/Unlock → `EncodePicture` → `LockBitstream` → EOS → `DestroyEncoder` | `cuvidCreateDecoder` → 循环 `cuvidParseVideoData` → `cuvidMapVideoFrame` → `cuvidUnmapVideoFrame` → `DestroyDecoder` |
| **Android** | `createEncoderByType` → `configure(format,...)` → `start()` → 异步 `onInputBufferAvailable` → `queueInputBuffer` → 异步 `onOutputBufferAvailable` → `stop()` → `release()` | 同上，仅创建时用 `createDecoderByType` |
| **Apple** | `VTCompressionSessionCreate` → `VTSessionSetProperty`×N → `PrepareToEncodeFrames` → `EncodeFrame` → 回调收 `CMSampleBuffer` → `CompleteFrames` → `Invalidate` | `CMVideoFormatDescriptionCreateFromH264ParameterSets` → `VTDecompressionSessionCreate` → `DecodeFrame` → 回调收 `CVPixelBuffer` → `WaitForAsynchronousFrames` → `Invalidate` |

---

## 七、面试高频追问

### Q1：软编和硬编在接口层的本质区别是什么？

> 软编都是 CPU 指令集实现，接口就是标准的 `avcodec_open2`，然后 `send_frame`/`receive_packet`。硬编分两档：**一档是走 FFmpeg 封装的**（`h264_nvenc`、`h264_videotoolbox`），接口还是 send/receive，只是 `pix_fmt` 是硬件格式、需要 `hw_frames_ctx`——这对上层是透明的；**另一档是直调平台 SDK**（NVIDIA Video Codec SDK、MediaCodec、VideoToolbox），接口完全不一样，但心智模型完全一样——都是"创建会话→配参数→推帧→收包→flush→销毁"这一套。面试官要听的就是你知道这两层的区别，知道什么场景走哪一层。

### Q2：编解码中都会调用哪些接口？按生命周期讲一遍。

> 分五个阶段。**阶段一、创建**：FFmpeg 是 `avcodec_find_decoder` + `avcodec_alloc_context3`，MediaCodec 是 `createDecoderByType`，VideoToolbox 是 `VTDecompressionSessionCreate`，NVIDIA 是 `NvEncOpenEncodeSessionEx`。**阶段二、配置**：FFmpeg 往 `AVCodecContext` 里填参数 + `avcodec_open2`，MediaCodec 是 `configure(format)`，VideoToolbox 是调 `VTSessionSetProperty` 反复设属性，NVIDIA 是填 `NV_ENC_INITIALIZE_PARAMS` struct + `NvEncInitializeEncoder`。**阶段三、编解码循环**：输入侧 FFmpeg 是 `send_packet`/`send_frame`，MediaCodec 是 `queueInputBuffer`，VideoToolbox 是 `EncodeFrame`/`DecodeFrame`，NVIDIA 是 Lock 输入 buf → `EncodePicture`。输出侧 FFmpeg 是 `receive_frame`/`receive_packet`，MediaCodec 是 `dequeueOutputBuffer`，VideoToolbox 是通过回调收 `CMSampleBuffer`，NVIDIA 是 `LockBitstream`。**阶段四、Flush**：FFmpeg 是 `send_packet(ctx, NULL)`，MediaCodec 是 `queueInputBuffer` 带 `BUFFER_FLAG_END_OF_STREAM`，VideoToolbox 是 `CompleteFrames`，NVIDIA 是 `EncodePicture` + `EOS` flag。**阶段五、释放**：各自调 destroy/release/invalidate——**必须在 flush 之后再释放**，否则丢帧。

### Q3：异步编解码的回调在哪个线程？同步和异步怎么选？

> NVIDIA：回调在内部线程执行，CUDA event 做同步。MediaCodec：`onInputBufferAvailable` / `onOutputBufferAvailable` 在 framework 内部线程回调，不是你的主线程——所以回调里不能直接更新 UI，要做线程切换。VideoToolbox：回调线程不保证，通常是一个系统管理的后台线程。FFmpeg：本身是同步的，没有内置异步，你得自己在独立线程里跑 send/receive 循环。选型上：**实时通信（WebRTC、直播）必须异步**，因为同步模式里的 dequeue 超时会把延迟抖动放大；**离线处理（转码、剪辑导出）同步更简单**，而且不用担心回调时序问题。

### Q4：编码器输出格式不一致怎么办？（AVCC / Annex-B 互转）

> 这是跨平台最常见的数据处理坑。**MediaCodec 和 NVIDIA 走 Annex-B**（`00 00 00 01` 起始码），**VideoToolbox 走 AVCC**（4 字节长度前缀）。推 RTP 必须 Annex-B，所以 VideoToolbox 输出要先转：遍历 block buffer，每个 NALU 前面 4 字节大端长度读出来做偏移，写 `00 00 00 01` 起始码。反过来存 MP4 必须 AVCC，所以 Annex-B 输出要先转：扫描起始码、记录每个 NALU 长度、去掉起始码写 4 字节长度前缀。**面试时能把这个讲清楚、知道起始码有 3 字节 `00 00 01` 和 4 字节 `00 00 00 01` 两种、知道 HEVC 还多一个 VPS**，就是高级了。

### Q5：零拷贝链路怎么串？

> 拿视频通话举例——**采集**用 `AVCaptureVideoDataOutput` 拿到 `CVPixelBuffer`，**编码**直接把这个 `CVPixelBuffer` 传给 `VTCompressionSessionEncodeFrame`——注意 `CVPixelBuffer` 必须是用 `CVPixelBufferPool` 创建的、且带了 IOSurface backend，这样 Camera 和 VT 操作的是同一块内核内存。**发送**在编码回调里拿到 `CMSampleBuffer`→`CMBlockBuffer` 指针直接喂网络。整个过程**CPU 只跑了协议栈和回调调度，没有一次像素拷贝**。这跟 NVIDIA 的"NVDEC 解码→CUDA kernel 处理（显存）→NVENC 编码→网络发送"是完全对等的——只是 GPU 总线从 PCIe 换成了 SoC 内部总线。

---

## 八、学习建议

1. **先通读这一篇**，把四套接口的"模板"背下来——面试时画在纸上就是满分答案
2. **回 07** 建立"硬件帧 vs 软件帧"的核心直觉（为什么不能 memcpy、sws_scale 不处理硬件帧）
3. **按你主攻的平台深挖专题**：
   - 桌面/服务端 → [13-NVIDIA](./13-NVIDIA硬件编解码.md)（NVENC/NVDEC 深入）
   - Android → [14-Android](./14-Android硬件编解码.md)（MediaCodec 深入）
   - iOS/macOS → [15-iOS](./15-iOS硬件编解码.md)（VideoToolbox 深入）
   - 跨平台高级考点 → [16-硬件编解码高级专题](./16-硬件编解码高级专题.md)
4. **动手写**：每个平台挑一个框架，从创建到销毁完整写一遍。写的过程中你会撞到参数校验、错误处理、内存管理等各种细节——这些东西才是在面试官追问时你能展开讲的素材
