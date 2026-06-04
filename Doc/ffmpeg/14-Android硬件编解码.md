# 14 - Android 硬件编解码深入（MediaCodec / AMediaCodec）

> 这是 [10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 的 **Android 专题深挖篇**。10 是移动端总览（Android + iOS 一篇讲完），本篇把 Android 这一侧拆开、讲透、做成面试导向——目标是读完能应对面试里关于 Android 硬编硬解的绝大部分问题。
> 本篇覆盖：MediaCodec 在 Android 媒体栈里的位置、OMX → Codec2 演进、硬件 codec vs 软件 codec 怎么区分与选择、Java `MediaCodec` vs NDK `AMediaCodec`、缓冲区队列模型与生命周期状态机、CSD（SPS/PPS）、颜色格式坑、Surface 零拷贝、编码参数与运行时控制、FFmpeg 在 Android 的支持边界、和 iOS / NVENC 的横向对比、Android 特有陷阱、面试高频问答、学习路径。
> 前置：先读 [07-硬件编解码.md](./07-硬件编解码.md) 建立"硬件帧 vs 软件帧"的核心直觉；本篇频繁引用 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) 的 Annex-B / SPS/PPS、[02-像素格式与内存布局.md](./02-像素格式与内存布局.md) 的 NV12 / stride、[06-编码参数与码控.md](./06-编码参数与码控.md) 的 GOP / CBR / 低延迟。**零拷贝底层原理（UMA 为何仍慢、dma-buf/gralloc/fence）在 [10 §四点五](./10-移动端硬件编解码.md#四点五ios-与-android-的零拷贝核心考点) 已讲透，本篇只做 Android 编解码这一侧的用法，不重复底层。**
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

> 十有八九是 **ByteBuffer 模式下的颜色格式 / stride 坑**。问题有两层：第一，输出的 YUV 颜色格式因芯片而异，有 I420、有 NV12、还有厂商私有的 tiled 排布，你拿 NV12 的方式去读 tiled 的数据，直接马赛克；第二，就算格式对了，**每行还有对齐填充，stride 和 slice-height 不等于 width 和 height**，你按 width 紧凑拷贝必然错位花屏。正确做法是用 `Image`（NDK 是 `AImage`）按 `rowStride` / `pixelStride` 一行行读。但**最省事的是压根别用 ByteBuffer——能用 Surface 输出就用 Surface**，把颜色格式的烂摊子整个甩给系统。

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

```
画面花屏/错位/马赛克
   |
   v
1. 是不是 ByteBuffer 模式? --是否能改 Surface?--> 能 -> 直接上 Surface,坑全消失(§八)
   |  (确实要 CPU 像素才往下)
   v
2. 收到 INFO_OUTPUT_FORMAT_CHANGED 了吗? -> 没收到就读不到真实 stride
   |
   v
3. 用 getOutputImage()/AImage 了吗? -> 没用 -> 改用,按 rowStride/pixelStride 读
   |
   v
4. 还是马赛克? -> 大概率厂商私有 tiled 格式 -> 别 memcpy 当线性读;改 Surface 或换 Flexible
```

**一句话最佳实践:能用 Surface 就别用 ByteBuffer**——把颜色格式的麻烦整个甩给系统(下一节)。只有确需 CPU 像素(算法分析、截图)才用 ByteBuffer,且老实用 `Image`/`AImage`。

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
