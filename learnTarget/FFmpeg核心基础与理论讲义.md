# FFmpeg 核心基础与底层理论讲义（C++ 视角）

本讲义专为具备 C++ 基础的开发者编写，旨在剥离 UI 与渲染的干扰，直击音视频开发中最核心的**底层理论**、**FFmpeg 核心 API 链路**以及**内存与时间戳管理**。

---

## 第一章：音视频底层理论基石

在调用 FFmpeg API 之前，必须深刻理解我们在处理什么样的数据。

### 1.1 图像与色彩空间：为什么是 YUV？
*   **RGB**：显示器使用的色彩空间（红绿蓝）。每个像素通常占 24 bit（3 字节）或 32 bit（带 Alpha 通道）。
*   **YUV**：音视频领域最核心的色彩空间。`Y` 代表亮度（Luma），`U` 和 `V` 代表色度（Chroma）。
*   **为什么用 YUV？** 人眼对亮度敏感，对颜色不敏感。因此可以压缩 `U` 和 `V` 的数据量而不影响主观观感。
    *   **YUV420P**：最常用的格式。每 4 个 Y 共用 1 个 U 和 1 个 V。数据量是 RGB24 的一半（1.5 字节/像素）。`P` 代表 Planar（平面格式），即 Y、U、V 数据在内存中是分三个独立数组连续存放的。
        *   **内存分布图解（以 4x4 像素的图像为例）**：
            总像素数为 16。
            Y 分量大小：4x4 = 16 字节
            U 分量大小：2x2 = 4 字节
            V 分量大小：2x2 = 4 字节
            总大小：16 + 4 + 4 = 24 字节（正好是 16 * 1.5）

            ```text
            [ Y 平面 (Plane 0) - 连续 16 字节 ]
            Y00 Y01 Y02 Y03
            Y10 Y11 Y12 Y13
            Y20 Y21 Y22 Y23
            Y30 Y31 Y32 Y33

            [ U 平面 (Plane 1) - 连续 4 字节 ]
            U00 U01
            U10 U11

            [ V 平面 (Plane 2) - 连续 4 字节 ]
            V00 V01
            V10 V11
            ```
            在 FFmpeg 的 `AVFrame` 中，这种分布对应如下指针：
            - `frame->data[0]` 指向 Y 平面的起始地址。
            - `frame->data[1]` 指向 U 平面的起始地址。
            - `frame->data[2]` 指向 V 平面的起始地址。
            - `frame->linesize[0]` 是 Y 平面一行的字节数（包含对齐的 padding）。
            - `frame->linesize[1]` 是 U 平面一行的字节数（通常是 Y 的一半）。
            - `frame->linesize[2]` 是 V 平面一行的字节数（通常是 Y 的一半）。

### 1.2 音频基础：PCM 裸流
声音是模拟信号，计算机处理需要将其数字化，这个过程叫 PCM（脉冲编码调制）。
*   **采样率（Sample Rate）**：每秒采样的次数，如 44100 Hz（CD音质）、48000 Hz。
*   **位深（Bit Depth）**：每次采样用多少位表示，如 16 bit（2 字节）、32 bit 浮点数。
*   **声道数（Channels）**：单声道（1）、立体声（2）。
*   **数据量计算**：1秒钟 44.1kHz、16bit、双声道的 PCM 数据大小 = 44100 * (16/8) * 2 = 176,400 字节。

### 1.3 视频编码基础：H.264 与 GOP
未经压缩的 YUV 数据极其庞大，必须进行编码（压缩）。H.264 是目前最主流的编码标准。
*   **帧类型**：
    *   **I 帧（关键帧/内部编码帧）**：自带全部画面信息，可独立解码。压缩率最低。
    *   **P 帧（前向预测帧）**：参考前面的 I 帧或 P 帧，只存储差异数据。
    *   **B 帧（双向预测帧）**：参考前面和后面的帧，压缩率最高，但**会引入解码延迟**（必须等后面的帧到达才能解码）。
