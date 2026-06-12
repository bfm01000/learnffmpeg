# iOS 音视频开发全景导读

> **适用方向**：iOS 移动端音视频 SDK 开发（采集→编码→推流 / 拉流→解码→渲染 全链路）
> **前置知识**：有 Android 音视频开发经验（MediaCodec / Camera2 / OpenGL ES），需要补齐 iOS 端对应技术栈
> **难度分层**：中级（必须掌握）标 🟢 / 高级（进阶加分）标 🟡 / 专家深水区标 🔵
> **预计阅读**：速记 10 分钟｜全文 30 分钟
> **关联知识**：[[../ffmpeg/00-FFmpeg全景导读]] — 音视频通用底座；[[../ffmpeg/15-iOS硬件编解码]] — VideoToolbox 专题；[[../OC/【重点】OC面试高频考点与标准回答大全]] — OC 语言基础

---

## 一、全景导读：iOS 媒体栈在技术版图里的位置

### 1.1 从场景说起

你就是做移动端跨平台音视频 SDK 的，已经在 Android 上把 MediaCodec 硬编、Camera2 采集、OpenGL 渲染这套链路摸熟了。现在有一个需求：**同一个 C++ 核心引擎，也要在 iOS 上跑起来**——iPhone 摄像头采集画面、硬编码成 H.264、推 RTMP、同时预览渲染。

如果你一上来就翻 AVFoundation 的 API 文档，你会迅速迷失在 `AVCaptureSession`、`AVCaptureDevice`、`AVCaptureVideoDataOutput`、`CMSampleBuffer`、`CVPixelBuffer`、`CMVideoFormatDescription`、`VTCompressionSession`、`CVMetalTextureCache` 这一大陀概念里。

这时候你真正需要的是**一张地图**——iOS 整个媒体栈长什么样？每一层负责什么？和 Android 的那一套怎么对应？

### 1.2 iOS 媒体栈是什么 / 它从哪来

**一句话定义**：iOS 媒体栈是 Apple 从硬件到应用层的一套完整音视频处理体系，从上到下分三层——**高层黑盒（AVFoundation）、中层精细控制（VideoToolbox / AudioToolbox / CoreMedia）、底层硬件（Media Engine）**。

**产生背景**：

- 2008 年 iPhone OS 2.0 发布时，开发者只能用 `UIImagePickerController` 拍个照、录个视频，完全没有底层控制。
- 2010 年 iOS 4.0 引入 **AVFoundation**，第一次让第三方开发者能控制摄像头采集和视频编解码。
- 2014 年 iOS 8.0 发布 **VideoToolbox**（之前是私有框架），开放硬件编解码能力给第三方——这是 iOS 音视频开发的真正分水岭。同期的 **Metal** 替代了 OpenGL ES 成为更底层的 GPU API。
- 2016 年 iOS 10.0 开始，VideoToolbox 支持 HEVC（H.265）硬编解。

**与 Android 的根本差异**：


| 维度      | Android                                     | iOS                                             |
| ------- | ------------------------------------------- | ----------------------------------------------- |
| 硬件芯片    | 高通/联发科/三星/麒麟，各家行为不一                         | Apple Silicon 一家，行为一致                           |
| 颜色格式    | NV12/NV21/YUV420P 都可能，碎片化严重                 | 基本固定 NV12（BiPlanar）                             |
| 硬编解码    | MediaCodec（同步/异步两套模式）                       | VideoToolbox（异步回调模式）                            |
| 零拷贝     | AHardwareBuffer + Surface                   | IOSurface + CVPixelBuffer + CVMetalTextureCache |
| GPU API | OpenGL ES / Vulkan                          | Metal（OpenGL ES 2018 年起 deprecated）             |
| 音频      | AudioTrack/AudioRecord + OpenSL ES / AAudio | AudioUnit / AVAudioEngine                       |
| 构建生态    | CMake + NDK 交叉编译                            | Xcode + clang，framework 分发                      |


