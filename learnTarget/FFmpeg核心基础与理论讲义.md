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

    *   **其他常见 YUV/像素格式及使用场景**：
        *   **NV12 (YUV420SP)**：`SP` 代表 Semi-Planar（半平面格式）。属于 4:2:0 采样，数据量同样是 1.5 字节/像素。Y 分量连续存放，而 U 和 V 分量交错存放（UVUVUV...）。
            *   **内存分布图解（以 4x4 像素的图像为例）**：
                总大小：16 (Y) + 8 (UV交错) = 24 字节。

                ```text
                [ Y 平面 (Plane 0) - 连续 16 字节 ]
                Y00 Y01 Y02 Y03
                Y10 Y11 Y12 Y13
                Y20 Y21 Y22 Y23
                Y30 Y31 Y32 Y33

                [ UV 平面 (Plane 1) - 交错连续 8 字节 ]
                U00 V00 U01 V01
                U10 V10 U11 V11
                ```
                在 FFmpeg 中：`frame->data[0]` 指向 Y，`frame->data[1]` 指向 UV 交错平面。
            *   **使用场景**：**硬件编解码的“宠儿”**。iOS 的 VideoToolbox、Intel 的 QSV、NVIDIA 的 NVENC 等硬件加速框架默认或强烈推荐使用 NV12。在移动端和 PC 端的硬解硬编中极为常见。
            *   **为什么硬件编解码偏爱 NV12？**
                1.  **内存访问效率（Cache 友好）**：在 YUV420P 中，U 和 V 完全分离，GPU/硬件在处理像素时需要从三个不同的内存地址读取数据。而 NV12 将 UV 交错存放，硬件只需两次内存读取（一次读 Y，一次读 UV），大大提高了内存带宽利用率和 Cache 命中率。
                2.  **内存对齐与跨距（Stride）一致性**：在 NV12 中，UV 平面虽然高度只有 Y 的一半，但因为 U 和 V 交错存放，其一行的字节数（Width/2 * 2 = Width）正好与 Y 平面一行的字节数完全相等。这使得硬件 DMA 在搬运数据时，Y 平面和 UV 平面可以使用相同的跨距（Stride/Linesize），极大简化了硬件电路和驱动的设计。

        *   **NV21 (YUV420SP)**：与 NV12 类似，只是 UV 交错的顺序反了（VUVUVU...）。
            *   **内存分布图解（以 4x4 像素的图像为例）**：

                ```text
                [ Y 平面 (Plane 0) - 连续 16 字节 ]
                Y00 Y01 Y02 Y03
                Y10 Y11 Y12 Y13
                ... (同 NV12)

                [ VU 平面 (Plane 1) - 交错连续 8 字节 ]
                V00 U00 V01 U01
                V10 U10 V11 U11
                ```
            *   **使用场景**：**Android 相机的标准输出格式**。在 Android 开发中处理 Camera 预览原始数据时，最常遇到的就是 NV21。

        *   **YUV422P**：属于 4:2:2 采样，每 2 个 Y 共用 1 个 U 和 1 个 V。数据量是 2 字节/像素。
            *   **与 YUV420P 的核心区别**：
                *   **采样方式**：YUV420P 是在水平和垂直方向上都进行了色度减半（4个Y共用1组UV）；而 YUV422P **只在水平方向上色度减半，垂直方向不减半**（2个Y共用1组UV）。
                *   **数据量**：YUV420P 中 U 和 V 的高度是 Y 的一半；而 YUV422P 中 U 和 V 的**高度与 Y 相同**，只有宽度是 Y 的一半。
            *   **内存分布图解（以 4x4 像素的图像为例）**：
                总大小：16 (Y) + 8 (U) + 8 (V) = 32 字节。

                ```text
                [ Y 平面 (Plane 0) - 连续 16 字节 ]
                Y00 Y01 Y02 Y03
                Y10 Y11 Y12 Y13
                Y20 Y21 Y22 Y23
                Y30 Y31 Y32 Y33

                [ U 平面 (Plane 1) - 连续 8 字节 ]
                U00 U01  <-- 注意：高度与Y一致，有4行！
                U10 U11
                U20 U21
                U30 U31

                [ V 平面 (Plane 2) - 连续 8 字节 ]
                V00 V01
                V10 V11
                V20 V21
                V30 V31
                ```
            *   **使用场景**：**专业视频制作与广播电视**。比 YUV420 保留了更多的色彩细节，常用于非线性编辑（NLE）、高质量视频传输和后期特效制作。

        *   **YUYV422 / UYVY422 (Packed 格式)**：同样是 4:2:2 采样，但是是 Packed（打包/交错）格式，所有分量存在同一个数组中。
            *   **内存分布图解（以 4x4 像素的图像为例，YUYV422）**：
                总大小：32 字节（单平面）。

                ```text
                [ 唯一平面 (Plane 0) - 连续 32 字节 ]
                Y00 U00 Y01 V00  Y02 U01 Y03 V01
                Y10 U10 Y11 V10  Y12 U11 Y13 V11
                Y20 U20 Y21 V20  Y22 U21 Y23 V21
                Y30 U30 Y31 V30  Y32 U31 Y33 V31
                ```
                在 FFmpeg 中：只有 `frame->data[0]` 有效。
            *   **使用场景**：**USB 摄像头（UVC 设备）的常见输出格式**。很多免驱摄像头默认输出这种格式的裸流，使用 FFmpeg (`dshow` 或 `v4l2`) 采集摄像头时经常会遇到。

        *   **YUV444P**：没有色度子采样（Chroma Subsampling），每个 Y 对应独立的 U 和 V。数据量是 3 字节/像素（与 RGB24 相同）。
            *   **内存分布图解（以 4x4 像素的图像为例）**：
                总大小：16 (Y) + 16 (U) + 16 (V) = 48 字节。

                ```text
                [ Y 平面 (Plane 0) - 16 字节 ]
                Y00 Y01 Y02 Y03 ...
                [ U 平面 (Plane 1) - 16 字节 ]
                U00 U01 U02 U03 ...
                [ V 平面 (Plane 2) - 16 字节 ]
                V00 V01 V02 V03 ...
                ```
            *   **使用场景**：**屏幕录制、无损视频、高质量图像处理**。在 4:2:0 下，红底蓝字或精细线条等高频边缘会出现色彩模糊（色度溢出），此时必须使用 4:4:4 格式来保证极致的清晰度。

        *   **RGB24 / RGBA / BGRA**：红绿蓝三原色交错存放（Packed 格式）。
            *   **内存分布图解（以 4x4 像素的图像为例，RGB24）**：
                总大小：16 * 3 = 48 字节（单平面）。

                ```text
                [ 唯一平面 (Plane 0) - 连续 48 字节 ]
                R00 G00 B00  R01 G01 B01  R02 G02 B02  R03 G03 B03
                R10 G10 B10  R11 G11 B11  R12 G12 B12  R13 G13 B13
                ...
                ```
            *   **使用场景**：**最终渲染与图像算法**。无论是 OpenGL、Metal 还是 SDL，最终交给 GPU 渲染上屏的数据通常需要是 RGB/RGBA 格式。此外，OpenCV 等图像处理库也默认使用 BGR/RGB 格式。因此在播放器末端或 AI 算法处理前，通常需要使用 `libswscale` 等工具做 `YUV <-> RGB` 的色彩空间转换（Color Space Conversion）。

