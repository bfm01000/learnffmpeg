# Android 音视频开发全景导读

## 0. 本篇定位

- 面试复习：先记住 Android 媒体栈的三层分工、`Surface`/`BufferQueue`/`MediaCodec` 的关系，以及 Android 相比 iOS 的碎片化和状态机复杂度。
- 深入学习：重点看采集、编码、渲染、音频、推流如何拼成实时链路，并把每个模块回链到后续专题文档。
- 中高级加分点：不要只说“会用 API”，要能解释为什么 Android 高性能视频链路通常围绕 `Surface`、`GraphicBuffer` 和 GPU/硬件编码器组织。
> **适用方向**：Android 移动端音视频 SDK 开发（采集→编码→推流 / 拉流→解码→渲染 全链路）
> **前置知识**：有 C/C++ 基础，了解音视频基本概念（编码/解码/渲染）
> **难度分层**：中级（必须掌握）标 🟢 / 高级（进阶加分）标 🟡 / 专家深水区标 🔵
> **预计阅读**：速记 10 分钟｜全文 30 分钟
> **关联知识**：[[../ffmpeg/14-Android硬件编解码]] — MediaCodec 专题深挖；[[../ffmpeg/00-FFmpeg全景导读]] — 音视频通用底座；[[../JNI/00-导读与索引]] — JNI 跨语言桥接

---

## 一、全景导读：Android 媒体栈在技术版图里的位置

### 1.1 Android 媒体栈三层架构