**今天的位置**：iOS 的媒体栈是业界公认**行为最一致、坑最少**的移动端音视频平台——代价是只能在 Apple 设备上跑。

### 1.3 三层架构全景图

```
┌─────────────────────────────────────────────────────────────────┐
│ 高层黑盒：AVFoundation                                           │
│ AVCaptureSession（采集）· AVAssetWriter（录制）· AVPlayer（播放）   │
│ → 省心但控制不了比特流，适合简单拍录播场景                         │
├─────────────────────────────────────────────────────────────────┤
│ 中层精细控制：                                                   │
│ VideoToolbox（硬编解码）· AudioToolbox（音频处理）                 │
│ CoreMedia（CMSampleBuffer / CMTime / CMFormatDescription）      │
│ CoreVideo（CVPixelBuffer / CVOpenGLESTextureCache /             │
│           CVMetalTextureCache）                                 │
│ → 逐帧控制比特流，适合实时推流/RTC/低延迟场景                      │
├─────────────────────────────────────────────────────────────────┤
│ 底层硬件：Apple Media Engine + GPU（Apple Silicon）              │
│ → 物理编解码器 + 显示引擎 + ISP（图像信号处理器）                  │
└─────────────────────────────────────────────────────────────────┘
```

**记住一条原则**：AVFoundation 底层也是调用 VideoToolbox / AudioToolbox——它只是帮你把常见操作封装了。**当你需要精细控制比特流（直播推流、RTC、自定义滤镜链），你就必须下到中层**。

### 1.4 Android → iOS 技术对应表

你已经有 Android 音视频开发经验，最快的学习方式是把 iOS 的概念映射到你已经懂的 Android 概念上：


| 你要做的事         | Android 你用                        | iOS 你用                                          |
| ------------- | --------------------------------- | ----------------------------------------------- |
| 打开摄像头采 YUV    | Camera2 + ImageReader / Surface   | **AVCaptureSession** + AVCaptureVideoDataOutput |
| 硬编码 H.264     | MediaCodec（async mode）            | **VTCompressionSession**（异步回调）                  |
| 硬解码 H.264     | MediaCodec（async mode）            | **VTDecompressionSession**（异步回调）                |
| 编码后拿到比特流      | MediaCodec.BufferInfo             | CMSampleBuffer → **CMBlockBuffer**              |
| 解码后拿到像素       | MediaCodec output Surface / Image | **CVPixelBuffer** → Metal texture               |
| GPU 渲染        | OpenGL ES / EGL                   | **Metal** / **MTKView**                         |
| GPU 零拷贝访问 YUV | EGLImage + AHardwareBuffer        | **CVMetalTextureCache** + IOSurface             |
| 音频播放          | AudioTrack                        | **AudioUnit**（RemoteIO）                         |
| 音频录制          | AudioRecord                       | **AudioUnit**（RemoteIO）                         |
| 像素格式转换        | 自己写 shader / libyuv               | **vImage** / Metal shader / libyuv              |
| 屏幕录制          | MediaProjection                   | **ReplayKit**                                   |
| 线程模型          | JNI 线程 attach                     | GCD queue / **AVFoundation 的回调队列**              |


### 1.5 关联技术地图

```
iOS 音视频开发
│
├── 采集层 ── AVFoundation Capture ──── 关联 ── CoreMedia（CMSampleBuffer 承载一切）
│           ──── iOS 没有 Camera2 那样可自由配 session 的多路并发，采集只能主相机
│
├── 编码层 ── VideoToolbox ──────────── 关联 ── CoreVideo（CVPixelBuffer 是编码的输入）
│          ──── AudioToolbox（音频编码）── 关联 ── AudioConverter
│          ─────────────────────────── 见 [[../ffmpeg/15-iOS硬件编解码]]
│
├── 渲染层 ── Metal / MTKView ──────── 关联 ── CVMetalTextureCache（零拷贝 YUV→RGB）
│          ── CoreAnimation ────────── 关联 ── CALayer / CAEAGLLayer（传统方式，不推荐）
│
├── 音频层 ── AudioUnit ────────────── 关联 ── AudioSession（系统级音频策略）
│          ── AVAudioEngine ────────── 关联 ── 是对 AudioUnit 的 Swift/OC 封装
│
├── 存储层 ── AVAssetWriter ─────────── 关联 ── CMSampleBuffer 直接塞进去
│          ── Photos.framework ─────── 关联 ── 写入系统相册
│
└── 传输层 ── 与 Android 完全一致（RTMP/WebRTC/SRT 都用 C++ 跨平台实现）
```