### 1.2 音频基础：PCM 裸流
声音是模拟信号，计算机处理需要将其数字化，这个过程叫 PCM（脉冲编码调制）。
*   **采样率（Sample Rate）**：每秒采样的次数，如 44100 Hz（CD音质）、48000 Hz。
    *   *面试考点：为什么 CD 音质的采样率是 44100 Hz？* 根据**奈奎斯特-香农采样定理（Nyquist-Shannon sampling theorem）**，为了不失真地恢复模拟信号，采样频率必须大于被采样信号最高频率的两倍。人耳能听到的最高频率大约是 20000 Hz，所以采样率至少需要 40000 Hz。44100 Hz 是早期录像带存储数字音频时妥协计算出的一个标准值，后来被 CD 沿用。
*   **位深（Bit Depth / Sample Format）**：每次采样用多少位表示，如 16 bit（2 字节）、32 bit 浮点数。位深决定了声音的动态范围（信噪比），位深越大，声音的细节越丰富，底噪越小。
    *   *面试考点：FFmpeg 中的 Packed 与 Planar 音频格式有什么区别？* 这是一个极其高频的坑点。
        *   **Packed（交错格式）**：双声道数据交错存放，如 `L R L R L R...`。在 FFmpeg 中对应 `AV_SAMPLE_FMT_S16`。此时所有数据都在 `frame->data[0]` 中。
        *   **Planar（平面格式）**：双声道数据分开存放，如 `L L L...` 和 `R R R...`。在 FFmpeg 中带有 `P` 后缀，如 `AV_SAMPLE_FMT_S16P`。此时左声道在 `frame->data[0]`，右声道在 `frame->data[1]`。
    *   *坑点与底层原因*：为什么编码器（如 AAC）偏爱 Planar，而播放器/硬件（如 SDL）偏爱 Packed？
        *   **编码器偏爱 Planar 的原因**：音频编码（如 AAC、MP3）的核心算法通常是基于**单声道**进行频域变换（如 MDCT）和心理声学模型分析的。如果数据是 Planar 格式（LLLL... RRRR...），编码器可以直接将左声道的连续内存块送入算法处理，再处理右声道，最后计算声道间的相关性（如联合立体声编码）。如果是 Packed 格式，编码器还得先自己做一次解交错，降低了效率。
        *   **播放器/硬件偏爱 Packed 的原因**：声卡硬件（DAC 数模转换器）在播放声音时，是按照时间顺序**同时**驱动左右喇叭发声的。在极短的一个采样周期内，硬件需要同时拿到左声道和右声道的样本（即 L 和 R 紧挨着）。Packed 格式（L R L R...）天然契合硬件的这种时序读取需求，硬件通过 DMA 顺序读取内存即可直接播放。如果是 Planar 格式，硬件或驱动就得维护两个指针，在两个相距很远的内存区域来回跳跃读取，这对硬件设计极不友好。
