# FFmpeg 新手必读：AVPacket 与 AVFrame 核心指南

在 FFmpeg 的世界里，数据的流转主要围绕两个核心结构体展开：`AVPacket` 和 `AVFrame`。
很多新手在刚接触 FFmpeg 时，会在这两个结构体的内存管理、时间戳计算、以及数据读取上栽跟头。

为了方便理解，我们可以用一个生动的比喻：
*   📦 **`AVPacket`（压缩包）**：就像是**“压缩饼干”**或**“快递包裹”**。它体积小，方便网络传输和磁盘存储，但你**不能直接看或听**，必须经过“解压（解码）”。
*   🖼️ **`AVFrame`（原始帧）**：就像是**“泡开的饼干”**或**“拆开的实物”**。它体积庞大，占用大量内存，但它是显卡和声卡真正能认识的数据，可以**直接渲染到屏幕或输出到喇叭**。

整个播放/转码的流水线就是：
`文件 -> 解封装(Demux) -> 【AVPacket】 -> 解码(Decode) -> 【AVFrame】 -> 滤镜(Filter) -> 【AVFrame】 -> 编码(Encode) -> 【AVPacket】 -> 封装(Mux) -> 文件`

---

## 1. AVPacket (压缩数据包)

`AVPacket` 存放的是编码后的数据（例如 H.264 视频流、AAC 音频流）。

### 1.1 视频 Packet 与 音频 Packet 的重要区别！
这是新手极易忽略的一点：
*   🎬 **视频 AVPacket**：通常情况下，**1 个视频 Packet 只包含 1 帧完整的视频画面**（比如一个 H.264 的 NALU）。
*   🎵 **音频 AVPacket**：情况比较复杂。**1 个音频 Packet 可能包含 1 帧，也可能包含多帧音频数据**（比如一个 AAC Packet 可能打包了多个音频帧）。
    *   **推论**：在写解码代码时，送入（`avcodec_send_packet`）1 个音频 Packet，你可能需要在一个 `while` 循环里调用多次 `avcodec_receive_frame` 才能把里面的音频帧全部读完！

### 1.2 核心属性解析
*   **`uint8_t *data` / `int size`**
    *   **含义**：指向实际压缩数据的指针，以及这坨数据的大小（字节）。
*   **`int64_t pts` (Presentation Time Stamp)**
    *   **含义**：**显示时间戳**。告诉播放器，这包数据解压后，应该在视频的第几秒显示出来。
*   **`int64_t dts` (Decode Time Stamp)**
    *   **含义**：**解码时间戳**。告诉解码器，应该在什么时候解压这包数据。
    *   **为什么会有两个时间？** 因为视频有 **B帧（双向预测帧）**。B帧需要依赖它后面的帧才能解码。这就导致了“先解码的帧，可能要后显示”，所以 DTS 和 PTS 会产生错位。
*   **`int stream_index`**
    *   **含义**：流索引。一个 MP4 文件里同时有视频和音频，你用 `av_read_frame` 读出一个包时，必须看这个字段：`0` 可能是视频，`1` 可能是音频。靠它来决定把包送给哪个解码器。
*   **`int flags`**
    *   **含义**：标志位。最重要的是 `AV_PKT_FLAG_KEY`。如果包含这个标志，说明这是一个**关键帧（I 帧）**。视频的拖动进度条（Seek）操作，通常只能跳到关键帧上。

---

## 2. AVFrame (原始数据帧)

`AVFrame` 存放的是解压后的原始数据（如 YUV/RGB 像素，PCM 音频采样）。它非常庞大，一张 1080P 的 RGB 图像大约需要 6MB 内存。

### 2.1 内存布局：Planar（平面） vs Packed（打包）
这是新手必须跨过的第一道坎。`AVFrame` 里的数据存放在 `uint8_t *data[AV_NUM_DATA_POINTERS]` 这个数组里。为什么是一个数组而不是一个指针？

*   **Packed 格式（打包格式，如 RGB24）**：
    *   所有像素交错存放在一起：`[R,G,B, R,G,B, R,G,B ...]`
    *   此时，**只有 `data[0]` 有值**，指向这整块内存。
*   **Planar 格式（平面格式，如 YUV420P，最常见的视频格式）**：
    *   Y、U、V 三个分量是**分开存放**的。
    *   `data[0]` 指向所有的 Y 数据 `[Y,Y,Y,Y...]`
    *   `data[1]` 指向所有的 U 数据 `[U,U,U,U...]`
    *   `data[2]` 指向所有的 V 数据 `[V,V,V,V...]`