### 1.6 学习优先级总览


| 层级       | 内容                                                                  | 重要度    | 为什么                                      |
| -------- | ------------------------------------------------------------------- | ------ | ---------------------------------------- |
| 🟢 中级必会  | **AVFoundation 采集**（AVCaptureSession 全流程）                           | 🔥🔥🔥 | 拍摄/直播的第一步，面试问「iOS 端怎么采视频」必答              |
| 🟢 中级必会  | **VideoToolbox 编解码**（VTCompressionSession / VTDecompressionSession） | 🔥🔥🔥 | iOS 硬编硬解唯一方式，见 [[../ffmpeg/15-iOS硬件编解码]] |
| 🟢 中级必会  | **CMSampleBuffer / CVPixelBuffer 理解**                               | 🔥🔥🔥 | iOS 所有帧数据都在这俩里流转，不懂等于没法干活                |
| 🟢 中级必会  | **Metal 基础渲染**（MTKView + 简单 shader）                                 | 🔥🔥🔥 | 渲染 YUV 到屏幕，iOS 上 OpenGL ES 已废弃           |
| 🟡 高级加分  | **CVMetalTextureCache 零拷贝**                                         | 🔥🔥   | 高性能场景必须，但普通预览不优化也能跑                      |
| 🟡 高级加分  | **AudioUnit 音频采集与播放**（RemoteIO 双向）                                  | 🔥🔥   | RTC/直播场景必要，纯视频播放可用高层 API                 |
| 🟡 高级加分  | **AudioSession 管理**（中断/路由切换）                                        | 🔥🔥   | 保证产品级音频体验的必修课                            |
| 🟡 高级加分  | **GPUImage 滤镜链**                                                    | 🔥🔥   | 移动端特效/美颜的常用框架                            |
| 🔵 专家深水区 | **IOSurface 跨进程共享**                                                 | 🔥     | 系统级优化，多数 SDK 不需要直接碰                      |
| 🔵 专家深水区 | **Core Audio 底层**（AudioQueue / AudioConverter 细节）                   | 🔥     | 做音频引擎才会深入                                |
| 🔵 专家深水区 | **ReplayKit 屏幕录制**                                                  | 🔥     | 特定场景才需要                                  |


> **你作为从 Android 转过来的开发者，建议路线**：先看 §1.4 的映射表建立大局观 → 看本文 §二 面试速记 → 看 §三「采集→编码→渲染」端到端流程 → 分别深入 01（AVFoundation 采集）/ 15（VideoToolbox）/ 02（AudioUnit）。

---

## 二、面试速记（考前 15 分钟扫一遍）

### 2.1 高频考点速查