*   **GOP（Group of Pictures）**：两个 I 帧之间的距离。直播场景通常设置较小的 GOP（如 1-2 秒）以降低延迟和实现秒开；点播场景可以设置较大的 GOP 以提高压缩率。
*   **NALU（Network Abstraction Layer Unit）**：H.264 码流的基本单元，包含 StartCode（`00 00 00 01` 或 `00 00 01`）和实际数据。

### 1.4 核心痛点：时间戳与同步 (PTS/DTS)
由于 B 帧的存在，视频帧的**解码顺序**和**显示顺序**是不同的！
*   **DTS (Decoding Time Stamp)**：解码时间戳，告诉解码器什么时候解码这一帧。
*   **PTS (Presentation Time Stamp)**：显示时间戳，告诉播放器什么时候渲染这一帧。
*   *举例*：如果帧序列是 `I B P`，那么：
    *   接收/解码顺序 (DTS)：`I -> P -> B`（必须先解 P 才能解 B）
    *   显示顺序 (PTS)：`I -> B -> P`

---

## 第二章：FFmpeg 核心架构与内存管理

作为 C++ 开发者，防止内存泄漏是第一要务。FFmpeg 是 C 语言编写的，大量使用了指针和手动内存管理。

### 2.1 核心结构体全景图
1.  **`AVFormatContext`**：解封装上下文。代表一个完整的媒体文件或网络流（如 MP4、FLV、RTMP 流）。
2.  **`AVStream`**：代表文件中的一条流（视频流、音频流、字幕流）。
3.  **`AVCodecContext`**：编解码上下文。保存了编解码器的所有参数（宽高、码率、像素格式等）。
4.  **`AVPacket`**：**压缩数据**（如 H.264 的 NALU、AAC 数据）。通常属于 demux 之后、decode 之前的数据。
5.  **`AVFrame`**：**未压缩的原始数据**（如 YUV、PCM）。属于 decode 之后、渲染/处理之前的数据。

### 2.2 内存管理与引用计数机制（重点）
`AVPacket` 和 `AVFrame` 内部包含大量数据缓冲。为了避免频繁的内存拷贝，FFmpeg 引入了**引用计数（Reference Counting）**机制（类似 C++ 的 `std::shared_ptr`）。

```cpp
// 1. 分配 AVPacket 结构体本身（不包含数据 buffer）
AVPacket* pkt = av_packet_alloc();

// 2. 假设从文件中读取了一帧压缩数据，此时 pkt 内部的 buf 引用计数变为 1
av_read_frame(format_ctx, pkt);

// 3. 如果你想把这个 pkt 放入队列给另一个线程处理，必须增加引用计数！
AVPacket* new_pkt = av_packet_alloc();
av_packet_ref(new_pkt, pkt); // 此时底层数据 buffer 引用计数变为 2

// 4. 使用完毕后，必须解除引用。当引用计数为 0 时，底层 buffer 才会被释放
av_packet_unref(pkt);
av_packet_unref(new_pkt);

// 5. 最终释放结构体本身
av_packet_free(&pkt);
av_packet_free(&new_pkt);
```
**避坑指南**：在多线程架构（如解封装线程 -> 队列 -> 解码线程）中，入队前必须 `av_packet_ref`，出队消费完后必须 `av_packet_unref`，否则必定内存泄漏！

---

## 第三章：FFmpeg 核心链路代码实战

下面我们将串联解封装（Demuxing）和解码（Decoding）的核心链路。

### 3.1 解封装与解码初始化

```cpp
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <iostream>

int main() {
    const char* input_url = "test.mp4";
    
    // 1. 打开输入文件/流，分配 AVFormatContext
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (avformat_open_input(&fmt_ctx, input_url, nullptr, nullptr) < 0) {
        std::cerr << "Failed to open input" << std::endl;
        return -1;
    }

    // 2. 探测流信息（获取时长、流数量等）
    avformat_find_stream_info(fmt_ctx, nullptr);

    // 3. 找到视频流的索引
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    // 4. 查找解码器并初始化解码上下文
    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    
    // 将流参数拷贝到解码器上下文
    avcodec_parameters_to_context(codec_ctx, codecpar);
    
    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        return -1;
    }
    
    // ... 进入读取循环 (见 3.2) ...
}
```