```
┌─────────────────────────────────────────────────────────────────┐
│ 高层黑盒：MediaPlayer / MediaRecorder / ExoPlayer               │
│ → 省心但管控不了比特流，适合简单的播放/录制场景                    │
├─────────────────────────────────────────────────────────────────┤
│ 中层精细控制：                                                   │
│ Camera2（采集）· MediaCodec（硬编解码）· MediaMuxer（封装）       │
│ OpenGL ES / Vulkan（GPU 渲染）· AudioTrack / AudioRecord（音频） │
│ → 逐帧控制比特流，适合实时推流/RTC/低延迟场景                      │
├─────────────────────────────────────────────────────────────────┤
│ 底层硬件：                                                       │
│ OMX HAL / Codec2 HAL（编解码驱动）· gralloc（图形内存分配）       │
│ dma-buf（零拷贝共享内存）· 厂商 VPU/GPU（物理硬件）               │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Android vs iOS 媒体栈对照

| 你要做的事 | Android | iOS |
|----------|---------|-----|
| 打开摄像头采 YUV | **Camera2** + ImageReader / Surface | AVCaptureSession + AVCaptureVideoDataOutput |
| 硬编码 H.264 | **MediaCodec**（configure 为 encoder） | VTCompressionSession（异步回调） |
| 硬解码 H.264 | **MediaCodec**（configure 为 decoder） | VTDecompressionSession（异步回调） |
| 编码后拿比特流 | MediaCodec.BufferInfo + ByteBuffer | CMSampleBuffer → CMBlockBuffer |
| 解码后拿像素 | Surface（零拷贝）/ Image（CPU 可读） | CVPixelBuffer → Metal texture |
| GPU 渲染 | **OpenGL ES** / Vulkan | Metal（OpenGL ES 2018 起废弃） |
| GPU 零拷贝 | SurfaceTexture → OES 纹理 / AHardwareBuffer | CVMetalTextureCache + IOSurface |
| 音频播放 | **AudioTrack** / AAudio | AudioUnit（RemoteIO） |
| 音频录制 | **AudioRecord** / AAudio | AudioUnit（RemoteIO） |
| 屏幕录制 | MediaProjection + VirtualDisplay | ReplayKit |
| 线程模型 | Handler / HandlerThread / JNI env attach | GCD queue |
| C++ 桥接 | **JNI**（C ↔ Java） | **Objective-C++**（C++ 可直接用 OC） |

### 1.3 Android 独有的挑战

**碎片化**——这是 Android 音视频开发最大的特点，也是最大的痛点：

| 维度 | 碎片化表现 |
|------|----------|
| 厂商 | 高通/联发科/三星/华为麒麟/展锐，各家 MediaCodec 行为不一 |
| 颜色格式 | NV12/NV21/YUV420P/YUV420SP/tiled/swizzled，同一段代码可能高通正常联发科花屏 |
| OMX vs Codec2 | Android 9- 用 OMX，Android 10+ 引入 Codec2，行为有差异 |
| 硬件能力 | 某些低端机不支持 4K 编码、不支持 HEVC、不支持 Main Profile |
| GPU | OpenGL ES 各家实现质量不一，Vulkan 覆盖率仍在提升中 |
| 音频延迟 | 不同设备 AudioTrack 最低 buffer 大小差异巨大（5ms~100ms+） |

**应对碎片化的核心原则**：永远以实际能力查询为准（CodecCapabilities）、永远处理 stride/crop、永远有软编软解兜底方案。

### 1.4 学习优先级总览

| 层级 | 内容 | 重要度 | 为什么 |
|------|------|--------|--------|
| 🟢 中级必会 | **Camera2 采集**（CameraDevice + CaptureSession） | 🔥🔥🔥 | 拍摄/直播的第一步 |
| 🟢 中级必会 | **MediaCodec 编解码**（同步/异步模式） | 🔥🔥🔥 | Android 硬编解唯一方式 |
| 🟢 中级必会 | **颜色格式与 stride 处理** | 🔥🔥🔥 | 不懂等于花屏，面试必问 |
| 🟢 中级必会 | **Surface/SurfaceTexture** 渲染 | 🔥🔥🔥 | 零拷贝输出到 GL 纹理 |
| 🟡 高级加分 | **OpenGL ES 渲染管线**（EGL + shader） | 🔥🔥 | 渲染 YUV 到屏幕 |
| 🟡 高级加分 | **AudioTrack/AudioRecord**（低延迟模式） | 🔥🔥 | RTC/直播音频 |
| 🟡 高级加分 | **JNI 跨语言桥接**（性能优化） | 🔥🔥 | C++ 引擎接 Android |
| 🔵 专家深水区 | **AHardwareBuffer 零拷贝** | 🔥 | 跨进程 GPU 内存共享 |
| 🔵 专家深水区 | **Codec2 HAL 定制** | 🔥 | 系统级优化 |

---

## 二、面试速记（考前 10 分钟）

### 2.1 Android 音视频面试一句话速查

| # | 考点 | 一句话答案 |
|---|------|-----------|
| 1 | Camera2 怎么采 YUV | CameraManager → CameraDevice → createCaptureSession → ImageReader(YUV_420_888) → OnImageAvailableListener |
| 2 | MediaCodec 怎么用 | createByCodecName → configure → start → dequeueInputBuffer/queueInputBuffer → dequeueOutputBuffer/releaseOutputBuffer → stop/release |
| 3 | 编码完吐什么格式 | **Annex-B**（起始码 00 00 00 01），SPS/PPS 在 csd-0/csd-1 里 configure 时传入 |
| 4 | 解码花屏最常见原因 | ByteBuffer 模式下颜色格式不同 + stride ≠ width + sliceHeight 对齐 + 私有 tiled 格式 |
| 5 | 怎么零拷贝渲染 | SurfaceTexture → OES texture → OpenGL ES shader，全程在 GPU |
| 6 | 同步 vs 异步模式 | 同步自己 dequeue 轮询；异步 setCallback 回调，生产环境推荐异步 |
| 7 | Android 和 iOS 比特流格式什么区别 | **相反**：Android 吐/吃 Annex-B，iOS 吐/吃 AVCC |
| 8 | SPS/PPS 在 Android 怎么传 | codec.configure() 时在 MediaFormat 里设 "csd-0"/"csd-1" key |

### 2.2 面试标准回答（综合题）

**Q：你给我们完整讲讲 Android 端从摄像头打开到画面推出去整个链路**

> "我按数据流来讲。第一段采集——用 Camera2 API：CameraManager 打开后置摄像头，创建 CameraCaptureSession，配置 ImageReader 作为输出目标，format 设为 YUV_420_888。OnImageAvailableListener 回调里拿到 Image，按 plane 的 rowStride 和 pixelStride 正确读取 Y、U、V 三个平面——或者直接走 Surface 通路喂给编码器。
>
> 第二段编码——创建 MediaCodec，configure 为 H.264 编码模式，传入码率/帧率/关键帧间隔/Profile 等参数，csd-0/csd-1 不需要传（编码器自产 SPS/PPS）。start 后循环 dequeueInputBuffer → 填 YUV 数据 → queueInputBuffer → dequeueOutputBuffer 取编码好的 Bitstream。Android 输出就是 Annex-B 格式，直接可以推 RTP/RTMP 不用转换——这点和 iOS 相反。
>
> 第三段渲染——编码输出的 Surface 模式可以直接喂给 MediaCodec 的 input Surface，用 OpenGL ES 渲染到那个 Surface 上——采集→渲染→编码全部在 GPU 端，零拷贝。或者单独用 ImageReader 拿 YUV 自己管理。
>
> 第四段推流——Annex-B 数据进入 C++ 推流引擎封装 FLV tag 通过 RTMP 发送。音频用 AudioRecord 采 PCM → MediaCodec 编 AAC → 同样的推流通道。整个管线是异步的，用 HandlerThread 做各环节线程隔离。"

---

## 三、完整知识图谱

```
Android 音视频开发
│
├── 采集层 ── Camera2 ───────────────── 关联 ── ImageReader（拿 YUV）/ Surface（零拷贝）
│           ── CameraX ───────────────── 高层封装，简化 Camera2 使用
│
├── 编码层 ── MediaCodec（编码模式） ── 关联 ── 输入 Surface / ByteBuffer
│          ── 见 [[../ffmpeg/14-Android硬件编解码]]
│
├── 解码层 ── MediaCodec（解码模式） ── 关联 ── 输出 Surface / ByteBuffer + Image
│          ── 颜色格式是最大坑：必须处理 stride / crop / tiled
│
├── 渲染层 ── OpenGL ES 2.0/3.0 ─────── 关联 ── EGL（上下文管理）+ GLSurfaceView / TextureView
│          ── SurfaceTexture ─────────── 关联 ── 把 Camera/MediaCodec 输出转为 OES 纹理
│          ── Vulkan ────────────────── Android 7+，新一代 GPU API
│
├── 音频层 ── AudioRecord（录）/ AudioTrack（播）── 关联 ── AudioManager / AudioFocus
│          ── AAudio ─────────────────── Android 8+，更低延迟
│          ── OpenSL ES ─────────────── Android 4.1+，NDK 音频 API
│
├── 桥接层 ── JNI ───────────────────── 关联 ── C++ 引擎 ↔ Java/Kotlin
│          ── 见 [[../JNI/00-导读与索引]]
│
├── 传输层 ── RTMP / WebRTC / SRT ──── 与 iOS 完全一致（C++ 跨平台实现）
│
└── 存储层 ── MediaMuxer ─────────────── 封装 MP4
```

---

## 四、关联技术地图：Android 独有概念

| 概念 | 作用 | iOS 对应 |
|------|------|---------|
| `Surface` | GPU producer-consumer 队列的句柄 | `IOSurface` |
| `SurfaceTexture` | Surface → OpenGL OES 纹理的桥梁 | `CVMetalTextureCache` |
| `ImageReader` | 从 Surface 读 CPU 可访问的 Image | `CVPixelBufferLockBaseAddress` |
| `EGL` | OpenGL ES 上下文管理 | `MTLDevice` / `CAMetalLayer` |
| `HardwareBuffer` / `AHardwareBuffer` | 跨进程/跨 API 共享的 GPU 内存 | `IOSurface` |
| `gralloc` | 图形内存分配器（HAL 层） | Apple 不暴露 |
| `dma-buf` | Linux 内核跨设备零拷贝共享内存 | UMA 统一内存（无此概念） |
| `OMX` | 老编解码 HAL（Android 9- 及许多旧设备） | Apple 不暴露 |
| `Codec2` | 新编解码 HAL（Android 10+） | Apple 不暴露 |

---

## 一句话总结
> Android 音视频开发就是用 Camera2 采、MediaCodec 编解码、OpenGL ES 渲染、Surface/SurfaceTexture 做零拷贝——碎片化是最大挑战，颜色格式+stride 是最常见的坑，Surface 零拷贝通路是避开这些坑的最佳方案。

## 关联文档
### Android 核心文档（本系列）
- [[01-Camera2采集详解]] — Camera2 从配置到回调的完整指南
- [[02-MediaCodec硬编码实战]] — MediaCodec 编码完整 Demo
- [[03-MediaCodec硬解码实战]] — 解码 + 颜色格式处理实战
- [[04-OpenGLES渲染与Surface详解]] — OpenGL ES + SurfaceTexture 渲染 YUV
- [[05-AudioTrack与AudioRecord详解]] — 音频采集与播放
- [[06-端到端采集编码推流管线]] — Camera2 → MediaCodec → RTMP 全链路
- [[99-Android音视频面试题全集]] — 30+ 道面试题 + 标准回答

### FFmpeg / 底层深度
- [[../ffmpeg/14-Android硬件编解码]] — MediaCodec 专题深挖（1525 行，OMX/Codec2/CSD/陷阱全覆盖）
- [[../ffmpeg/10-移动端硬件编解码]] — Android + iOS 移动端总览
- [[../ffmpeg/07-硬件编解码]] — 硬件编解码通用底座

### 桥接 & 零拷贝
- [[../JNI/00-导读与索引]] — JNI 跨语言桥接
- [[02-MediaCodec硬编码实战]] — 含 AHardwareBuffer 零拷贝深度解析（附录）
- [[04-OpenGLES渲染与Surface详解]] — 含 BufferQueue / GPU 同步 / PBO 演进史
- [[06-端到端采集编码推流管线]] — 含跨平台 RHI 架构 / iOS vs Android 零拷贝对比