| #   | 考点                              | 一句话答案                                                                              | 出现频率   | 难度  |
| --- | ------------------------------- | ---------------------------------------------------------------------------------- | ------ | --- |
| 1   | AVFoundation 和 VideoToolbox 的区别 | AVFoundation 是高层黑盒（录/播），VideoToolbox 是中层（逐帧控比特流）                                   | 🔥🔥🔥 | 🟢  |
| 2   | CVPixelBuffer 是什么               | iOS 的图像内存容器，底层是 IOSurface，可零拷贝跨 GPU/编码器共享                                          | 🔥🔥🔥 | 🟢  |
| 3   | iOS 怎么采 YUV 数据                  | AVCaptureSession → AVCaptureVideoDataOutput → 回调给 CMSampleBuffer → 取 CVPixelBuffer | 🔥🔥🔥 | 🟢  |
| 4   | VideoToolbox 编码完怎么拿数据           | 异步回调给 CMSampleBuffer → CMBlockBufferGetDataPointer 取比特流                            | 🔥🔥🔥 | 🟢  |
| 5   | iOS 怎么渲染 YUV 到屏幕                | CVMetalTextureCache 把 NV12 两个平面 alias 成两张 MTLTexture → shader 转 RGB                | 🔥🔥   | 🟡  |
| 6   | 音频怎么采集和播放                       | AudioUnit RemoteIO 的 input callback（录音）和 render callback（播放）                       | 🔥🔥   | 🟡  |
| 7   | 为什么 iOS 推流要转 Annex-B            | VideoToolbox 吐 AVCC，RTMP/RTP 要 Annex-B（起始码 + SPS/PPS 内联）                           | 🔥🔥   | 🟢  |
| 8   | iOS 和 Android 做跨平台的最⼤差异         | 比特流格式相反 + GPU API 不同（Metal vs OpenGL）+ 线程模型不同                                      | 🔥🔥   | 🟡  |


### 2.2 面试标准回答

#### Q1：你做过 iOS 端的视频采集和编码吗？整个链路是怎么样的？

**面试官想听什么：** 你有没有从零搭过 iOS 端的采集→编码→推流/渲染链路，能不能说清每个环节的数据形态。

**🗣️ 标准回答（可背诵）：**

> "我在跨平台 SDK 项目里负责过 iOS 端的采集和编码部分。整个链路是：先用 AVCaptureSession 配置前后摄像头，创建 AVCaptureVideoDataOutput 拿到 YUV 数据——iOS 摄像头默认输出 NV12 格式的 CVPixelBuffer。拿到之后分两路：预览路线走 CVMetalTextureCache 把 CVPixelBuffer 零拷贝映射成 Metal 纹理，在 shader 里做 NV12→RGB 转换后渲染到 MTKView；编码路线直接把同一个 CVPixelBuffer 喂给 VTCompressionSession，设置好 RealTime 和 AllowFrameReordering=false 这两个低延迟旋钮，编码完成后的回调里收到 CMSampleBuffer，从里面取出 H.264 比特流。这里有一个关键点——VideoToolbox 吐出来是 AVCC 格式，SPS/PPS 存在 CMVideoFormatDescription 里而不是数据流里。所以推 RTMP 之前必须做两件事：用 CMVideoFormatDescriptionGetH264ParameterSetAtIndex 取出 SPS/PPS，再把每个 NALU 的 4 字节长度前缀换成起始码，转成 Annex-B。这个和 Android 正好相反——MediaCodec 本身就是 Annex-B，不需要转。"

**👨‍💻 追问预警：**

> Q: "为什么不用 AVCaptureMovieFileOutput 直接录文件？"
> A: 因为 MovieFileOutput 是黑盒，拿不到原始 YUV 和编码中间的比特流——做实时推流、自定义滤镜、美颜特效这些都需要在中层拦截数据。AVCaptureVideoDataOutput 才能在每一帧回调里拿到 CVPixelBuffer。

**⚠️ 常见误区：** 很多人以为 iOS 摄像头输出是 YUV420P（三平面），实际是 NV12（Y 平面 + UV 交错平面），如果你按三平面去读会读到脏数据。

---

#### Q2：CVPixelBuffer 和 CMSampleBuffer 是什么关系？

**面试官想听什么：** 你是否理解 iOS 核心数据容器的层次关系。

**🗣️ 标准回答（可背诵）：**