### 3.2 核心循环：读取与解码 (Send / Receive API)
FFmpeg 3.x 之后引入了全新的收发解耦 API：`avcodec_send_packet` 和 `avcodec_receive_frame`。这完美契合了 B 帧带来的输入输出不对等问题（塞入 1 个 Packet，可能输出 0 个或多个 Frame）。

```cpp
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // 持续从文件中读取压缩数据包
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        // 只处理视频流
        if (pkt->stream_index == video_stream_idx) {
            
            // 1. 将压缩数据包发送给解码器
            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "Error sending packet to decoder" << std::endl;
                break;
            }

            // 2. 从解码器接收未压缩的原始帧 (YUV)
            // 一个 Packet 可能解出多个 Frame，也可能解出 0 个 Frame，所以需要 while 循环
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break; // 需要更多输入，或者已经解码完毕
                } else if (ret < 0) {
                    std::cerr << "Error during decoding" << std::endl;
                    break;
                }

                // 成功获取到一帧 YUV 数据！
                std::cout << "Decoded frame: width=" << frame->width 
                          << " height=" << frame->height 
                          << " pts=" << frame->pts << std::endl;

                // 在这里可以进行你的算法分析、滤镜处理等操作
                // Y 数据: frame->data[0], U 数据: frame->data[1], V 数据: frame->data[2]
                // Y 行宽: frame->linesize[0] (注意：行宽可能大于真实的 width，用于内存对齐)

                // 帧使用完毕，解除引用，以便复用 frame 结构体
                av_frame_unref(frame);
            }
        }
        // Packet 使用完毕，解除引用，释放内部 buffer
        av_packet_unref(pkt);
    }

    // 刷新解码器内部缓存 (Flush)
    avcodec_send_packet(codec_ctx, nullptr);
    // ... 再次调用 avcodec_receive_frame 直到返回 AVERROR_EOF ...

    // 资源释放
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
```

---

## 第四章：进阶避坑指南（中高级必问）

### 4.1 Timebase（时间基）的转换
在 FFmpeg 中，时间不是用“秒”或“毫秒”直接表示的，而是用一个分数 `AVRational`（时间基）来表示。
*   **容器层（AVStream）**：有自己的 time_base（例如 1/90000）。
*   **编解码层（AVCodecContext）**：有自己的 time_base（例如 1/25，代表 25fps）。

当你把解封装（Demux）得到的 `AVPacket` 送去解码，或者把解码得到的 `AVFrame` 送去编码封装时，**必须进行时间基转换**，否则时间戳全乱，播放极快或极慢。

```cpp
// 示例：将 AVPacket 的时间戳从 输入流的时间基 转换到 输出流的时间基
av_packet_rescale_ts(pkt, input_stream->time_base, output_stream->time_base);

// 底层其实是调用了 av_rescale_q_rnd 函数进行分数运算，防止溢出
int64_t dst_pts = av_rescale_q_rnd(src_pts, src_time_base, dst_time_base, 
                                   (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
```

### 4.2 视频变速的底层逻辑（结合 PTS 与 Timebase）
很多开发者会问：**实现视频倍速播放，是不是直接修改 `time_base`？**
答案是：**通常不改 `time_base`，而是直接按比例缩放 PTS 和 DTS。**

`time_base` 只是时间的一把“尺子”（比如尺子的刻度是 1/90000 秒）。要让视频快进，我们不需要换尺子，而是把每一帧在尺子上的“刻度值”（PTS）按比例缩小。

**场景一：封装层面的变速（不重新编解码，通常用于纯视频）**
如果你只是想把一个 MP4 变成 2 倍速的 MP4，可以在 Demux 拿到 `AVPacket` 后，直接修改它的时间戳：
```cpp
double speed_factor = 2.0; // 2倍速快进

// 将时间戳除以倍数，时间缩短，播放变快
pkt->pts = pkt->pts / speed_factor;
pkt->dts = pkt->dts / speed_factor;
pkt->duration = pkt->duration / speed_factor;
```
*坑点提示*：这种方法对视频有效，但如果包含音频，直接改音频的 PTS 会导致播放器解码异常或变调（变成“花栗鼠”音）。因此，纯改时间戳的变速通常会选择丢弃音频流。