*   **音频同理**：双声道 PCM 数据，如果是 Packed（`AV_SAMPLE_FMT_S16`），左右声道交错 `[L,R, L,R]` 都在 `data[0]`；如果是 Planar（`AV_SAMPLE_FMT_S16P`），左声道在 `data[0]`，右声道在 `data[1]`。

### 2.2 核心属性解析
*   **`int linesize[AV_NUM_DATA_POINTERS]`**
    *   **含义**：**跨度（Stride）**。表示图像每一行数据在内存中占用的**实际字节数**。（见下方“常见错误 1”的详细图解）。
*   **`int width`, `int height`**
    *   **含义**：视频的实际像素宽高（如 1920 x 1080）。
*   **`int format`**
    *   **含义**：数据格式。视频对应 `AVPixelFormat`（如 `AV_PIX_FMT_YUV420P`），音频对应 `AVSampleFormat`（如 `AV_SAMPLE_FMT_FLTP`）。
*   **`int64_t pts` (Presentation Time Stamp)**
    *   **含义**：**显示时间戳**。告诉渲染器（屏幕/声卡）这帧画面或这段声音应该在什么时候播放。
    *   **主要目的**：
        1.  **音视频同步 (A/V Sync)**：这是最核心的作用！视频和音频是分别解码的，播放器怎么知道哪张画面该配哪句台词？就是通过对比视频 Frame 和音频 Frame 的 `pts`。如果视频的 `pts` 比音频慢了，视频就要加速播放或丢帧；如果快了，视频就要等待。
        2.  **转码与滤镜 (Transcoding & Filter)**：在给视频加滤镜（比如加水印、变速、裁剪）或者重新编码时，滤镜和编码器必须知道每一帧的精确时间顺序。如果没有正确的 `pts`，编码出来的视频可能会像快进一样，或者报错 `Invalid pts`。
    *   **注意**：和 `AVPacket` 一样，它的单位不是秒，而是依赖于所在流的 `time_base`。
*   **`int64_t best_effort_timestamp`**
    *   **含义**：FFmpeg 内部算法推算出的“最靠谱”的显示时间戳。当你发现解出来的 `frame->pts` 是 `AV_NOPTS_VALUE`（无效值）时，首选使用这个值。
*   **`int64_t pkt_dts` (Packet Decode Time Stamp)**
    *   **含义**：**溯源解码时间戳**。记录了“这个 Frame 是由哪个 DTS 的 Packet 解码出来的”。
    *   **为什么已经解码了还需要它？**
        1.  **救命稻草 (Fallback)**：现实中很多视频文件是损坏的，或者像 H.264 裸流根本没有 PTS。当解码器吐出 `AVFrame` 时，如果发现 `pts` 丢失，开发者只能靠这个 `pkt_dts` 来推算（猜）它应该什么时候播放。上面的 `best_effort_timestamp` 底层其实就严重依赖 `pkt_dts` 来做推算。
        2.  **计算延迟与溯源**：用于调试，计算“从送入解码器到拿到画面”到底花了多长时间，或者结合 `pkt_pos` 知道这个画面对应在硬盘文件里的哪个字节位置。
    *   **⚠️ 进阶认知 (FFmpeg 的进化)**：你的直觉非常准确！逻辑上，解压后的 Frame 确实不该再关心 Packet 的属性（因为 Frame 也可能是滤镜凭空生成的，根本没有 Packet）。所以，**在最新的 FFmpeg 版本（6.0 之后）中，`pkt_dts` 等带有 `pkt_` 前缀的属性正在被逐渐废弃 (Deprecated)**。官方也认为这种设计破坏了结构体的纯粹性。
*   **`int nb_samples` (音频专属)**
    *   **含义**：这帧音频包含了多少个采样点（单通道）。
*   **`int sample_rate` (音频专属)**
    *   **含义**：音频的采样率（如 44100 Hz, 48000 Hz）。

---

## 3. 内存管理：信封与信件（引用计数）

FFmpeg 为了追求极致性能，使用了**引用计数（Reference Counting）**机制，实现了“零拷贝”。
新手最容易在这里写出内存泄漏或野指针导致崩溃（Segfault）的代码。

我们可以把 `AVPacket` / `AVFrame` 想象成一个 **“信封”**，而里面 `data` 指向的几兆内存是 **“信件”**。

*   **`av_packet_alloc()` / `av_frame_alloc()`**：
    *   **动作**：买了一个空“信封”。此时 `data` 是空的，没有信件。
*   **`av_packet_free()` / `av_frame_free()`**：
    *   **动作**：把“信封”和里面的“信件”一起扔进垃圾桶。彻底销毁。
*   **`av_packet_unref()` / `av_frame_unref()`**：
    *   **动作**：**极其重要！** 把“信件”拿出来扔掉（引用计数减 1，归零则销毁内存），但**保留“信封”**。
    *   **用途**：在 `while` 循环读取视频时，我们不需要每次都买新信封。只需要把旧信件 `unref` 倒掉，空信封就可以装下一帧新信件了。