> "它俩是包含关系——CMSampleBuffer 是上层容器，里面可以装压缩数据或者原始数据。CMSampleBuffer 包含三个东西：CMTime 时间戳、CMVideoFormatDescription 格式描述，以及实际的图像数据。如果它是视频帧，图像数据就是 CVPixelBuffer；如果它是编码后的 H.264 帧，图像数据就是 CMBlockBuffer。可以理解为 CMSampleBuffer 是快递包裹，地址贴纸是 CMTime 和 FormatDescription，里面装的东西可能是 CVPixelBuffer（原始 YUV）也可能是 CMBlockBuffer（压缩比特流）。分辨率、编码格式这些信息存在 CMVideoFormatDescription 里，所以不用四处传上下文——iOS 的设计理念就是让这个 buffer 自描述、自包含。"

**👨‍💻 追问预警：**

> Q: "为什么要用 CVPixelBufferPool？"
> A: 避免每帧 alloc/free 的内存抖动。池子里预分配 N 个 CVPixelBuffer，取出来用完放回去——和 FFmpeg 里循环 unref 复用 AVFrame 是同一个思想。

---

#### Q3：iOS 端怎么做到采集到渲染的零拷贝？

**面试官想听什么：** 你是否理解 iOS 上的零拷贝机制和 IOSurface 的作用。

**🗣️ 标准回答（可背诵）：**

> "iOS 零拷贝的核心是 IOSurface——它是内核管理的一块共享图形内存，可以同时被摄像头 ISP 写、被 GPU 读、被 VideoToolbox 读，全程一份数据不下 CPU。具体做法：创建 AVCaptureVideoDataOutput 的时候不做任何格式转换，直接吐出 NV12 的 CVPixelBuffer。这个 buffer 底层就是 IOSurface。然后通过 CVMetalTextureCache 创建两个纹理——亮度平面映射成一张 R8 格式的 MTLTexture，色度平面映射成一张 RG8 格式的 MTLTexture——在 Metal shader 里采样这两个纹理把 YUV 转成 RGB，整个过程数据没有离开过 GPU 显存。对比 Android 的 AHardwareBuffer + EGLImage + Surface 零拷贝方案，思路一样但 iOS 的 API 更统一。这里有个容易踩的坑：创建 AVCaptureVideoDataOutput 时如果指定了非原生 pixel format，系统会做一次内部拷贝转格式——零拷贝直接就断了。所以 format 一定要设成摄像头的原生格式 NV12。"

**👨‍💻 追问预警：**

> Q: "如果我不用 Metal 而是用 OpenGL ES 呢？"
> A: OpenGL ES 在 iOS 上从 2018 年就被标记 deprecated 了，新项目建议直接用 Metal。如果必须兼容，用 CVOpenGLESTextureCache 替代 CVMetalTextureCache——原理完全一样，只是 bind 到 GL 纹理而不是 Metal 纹理。

---

#### Q4：iOS 端的音频采集和播放怎么做？

**面试官想听什么：** 你有没有用过 AudioUnit，理解它的回调模型。

**🗣️ 标准回答（可背诵）：**

> "iOS 做实时音频一般用 AudioUnit 的 RemoteIO 类型。它一个 AudioUnit 同时负责录音和播放——输入端接麦克风，通过 input callback 给你送 PCM 数据；输出端接扬声器，通过 render callback 问你拿 PCM 数据去播。这两个 callback 都跑在系统的高优先级音频线程上，所以绝对不能在里面做加锁、分配内存、或者任何可能阻塞的操作。通常的做法是用一个无锁环形缓冲（ring buffer）把音频线程的 PCM 数据传给业务线程处理。配置 AudioUnit 的时候，AudioStreamBasicDescription 很关键——采样率一般 44100 或 48000，格式通常用 16 位整数交错（LinearPCM + SignedInteger），声道数按场景。另外 iOS 还有一个 AudioSession 的概念，控制整个 App 的音频行为——比如电话来了要不要打断、锁屏要不要停、要不要和其他 App 混音——这些都要设对，否则上架审核或者用户体验都会出问题。"

**👨‍💻 追问预警：**

> Q: "为什么不用 AVAudioEngine？"
> A: AVAudioEngine 是对 AudioUnit 的高层封装，省心但灵活性差。做实时推流/RTC 需要精细控制音频缓冲区大小和回调时机时，还是直接调 AudioUnit 更可控。