*   **声道数（Channels / Channel Layout）**：单声道（1）、立体声（2）、5.1环绕声（6）。
*   **数据量计算**：1秒钟 44.1kHz、16bit、双声道的 PCM 数据大小 = 44100 * (16/8) * 2 = 176,400 字节。
    *   *面试考点：如何计算一帧音频（如 AAC）包含多少 PCM 数据？它的播放时长是多少？*
        *   音频与视频不同，视频一帧就是一张画面；而音频一帧包含**多个采样点（Samples）**。
        *   对于 AAC 编码，一帧固定包含 **1024 个采样点**（每个声道）。
        *   **数据量**：解码后一帧 PCM 的大小 = 1024 * (16/8) * 2 = 4096 字节。
        *   **播放时长**：一帧的播放时间 = 采样点数 / 采样率 = 1024 / 44100 ≈ 0.0232 秒（23.2 毫秒）。这个概念在做音视频同步（计算音频 PTS）时至关重要。

### 1.3 视频编码基础：H.264 与 GOP
未经压缩的 YUV 数据极其庞大，必须进行编码（压缩）。H.264 是目前最主流的编码标准。
*   **帧类型**：
    *   **I 帧（关键帧/内部编码帧/Intra-coded picture）**：自带全部画面信息，可独立解码。压缩率最低。
        *   *面试考点：IDR 帧与普通 I 帧的本质区别是什么？一般在代码/码流中怎么区分？*
            *   **本质区别**：
                *   **IDR 帧（Instantaneous Decoding Refresh，即时解码刷新帧）** 是特殊的 I 帧。它的核心作用是**强行切断参考链**。当解码器遇到 IDR 帧时，会立即清空参考帧队列（DPB, Decoded Picture Buffer）。这意味着：**IDR 帧之后的任何帧，都绝对不可能跨过该 IDR 帧去参考它之前的帧**。
                *   **普通 I 帧** 只是自身独立编码，不参考其他帧。但它**不会清空参考帧队列**。这意味着：在普通 I 帧之后的 P 帧或 B 帧，**有可能跨过这个普通 I 帧，去参考普通 I 帧之前的画面**。
                *   *结论*：视频的第一个帧必须是 IDR 帧；如果从视频中间的某个 IDR 帧开始解码，画面一定能正常播放（因为没有前向依赖）；但如果从中间的普通 I 帧开始解码，由于其后的帧可能依赖该 I 帧之前的帧（而这些帧没被解码），就会导致花屏。
            *   **如何区分（代码/码流层面）**：
                *   **在 H.264 裸流中（通过 NALU Type 区分）**：
                    解析 NALU 的第一个字节（Header），提取低 5 位即为 `nal_unit_type`。
                    *   `nal_unit_type == 5`：表示这是 IDR 帧的切片数据。
                    *   `nal_unit_type == 1`：表示这是非 IDR 帧的切片数据（可能是普通 I 帧、P 帧或 B 帧，需要进一步解析 Slice Header 才能确定是不是普通 I 帧）。
                *   **在 FFmpeg API 中**：
                    通过 `AVPacket` 的 `flags` 字段判断：
                    *   如果 `(pkt->flags & AV_PKT_FLAG_KEY)` 为真，在 H.264 中通常代表这是一个 IDR 帧（FFmpeg 的 Keyframe 概念在 H.264 中基本等同于 IDR 帧）。
                    通过 `AVFrame` 判断：
                    *   `frame->pict_type == AV_PICTURE_TYPE_I` 表示这是一个 I 帧（包含 IDR 和普通 I 帧）。
                    *   `frame->key_frame == 1` 表示这是一个关键帧（通常对应 IDR）。
    *   **P 帧（前向预测帧/Predictive-coded picture）**：参考前面的 I 帧或 P 帧，只存储差异数据。
    *   **B 帧（双向预测帧/Bi-predictive-coded picture）**：参考前面和后面的帧，压缩率最高，但**会引入解码延迟**（必须等后面的帧到达才能解码）。
        *   *面试考点：为什么实时互动（如 WebRTC/视频会议）通常会关闭 B 帧？* 因为 B 帧需要依赖未来的帧进行解码，这会强制引入缓冲延迟（算法延迟），违背了实时通信对极低延迟（<400ms）的苛刻要求。