*   **`av_packet_ref()` / `av_frame_ref()`**：
    *   **动作**：增加引用计数（浅拷贝）。就像给信件加了一把锁，告诉系统“我还在看这封信，别人不能把它扔了”。

---

## 4. 新手最常犯的错误与认知误区 (Gotchas)

### ❌ 误区 1：认为 `linesize` 永远等于 `width` (导致花屏/绿屏/倾斜)
*   **错误认知**：遍历图像像素时，认为每行的数据量就是 `width` 个像素占用的字节数。
*   **真相**：为了 CPU 处理效率（SIMD 指令优化），FFmpeg 在分配内存时通常会进行 16 或 32 字节**对齐**。如果 `width` 不是对齐的倍数，每行末尾会有无用的 padding（填充）字节。
    ```text
    假设真实画面宽度 (width) = 100 字节
    为了 32 字节对齐，内存实际宽度 (linesize) 会被分配为 128 字节
    [ 真实像素 100 bytes ] [ 填充废数据 28 bytes ]
    [ 真实像素 100 bytes ] [ 填充废数据 28 bytes ]
    ```
*   **正确做法**：拷贝或遍历图像数据时，**必须按 `linesize` 进行偏移**，而不是 `width`。
    ```cpp
    // ❌ 错误：直接整块拷贝，会导致画面倾斜或花屏
    memcpy(dst, frame->data[0], width * height); 
    
    // ✅ 正确：逐行拷贝，跳过行末的 padding 废数据
    for (int y = 0; y < height; y++) {
        memcpy(dst + y * dst_linesize, frame->data[0] + y * frame->linesize[0], width);
    }
    ```

### ❌ 误区 2：认为 PTS 的单位是“秒”
*   **错误认知**：`frame->pts = 2;` // 以为是 2 秒
*   **真相**：PTS 只是一个**刻度值**。它的实际时间取决于所在流的**时间基（`time_base`）**。
    *   假设 `time_base` 是 `1/90000`（这在 MP4 视频中很常见）。
    *   如果 `pts = 90000`，那么实际时间 = `90000 * (1 / 90000)` = 1 秒。
*   **正确做法**：在不同组件（Demuxer -> Decoder -> Encoder -> Muxer）之间传递数据时，由于各自的 `time_base` 可能不同，必须使用 `av_rescale_q` 进行时间基转换。

### ❌ 误区 3：在循环中不断 Alloc 和 Free 对象 (导致性能极差)
*   **❌ 错误写法**：
    ```cpp
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        AVPacket *pkt = av_packet_alloc(); // 每次循环都向系统申请内存
        // ... process ...
        av_packet_free(&pkt);              // 每次循环都销毁
    }
    ```
*   **✅ 正确写法**：在循环外分配一次外壳，在循环内使用 `unref` 清理数据。
    ```cpp
    AVPacket *pkt = av_packet_alloc(); // 只买一个信封
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        // ... process ...
        av_packet_unref(pkt);          // 处理完后，把信件倒掉，信封留着下次用
    }
    av_packet_free(&pkt);              // 彻底结束时才扔掉信封
    ```

### ❌ 误区 4：认为 SendPacket 后就可以安全修改或释放 Packet
*   **错误认知**：调用 `avcodec_send_packet(ctx, pkt)` 后，认为解码器已经把数据“吃”进去了，立刻手动修改 `pkt->data` 或者强行释放底层内存。
*   **真相**：`avcodec_send_packet` 默认是增加引用计数（浅拷贝）。如果你强行破坏了底层数据，解码器在后续异步解码时会发生段错误（Segfault）。
*   **正确做法**：调用 `av_packet_unref(pkt)`。它会安全地减少引用计数，解码器那边如果还在用，数据就不会被真正释放，保证了多线程安全。

### ❌ 误区 5：音频解码时，认为一个 Packet 只能解出一个 Frame
*   **错误认知**：
    ```cpp
    avcodec_send_packet(ctx, pkt);
    avcodec_receive_frame(ctx, frame); // ❌ 只读一次
    ```
*   **真相**：就像前面提到的，一个音频 Packet 可能包含多个音频帧。如果你只读一次，剩下的音频帧就丢失了（表现为声音卡顿、断断续续）。
*   **✅ 正确做法**：必须用 `while` 循环去读，直到解码器告诉你 `EAGAIN`（没数据了，需要新的 packet）。
    ```cpp
    avcodec_send_packet(ctx, pkt);
    while (avcodec_receive_frame(ctx, frame) >= 0) {
        // 处理音频帧...
    }
    ```