---

#### Q5：iOS 和 Android 做跨平台音视频 SDK，代码结构怎么设计？

**面试官想听什么：** 你的跨平台架构能力——怎么隔离平台差异。

**🗣️ 标准回答（可背诵）：**

> "核心思路是 C++引擎 + 平台桥接层。C++ 层负责所有不依赖平台的东西：编码参数计算、码率控制算法、音视频同步逻辑、网络推流、FFmpeg 软编软解。iOS 端用 Objective-C++写桥接层，Android 端用 JNI 写桥接层，各端负责各自平台的采集和硬编解码。数据在桥接层统一转成 AVFrame 或自定义的 VideoFrame 结构进入 C++ 引擎。因为 iOS 和 Android 的比特流格式是反的——VideoToolbox 吐 AVCC、MediaCodec 吐 Annex-B——桥接层的编码输出要做一次格式统一，通常都统一成 Annex-B 进 C++推流层。渲染方面差异最大：iOS 用 Metal、Android 用 OpenGL ES，但 C++ 引擎只负责产出 YUV，具体的 YUV→RGB 转换和纹理提交由各端自己的渲染模块做，共用同一组 shader 逻辑。"

**👨‍💻 追问预警：**

> Q: "那你写的 KMP 项目在 iOS 侧是怎么桥接的？"
> A: KMP 在 iOS 侧导出的是 Objective-C framework，通过 Kotlin/Native 的 C-interop 调用 C++引擎。但实际上 KMP 对 C++ 的支持还不成熟，生产环境里更稳的做法还是 Objective-C++直接调用 C++ ——这也是我们当时 KMP SDK 验证阶段发现的最大挑战之一。

---

### 2.3 一个「串起来」的综合回答模板

**面试官问**：「你给我们完整讲讲 iOS 端从摄像头打开到画面推出去整个链路吧。」

> "好，我按数据流向来讲。首先 AVFoundation 采集层——用一个 AVCaptureSession，把后置摄像头设为输入，加一个 AVCaptureVideoDataOutput 作为视频输出。输出回调跑在一个串行队列上，每一帧给我一个 CMSampleBuffer，我从中取出 CVPixelBuffer，格式是 NV12。
>
> 然后是编码层——把 CVPixelBuffer 连同它的 PTS 一起喂给 VTCompressionSession。编码是异步的，结果在回调里回来，也是 CMSampleBuffer，但包的是 CMBlockBuffer（H.264 比特流）。拿到后把 AVCC 转 Annex-B，SPS/PPS 在关键帧前注入。
>
> 预览是并行的——CVMetalTextureCache 把同一个 CVPixelBuffer 映射成 Metal 纹理，在 MTKView 的 draw 回调里渲染。因为是同一个 IOSurface，预览和编码之间零拷贝。
>
> 编码后的 Annex-B 数据进入我们的 C++推流引擎，封装成 FLV tag 通过 RTMP 发出去。以上——AVFoundation 采 → VideoToolbox 编 → Metal 渲染 + C++ 推流。您可以深入了解任何一个环节。"（🟢 中级讲到这里够了，🟡 高级可以补充 IOSurface、CVMetalTextureCache 内部细节）

---

## 三、端到端：一帧画面从 iPhone 摄像头到屏幕

这是 iOS 上最经典的一条链路——摄像头采集 → 预览渲染 + H.264 编码：