*   **GOP（Group of Pictures）**：两个 I 帧（通常指 IDR 帧）之间的距离。
    *   *面试考点：直播场景下 GOP 设置多大合适？为什么？* 直播场景通常设置较小的 GOP（如 1-2 秒，即帧率的 1-2 倍）。原因有二：1. **秒开体验**：播放器必须拿到 I 帧才能开始解码画面，GOP 越小，观众进房时等待 I 帧的时间越短；2. **抗弱网**：发生丢包时，画面会花屏，直到下一个 I 帧到来才能恢复，小 GOP 能更快刷新画面。点播场景则可以设置较大的 GOP（如 5-10 秒）以提高压缩率节省带宽。
*   **NALU（Network Abstraction Layer Unit）**：H.264 码流的基本单元，包含 StartCode（`00 00 00 01` 或 `00 00 01`）和实际数据。
    *   *面试考点：H.264 码流中除了音视频数据还有什么关键 NALU？* 必须知道 **SPS（Sequence Parameter Set，序列参数集）** 和 **PPS（Picture Parameter Set，图像参数集）**。SPS 包含视频的宽、高、帧率、Profile/Level 等全局信息；PPS 包含熵编码模式、切片组等图像级信息。解码器如果没有拿到 SPS 和 PPS，即使拿到 I 帧也无法解码。在直播推流时，通常每个 IDR 帧前面都要附带 SPS 和 PPS。

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