**场景二：使用 AVFilter 滤镜变速（标准做法，需重新编解码）**
在真正的工程中（如主流播放器的倍速播放），为了保证音视频同步且音频不变调，必须解码成 `AVFrame`，然后送入 FFmpeg 滤镜处理：
*   **视频**：使用 `setpts` 滤镜（例如 `setpts=0.5*PTS`，让显示时间减半）。
*   **音频**：使用 `atempo` 滤镜。`atempo` 的核心算法（如 WSOLA）可以在改变音频播放速度的同时，**保持音频的音高（Pitch）不变**，这是单纯修改 PTS 绝对做不到的。

### 4.3 行宽对齐（Linesize Alignment）与内存分布
在处理 YUV 数据时（如拷贝到内存分析），新手常犯的错误是认为 `数据总大小 = width * height * 1.5`，然后直接用一整块 `memcpy` 拷贝。
实际上，出于 CPU 向量化指令（SIMD/AVX/NEON）优化的考虑，FFmpeg 分配的内存通常是 **16 或 32 字节对齐**的。

**1. 为什么需要 16/32 字节对齐？**
现代 CPU 处理图像时，不会一个像素一个像素地处理，而是使用 SIMD 指令（单指令多数据流）一次性处理 16 个或 32 个字节。这些底层汇编指令（比如你之前做性能优化时可能接触过的 NEON 指令集）要求数据的起始内存地址必须是 16 或 32 的倍数，否则会导致 CPU 抛出异常或读取性能大幅下降。

**2. 对齐是如何影响内存分布的？**
假设你有一张分辨率为 **1918 x 1080** 的图片（注意：1918 不是 16 或 32 的倍数）。
*   `frame->width` = 1918（真实的图像宽度）。
*   为了让**下一行的起始地址**依然能被 32 整除，FFmpeg 会在每一行的末尾**填充（Padding）**一些无用的空白字节。
*   计算对齐后的行宽：1918 向上取 32 的倍数，结果是 1920。
*   因此，`frame->linesize[0]` = 1920（Y 分量在内存中一行的实际字节数，包含了 Padding）。

**3. 内存分布图解**
为了直观，我们以一个真实宽度 `width = 5`，要求 **8 字节对齐**的极简例子来看：
*   `width` = 5 字节（有效像素）
*   `linesize` = 8 字节（物理跨度）
*   `padding` = 3 字节（无用填充）

```text
[ 内存中的一维连续空间 ]
|<- width (5) ->|<- pad (3) ->|<- width (5) ->|<- pad (3) ->|
[Y0 Y1 Y2 Y3 Y4 | x  x  x     ][Y5 Y6 Y7 Y8 Y9 | x  x  x     ]...
^                             ^
第0行起始地址                  第1行起始地址 (地址是8的倍数)
```
*如果你直接把这块内存当成连续的图像数据读出来，那些 `x x x` 的垃圾数据就会混入图像中，导致画面倾斜、撕裂或者右侧出现绿边。*

**4. 正确的数据拷贝方式**
因为有 Padding 的存在，你不能直接 `memcpy` 整个画面。**必须逐行拷贝，剔除 Padding！**

```cpp
// 逐行拷贝 Y 分量到连续的内存 buffer 中（去除 Padding）
for (int i = 0; i < frame->height; i++) {
    // 目标地址: buffer + i * width (紧凑排列)
    // 源地址:   frame->data[0] + i * linesize[0] (带 Padding 的排列)
    // 拷贝长度: width (只拷贝有效像素)
    memcpy(buffer + i * frame->width, 
           frame->data[0] + i * frame->linesize[0], 
           frame->width);
}
```

### 总结
本讲义带你走通了 FFmpeg 最核心的数据流转链路：**文件流 -> AVFormatContext -> AVPacket -> AVCodecContext -> AVFrame -> 裸数据处理**。
掌握了引用计数的内存管理、Send/Receive 解码模型以及 Timebase 转换，你就已经跨过了 FFmpeg 最陡峭的学习曲线。下一步，你可以尝试将这段代码封装为 C++ 的类，并引入多线程队列来实现并发解码。