```
[1] AVCaptureSession startRunning
        |
        v
[2] 摄像头 ISP 输出 NV12 的 CVPixelBuffer（底层是 IOSurface）
    回调到 AVCaptureVideoDataOutput 的串行队列
        |
        v
[3] 在回调里拿到 CMSampleBuffer
    从中取出:
    - CVPixelBuffer（NV12 YUV 数据，GPU 显存里）
    - CMTime（PTS，摄像头硬件时钟）
        |
        +──── 预览路线 ────+──── 编码路线 ────+
        |                                        |
        v                                        v
[4p] CVMetalTextureCache               [4e] VTCompressionSessionEncodeFrame
     alias NV12 → 两张 MTLTexture            输入同一个 CVPixelBuffer + PTS
        |                                        |
        v                                        v
[5p] Metal shader 做 YUV→RGB            [5e] 异步回调拿 CMSampleBuffer
     写入 MTKView 的 drawable                里面是 CMBlockBuffer（H.264）
        |                                        |
        v                                        v
[6p] MTKView 提交到 display             [6e] CMBlockBufferGetDataPointer
                                           取 H.264 比特流
                                              |
                                              v
                                       [7e] AVCC → Annex-B 转换
                                            从 CMVideoFormatDescription
                                            取 SPS/PPS，在每个 IDR 前注入
                                              |
                                              v
                                       [8e] 推流 / 写文件
```

**关键数据形态变化**：


| 环节   | 数据类型                           | 格式              | 位置                         |
| ---- | ------------------------------ | --------------- | -------------------------- |
| 采集输出 | CMSampleBuffer → CVPixelBuffer | NV12 (BiPlanar) | GPU 显存（IOSurface）          |
| 编码输入 | CVPixelBuffer                  | NV12            | GPU 显存（零拷贝）                |
| 编码输出 | CMSampleBuffer → CMBlockBuffer | H.264 AVCC      | CPU 内存                     |
| 转换后  | raw bytes                      | H.264 Annex-B   | CPU 内存                     |
| 预览输入 | MTLTexture（两张）                 | R8 + RG8        | GPU 显存（alias 同一 IOSurface） |


---

## 四、自检题（合上文档能回答吗？）

1. iOS 媒体栈的三层架构是什么？每一层的代表框架有哪些？
2. AVFoundation 和 VideoToolbox 分别什么时候用？能不能简单说一句判断标准？
3. CVPixelBuffer 和 CMSampleBuffer 什么关系？各自的底层存储是什么？
4. 你从 AVCaptureVideoDataOutput 的回调里拿到一个 CMSampleBuffer，怎么从中取出 YUV 数据和 PTS？
5. iOS 上采集一帧 YUV 数据，如果不做任何处理直接渲染到屏幕，要经过哪些关键 API 调用？
6. 为什么 VideoToolbox 编码的输出不能直接推 RTMP？要做什么转换？漏掉 SPS/PPS 会怎样？
7. CVMetalTextureCache 的作用是什么？为什么需要创建两张纹理而不是一张？
8. iOS 和 Android 在比特流格式上有什么相反的设定？这对跨平台代码意味着什么？
9. AudioUnit 的 RemoteIO 是什么？input callback 和 render callback 各自干什么？
10. 你有一个已经在 Android 上跑通的 C++ 音视频引擎，怎么给它加 iOS 端支持？架构上怎么分层？

能流畅回答 7/10 以上，iOS 音视频开发的框架感已经建立。

---

## 🎯 一句话总结

> iOS 音视频开发就是读懂三层架构：AVFoundation 省心但黑盒，VideoToolbox/AudioToolbox/CoreMedia 精细但繁琐，Metal 是唯一 GPU 门票——好在全家桶行为一致，没有 Android 的碎片化噩梦。

## 🔗 关联文档

- [[../ffmpeg/15-iOS硬件编解码]] — VideoToolbox 编解码全流程 + 零拷贝 IOSurface 专讲
- [[01-AVFoundation采集详解]] — AVCaptureSession 从配置到回调的完整指南
- [[02-AudioUnit与音频处理详解]] — AudioUnit RemoteIO 音频采集与播放
- [[03-GPUImage滤镜链详解]] — GPUImage 滤镜链框架速成
- [[../ffmpeg/00-FFmpeg全景导读]] — 音视频通用地基（Android/iOS 通用）
- [[../OC/【重点】OC面试高频考点与标准回答大全]] — Objective-C 语言基础（iOS 开发的前提）

