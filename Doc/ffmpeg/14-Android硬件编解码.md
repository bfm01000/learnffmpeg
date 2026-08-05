# 14 - Android 硬件编解码深入（MediaCodec / AMediaCodec）

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | Android 硬件编解码专题：MediaCodec 状态机、CSD、颜色格式、Surface 零拷贝。 |
| 先背什么 | OMX/Codec2、硬软 codec 区分、CSD、stride/color format、Surface 输出是重点。 |
| 深入怎么学 | 从 buffer 模式和 Surface 模式两条链路理解解码、编码、渲染。 |
| 关联阅读 | 10、25 |

---

> 这是 [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 的 **Android 专题深挖篇**。10 是移动端总览（Android + iOS 一篇讲完），本篇把 Android 这一侧拆开、讲透、做成面试导向——目标是读完能应对面试里关于 Android 硬编硬解的绝大部分问题。
> 本篇覆盖：MediaCodec 在 Android 媒体栈里的位置、OMX → Codec2 演进、硬件 codec vs 软件 codec 怎么区分与选择、Java `MediaCodec` vs NDK `AMediaCodec`、缓冲区队列模型与生命周期状态机、CSD（SPS/PPS）、颜色格式坑、Surface 零拷贝、编码参数与运行时控制、FFmpeg 在 Android 的支持边界、和 iOS / NVENC 的横向对比、Android 特有陷阱、面试高频问答、学习路径。
> 前置：先读 [07-硬件编解码.md](./07-硬件编解码.md) 建立"硬件帧 vs 软件帧"的核心直觉；本篇频繁引用 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) 的 Annex-B / SPS/PPS、[02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 的 NV12 / stride、[06-编码参数与码控.md](./06-编码参数与码控.md) 的 GOP / CBR / 低延迟。零拷贝底层原理在 [10 §四点五](./10-移动端硬件编解码.md#四点五ios-与-android-的零拷贝核心考点) 有总览，本篇 §7.6 做了 gralloc/dma-buf/fence 三层协作的专题深挖（与 UMA 慢因、iOS IOSurface 的对比仍在 10）。
> **进阶**：弱网恢复（LTR / Intra-Refresh）、一对多分发（Simulcast / SVC）、画质量化（VMAF / BD-rate）、容量规划、安全解码（Widevine L1）等**跨平台高级考点**在 [16-硬件编解码高级专题.md](./16-硬件编解码高级专题.md)——冲高级岗必读。

---

## 〇、面试速答模板（口语化，开口就能用）

> 先放这一节给临场用——"张嘴就能说"的完整话术，练顺嘴。文末 §十三 是同样问题的要点速记版，正文各节是展开。两套配合：这里练话，那里背词。

**Q：先讲讲 MediaCodec 到底是什么？硬解软解怎么区分？**

> MediaCodec 是 Android 给 App 的**统一编解码门面**——你永远调它这一个类，但它自己不干活，只是把请求转交给底下一个具体的 codec 组件。这个组件可能是 SoC 里的硬件编解码单元，也可能是跑在 CPU 上的软件实现，**同一套 API、同一套缓冲区模型，硬解软解长得一模一样**。这跟桌面不一样——桌面你显式选 `h264_nvenc`（硬）还是 `libx264`（软），Android 是看你**选了哪个组件名**：`OMX.google.*` / `c2.android.*` 是软件，`OMX.qcom.*` / `c2.qti.*` / `OMX.MTK.*` 这种厂商前缀是硬件。API 29 以上还能直接用 `isHardwareAccelerated()` 判断。

**Q：MediaCodec 的缓冲区模型是怎么工作的？同步还是异步？**

> 心智模型就是**两条缓冲区队列**：你从输入队列要一个空 buffer、填数据、还回去；硬件处理完，你从输出队列拿处理好的 buffer、用完再还回去。驱动方式有两种：同步模式自己拿 timeout 轮询 dequeue，好理解但容易写出忙等；异步模式 `setCallback` 注册回调，buffer 可用时系统通知你，没有忙等、更适合生产环境——但 setCallback 必须在 configure 之前调。**有一条铁律**：output buffer 用完必须 `releaseOutputBuffer` 还回去，因为 buffer 总数固定、在两条队列间轮转，你拿了不还，池子很快耗尽，编解码就卡死了——跟 AVFrame 引用计数是同一个"借了要还"的道理。

**Q：OMX 和 Codec2 是什么？为什么 Android 10 要换？**

> 这是门面**底下接硬件那一层**的换代。Android 9 及以前用 OMX，是从 Khronos 沿用下来的老标准，它的 buffer 模型跟 Android 图形栈（gralloc / dma-buf）衔接得很别扭、厂商扩展又乱、早期还跑在 mediaserver 进程里一崩全崩。Android 10 引入的 Codec2 是 Google 自己重写的，**原生围绕 dma-buf 设计、零拷贝衔接顺、跑在独立沙箱进程里崩了也不拖垮系统**。对 App 是透明的——`MediaCodec` 这个门面 API 没变，只是组件名前缀从 `OMX.*` 变成了 `c2.*`。

**Q：Android 解码花屏，最常见是什么原因？**

> 十有八九是 **ByteBuffer 模式下的颜色格式 / stride 坑**。蹲两点：**第一，YUV 颜色格式因芯片而异**，有 I420、NV12、厂商私有 tiled，拿 NV12 的方式去读 tiled 数据直接马赛克；**第二，就算格式对了，每行有对齐填充，stride ≠ width**，按 width 紧凑拷贝必然错位花屏。

下面把这题拆成三层：先看**错在哪儿**，再看**怎么正确读**，最后说**为什么 Surface 一了百了**。

---

**第一层：最常见的错误写法——为什么花屏**

很多人拿到解码 output buffer 后的直觉做法：

```java
// ❌ 错误：把 ByteBuffer 当紧凑 YUV 直读
int outputBufferIndex = codec.dequeueOutputBuffer(info, TIMEOUT_US);
if (outputBufferIndex >= 0) {
    ByteBuffer outputBuffer = codec.getOutputBuffer(outputBufferIndex);

    // 以为 Y 分量就是 width × height 字节，紧挨着排
    int ySize = width * height;
    byte[] yuv = new byte[ySize * 3 / 2];  // 假设 NV12，总大小按 width*height*1.5
    outputBuffer.get(yuv);                  // 整块读出来
    // → 画面右半边偏绿、底部错位、或者整屏马赛克
}
```

这段代码在 Pixel 上可能正常，换台小米/OV 直接花。原因有三个，层层递进：

**(a) stride ≠ width**：硬件要求每行对齐到 16/32/64/128 字节。假如 width=1920，硬件 stride 可能是 2048（对齐 128）。那 `outputBuffer` 里实际每行是 2048 字节的 Y 数据，只有前 1920 是有效像素，后 128 是填充（garbage）。你按 width=1920 紧凑读，第一行 Y 读对了，第二行开头就错位了 128 字节，第一行垃圾被当成了第二行开头——画面右半边出现绿条、底部像"电视信号不好一样斜着跑"。

```
  row 0: [ Y0 Y1 ... Y1919 | pad pad ... pad ]  ← stride=2048, width=1920
  row 1: [ Y1920 Y1921 ...                    ]
  ^^^^^^^ compact reader 在 row 0 第 1921 字节开始读 row 1
          实际拿到的是 row 0 的 padding 垃圾
```

**(b) slice-height ≠ height**：Y 平面和 UV 平面之间也有对齐间隔。Y 数据不是到 `width * height` 结束，而是到 `stride * sliceHeight`。UV 的起始偏移 = `stride * sliceHeight`（sliceHeight 常对齐到 16 的倍数）。假设 height=1080，sliceHeight=1088——你按 `width * height` 计算 UV 起点，位置就偏了 2048×8 字节，UV 全读错。

**(c) 私有 tiled/swizzled 格式**：某些高通芯片输出不是线性 YUV，而是 GPU 纹理最优化的分块（tiled）排布——数据按 32×32 等小块重排过。**这种情况你 memcpy 出来连一个像素都读不对**，画面是规则的大块马赛克。tiled 格式无法用任何 memcpy 修正——它不是 stride 问题，是数据排布逻辑根本不同。

---

**第二层：正确做法——Java `Image` / NDK `AImage`**

不要直接从 ByteBuffer 读裸字节，用系统给的 `Image` 抽象，它帮你处理了 stride 和 pixelStride：

```java
// ✅ 正确：用 Image 按 rowStride/pixelStride 逐行读
int outputBufferIndex = codec.dequeueOutputBuffer(info, TIMEOUT_US);
if (outputBufferIndex >= 0) {
    Image image = codec.getOutputImage(outputBufferIndex);  // API 21+
    if (image != null) {
        Image.Plane[] planes = image.getPlanes();

        // -------- Y 平面 --------
        Image.Plane yPlane = planes[0];
        ByteBuffer yBuffer = yPlane.getBuffer();
        int yRowStride   = yPlane.getRowStride();    // 每行字节数（含 padding）
        int yPixelStride = yPlane.getPixelStride();  // Y 都是 1

        // -------- UV 平面（NV12 为交织，I420 为两个独立平面）-----
        Image.Plane uPlane = planes[1];
        ByteBuffer uBuffer = uPlane.getBuffer();
        int uRowStride   = uPlane.getRowStride();
        int uPixelStride = uPlane.getPixelStride();  // NV12 为 2（U,V 交替）

        Image.Plane vPlane = (planes.length >= 3) ? planes[2] : null;
        // I420: planes[1]=U, planes[2]=V, pixelStride 都是 1
        // NV12: planes[1]=UV 交织, planes[2]=null, pixelStride=2

        // 按行拷贝：每行只拷贝有效像素，跳过 padding
        for (int row = 0; row < height; row++) {
            yBuffer.position(row * yRowStride);  // 定位到当前行起点
            yBuffer.get(dstY, row * width, width); // 只读 width 有效字节
        }
        int uvHeight = height / 2;  // YUV420
        for (int row = 0; row < uvHeight; row++) {
            uBuffer.position(row * uRowStride);
            uBuffer.get(dstUV, row * width, width); // pixelStride=2: NV12 一次拿 U+V
        }

        image.close();  // 等价于 releaseOutputBuffer，必须关
    }
    codec.releaseOutputBuffer(outputBufferIndex, false);
}
```

**关键点**：`rowStride` 告诉你"这一行的数据在 buffer 里占多宽"，只取前 `width` 字节就避开了 padding。`pixelStride` 告诉你相邻像素的同一分量相隔多少字节——Y 永远是 1（逐像素排），NV12 的 UV 是 2（U 和 V 交替排，读一个 `int16` 就是 UV 一对）。

**NDK / C++ 侧**用 `AImage`，同一套逻辑：

```c
// ✅ NDK: AImageReader 接解码 Surface，从 AImage 读
media_status_t status;
AImage *image = nullptr;
status = AImageReader_acquireNextImage(reader, TIMEOUT_US, &image);

int32_t numPlanes;
AImage_getNumberOfPlanes(image, &numPlanes);  // NV12=2, I420=3

uint8_t *yDst = ...;  // 你的目标 buffer
for (int i = 0; i < numPlanes; i++) {
    uint8_t *data;
    int dataLen, rowStride, pixelStride;
    AImage_getPlaneData(image, i, &data, &dataLen);
    AImage_getPlaneRowStride(image, i, &rowStride);
    AImage_getPlanePixelStride(image, i, &pixelStride);

    int planeH = (i == 0) ? height : height / 2;  // Y 还是 UV
    for (int row = 0; row < planeH; row++) {
        memcpy(yDst + row * width,           // 目标：紧凑排布
               data + row * rowStride,        // 源：含 padding
               width * pixelStride);          // 有效字节
    }
}
AImage_delete(image);
```

> **临时救急**——如果实在要用旧 API 的 ByteBuffer（API < 21）：在 `INFO_OUTPUT_FORMAT_CHANGED` 之后从 `MediaFormat` 读 `KEY_STRIDE` / `KEY_SLICE_HEIGHT` / `COLOR_Format`，手动按平台处理。但这极其容易翻车，tiled 格式你根本没法手动处理。**优先升级到 Image。**

---

**第三层：治本——能用 Surface 就别用 ByteBuffer**

上面的代码写了二三十行，只为了把像素从 GPU 拷出来——而且每帧都在拷，功耗和延迟都在烧。更好的思路是：**多数场景根本不需要碰像素**。

```java
// ✅✅ 最佳：解码输出直接进 Surface，一行像素拷贝都没有
codec.configure(format, surface, null, 0);            // 传 Surface
codec.start();

// 解码循环：
int outputBufferIndex = codec.dequeueOutputBuffer(info, TIMEOUT_US);
codec.releaseOutputBuffer(outputBufferIndex, true);   // true = render to Surface
// 不需要 Image、不需要 memcpy、不需要 rowStride——你甚至不碰 ByteBuffer
```

解码结果由 GPU 直接渲染到 Surface——走的是 gralloc/dma-buf/fence 这套零拷贝栈（见 [10 §四点五](./10-移动端硬件编解码.md#四点五ios-与-android-的零拷贝核心考点)）。颜色格式、stride、tiling 这些坑**全部被系统吞掉了**，因为数据根本没离开 GPU。

什么场景非用 ByteBuffer 不可？只有**确需 CPU 访问像素**的少数情况——比如把某一帧存成 JPEG 截图、喂给 AI 推理模型、或者往非标准格式转码。其余场景（播放、推流、后处理进 GL/Metal 管线）一律用 Surface。

> **一句话结论**：面试时先说"ByteBuffer 颜色格式 / stride 碎片化是主因"，然后立刻补"但生产环境最好的修复是不修——直接切到 Surface 模式，把坑甩给系统"。这既展示了你踩过坑，又展示了你懂什么才是省力又正确的架构选择。

**Q：Surface 模式为什么能零拷贝？**

> 因为数据**全程不下到 CPU**。解码时 configure 传一个 Surface 进去，`releaseOutputBuffer(idx, true)` 第二个参数给 true，解码结果直接渲染到 Surface；要做后处理就接 SurfaceTexture，变成 OpenGL 的一张 OES 外部纹理。编码方向用 `createInputSurface()` 拿一个输入 Surface，让相机或 GL 渲染的画面直接把 GPU 纹理喂给编码器，省掉 YUV 回读 CPU 那一大笔开销。底层是 BufferQueue + gralloc/dma-buf + fence 那套（细节在 10 §4.5）。这也是移动端高性能采集编码的标准姿势。

**Q：SPS/PPS 怎么喂给解码器？**

> Android 这边叫 **CSD（Codec-Specific Data）**，通过 MediaFormat 的 csd key 喂：H.264 是 csd-0 放 SPS、csd-1 放 PPS；HEVC 是 csd-0 把 VPS+SPS+PPS 拼一块。如果你用 MediaExtractor 拆 MP4，trackFormat 里已经带好 csd 了，直接 configure 就行。编码方向反过来——编码器产出的第一个 buffer 会带 `BUFFER_FLAG_CODEC_CONFIG` 标志，那就是 CSD，要单独存好，别当普通画面帧写进流里。

**Q：Android 的比特流是 Annex-B 还是 AVCC？和 iOS 有什么区别？**

> Android MediaCodec 喂入和吐出**都是 Annex-B**，也就是带 `00 00 00 01` 起始码、SPS/PPS 作为普通 NALU 内联在流里。**iOS 的 VideoToolbox 正好相反，是 AVCC**——长度前缀，而且 SPS/PPS 单独存在 format description 里。所以一个很现实的坑：Android 推 RTP 基本不用转（本来就是 Annex-B），但 iOS 推流必须做 AVCC→Annex-B 转换。写跨平台代码时两端的比特流处理逻辑正好相反，最容易翻车。

**Q：FFmpeg 在 Android 上能硬件编码吗？**

> **基本不能指望。** 严格说，FFmpeg 6.0（2023）起加入了 `h264_mediacodec` / `hevc_mediacodec` **编码器**，但它出现晚、能力和可控性都很有限（API level 限制、低版本设备输出异常、参数远不如直调灵活），**生产环境的 Android 硬编基本还是绕过 FFmpeg 直接调 MediaCodec / AMediaCodec**（主流做法）。退一步用 FFmpeg 软编 libx264，功耗发热又扛不住，只能离线用。现实架构是**混合**的：FFmpeg 干它擅长的封装、协议、音频重采样，视频硬编解直接调系统 API。

**Q：编码器跑起来之后还能动态改码率、强制关键帧吗？**

> 能，而且这正是 WebRTC 在手机上必须用的两个旋钮。动态降码率用 `setParameters` + `PARAMETER_KEY_VIDEO_BITRATE`，对应拥塞控制（GCC）发现网络变差了要降码率；强制立即产一个 IDR 用 `PARAMETER_KEY_REQUEST_SYNC_FRAME`，对应对端发来 PLI/FIR 说"我花屏了，给我个关键帧"。没有这俩，WebRTC 在弱网下既不能自适应、也没法快速从花屏恢复。

---

## 一、MediaCodec 在 Android 媒体栈里的位置

学 Android 硬编解的第一件事不是背 API，而是先建立一张"我调的这个 `MediaCodec`，底下到底是谁在干活"的地图。否则后面所有的"为什么回退软解""为什么换台手机就花屏"都无从下手。

### 1.1 一张分层图

```
   App (你的代码 / ExoPlayer / WebRTC)
        |  android.media.MediaCodec (Java/Kotlin)
        |  AMediaCodec               (NDK / C)
        v
   +============================+
   |   MediaCodec  (门面/Facade)  |   <- 统一入口,本身不编码
   +============================+
        |
        v   (framework 内部)
   ACodec (走 OMX)   或   CCodec (走 Codec2)   <- 两代实现,见 §二
        |                      |
        v                      v
   OMX IL HAL  (Android 9-)   Codec2 HAL (Android 10+)
        |                      |
        +----------+-----------+
                   v
        厂商 codec 组件 (component)
        硬件: OMX.qcom.* / OMX.MTK.* / c2.qti.* / c2.mtk.* ...
        软件: OMX.google.* / c2.android.*
                   |
                   v
        SoC 里的硬件编解码单元 (高通 Adreno/Venus、联发科、三星、海思)
        或  CPU 上的软件实现 (软 codec 时)
```

一句话本质:

> **`MediaCodec` 是一个统一门面（Facade）。它本身不做编码，只负责把你的请求转交给底层一个具体的 codec 组件。这个组件既可能是 SoC 里的硬件编解码单元，也可能是跑在 CPU 上的软件实现——同一套 API、同一套缓冲区模型，硬解软解长得一模一样。**

这一点是 Android 和桌面最大的认知差别：在桌面你显式选 `h264_nvenc`（硬）还是 `libx264`（软）；在 Android 你永远调 `MediaCodec`，到底落到硬件还是软件，取决于你**选了哪个 codec 组件名**（§三）。

### 1.2 类比：点外卖

把 `MediaCodec` 想成一个外卖平台 App：

- 你（App）只跟外卖平台（`MediaCodec` 门面）打交道——下单、取餐，接口统一。
- 平台把单子派给某家餐厅（codec 组件）。可能是"米其林后厨"（厂商硬件 codec，又快又省电），也可能是"平台自营的标准化中央厨房"（`c2.android.*` 软件 codec，哪都能开但慢）。
- 你不用关心后厨是燃气灶还是电磁炉（OMX 还是 Codec2），那是平台内部的事。

但你**可以指定餐厅**（`createByCodecName`），也可以让平台**按口味自动派单**（`createDecoderByType` + 能力查询）。这正是 §三要讲的"怎么确保派到硬件后厨"。

---

## 二、OMX vs Codec2（重要演进，面试常问）

`MediaCodec` 这个门面从 Android 4.1（API 16）就有了，但门面**底下接 HAL 的那层**经历了一次大换代。这是面试高频题。

### 2.1 两代对照

| 维度 | OMX IL（Android 9 及以前） | Codec2（Android 10+，逐步成为主力） |
|---|---|---|
| 全称 | OpenMAX Integration Layer | Codec2.0（Google 自研） |
| framework 内部组件 | ACodec | CCodec |
| 来历 | Khronos 的跨厂商老标准，2010 年代沿用至今 | Google 为解决 OMX 历史包袱重写的现代框架 |
| buffer 管理 | 基于 OMX buffer header，和 gralloc/dma-buf 衔接别扭 | 原生围绕 **`C2Buffer` / dma-buf** 设计，零拷贝衔接顺 |
| HAL 进程模型 | 早期在 mediaserver 进程内 | 跑在独立的 `media.codec` / `mediacodec` 进程（Treble 隔离，崩溃不拖垮系统） |
| 扩展参数 | OMX index，厂商各自扩展、混乱 | `C2Param` 体系，更规整 |
| 现状 | 仍兼容存在（老组件、老设备） | Android 10+ 新设备的默认通路 |

### 2.2 为什么要换（讲清楚演进动机就能拿分）

OMX 是 2010 年前后从 Khronos 搬来的跨厂商标准，背了一堆历史包袱：

- **buffer 模型和 Android 图形栈（gralloc / dma-buf）不是一套语言**，做零拷贝要各种胶水和私有扩展，每家厂商实现得还不一样——这是 Android 碎片化的一大来源。
- **厂商扩展靠 OMX extension index**，杂乱无章、互不兼容。
- 早期还跑在 mediaserver 进程里，一个 codec 崩了能把媒体服务拖垮。

Codec2 是 Google 推倒重来的答卷：**原生围绕 dma-buf / `C2Buffer` 设计**（和 §一那张图里的 HAL 对齐），buffer 在相机/GPU/编解码/显示之间流转天然零拷贝；参数走规整的 `C2Param`；跑在独立沙箱进程里（配合 Project Treble 的 HAL 隔离）。Android 10 起新设备逐步切到 Codec2。

### 2.3 对你写代码有什么影响

**几乎没有**——这正是门面的价值。`MediaCodec` / `AMediaCodec` 的 API 在两代底下完全一样，你的缓冲区队列代码不用改。区别只在:

- 老设备上 `MediaCodecList` 列出的组件名多是 `OMX.*`；新设备上同一个 codec 可能叫 `c2.qti.*` / `c2.mtk.*`（厂商 Codec2 组件）或 `c2.android.*`（Google 软件 Codec2）。
- Codec2 设备上零拷贝路径更稳、HAL 崩溃不拖垮系统。

> 面试答法：**"OMX 是从 Khronos 沿用的老 HAL，buffer 模型和 Android 图形栈衔接差、厂商扩展乱；Android 10 引入自研 Codec2，原生围绕 dma-buf 设计、零拷贝更顺、跑独立沙箱进程。但对 App 透明——`MediaCodec` 门面 API 不变，只是组件名前缀从 `OMX.*` 变成 `c2.*`。"**

---

## 三、硬件 codec vs 软件 codec：怎么区分、怎么选、为什么会回退

§一说了 `MediaCodec` 同一套 API 既能驱动硬件 codec 也能驱动软件 codec。那"我现在用的到底是硬还是软"就成了一个必须能回答的问题——尤其面试和性能排查时。

### 3.1 看名字：codec 组件名前缀规律

最快的判断方式是看组件名前缀。规律是**厂商前缀 = 硬件，google/android 前缀 = 软件**:

| 组件名前缀 | 软/硬 | 说明 |
|---|---|---|
| `OMX.qcom.*` / `c2.qti.*` | 硬件 | 高通（Qualcomm / QTI） |
| `OMX.MTK.*` / `c2.mtk.*` | 硬件 | 联发科（MediaTek） |
| `OMX.Exynos.*` / `c2.exynos.*` | 硬件 | 三星 Exynos |
| `OMX.hisi.*` / `c2.hisi.*` | 硬件 | 海思（华为） |
| `OMX.google.*` | 软件 | Google 软件 codec（OMX 时代） |
| `c2.android.*` | 软件 | Google 软件 codec（Codec2 时代，如 `c2.android.avc.decoder`） |

经验法则:**名字里带 `google` / `android` 的是软件兜底实现；带具体厂商名（qcom/qti/MTK/Exynos/hisi 等）的是硬件。** 但这只是经验,正规判断用下面的 API。

### 3.2 看 API：`isHardwareAccelerated()` / `isSoftwareOnly()`

API 29（Android 10）起 `MediaCodecInfo` 给了官方判据,别再靠猜名字:

```java
for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
    if (!info.isEncoder() && info.getSupportedTypes()[0].equals("video/avc")) {
        boolean isHardware = info.isHardwareAccelerated();  // API 29+: 是不是硬件加速
        boolean isSoftwareOnly = info.isSoftwareOnly();     // API 29+: 是不是纯软件
        boolean isVendor = info.isVendorProvided();         // 是不是厂商提供
        // ...
    }
}
```

API 29 以前没有这几个方法,只能退回 §3.1 的名字前缀启发式。

### 3.3 怎么主动选到硬件 codec

两种创建方式,对应"自动派单"和"指定餐厅":

```java
// 方式一: 按 MIME 让系统挑(通常优先硬件,但不保证)
MediaCodec codec = MediaCodec.createDecoderByType("video/avc");

// 方式二: 按能力查询 + 指定组件名(最可控)
MediaCodecList list = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
String name = list.findDecoderForFormat(format);   // 给定 MediaFormat 找最合适的 codec
MediaCodec codec = MediaCodec.createByCodecName(name);
```

- `createDecoderByType(mime)`:把选择权交给系统,**通常**会优先列表里靠前的硬件 codec,但不是硬保证。
- `MediaCodecList.findDecoderForFormat(format)`:**按完整 `MediaFormat`（分辨率、profile、帧率等）匹配**,再用 `createByCodecName` 精确创建。要可控就走这条。
- 关键:**先用 `MediaFormat` 设全约束（宽高、`KEY_PROFILE`、`KEY_FRAME_RATE`、HDR 等）再查询**,因为同一个硬件 codec 在不同分辨率/profile 下能力不同(见下)。

### 3.4 为什么系统会"回退软解"（高频追问）

明明手机有硬件 codec,为什么有时跑着跑着用的是 `c2.android.*` 软解?常见原因:

| 触发条件 | 为什么回退 |
|---|---|
| **分辨率/帧率超出硬件 Level 上限** | 硬件 codec 标了最高支持的 Profile+Level（见 [06 §4.2](./06-编码参数与码控.md)）,超了（如 8K、120fps）就只能软解 |
| **Profile 不支持** | 硬件不支持某 profile（如某些设备 High 10 / 4:4:4），系统挑能解的软 codec |
| **并发实例超限** | 硬件解码器同时只能开有限路（常 1~几路）,第 N+1 路 `createByCodecName` 失败或被分配软 codec（见 §十二） |
| **codec 名直接选了软的** | 你自己 `createByCodecName("c2.android.avc.decoder")`,那当然是软解 |
| **格式/特性硬件不支持** | 如某些 HDR、特殊色彩、加密(DRM)组合,硬件路径不可用 |

排查口诀:**先 `MediaCodecList` 把设备支持的 codec 和它们的 `CodecCapabilities`（`VideoCapabilities` 的分辨率/帧率范围、`getSupportedProfileLevels()`）打出来,对比你要解码的实际参数,就知道为什么没走硬件。** 健壮的播放器要**主动查询 + 软解兜底**,而不是假设硬件万能。

---

## 四、Java/Kotlin `MediaCodec` vs NDK `AMediaCodec`

Android 给了两套入口,做哪个项目走哪套要心里有数:

| | `android.media.MediaCodec`（Java/Kotlin） | `AMediaCodec`（NDK / C，`media/NdkMediaCodec.h`） |
|---|---|---|
| 语言 | Java / Kotlin | C / C++ |
| 典型使用方 | 绝大多数 App、ExoPlayer | 跨平台播放器（ijkplayer/部分自研）、**libwebrtc native**、C/C++ 引擎 |
| 配套类 | `MediaFormat` / `MediaCodecList` / `MediaExtractor` / `Image` | `AMediaFormat` / `AMediaCodecList`(部分) / `AMediaExtractor` / `AImageReader`+`AImage` |
| 引入版本 | API 16 | API 21（NDK） |

API 名字几乎一一对应,迁移成本低:

| 动作 | Java | NDK C |
|---|---|---|
| 创建 | `createDecoderByType` | `AMediaCodec_createDecoderByType` |
| 配置 | `configure(format, surface, …)` | `AMediaCodec_configure` |
| 启动 | `start()` | `AMediaCodec_start` |
| 要输入 buffer | `dequeueInputBuffer` | `AMediaCodec_dequeueInputBuffer` |
| 提交输入 | `queueInputBuffer` | `AMediaCodec_queueInputBuffer` |
| 取输出 buffer | `dequeueOutputBuffer` | `AMediaCodec_dequeueOutputBuffer` |
| 还输出 buffer | `releaseOutputBuffer` | `AMediaCodec_releaseOutputBuffer` |
| 异步回调 | `setCallback` | `AMediaCodec_setAsyncNotifyCallback` |

> 为什么 WebRTC / 跨平台播放器走 NDK:它们的核心是 C++,不想为了调编解码跨 JNI 来回弹（JNI 调用开销 + 线程附加麻烦）。libwebrtc 的 Android 实现里,`MediaCodecVideoEncoder` / `Decoder` 这层封装就是把 `AMediaCodec`(或经 JNI 的 Java MediaCodec,历史上两种都有过)接进 `VideoEncoderFactory` / `VideoDecoderFactory`（见 §十四、[10 §六](./10-移动端硬件编解码.md)）。CPU 侧取帧则配 `AImageReader`/`AImage`（见 [10 §3.4](./10-移动端硬件编解码.md)）。

---

## 五、核心心智模型：缓冲区队列 + 生命周期状态机

这是 MediaCodec 的灵魂。建立不了这个直觉,后面全是迷糊。10 §3.2 给了缓冲区队列的骨架,这里**补全生命周期状态机**——面试和实战的真正难点在状态切换,不在 happy path。

### 5.1 两条缓冲区队列（先回顾）

```
                 +---------------------+
   待编/解数据 -> | input buffer queue  | -> [codec 组件:硬件单元/软件] ->
                 +---------------------+                                 |
                                                                         v
   已编/解数据 <- | output buffer queue |  <------------------------------+
                 +---------------------+
```

你的循环永远是:**要一个空输入 buffer → 填数据 → 提交 → 取一个满输出 buffer → 消费 → 还回去**。

### 5.2 同步 vs 异步两种驱动方式

**同步模式**（`dequeueInputBuffer` / `dequeueOutputBuffer` 带 timeout 轮询,好理解,易写出忙等）和**异步模式**（`setCallback`,系统在 buffer 可用时回调你,无忙等、推荐生产用）。代码骨架见 [10 §3.2](./10-移动端硬件编解码.md),这里不重复。补两个要点:

- **异步模式必须在 `configure` 之前 `setCallback`**,否则抛异常。
- 异步回调跑在 codec 内部线程/你指定的 Handler 线程上,**别在回调里做重活/阻塞**,否则拖垮整条流水线。

### 5.3 生命周期状态机（重点,比队列更容易考砸）

`MediaCodec` 内部是一个状态机,违规调用直接抛 `IllegalStateException`。完整状态:

```
   created (createByXxx)
      |
      | configure()
      v
  Configured
      |
      | start()
      v
  +-----------------------------  Executing  -----------------------------+
  |                                                                       |
  |   Flushed  --(首个 queueInputBuffer)-->  Running  --(收到 EOS)-->  End-of-Stream
  |     ^                                       |                         |
  |     +-------------- flush() ----------------+-------------------------+
  +-----------------------------------------------------------------------+
      |
      | stop()
      v
  Uninitialized  --(可再次 configure 复用)
      |
      | release()
      v
  Released  (终态,对象作废)
```

把 Executing 拆成三个子态是理解的关键:

| 子状态 | 含义 | 怎么进来 |
|---|---|---|
| **Flushed** | 刚 `start()` 或刚 `flush()` 后,队列干净 | `start()` / `flush()` |
| **Running** | 正常吞吐中 | 提交第一个 input buffer |
| **End-of-Stream** | 已提交带 `BUFFER_FLAG_END_OF_STREAM` 的输入,等把剩余输出吐完 | 提交 EOS flag |

各操作的语义和易错点:

| 方法 | 干什么 | 之后处于 | 易错点 |
|---|---|---|---|
| `configure()` | 设格式/Surface/flags(编码用 `CONFIGURE_FLAG_ENCODE`) | Configured | 只能在 Uninitialized/created 调 |
| `start()` | 分配 buffer、进 Executing | Flushed | 必须先 configure |
| `flush()` | **丢弃两条队列里所有未处理 buffer**,清空状态 | Flushed | flush 后**手里之前 dequeue 到的 index 全部作废**,不能再 `releaseOutputBuffer` 旧 index;解码器 flush 后通常**要重发关键帧(IDR)**才能恢复出图 |
| `stop()` | 回到 Uninitialized,可重新 configure 复用对象 | Uninitialized | 释放了 buffer,但对象还能再 configure |
| `reset()` | 比 stop 更彻底地回到初始,出错恢复用 | Uninitialized | 比 stop+configure 更稳的纠错手段 |
| `release()` | **彻底销毁,释放底层硬件 codec 实例** | Released | **终态**;不 release 会占着稀缺的硬件实例(见 §十二并发) |

### 5.4 releaseOutputBuffer 铁律（呼应 10 §3.2,这里讲全）

> **每个 dequeue 出来的 output buffer,用完必须 `releaseOutputBuffer` 还回去。**

为什么是铁律:output buffer 总数是固定的(就那么几个在两条队列间轮转)。你拿出来不还,池子很快见底,`dequeueOutputBuffer` 一直返回 `INFO_TRY_AGAIN_LATER`,**编解码彻底卡死**。这和 [01-数据结构与生命周期.md](./01-数据结构与生命周期.md) 的 AVFrame 引用计数、[10 §4.5.9](./10-移动端硬件编解码.md) 的 buffer pool 耗尽是同一个"借了要还"的世界观。

`releaseOutputBuffer` 的第二个参数 `render` 决定数据去哪:

- `releaseOutputBuffer(index, false)`:还回去,**丢弃**这帧(ByteBuffer 模式下你已经从 buffer 拷走数据了)。
- `releaseOutputBuffer(index, true)`:还回去,并**渲染到 configure 时传入的 Surface**(零拷贝上屏,见 §八)。

### 5.5 `dequeueOutputBuffer` 的三个特殊返回值（必处理）

解码循环里 `dequeueOutputBuffer` 不是只返回正常 index,还有三个负数哨兵值必须分支处理,漏了就出 bug:

| 返回值 | 含义 | 怎么处理 |
|---|---|---|
| `INFO_TRY_AGAIN_LATER` (-1) | 这次没有可用输出(timeout 内没数据) | 正常,继续循环 |
| `INFO_OUTPUT_FORMAT_CHANGED` (-2) | **输出格式确定/变化**(解码刚拿到 SPS 解析出真实宽高、stride 等) | 用 `getOutputFormat()` 读**真实**的宽高/颜色格式/stride —— ByteBuffer 模式取像素必须以这里为准,不是 configure 时填的 |
| `INFO_OUTPUT_BUFFERS_CHANGED` (-3) | 输出 buffer 数组变了(旧 API) | API 21+ 用 `getOutputBuffer(index)` 就不用管;老代码要重新获取 buffer 数组 |

> `INFO_OUTPUT_FORMAT_CHANGED` 是颜色格式坑(§七)的关键入口:**真实的颜色格式、stride、slice-height 要在收到这个事件后从 `MediaFormat` 读,configure 时你填的宽高只是请求,不代表输出布局。**

---

## 六、CSD（Codec-Specific Data）：SPS/PPS 怎么喂、怎么吐

解码前必须先把参数集交给解码器,否则它不知道分辨率、profile。MediaCodec 把这叫 **CSD（Codec-Specific Data）**,通过 `MediaFormat` 的 `csd-N` key 传递。10 §3.3 给了基础,这里系统化 + 补 HEVC + 编码器吐 CSD。

### 6.1 各编码的 CSD 映射

| 编码 | csd-0 | csd-1 | csd-2 |
|---|---|---|---|
| **H.264 (AVC)** | SPS | PPS | — |
| **H.265 (HEVC)** | **VPS + SPS + PPS 拼在一起**（一整块,带起始码） | — | — |
| **AAC** | AudioSpecificConfig（2 字节,描述采样率/声道） | — | — |
| VP8/VP9 | 通常不需要(无独立参数集) | — | — |

关键差异:**H.264 的 SPS、PPS 分开放 csd-0 / csd-1;HEVC 把 VPS+SPS+PPS 三件拼成一块全塞 csd-0**（呼应 [05 §5.4](./05-H264-MP4-NALU.md):HEVC 多了 VPS,手写补齐最容易漏)。

### 6.2 解码:配 CSD

```java
MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, height);
format.setByteBuffer("csd-0", spsByteBuffer);   // H.264: csd-0 = SPS(带起始码)
format.setByteBuffer("csd-1", ppsByteBuffer);   // H.264: csd-1 = PPS(带起始码)
codec.configure(format, surface, null, 0);
```

数据从哪来:

- 用 `MediaExtractor` 拆 MP4 时,`extractor.getTrackFormat(i)` 返回的 `MediaFormat` **已经带好 csd-0/csd-1**,直接 `configure` 即可——这是最省事的路径(学习路径第一步,§十四)。
- 从 RTP / Annex-B 裸流拿数据时,SPS/PPS 是带 `00 00 00 01` 起始码的 NALU,要自己拆出来填 csd（见 [05 §六](./05-H264-MP4-NALU.md) SPS/PPS 补齐）。

### 6.3 编码:首帧吐出 CSD

编码器 `start()` 后产出的**第一个输出 buffer 带 `BUFFER_FLAG_CODEC_CONFIG` 标志**,内容就是 SPS/PPS(H.264)或 VPS+SPS+PPS(HEVC)——这就是 CSD。处理铁律:

```java
if ((bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
    // 这不是一帧画面,是参数集。单独存好(用来写文件头 / 建 MP4 的 avcC / 给 WebRTC 反复补在 IDR 前)
    // 不要当普通帧写进裸流的随意位置
    saveCsd(outputBuffer, bufferInfo);
    codec.releaseOutputBuffer(index, false);
    continue;   // 别把它当画面帧处理
}
```

- 异步模式下也可以在 `onOutputFormatChanged` 回调里从 `MediaFormat` 取 `csd-0`/`csd-1`,等价。
- 推流/WebRTC 要把这份 CSD 缓存好,因为很多设备**默认只在首帧带一次**,后续 IDR 不重复带——解码端中途入会就没参数集(见 §十二陷阱、[05 §6.3](./05-H264-MP4-NALU.md) 每个 IDR 前注入策略)。

### 6.4 Android 喂入/吐出是 Annex-B（和 iOS 相反,重要对比点）

> **MediaCodec 的视频比特流是 Annex-B（带 `00 00 00 01` 起始码）**:喂给解码器的 NALU、编码器吐出的 NALU,都是起始码风格。而 **iOS VideoToolbox 是 AVCC（长度前缀,SPS/PPS 另存 format description）**。

这是两端最经典的对比题(见 [05 §四](./05-H264-MP4-NALU.md) AVCC vs Annex-B、[10 §4.3](./10-移动端硬件编解码.md) iOS 的转换坑):

| | Android MediaCodec | iOS VideoToolbox |
|---|---|---|
| NALU 切割 | **Annex-B**(`00 00 00 01` 起始码) | **AVCC**(4 字节长度前缀) |
| SPS/PPS 位置 | 作为带起始码的 NALU,经 csd 配置 / 流内 | 单独存在 `CMVideoFormatDescription` |
| 从 MP4(AVCC) 喂入要做的转换 | AVCC → Annex-B(但 `MediaExtractor` 出来已是可直接喂的格式 + csd,省心) | 反而本来就近 AVCC |
| 推 RTP(要 Annex-B) | **天然就是 Annex-B,基本不用转** | **必须 AVCC→Annex-B**(iOS 推流经典坑) |

工程含义:**做 Android 推流/WebRTC,比特流格式这块比 iOS 省心**——MediaCodec 本来就吐 Annex-B,RTP 打包要的也是 Annex-B 风格 NALU,少一道 iOS 那样的长度前缀↔起始码转换。

---

## 七、颜色格式:Android 花屏之源（ByteBuffer 模式）

这是 Android 硬解 ByteBuffer 模式最大的坑,本质是 [02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 所有问题在手机上的放大版。10 §3.4 讲过,这里做成**对照表 + 排查流程**,点到为止(底层 tiling 原理见 [10 §4.5.3](./10-移动端硬件编解码.md))。

### 7.1 颜色格式对照表

| `COLOR_Format` 常量 | 实际布局 | 谁在用 | 坑 |
|---|---|---|---|
| `COLOR_FormatYUV420Planar` | I420(Y/U/V 三平面) | 部分软 codec、部分芯片 | stride 对齐 |
| `COLOR_FormatYUV420SemiPlanar` | NV12 类(Y + UV 交织) | 高通常见 | stride 对齐 |
| `COLOR_FormatYUV420Flexible` | 运行时查(用 `Image` 按 plane 读) | **API 21+ 推荐** | 必须用 `Image` 的 rowStride/pixelStride |
| `COLOR_FormatSurface` | 不下 CPU,直接进 Surface | **零拷贝首选** | 不适用 ByteBuffer 取像素 |
| `COLOR_FormatYUVP010` | P010(10bit NV12) | HDR/HEVC/AV1 10bit | 10bit,别当 8bit 读 |
| 厂商私有 tiled 格式 | 各种对齐/分块(swizzled) | 某些高通等 | **不能当线性 YUV memcpy,否则马赛克** |

### 7.2 两个致命点（和桌面 linesize 同源）

1. **stride / slice-height ≠ width / height**。每行有对齐填充(常对齐 16 或更大),slice-height(Y 平面实际行数,UV 起点)也常 > height。**按 width 紧凑拷贝必然花屏/错位**。要从 `MediaFormat`(在 `INFO_OUTPUT_FORMAT_CHANGED` 后)读 `KEY_STRIDE` / `KEY_SLICE_HEIGHT`,或直接用 `Image`。
2. **私有 tiled 格式不能当线性 YUV memcpy**——某些芯片为硬件效率输出分块/扭曲排布,强行按 NV12 线性读得到马赛克。

### 7.3 正确取像素:用 `Image` / NDK `AImage`

```java
// 解码(非 Surface)后:
Image image = codec.getOutputImage(outputBufferIndex);   // API 21+
if (image != null) {
    Image.Plane[] planes = image.getPlanes();
    for (Image.Plane plane : planes) {
        ByteBuffer buffer = plane.getBuffer();
        int rowStride = plane.getRowStride();      // 每行字节数(含填充)≠ width
        int pixelStride = plane.getPixelStride();  // 相邻像素同分量间隔(NV12 的 UV 为 2)
        // 按 rowStride/pixelStride 逐行读,绝不能假设紧凑
    }
    image.close();   // 用完关掉,等价于归还
}
```

- `Image` 帮你把 stride / 平面布局抽象好,**按 `rowStride`/`pixelStride` 读就不会花屏**,这是 ByteBuffer 模式的正解。
- **NDK 侧对应 `AImageReader` + `AImage`**:把解码输出接到 `AImageReader` 的 Surface,从 `AImage` 按 plane 的 rowStride 读(见 [10 §3.4](./10-移动端硬件编解码.md))。跨平台播放器/libwebrtc native 走这条。

### 7.4 排查流程（花屏时按这个走）

下面把流程图的每一步对应到一个**真实场景 + 代码**，照着走一遍就能定位。

#### 步骤 1：你是不是在 ByteBuffer 模式？能切 Surface 吗？

**查自己的代码**——看 `configure` 第二个参数：

```java
// ❌ ByteBuffer 模式——第二个参数传 null，你就要自己处理像素
codec.configure(format, null, null, 0);

// ✅ Surface 模式——第二个参数传 Surface，系统帮你消化一切
codec.configure(format, surface, null, 0);
```

**决策**：如果你只是要把画面显示出来（SurfaceView/TextureView）、或者喂给另一个 MediaCodec 编码，那不需要碰像素。直接把 `null` 换成 Surface，`releaseOutputBuffer(idx, true)` ——两行改完，花屏消失。跳到 §八，完成了。

只有**确需 CPU 像素**（存图、算法分析、非标格式转码）才继续往下走。

---

#### 步骤 2：收到 `INFO_OUTPUT_FORMAT_CHANGED` 了吗？

这是最常见的"低级错误"——**解码正常没报错，但画面偏绿/错位**的根因。

**❌ 典型翻车现场**（只处理了正常 index，漏了 -2 哨兵）：

```java
int outputBufferIndex = codec.dequeueOutputBuffer(info, TIMEOUT_US);
if (outputBufferIndex >= 0) {
    // 直接读 buffer，以为宽高就是 configure 时填的 1920×1080
    ByteBuffer buf = codec.getOutputBuffer(outputBufferIndex);
    byte[] yuv = new byte[1920 * 1080 * 3 / 2];
    buf.get(yuv);  // 花屏！
    codec.releaseOutputBuffer(outputBufferIndex, false);
}
// ❌ 漏了 outputBufferIndex == INFO_OUTPUT_FORMAT_CHANGED (-2)
//    没拿到真实 stride/slice-height，按 configure 的宽高读必然错位
```

`INFO_OUTPUT_FORMAT_CHANGED` 是解码器拿到 SPS 后告诉你："真实输出格式确定了，宽高/stride/颜色格式可能是这样的"。你 configure 时填的 1920×1080 只是"请求"，真实输出可能是 stride=2048、slice-height=1088——漏掉这个信号就等于闭着眼读数据。

**✅ 正确写法**：

```java
int outputBufferIndex = codec.dequeueOutputBuffer(info, TIMEOUT_US);

if (outputBufferIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
    // 必须在这里读真实格式！
    MediaFormat outFormat = codec.getOutputFormat();
    int realWidth  = outFormat.getInteger(MediaFormat.KEY_WIDTH);      // 可能与 configure 不同
    int realHeight = outFormat.getInteger(MediaFormat.KEY_HEIGHT);
    int stride     = outFormat.getInteger(MediaFormat.KEY_STRIDE);     // 关键！
    int sliceHeight= outFormat.getInteger(MediaFormat.KEY_SLICE_HEIGHT); // 关键！
    int colorFormat= outFormat.getInteger(MediaFormat.KEY_COLOR_FORMAT);

    // stride 常 > width, sliceHeight 常 > height
    // 后续所有像素拷贝以这些值为准，不是 configure 时的宽高
    Log.d(TAG, String.format("real=%dx%d, stride=%d, sliceH=%d, fmt=0x%x",
              realWidth, realHeight, stride, sliceHeight, colorFormat));
    return;  // 本轮没有帧，继续等

} else if (outputBufferIndex >= 0) {
    // 拿到帧，用步骤 3 的方法读
    processFrame(codec, outputBufferIndex, stride, sliceHeight);
    codec.releaseOutputBuffer(outputBufferIndex, false);
}
```

> **实际案例**：某高通 845 机型解码 1080p H.264，configure 填 1920×1080。`INFO_OUTPUT_FORMAT_CHANGED` 返回 stride=2048、sliceHeight=1088、格式 `COLOR_FormatYUV420SemiPlanar`。按 1920 紧凑读：Y 平面底部 8 行绿边（取到了 padding），UV 全部错位（UV 起始偏移是 2048×1088 而不是 1920×1080），画面下半截颜射完全错误。

---

#### 步骤 3：用 `getOutputImage()` / `AImage` 读了吗？

即使拿到了正确的 stride/slice-height，**手动按行偏移读也是一堆边界计算**，容易写错。

**❌ 容易写错的"手动版"**（拿了 stride 但 UV 平面还是算错了）：

```java
// 踩坑：手动按 stride 读，但 UV 起始偏移只算了 Y 的有效像素
ByteBuffer buf = codec.getOutputBuffer(index);
byte[] y = new byte[width * height];
byte[] uv = new byte[width * height / 2];

// 读 Y：正确，用了 stride
for (int row = 0; row < height; row++) {
    buf.position(row * stride);
    buf.get(y, row * width, width);
}

// 读 UV：❌ 错——UV 起始是 stride * sliceHeight，不是 width * height
buf.position(stride * sliceHeight);  // 这才是正确偏移，但很多人写 width * height
for (int row = 0; row < height / 2; row++) {
    buf.get(uv, row * width, width);  // NV12 UV 交织，pixelStride=2，这一行也读不对
}
// 结果：Y 是对的但 UV 依然花
```

**✅ 用 Image 省掉所有这些计算**（Android 5.0+）：

```java
Image image = codec.getOutputImage(outputBufferIndex);
if (image != null) {
    Image.Plane[] planes = image.getPlanes();

    // Y 平面：rowStride 可能是 2048，只取前 width(1920) 字节
    ByteBuffer yBuf = planes[0].getBuffer();
    int yRowStride = planes[0].getRowStride();
    for (int row = 0; row < height; row++) {
        yBuf.position(row * yRowStride);
        yBuf.get(dstY, row * width, width);
    }

    // UV 平面：rowStride 处理同上，pixelStride=2 代表 U,V 交叠
    if (planes.length >= 2) {
        ByteBuffer uvBuf = planes[1].getBuffer();
        int uvRowStride = planes[1].getRowStride();
        int uvPixelStride = planes[1].getPixelStride();  // NV12=2, I420=1
        for (int row = 0; row < height / 2; row++) {
            uvBuf.position(row * uvRowStride);
            if (uvPixelStride == 2) {
                // NV12: UV 交叠，直接宽×2 字节一次读
                uvBuf.get(dstUV, row * width, width * 2);  // U 和 V 一起
            } else {
                // I420: 独立平面，按 width 读
                uvBuf.get(dstU, row * width, width);
            }
        }
    }
    image.close();
}
// 关键：Image 帮你处理了 plane 切分、stride、pixelStride——
//      你不用算 UV 起始偏移，不用硬记 NV12 vs I420 的差异
```

**NDK C++ 侧对应**（`AImage`，前面 §〇 已展开）：`AImage_getPlaneRowStride` / `AImage_getPlanePixelStride`——同一套逻辑。

---

#### 步骤 4：还是马赛克？——厂商私有 tiled/swizzled 格式

如果走完步骤 3 **画面依然是规则的大块马赛克**（不是错位/偏绿，而是像拼图打乱了一样）：

```
正常画面:  ┌─────┬─────┐      马赛克:  ┌──┬──┬──┬──┐
          │     │     │              │ A│ C│ B│ D│   ← 块被重排了
          ├─────┼─────┤              ├──┼──┼──┼──┤
          │     │     │              │ E│ G│ F│ H│
          └─────┴─────┘              └──┴──┴──┴──┘
```

这就是**厂商私有 tiled 格式**——芯片为 GPU 纹理效率把像素按 32×32 等小块重排过（swizzled），不是线性排列。**线性 memcpy 任何 offset/stride 都读不对**，因为数据的物理排布就不是 scanline 顺序。

**实际机型案例**：
- 某小米机型（高通 865）解码输出 `COLOR_FormatYUV420SemiPlanar`，看起来是普通 NV12——但实际是 `QTI_TILE_NV12`，块大小 64×64
- `getOutputImage()` 的 `rowStride` 返回的是 tile 对齐后的逻辑行宽，你按行读出来的像素值本身就错（同一个 tile 内的像素不是按行排的）
- 画面呈现规则马赛克，每块大约 2-4 像素宽，块内颜色基本一致

**此时只有三条路**：

| 方案 | 做法 | 适用场景 |
|---|---|---|
| **A. 切 Surface**（推荐） | 不改解码代码，把输出从 ByteBuffer 改成 Surface。用 SurfaceTexture 接，转为 GL OES 纹理——数据从解码器 GPU → GL GPU，全程没下 CPU，也不存在 tiled 线性读的问题 | 多数场景都能走这条路 |
| **B. 换 Flexible 格式** | `configure` 前把颜色格式设成 `COLOR_FormatYUV420Flexible`，解码器会选一个 CPU 能线性读的格式输出（保证 `getOutputImage()` 有效） | 确需 CPU 像素 + 不想适配各机型 |
| **C. 针对特定机型适配** | 查该 SoC 的 tiled 排布算法（高通 QTI 某版、三星某版），写专门的 deswizzle 函数。**不推荐**——每换一个机型/soc/Android 版本都可能变 | 极少数没有退路的情况 |

> **面试时怎么讲**：走到步骤 4 了还马赛克，直接说"这大概率是厂商 tiled 格式，线性 memcpy 无解。生产环境直接切 Surface/Flexible，不要跟具体机型的 swizzle 硬刚——那个维护成本没有上限。"

---

**排查流程总览**（保留原流程图的精炼版）：

```
画面花屏/错位/马赛克
   │
   ├─[1] configure 第二个参数是 null?
   │      └─ 是 → 能改 Surface 吗? → 能 → 改 Surface, 完成 ✅
   │            (确需 CPU 像素才往下)
   ├─[2] 漏了 INFO_OUTPUT_FORMAT_CHANGED 分支?
   │      └─ 漏了 → 补上, 从 getOutputFormat() 读 stride/slice-height
   ├─[3] 在手动算 offset/memcpy?
   │      └─ 是 → 改用 getOutputImage()/AImage, 按 rowStride/pixelStride 逐行读
   └─[4] 还是马赛克(规则大块)?
          └─ 是 → 厂商 tiled 格式, 线性读无解
                   A. 切 Surface(首选)  B. 换 Flexible  C. 写 deswizzle(下策)
```

### 7.5 ByteBuffer 与 Surface 的本质区别（底层视角）

上面说了 ByteBuffer 容易花屏、Surface 不会。但这只是现象。把**底层是什么、内存从哪来、CPU 能不能读**讲透，才真正理解为什么会有这个差异——面试里被追问"那它们的本质区别是什么"也不虚。

#### 7.5.1 一张图讲清两条路径

```
MediaCodec 解码输出
        │
        ├── ByteBuffer 模式
        │   解码器内部: 硬件帧(gralloc buf, GPU 格式)
        │       │
        │       ▼  解码器(或 OMX/Codec2 层) 做一次拷贝/映射
        │   你拿到的 ByteBuffer (CPU 虚拟地址, 线性排布)
        │       │
        │       ├── memcpy / Image 逐行读  →  CPU 侧消费
        │       └── glTexImage2D 上传      →  回 GPU (又拷一次)
        │
        └── Surface 模式
            解码器内部: 硬件帧(gralloc buf, GPU 格式)
                │
                ▼  不拷贝不转换，只传递 buffer_handle_t 的引用
            Surface 持有的就是同一个 gralloc buffer
                │
                ├── GPU 渲染(SurfaceView/TextureView)       ← 零拷贝
                ├── 硬件编码器(MediaCodec Input Surface)     ← 零拷贝
                └── ImageReader (CPU 回读，有拷贝)            ← 唯一下 CPU 的点
```

**核心结论**：区别不在"数据存在什么物理内存里"（手机 UMA 架构，物理上都同一块 LPDDR），而在**数据被谁管理、按什么格式排、CPU 有没有直接可用的虚拟地址映射**。

#### 7.5.2 ByteBuffer 底层：CPU 可映射的线性内存

```
你的代码
    │  ByteBuffer.get(byte[])
    ▼
┌──────────────────────────────┐
│  Java NIO DirectByteBuffer   │  ← java.nio 包
│  (Java 层: 一个 long 存 native 地址) │
└──────────┬───────────────────┘
           │ 内部持有 native 指针
           ▼
┌──────────────────────────────┐
│  Native (C) heap             │  ← malloc / ashmem / mmap
│  一块连续的虚拟地址空间       │
│  CPU 可以直接 load/store     │
└──────────────────────────────┘
```

- **内存来源**：`DirectByteBuffer` 底层是 JNI `NewDirectByteBuffer`，最终调用 `malloc` 或 ashmem 分配一块系统内存，返回 CPU 虚拟地址。
- **CPU 能直接读**：`ByteBuffer.get()` 就是一次 `memcpy`——没有缺页、没有设备映射的坑。
- **但数据按硬件习惯排**：解码器往这块 buffer 写数据时，按硬件对齐要求（stride 对齐 16/32/64/128 字节，slice-height 对齐，甚至是 tiled 排布）写进去的，而不是按你期望的紧凑 `width × height` YUV。
- **本质**：ByteBuffer 是**系统内存的一扇窗口**——解码器把数据从图形域拷到系统域，让你用 CPU 指令自由读写。代价是那次拷贝 + 你要自己处理 stride/tiling。

> ByteBuffer 模式 = **硬件解码 → 拷贝进系统内存 → CPU 可见**。中间那次拷贝就是"下了 GPU"，零拷贝说的就是省掉这一步。

#### 7.5.3 Surface 底层：gralloc + dma-buf + BufferQueue

```
你的代码
    │  releaseOutputBuffer(idx, true)
    ▼
┌──────────────────────────────┐
│  Surface (Java)              │  ← android.view.Surface
│  内部持有一个 native Surface │
└──────────┬───────────────────┘
           │
           ▼
┌──────────────────────────────┐
│  ANativeWindow (native)      │  ← libnativewindow
│  对端: BufferQueue 的生产者   │
└──────────┬───────────────────┘
           │  dequeue / queue / acquire / release
           ▼
┌──────────────────────────────┐
│  BufferQueue                 │  ← libgui
│  管的是 buffer_handle_t 的   │
│  所有权传递，不碰数据        │
└──────────┬───────────────────┘
           │
           ▼
┌──────────────────────────────┐
│  gralloc buffer              │  ← gralloc HAL (厂商实现)
│  (buffer_handle_t)           │
│  ┌────────────────────────┐  │
│  │ dma-buf fd              │  │ ← 内核 dma-buf: 跨设备共享的
│  │ GPU 纹理格式(tiled)     │  │   文件描述符
│  │ stride/slice-height 元  │  │
│  │ acquire/release fence  │  │  ← 同步用的 fd
│  └────────────────────────┘  │
└──────────────────────────────┘
           │
           ▼
┌──────────────────────────────┐
│  ION / dma-buf heap         │  ← 内核驱动: 实际物理内存分配
│  (物理上 LPDDR, 和 ByteBuffer│
│   的内存在同一块芯片里)      │
└──────────────────────────────┘
```

关键点逐层拆开：

**(a) gralloc——"图形内存的 malloc"**

gralloc 是 Android HAL 层专门为图形 buffer 设计的分配器。它分配的不是普通 `malloc` 的内存，而是适合 GPU/Display/视频引擎访问的 buffer：

- 对齐到 GPU tile 边界（64×64、128×128 等）
- 排布用 tiled/swizzled 格式——GPU 纹理单元可以高效读写，但 CPU 看不懂
- 带有 stride、pixel format、usage flags（`GRALLOC_USAGE_HW_TEXTURE` | `GRALLOC_USAGE_HW_RENDER` | `GRALLOC_USAGE_HW_VIDEO_ENCODER`）

**(b) dma-buf——"零拷贝的通道"**

dma-buf 是 Linux 内核的机制（不是 Android 发明的，Android 只是重度用户）。核心思想：

- 分配一块物理内存，返回一个**文件描述符（fd）**
- 这个 fd 可以在不同进程间、不同硬件模块间传递——GPU 驱动拿到 fd、Display 驱动拿到 fd、视频解码器拿到 fd
- **注意这里的"硬件模块"**：指的是同一颗 SoC 内部的不同硬件单元（GPU、Display 控制器、视频解码器、Camera ISP 等），在 Linux 内核里每个都被抽象为一个 device。不是指"两台手机"——dma-buf 的"跨设备"始终在**同一块主板、同一颗 SoC 之内**，跟跨机器共享内存完全是两回事
- 所有消费者看到的是**同一块物理内存**，不需要拷贝
- 这是 Android 零拷贝链路的基石——从解码器到 SurfaceFlinger 到 Display，传递的全是 dma-buf fd，不是数据

```
解码器                       SurfaceFlinger                  Display
   │ 解码到 gralloc buf          │                              │
   │ (持有 dma-buf fd)           │                              │
   │                             │                              │
   │ ──── queueBuffer(fd) ────▶ │                              │
   │                             │ 合成(持有同一个 fd)           │
   │                             │ ──── 送显(fd) ─────────────▶ │
   │                             │                              │ 扫描输出
   
   全程: 同一个 dma-buf fd，同一个物理内存，0 次拷贝
```

**(c) BufferQueue——"管借还，不管数据"**

BufferQueue 是生产者-消费者模型的核心调度器：

- **生产者**（解码器）调 `dequeueBuffer` 拿到一个空闲 slot → 解码写进去 → `queueBuffer` 交还
- **消费者**（SurfaceFlinger / 你的 GL 线程）调 `acquireBuffer` 拿到 → 消费 → `releaseBuffer` 归还
- **fence 同步**：每个 buffer 带 `acquireFenceFd` / `releaseFenceFd`（也是 fd，内核 sync_file 机制）——GPU 没写完消费者就不能读，Display 没读完生产者就不能覆盖。这保证了**没有 data race，且不需要 CPU 轮询等**。

整个 BufferQueue 管的从来不是数据本身，是**buffer slot 的所有权和同步状态**——数据始终在原地没动过。

**(d) Surface 模式下 CPU 为什么"看不到"**

Surface 持有的 gralloc buffer：

- CPU 侧**没有**直接的虚拟地址映射（没有 `mmap` 到用户空间）
- 就算你用 `gralloc_lock` 强行映射——数据是 tiled 排布，读出来全是乱的
- 非得从 Surface 拿像素给 CPU？唯一办法是接 `ImageReader` / `glReadPixels`——这会产生一次 GPU→CPU 回读拷贝，就是那"唯一的一下"（见 7.5.1 图左下方的路径）

#### 7.5.4 UMA 的 nuance：物理相同，路径不同

手机 SoC 是统一内存架构（UMA），物理上 ByteBuffer 和 Surface buffer 都在同一块 LPDDR 颗粒里。但"同一块物理内存"≠"同一个访问路径"：

| | ByteBuffer | Surface buffer |
|---|---|---|
| 分配器 | `malloc` / ashmem | gralloc → ION/dma-buf heap |
| CPU 虚拟地址 | ✅ 有（`DirectByteBuffer` 的 native ptr） | ❌ 默认不映射 |
| GPU 可访问 | 能，但要通过 GPU 驱动上传（有拷贝） | ✅ native（tiled 排布，GPU 直接读） |
| 跨硬件模块共享<br>(同 SoC 内的 GPU/Decoder/Display 等) | ❌ 普通 `malloc` 内存没有 dma-buf fd | ✅ dma-buf fd 随便传 |
| 同步机制 | 无（CPU 侧自己管锁） | ✅ acquire/release fence（内核 sync_file） |
| 排布 | 线性（连续 row by row） | GPU tiled/swizzled（块状重排） |

所以**本质区别**一句话：

> **ByteBuffer 是"给 CPU 用的"**——有 CPU 虚拟地址、线性排布、但跟 GPU/Display 不沾边，要进 GPU 就得上载（upload）。
> **Surface 是"给图形栈用的"**——gralloc 分配、dma-buf 共享、tiled 排布、fence 同步，天然在 GPU/Display/硬解之间零拷贝流转，但 CPU 没门牌号。

面试怎么讲：先说 gralloc/dma-buf/BufferQueue 三个关键词，再画 7.5.1 那张分叉图，最后一句话收——"ByteBuffer 有 CPU 地址但格式乱，Surface 没 CPU 地址但零拷贝"。这比背 API 列表有说服力得多。

### 7.6 gralloc / dma-buf / fence：三个组件如何构成"一套零拷贝栈"

§7.5.3 把 gralloc、dma-buf、BufferQueue 拆开讲了各自干什么。但面试里真正拿分的是**把三者串成一条链路**——说明白它们分别解决了什么问题、为什么缺一不可，以及为什么叫"一套栈"而不是三个独立机制。

本节把三者缝合成一张完整的零拷贝链路图。

#### 7.6.1 一句话理解三个角色

| 组件 | 一句话 | 管什么 |
|---|---|---|
| **gralloc** | "图形内存的专用 malloc" | **分配**——GPU 看得懂、CPU 看不懂的内存从哪来 |
| **dma-buf** | "零拷贝的通道" | **共享**——同一块物理内存怎么在设备间以 fd 形式传递而不拷贝 |
| **fence** | "不用 CPU 轮询的同步锁" | **同步**——GPU 没写完消费者不能读、Display 没读完生产者不能覆盖 |

三个组件各管一段：**gralloc 管"内存在哪"、dma-buf 管"怎么传"、fence 管"什么时候能碰"。** 三者组合才构成完整的零拷贝栈——分配、传递、同步，缺一环都跑不通。

#### 7.6.2 fence 深入：为什么不是 CPU 锁

fence 是这套栈里最容易被忽略但最重要的组件。先看如果**没有 fence** 会发生什么：

```
解码器(GPU) 写 buffer A ──────► SurfaceFlinger(GPU) 读 buffer A
                                    │
                           CPU 怎么知道 GPU 写完了？
                           方案 1: CPU 轮询 GPU 状态寄存器 → CPU 空转耗电
                           方案 2: CPU sleep(固定时间)    → 要么等太久(延迟↑)、要么没写完就读(花屏)
```

fence 解决了这个问题——但关键是**它不用 CPU 参与**：

```
解码器(GPU) 写 buffer A
    │
    │  GPU 写完 → 硬件自动 signal acquireFenceFd
    │
    ▼
SurfaceFlinger 等 acquireFenceFd 被 signal
    │
    │  内核调度: fence signal 之前,SurfaceFlinger 的 GPU 线程被挂起
    │  fence signal 之后,内核唤醒它——CPU 中间在睡觉
    │
    ▼
SurfaceFlinger(GPU) 读 buffer A、合成、写 buffer B
    │
    │  GPU 读完 → 硬件自动 signal releaseFenceFd
    │  → 解码器现在可以安全覆盖 buffer A
```

每个 buffer 在 BufferQueue 里流转时，附带两个 fence fd：

- **`acquireFenceFd`**：生产者写完时 signal。消费者在 acquire 之前阻塞在这个 fence 上——GPU 没写完，读操作就被内核挂起。
- **`releaseFenceFd`**：消费者读完时 signal。生产者要覆盖写入前检查这个 fence——Display 还在扫描，就不能覆盖。

关键洞察：

> **fence 是硬件信号，不是软件锁。** GPU 完成写入的瞬间，硬件自己 signal fence，内核唤醒等在 fence 上的对端。CPU 全程不轮询、不等 sleep 超时——这就是"零拷贝**栈**"里的"栈"字：不仅数据不拷，**连同步开销也被硬件接管了**。

```
传统同步:
  CPU 轮询 "好了没？好了没？" → CPU 忙等/空转
  CPU sleep(5ms) 再查       → 延迟至少 5ms

fence 同步:
  GPU 完成 → 硬件 signal fence → 对端立即被唤醒
  CPU 全程: 睡觉
  延迟: ~微秒级（硬件中断延迟）
```

#### 7.6.3 完整链路：一次 Surface 解码的数据旅程

把三个组件拼成一条完整的帧传递链路，你就有了"一套栈"的完整心智模型：

```
┌─────────────────────────────────────────────────────────────────────┐
│                        一次 Surface 解码的帧传递                       │
└─────────────────────────────────────────────────────────────────────┘

  ① gralloc 分配
  ┌──────────────────────────────────────────┐
  │  gralloc HAL → ION/dma-buf heap          │
  │  分配一块物理内存（LPDDR）                  │
  │  属性: tiled 排布, GPU 可读写, CPU 不可见   │
  │  返回: buffer_handle_t (含 dma-buf fd)     │
  └──────────────────┬───────────────────────┘
                     │
                     ▼
  ② 解码器写入（生产者）
  ┌──────────────────────────────────────────┐
  │  MediaCodec 解码 → GPU 写像素到 gralloc buf │
  │  GPU 写完 → signal acquireFenceFd          │
  │  queueBuffer: 把 (dma-buf fd + fence fd)   │
  │               交给 BufferQueue              │
  └──────────────────┬───────────────────────┘
                     │
                     ▼
  ③ BufferQueue 调度（只传引用，不传数据）
  ┌──────────────────────────────────────────┐
  │  BufferQueue 管的从来不是数据本身           │
  │  它管的是:                                 │
  │    - 这个 slot 的所有权现在归谁             │
  │    - acquireFenceFd signal 了吗            │
  │    - releaseFenceFd signal 了吗            │
  │  数据始终在原地（同一块物理内存）没动过       │
  └──────────────────┬───────────────────────┘
                     │
                     ▼
  ④ SurfaceFlinger 合成（消费者）
  ┌──────────────────────────────────────────┐
  │  等 acquireFenceFd signal（GPU 写完）      │
  │  拿到同一个 dma-buf fd → GPU 直接读同一块    │
  │         物理内存做合成                      │
  │  读完 → signal releaseFenceFd              │
  └──────────────────┬───────────────────────┘
                     │
                     ▼
  ⑤ Display 扫描输出
  ┌──────────────────────────────────────────┐
  │  再等一个 fence → 拿到同一个 dma-buf fd     │
  │  Display 引擎从同一块物理内存扫描输出        │
  └──────────────────────────────────────────┘


  全程统计:
  ┌──────────────────────────────────────────┐
  │  数据拷贝: 0 次                            │
  │  数据写过: 1 次（解码器写入）                │
  │  后续传递: 全是 dma-buf fd（8 字节的文件描述符）│
  │  同步: 全是 fence（硬件信号，CPU 睡觉）       │
  └──────────────────────────────────────────┘
```

#### 7.6.4 对比 ByteBuffer：拷贝到底发生在哪

同样的帧，走 ByteBuffer 模式：

```
  ① gralloc 分配（同上）
         │
         ▼
  ② 解码器写入（同上）
         │
         ▼
  ③ ★ 第一次拷贝 ★
     框架层把 gralloc buf（GPU tiled 格式）
     → 拷贝/映射到 ByteBuffer（CPU 线性内存）
     这是"下 GPU"的那一步——数据从图形域进了系统域
         │
         ▼
  ④ ★ 第二次拷贝 ★
     你的代码: outputBuffer.get(byte[])
     ByteBuffer → 你的 byte[]（memcpy）
         │
         ▼
  ⑤ ★ 第三次拷贝 ★（如果要显示的话）
     glTexImage2D: 你的 byte[] → GPU 纹理
     数据又上传回 GPU——白兜了一大圈
```

| | Surface 模式 | ByteBuffer 模式 |
|---|---|---|
| 数据搬运次数 | **1 次写**（解码器写像素） | **3 次拷贝**（GPU→ByteBuffer→你的 buf→GPU） |
| CPU 参与 | 0（全程 GPU + 硬件 fence） | CPU memcpy × 2 |
| 同步方式 | fence（硬件信号，~μs 延迟） | CPU 轮询/sleep（~ms 延迟） |
| 内存占用 | 1 份物理内存 | 同一帧 2~3 份拷贝同时存在 |

#### 7.6.5 为什么叫"一套栈"

业界说"走的是 gralloc/dma-buf/fence 这套零拷贝栈"，而不是"用了 dma-buf 这个特性"——因为三者是**分层协作、缺一不可**的：

```
                 ┌──────────┐
    同步层       │  fence   │  ← "什么时候能碰"——硬件信号，不等 CPU
                 ├──────────┤
    传输层       │ dma-buf  │  ← "怎么传"——传 fd 不传数据
                 ├──────────┤
    分配层       │ gralloc  │  ← "内存在哪"——GPU 友好的物理内存
                 └──────────┘
```

- **只有 gralloc 没有 dma-buf**：内存分好了，但解码器和 SurfaceFlinger 在不同进程，没法共享——还得靠 Binder 传数据（有拷贝）。
- **有 gralloc + dma-buf 没有 fence**：能共享，但不知道什么时候该读——只能 CPU sleep/轮询，要么花屏要么白等。
- **三者齐全**：分配（gralloc）→ 共享（dma-buf fd 传递）→ 同步（fence 硬件信号），数据 0 次拷贝，CPU 0 次参与。

> **这就是"一套栈"的含义——不是三个独立优化点，而是一个三层协作的完整体系。抽掉任何一层，整条链路要么退化到有拷贝、要么退化到 CPU 轮询、要么直接不可用。**

#### 7.6.6 面试怎么讲（口语化话术）

面试被问到"Surface 零拷贝怎么回事"，脱口版：

> "Surface 零拷贝底层靠三个组件。**gralloc** 是图形内存的专用分配器，分出来的内存是 GPU tiled 排布、带 dma-buf fd，CPU 没有直接虚拟地址映射。**dma-buf** 是 Linux 内核机制——同一块物理内存，在解码器、SurfaceFlinger、Display 之间只传一个 fd，谁都不拷贝数据本身。**fence** 是同步——每次 buffer 转手都带 acquire fence 和 release fence，GPU 没写完消费者等着，Display 没读完生产者不能覆盖，而且这是硬件信号，CPU 全程不参与轮询。三者分三层——gralloc 管分配、dma-buf 管共享、fence 管同步，合起来才是一套完整的零拷贝栈。缺一层都不行：没 dma-buf 就要 Binder 传数据有拷贝，没 fence 就只能 CPU sleep 轮询要么花屏要么白等。"

---

## 八、Surface 零拷贝:Android codec 这一侧怎么用

> 零拷贝的底层原理(BufferQueue / acquire-release fence / dma-buf / gralloc / UMA 为何仍慢)在 [10 §四点五](./10-移动端硬件编解码.md#四点五ios-与-android-的零拷贝核心考点) 已讲透。本节**只讲 Android codec 这一侧怎么把 Surface 用起来**,不重复底层。

### 8.1 解码 → 显示(输出 Surface)

```java
codec.configure(format, surface, null, 0);            // 第 2 个参数传 Surface
// 解码后:
codec.releaseOutputBuffer(outputBufferIndex, true);   // 第 2 参 true = 渲染到 Surface
```

数据**全程不下 CPU**,直接进 GPU。两条去向:

- `surface` 来自 `SurfaceView` / `TextureView` → 直接上屏。
- `surface` 来自 `SurfaceTexture` → 变成一张 OpenGL ES 的 **`GL_TEXTURE_EXTERNAL_OES`** 外部纹理 → 做后处理/滤镜/美颜再画。两个必踩点(`samplerExternalOES`、`getTransformMatrix()` 要乘到纹理坐标)见 [10 §4.5.5](./10-移动端硬件编解码.md),这里不重复。

```
解码 -> (硬件 buffer,不下 CPU) -> SurfaceView 直接上屏
                              \-> SurfaceTexture -> OES 外部纹理 -> GL 后处理/渲染
```

### 8.1.1 怎么理解 OES 外部纹理的"外部"？为什么只能读？

面试被追问"为什么叫外部纹理、跟普通纹理什么区别"，下面是完整拆解。

**一句话本质**：普通纹理 `glTexImage2D` 是把像素拷进 GL 自己管理的内存池；OES 外部纹理通过 `EGLImage` 映射 gralloc/dma-buf 的 buffer，数据在解码器/Camera 等**外部**子系统，GL 只是"借来看"——不拷贝、不拥有。

**对比——普通纹理 (GL_TEXTURE_2D) vs 外部纹理 (GL_TEXTURE_EXTERNAL_OES)**

```
普通纹理 (GL_TEXTURE_2D):              外部纹理 (GL_TEXTURE_EXTERNAL_OES):
─────────────────────                  ─────────────────────────────
  glTexImage2D(..., pixels)               eglCreateImageKHR(NATIVE_BUFFER_ANDROID, native_buf)
         │ 拷贝                                   │ 映射
         ▼                                        ▼
  ┌── GL 内存池 ──┐                      ┌── gralloc buffer ──┐
  │ 数据归 GL 管   │                      │ 数据归解码器/Camera │
  │ GL 可读/写    │                      │ GL 只能采样(读)    │
  └──────────────┘                      └────────────────────┘
```

**代码层面体现这个差异**：

普通纹理——数据拷进去，归 GL 所有：

```c
GLuint tex;
glGenTextures(1, &tex);
glBindTexture(GL_TEXTURE_2D, tex);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
// ↑ 数据拷进 GL 内存池。之后 pixels 这个 CPU buffer 可以 free 了
```

外部纹理——数据在外部，GL 只是映射视图：

```c
// 1. 把解码器/Camera 产出的 gralloc buffer 包成 EGLImage
EGLImageKHR eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT,
    EGL_NATIVE_BUFFER_ANDROID,                    // ← 外部 buffer!
    (EGLClientBuffer) native_buffer_handle,       // gralloc 分配、解码器填入
    attrs);

// 2. 把 EGLImage 绑到 GL 纹理——★ 这步不拷贝
GLuint tex;
glGenTextures(1, &tex);
glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, eglImage);
// ★ 没有数据拷贝——tex 只是指向外部 buffer 的一个"指针"
//   这个 buffer 仍然由解码器/Camera 的那套 gralloc/dma-buf 体系管理
```

**为什么是"外部"**：

| | 内部 (GL_TEXTURE_2D) | 外部 (GL_TEXTURE_EXTERNAL_OES) |
|---|---|---|
| 数据所有权 | GL 拥有 | 外部拥有 (gralloc/dma-buf) |
| 数据在哪 | GL 内存池 | gralloc shared buffer |
| 创建方式 | `glTexImage2D` (拷贝) | `glEGLImageTargetTexture2DOES` (映射) |
| Shader sampler | `sampler2D` | `samplerExternalOES` |
| 类比 | `memcpy` 一份副本到自己的堆 | `mmap` 一块共享内存 |

**为什么只能读不能写**：

`GL_TEXTURE_EXTERNAL_OES` 有硬性限制，来自 `GL_OES_EGL_image_external` 扩展规范：

| 操作 | 允许？ | 原因 |
|---|---|---|
| `samplerExternalOES` 采样 (读) | ✅ | 基本用途 |
| `glTexImage2D` / `glTexSubImage2D` 往里写 | ❌ | 数据不在 GL 堆里 |
| FBO color attachment (渲染到它) | ❌ | GL 不知道 tiled 格式怎么写 |
| `glReadPixels` (读回 CPU) | ❌ | 绕过了 EGLImage 的语义 |
| mipmap 生成 | ❌ | 外部数据格式 GL 不保证认知 |
| wrap mode 设为 REPEAT/MIRROR | ❌ | 只能 `CLAMP_TO_EDGE` |

根因一句：**这块 buffer 是 gralloc 分配的、按 GPU tile 排布的、带 fence 同步的。GL 既不知道它的精确 tiling 算法（高通 vs Mali vs 三星——每家都不一样），也没有那套 fence 的协调权。如果允许 GL 写入，GL 驱动要在没有 gralloc 协议的情况下正确处理 tiling 和同步——规范一刀切：外部的东西，GL 你看就行，别碰。**

**那要"写"怎么办？——不在 GL 里写，找真正的生产者**：

```
写入方 (生产者)                        读取方 (消费者, GL)
──────────                             ──────────
MediaCodec 解码输出 ──┐               ┌── samplerExternalOES 采样
Camera2 HAL 采集 ────┤               ├── 做成 GL 纹理展示
GPU compute/Vulkan ──┘               └── 零开销在 GPU 切换 handoff
        │                                    │
        └── gralloc buffer (dma-buf) ────────┘
              只有一块内存，只传 fd
```

真要"GL 渲染了再给别人"的链路？——GL 渲染到一个 `GL_TEXTURE_2D`（FBO），然后把这个内部纹理通过 `eglCreateImageKHR` 导出成 EGLImage，再传给编码器。此时对于编码器来说，这个 GL 内部纹理又是个"外部纹理"。

**和 SurfaceTexture 的关系——一条完整的线**：

```
解码器/Camera
  │ 输出到 Surface (gralloc buffer in BufferQueue)
  ▼
SurfaceTexture.updateTexImage()
  │ 把 BufferQueue 里最新的 slot 绑到 GL_TEXTURE_EXTERNAL_OES
  │ ★ 不拷贝——只是让 GL 纹理"指向"那个 gralloc buffer
  ▼
你的 GL shader 用 samplerExternalOES 采样
  │ 数据在 GPU 里，没下过 CPU
  ▼
SurfaceTexture.getTransformMatrix()
  │ 拿到 4×4 变换矩阵——解码器/Camera 可能旋转了输出
  │ 必须在顶点 shader 里把纹理坐标乘上这个矩阵
  ▼
渲染到 SurfaceView / 喂给编码器
```

面试说起来就一句话：**"普通纹理 `glTexImage2D` 把数据拷进 GL 内存池，数据归 GL。OES 外部纹理通过 `EGLImage` 映射 gralloc/dma-buf，数据在解码器/Camera 那边，GL 只有读权限——这是零拷贝的关键一环，省掉的就是 `glTexImage2D` 那次 CPU→GPU 上载。"**

### 8.2 编码 ← GPU 画面(输入 Surface)

编码方向用 `createInputSurface()`:**让相机预览/GL 渲染结果直接画进编码器的输入 Surface,省掉 YUV 回读 CPU 这一大笔开销**。这是移动端高性能采集编码的标准姿势。

```java
codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
Surface inputSurface = codec.createInputSurface();    // 必须在 configure 之后、start 之前
codec.start();
// 把相机/GL 渲染目标指向 inputSurface(EGL eglCreateWindowSurface 包它),正常 GL 绘制
// 编码器自动从 Surface 取帧编码,你这边不用 queueInputBuffer
// 收尾用 signalEndOfInputStream() 而不是给输入塞 EOS flag
```

要点:

- 用了 input Surface,**输入侧没有 `dequeueInputBuffer`/`queueInputBuffer`**——帧由 GPU 直接喂,你只管在输出侧取编码数据。
- 颜色格式要设 `COLOR_FormatSurface`。
- 结束时调 `signalEndOfInputStream()` 通知编码器没有更多输入。

```
相机/GL 渲染 -> createInputSurface() (GPU 纹理,不回 CPU) -> 编码器 -> H.264/HEVC 输出
```

### 8.3 一句话

**Surface 模式 = 把"颜色格式 + 零拷贝"两个最大的坑一次性甩给系统。** 学习和生产都应优先走 Surface,只有确需 CPU 拿像素时才退回 ByteBuffer。

---

## 九、编码参数与运行时控制（对照 06）

把 `MediaCodec` configure 成 encoder 时,`MediaFormat` 的 key 就是 [06-编码参数与码控.md](./06-编码参数与码控.md) 那些概念在 Android 上的具体旋钮。

### 9.1 初始化参数

```java
MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, height);
format.setInteger(MediaFormat.KEY_BIT_RATE, 4_000_000);          // 目标码率(bps)
format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2);          // 关键帧间隔(秒)-> GOP
format.setInteger(MediaFormat.KEY_BITRATE_MODE,
        MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);    // CBR / VBR / CQ
format.setInteger(MediaFormat.KEY_PROFILE,
        MediaCodecInfo.CodecProfileLevel.AVCProfileConstrainedBaseline);  // Profile
format.setInteger(MediaFormat.KEY_LEVEL,
        MediaCodecInfo.CodecProfileLevel.AVCLevel31);            // Level
format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
        MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);   // 用输入 Surface
format.setInteger(MediaFormat.KEY_LOW_LATENCY, 1);              // 低延迟(API 30+)
```

| MediaFormat key | 对应 06 的概念 | 备注 |
|---|---|---|
| `KEY_BIT_RATE` | 目标码率 | 配合 `KEY_BITRATE_MODE` |
| `KEY_FRAME_RATE` | 帧率 | |
| `KEY_I_FRAME_INTERVAL` | **GOP/关键帧间隔**(单位秒,不是帧数!) | 0 = 每帧都 I,负值/很大 = 极少 I |
| `KEY_BITRATE_MODE` | CBR / VBR / CQ(恒定质量) | `BITRATE_MODE_CBR/VBR/CQ` |
| `KEY_PROFILE` / `KEY_LEVEL` | [06 §四](./06-编码参数与码控.md) Profile/Level | 硬编实际是否生效因设备而异 |
| `KEY_LOW_LATENCY` | 低延迟(对应 `tune=zerolatency` 取向) | **API 30+**;老设备用厂商私有 key 兜底 |
| `KEY_COLOR_FORMAT` | 输入像素格式 | Surface 编码用 `COLOR_FormatSurface` |

**和 x264 的根本差别:硬编可调参数远少于 x264。** 没有 `-preset veryslow`、没有精细运动搜索、没有 psy-RD——厂商只暴露上面这些旋钮(为什么硬编画质天生追不上软编极致,见 [07 §七](./07-硬件编解码.md))。

### 9.2 运行时动态控制（WebRTC 拥塞控制/PLI 必用）

编码器 configure 完,码率和关键帧**运行中可以动态改**,这正是 WebRTC 拥塞控制(GCC)和丢包恢复(PLI/FIR)要的旋钮(见 [10 §3.7](./10-移动端硬件编解码.md)、[08 §十](./08-网络协议与流媒体.md))。Android 用 `setParameters`:

```java
// 拥塞控制要降码率: 动态改目标码率
Bundle bitrateUpdate = new Bundle();
bitrateUpdate.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, newBitrate);
codec.setParameters(bitrateUpdate);

// 对端发来 PLI/FIR 请求关键帧: 让编码器立刻产一个 IDR
Bundle requestKeyFrame = new Bundle();
requestKeyFrame.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
codec.setParameters(requestKeyFrame);
```

| 动态旋钮 | 用途 | iOS 对应物 |
|---|---|---|
| `PARAMETER_KEY_VIDEO_BITRATE` | 中途改码率(弱网降码率) | `kVTCompressionPropertyKey_AverageBitRate` |
| `PARAMETER_KEY_REQUEST_SYNC_FRAME` | 强制立即产 IDR(丢包恢复) | `kVTEncodeFrameOptionKey_ForceKeyFrame` |

**没有这两个旋钮,WebRTC 在弱网下既不能自适应降码率、也不能花屏后快速恢复**——它们是移动端 RTC 最实际的接口。

---

## 十、FFmpeg 在 Android 的支持边界（关键,最易踩空）

"FFmpeg 跨平台,硬件加速应该也跨"——**Android 上这个想当然会浪费大量时间**。Android 的 FFmpeg 硬件支持**很不对称**:解码成熟好用,编码虽然 6.0 起有了 wrapper 但很弱、生产基本不用(见 [10 §五](./10-移动端硬件编解码.md)):

| | 解码 | 编码 |
|---|---|---|
| **Android** | ✅ `h264_mediacodec` / `hevc_mediacodec` / `vp8_mediacodec` / `vp9_mediacodec` / `av1_mediacodec` | ⚠️ `h264_mediacodec` / `hevc_mediacodec` 编码器(**FFmpeg 6.0+**)存在但能力受限、有设备/API 限制,生产多直调系统 API |
| (对比 iOS) | ✅ hwaccel `-hwaccel videotoolbox` | ✅ 编码器 `h264_videotoolbox` / `hevc_videotoolbox`(成熟) |

### 10.1 解码:能用,但有前提

- 要在**编译时启用** MediaCodec 支持(`--enable-mediacodec --enable-jni`,通常配合 `--enable-decoder=h264_mediacodec`)。
- 运行时要正确建 **`AVMediaCodecContext`** 并**关联一个 Surface**(`av_mediacodec_alloc_context` + `av_mediacodec_default_init` 绑 `ANativeWindow`/Surface),否则可能回落软解或拿不到硬件帧。
- 解码输出的硬件帧形态是 `AV_PIX_FMT_MEDIACODEC`(对应 §八的 Surface),要 CPU 像素得 `av_hwframe_transfer_data` 下载(就破坏零拷贝了,见 [07 §四](./07-硬件编解码.md))。

### 10.2 编码:有 wrapper 但很弱,生产几乎都直调系统 API

FFmpeg 6.0 起有了 `h264_mediacodec` / `hevc_mediacodec` 编码器,但**别指望它**:能力和可控性远不及直调(参数有限、API level 限制、低版本设备输出异常)。Android 硬编实务两条路:

1. **直接调 MediaCodec / AMediaCodec**——**主流做法**,本篇第五~九章那套,可控性最强。
2. 用 FFmpeg 软编 `libx264`——**功耗/发热不可接受**(见 [10 §一](./10-移动端硬件编解码.md)),仅限离线/低分辨率/不在乎电量的场景。

### 10.3 现实架构:混合

```
   FFmpeg 负责:  demux / mux / 协议(RTMP/RTSP/HLS) / 音频解码重采样   <- FFmpeg 的强项
   系统 API 负责: 视频硬编(MediaCodec) / 视频硬解(MediaCodec 或 ffmpeg h264_mediacodec)
```

> **结论:Android 上"FFmpeg 一把梭"是错的。正解是 FFmpeg 做封装/解封装/协议/音频,视频硬编解直调系统 API(MediaCodec/AMediaCodec)。** 编码虽然 FFmpeg 6.0+ 也有 wrapper,但太弱,生产仍以直调为准。

---

## 十一、横向对比:Android vs iOS vs 桌面 NVENC

面试常让你横向对比,做成表最清楚。

### 11.1 Android MediaCodec vs iOS VideoToolbox

| 维度 | Android MediaCodec | iOS VideoToolbox |
|---|---|---|
| 碎片化 | **高**(高通/联发科/三星/海思各异,行为差异大) | **低**(苹果自家芯片,统一) |
| 比特流格式 | **Annex-B**(起始码) | **AVCC**(长度前缀,SPS/PPS 另存) |
| 推 RTP 要不要转格式 | 基本不用(本就 Annex-B) | **必须 AVCC→Annex-B**(经典坑) |
| CPU 可读帧载体 | `ByteBuffer` / `Image`(颜色格式坑多) | `CVPixelBuffer`(锁基址读) |
| 零拷贝载体 | `Surface` / `SurfaceTexture` → OES 纹理 | `CVPixelBuffer`(IOSurface) → Metal |
| 编码会话对象 | `MediaCodec`(configure 成 encoder) | `VTCompressionSession` |
| **FFmpeg 编码 wrapper** | ⚠️ 有(`*_mediacodec`,6.0+)但弱、生产少用 | ✅ 成熟(`h264_videotoolbox`) |
| FFmpeg 解码 wrapper | ✅ 解码器 `*_mediacodec` | ✅ hwaccel `-hwaccel videotoolbox`(无命名解码器) |
| 底层 HAL | OMX / Codec2 → 厂商芯片 | 苹果 Media Engine |

### 11.2 移动 MediaCodec vs 桌面 NVENC

| 维度 | Android MediaCodec | 桌面 NVENC(见 [07](./07-硬件编解码.md)) |
|---|---|---|
| 地位 | **必选项**(软编功耗不可接受) | **优化项**(可选软编 x264 求极致画质) |
| 可调参数 | 少(厂商只暴露码率/GOP/profile 等) | 较多(preset p1-p7、rc、多 pass lookahead 等) |
| 厂商碎片化 | **严重**(多家 SoC) | 单一(NVIDIA) |
| 并发实例 | **有限**(常 1~几路,见 §十二) | 受 license/显存约束,通常更宽松 |
| 帧载体 | Surface / ByteBuffer | CUDA device ptr(`AV_PIX_FMT_CUDA`) |
| FFmpeg 编码支持 | ⚠️ 有 wrapper(6.0+)但弱,生产直调系统 API | ✅ `h264_nvenc` 成熟 |
| 总线 | UMA,无独立总线(慢在 tiling/同步,见 [10 §4.5.3](./10-移动端硬件编解码.md)) | 独显有 PCIe,回读真跨总线(见 [07 §5.5.1](./07-硬件编解码.md)) |

一句话:**移动端 = 硬编必选 + 参数少 + 厂商碎片化 + 实例稀缺;桌面 = 硬编可选 + 参数较多 + 单一厂商 + 资源宽松。**

---

## 十二、Android 特有陷阱表（症状 → 真相/对策）

| 症状 | 真相 / 对策 |
|---|---|
| 编码失败 / 画面绿边 | **分辨率没对齐**——很多硬件编码器要求宽高对齐到 2,部分要 16 的倍数,否则失败或边缘绿边/绿条。编码前把宽高对齐 |
| 多路播放/画中画 `createByCodecName` 失败 | **并发硬件解码实例有限**(常 1~几路)。第 N+1 路拿不到硬件 codec,要么失败要么被分到软 codec。设计上要复用 codec、或主动软解兜底(见 §3.4) |
| 切后台/旋转屏回来花屏或崩溃 | **配置变更/后台丢了 codec 或 Surface 失效**。需要**重建 codec + 重新 `configure` + 重发 SPS/PPS(csd)/请求关键帧**;用了已 release 的 Surface 会崩 |
| 画面马赛克(尤其某些高通机型) | **私有 tiled 格式当 NV12 线性读了**。能用 Surface 就别碰 ByteBuffer;非要读用 `Image`+rowStride,遇 tiled 改 Surface(见 §七) |
| 编解码跑着跑着卡死,dequeue 一直 TRY_AGAIN | **忘了 `releaseOutputBuffer`**,output 池耗尽(§5.4 铁律) |
| HEVC/AV1 在某些设备解不了 | **设备支持参差**。编码/解码前 `MediaCodecList` 查询能力,不能假定支持,**软解兜底** |
| 同样代码换台手机就花屏/行为不同 | **不同 SoC(高通 vs 联发科 vs 三星)行为差异大**——颜色格式、对齐、私有格式都不同。别针对单一机型写死,走 Surface + 能力查询 |
| 解码端中途入会黑屏/花屏 | 编码器**默认只首帧带一次 SPS/PPS**,后续 IDR 不重复带。缓存 CSD,或配置每个 IDR 前注入(见 §6.3、[05 §6.3](./05-H264-MP4-NALU.md)) |
| `KEY_I_FRAME_INTERVAL` 设了不生效/GOP 不对 | 它**单位是秒不是帧**;且部分设备对该值响应不精确,需要时用 `PARAMETER_KEY_REQUEST_SYNC_FRAME` 主动打点 |
| flush 后解码不出图 | flush 丢了所有 buffer 且解码器状态被重置,**要重发关键帧(IDR)**才能恢复(§5.3) |

---

## 十三、面试高频问答

**Q1：MediaCodec 同步模式 vs 异步模式,区别和怎么选?**
同步:自己拿 timeout 轮询 `dequeueInputBuffer`/`dequeueOutputBuffer`,好理解但容易写出忙等、阻塞线程。异步:`setCallback` 注册回调,buffer 可用时系统通知你,无忙等、更适合生产。异步必须在 `configure` 之前 setCallback,且回调里别做重活。

**Q2：为什么 output buffer 必须 `releaseOutputBuffer`?**
output buffer 总数固定,在两条队列间轮转。拿出来不还,池子耗尽,`dequeueOutputBuffer` 一直返回 `INFO_TRY_AGAIN_LATER`,编解码卡死。和 AVFrame 引用计数、buffer pool 是同一个"借了要还"。

**Q3：怎么判断当前用的是硬件还是软件 codec?**
API 29+ 用 `MediaCodecInfo.isHardwareAccelerated()` / `isSoftwareOnly()`;老版本看组件名前缀——`OMX.google.*` / `c2.android.*` 是软件,`OMX.qcom.*` / `c2.qti.*` / `OMX.MTK.*` 等厂商前缀是硬件。

**Q4：OMX 和 Codec2 的区别?为什么 Android 10 要引入 Codec2?**
OMX 是从 Khronos 沿用的老 HAL,buffer 模型和 Android 图形栈(gralloc/dma-buf)衔接差、厂商扩展乱、早期在 mediaserver 进程内。Codec2 是 Google 自研、原生围绕 dma-buf 设计、零拷贝更顺、跑独立沙箱进程。对 App 透明,`MediaCodec` 门面 API 不变,组件名前缀从 `OMX.*` 变 `c2.*`。

**Q5：Android 为什么不能用 FFmpeg 硬件编码?那怎么硬编?**
FFmpeg 在 Android 解码有成熟的 `h264_mediacodec` 等 wrapper;编码虽然 6.0 起也有了 `h264_mediacodec` 编码器,但能力受限、有设备/API 限制,**生产里硬编基本还是直调 MediaCodec/AMediaCodec**。用 FFmpeg 软编 libx264 功耗发热不可接受。现实是混合架构:FFmpeg 做封装/协议/音频,视频硬编解直调系统 API。

**Q6：ByteBuffer 模式为什么花屏?**
颜色格式因芯片而异(I420/NV12/Flexible/私有 tiled),且 stride/slice-height ≠ width/height(有对齐填充)。按 width 紧凑拷贝必然错位花屏;私有 tiled 当线性 YUV 读得马赛克。正解:用 `Image`/`AImage` 按 rowStride/pixelStride 读,真实格式在 `INFO_OUTPUT_FORMAT_CHANGED` 后从 MediaFormat 取;能用 Surface 就别用 ByteBuffer。

**Q7：Surface 模式零拷贝原理?codec 这侧怎么用?**
解码 `configure` 传 Surface、`releaseOutputBuffer(idx, true)` 直接渲染到 Surface,数据不下 CPU;要后处理接 SurfaceTexture 变 OES 外部纹理。编码用 `createInputSurface()` 让相机/GL 直接把 GPU 纹理喂编码器。底层是 BufferQueue + gralloc/dma-buf + fence(见 [10 §4.5](./10-移动端硬件编解码.md))。

**Q8：SPS/PPS 怎么喂给解码器?(CSD)**
通过 `MediaFormat` 的 csd key:H.264 csd-0=SPS、csd-1=PPS;HEVC csd-0=VPS+SPS+PPS 拼一块。`MediaExtractor` 拆 MP4 时 trackFormat 已带好 csd。编码器首帧带 `BUFFER_FLAG_CODEC_CONFIG` 吐出 CSD,要单独存好。

**Q9：Annex-B 还是 AVCC,Android 用哪个?和 iOS 的区别?**
Android MediaCodec 喂入/吐出都是 **Annex-B**(起始码);iOS VideoToolbox 是 **AVCC**(长度前缀 + SPS/PPS 另存 format description)。所以 Android 推 RTP 基本不用转(本就 Annex-B),iOS 必须做 AVCC→Annex-B。

**Q10：设备并发解码实例有限会怎样?**
硬件解码器同时只能开有限路(常 1~几路)。多路播放/画中画时第 N+1 路 `createByCodecName` 失败或被分到软 codec。要复用 codec 实例、控制并发数、做软解兜底。

**Q11：编码器运行中能改码率/强制关键帧吗?WebRTC 怎么用?**
能。`setParameters` + `PARAMETER_KEY_VIDEO_BITRATE` 动态降码率(对应拥塞控制 GCC);`PARAMETER_KEY_REQUEST_SYNC_FRAME` 强制立即产 IDR(对应对端 PLI/FIR 丢包恢复)。没这两个旋钮 WebRTC 弱网下没法自适应。

---

## 十四、学习路径（Android 具体化,呼应 10 §八阶段3）

> 前提:先走完 [07](./07-硬件编解码.md) 的命令行 + FFmpeg HWAccel、读完 [10](./10-移动端硬件编解码.md) 建立移动端地图。

**第 1 步:解码显示(先走 Surface,避开颜色格式坑)**
`MediaExtractor` 拆 MP4 → `getTrackFormat` 拿到带 csd 的 `MediaFormat` → `MediaCodec.createDecoderByType` + `configure(format, surface, …)` → 循环 dequeue,`releaseOutputBuffer(idx, true)` 直接显示到 `SurfaceView`。**先 Surface 路径**,一次性绕开颜色格式和零拷贝两个坑。

**第 2 步:编码(createInputSurface)**
相机预览/GL 生成画面 → `configure(…, CONFIGURE_FLAG_ENCODE)` → `createInputSurface()` 拿输入 Surface,EGL 包它,正常 GL 绘制 → 输出侧取编码数据,处理首帧 `BUFFER_FLAG_CODEC_CONFIG`(存 CSD) → 写 `.h264`/MP4。用 ffmpeg 命令行验证产出能正常解码播放。

**第 3 步:踩 ByteBuffer + Image 的 stride 坑**
把第 1 步改成非 Surface 输出,`getOutputImage()` 按 rowStride/pixelStride 读 YUV,亲手踩一次 stride/私有格式坑——理解为什么生产要优先 Surface。

**第 4 步:接入 WebRTC**
把上面理解对到 libwebrtc 的 `VideoEncoderFactory`/`VideoDecoderFactory`,看 WebRTC 怎么封装 MediaCodec、接进 RTP 链路、用动态码率和强制 IDR 做拥塞控制/丢包恢复(见 §9.2、[10 §六](./10-移动端硬件编解码.md))。

```
MediaExtractor 拆 MP4 ──► csd ──► MediaCodec 配 Surface 解码显示   [第1步,避坑]
                                          │
                                          ▼
              createInputSurface() 编码 ──► 写裸流/MP4            [第2步]
                                          │
                                          ▼
              ByteBuffer + Image 读 YUV(踩 stride 坑)            [第3步,理解坑]
                                          │
                                          ▼
              WebRTC VideoEncoder/DecoderFactory 接入 RTP         [第4步,合流]
```

---

## 十五、自检

1. `MediaCodec` 是编码器吗?它和底层 codec 组件、OMX/Codec2、厂商硬件单元是什么关系?
2. OMX 和 Codec2 的区别是什么?Android 10 为什么引入 Codec2?对 App 代码有影响吗?
3. 给你一个手机,怎么判断某路视频用的是硬件还是软件 codec?(API 29 前后两种办法)
4. 系统在哪些情况下会把硬解"回退"成软解?怎么排查?
5. `MediaCodec` 的生命周期状态机有哪些状态?`flush`/`stop`/`reset`/`release` 各做什么、之后处于什么状态?
6. 为什么 output buffer 必须 release?不 release 会怎样?`releaseOutputBuffer` 第二参 true/false 区别?
7. `dequeueOutputBuffer` 的三个负数返回值各是什么?`INFO_OUTPUT_FORMAT_CHANGED` 为什么对颜色格式很关键?
8. H.264 和 HEVC 的 CSD 怎么放?(csd-0/csd-1 vs VPS+SPS+PPS 拼一块)编码器的 CSD 怎么吐出来、怎么处理?
9. Android 的视频比特流是 Annex-B 还是 AVCC?和 iOS 相反带来什么工程差异(推 RTP)?
10. ByteBuffer 模式为什么花屏?stride/slice-height 是什么?正确取像素怎么做?什么时候根本不该用 ByteBuffer?
11. Surface 解码零拷贝在 codec 这侧怎么用?编码的 `createInputSurface()` 解决了什么?
12. `KEY_I_FRAME_INTERVAL` 单位是什么?运行时怎么动态降码率、强制关键帧?对应 WebRTC 的什么机制?
13. FFmpeg 在 Android 能硬件**编码**吗?能**解码**吗?Android 硬编正确做法是什么?现实架构怎么分工?
14. Android MediaCodec vs iOS VideoToolbox,在碎片化、比特流格式、FFmpeg 编码 wrapper 上各有什么不同?
15. 并发硬件解码实例有限会导致什么问题?切后台/转屏后为什么要重建 codec 并重发 SPS/PPS?

---

> 一句话总结:**`MediaCodec` 是 Android 统一编解码门面,底下经 OMX(老)/Codec2(新)派给厂商硬件单元或软件实现;用好它的关键是握住"缓冲区队列 + 生命周期状态机 + releaseOutputBuffer 铁律"的心智模型,优先 Surface 零拷贝避开颜色格式坑,记住 Android 吐 Annex-B、FFmpeg 在 Android 解码成熟而编码弱(6.0+ 有 wrapper 但生产多直调系统 API)——硬编实务以直调为准。**


