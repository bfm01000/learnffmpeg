# 15 - iOS / macOS 硬件编解码深入（VideoToolbox 专题）

> 这一篇是 [07-硬件编解码.md](./07-硬件编解码.md)、[10-移动端硬件编解码.md](./10-移动端硬件编解码.md) 的 **Apple 专题深入篇**。07 讲硬件帧通用底座（§4 硬件帧、§5.5 桌面零拷贝 interop 总表）；10 把 Android + iOS 合在一篇、iOS 部分（§四 VideoToolbox 实时编码、§四点五零拷贝）已经把"实时编码低延迟旋钮""AVCC↔Annex-B""零拷贝底层原理"开了个头。本篇**不重抄**这些，而是把 iOS/macOS 这一家**单独拎出来系统讲透**：VideoToolbox 在 Apple 媒体栈里站在哪一层、编解码两条完整生命周期、属性旋钮怎么对照 [06](./06-编码参数与码控.md)、比特流转换的字节级细节、零拷贝的 VT 侧提炼，以及面试高频问答。
> **前置阅读**：先读 [10 §四 / §四点五](./10-移动端硬件编解码.md)（建立 iOS 实时编码 + 零拷贝的初步框架）、[07 §四 硬件帧](./07-硬件编解码.md)、[05 §四 AVCC vs Annex-B + §六 SPS/PPS 补齐](./05-H264-MP4-NALU.md)。本篇大量交叉引用它们，**零拷贝底层（IOSurface / 缓存一致性 / fence / detile）在 [10 §4.5](./10-移动端硬件编解码.md) 已讲透，本篇只做 iOS 视角提炼并 link 过去，不重写**。
> **目标**：读完能应对面试里关于 iOS / Apple 硬编硬解的绝大部分问题。

---

## 一、VideoToolbox 在 Apple 媒体栈的位置

学 VideoToolbox 之前，先搞清楚它在 Apple 这套媒体框架里**站在哪一层**——这决定了"什么时候该用它、什么时候不该用"。

Apple 的媒体栈是**三层夹心**：上层省心黑盒、中层精细控制、底层硬件。

```
┌──────────────────────────────────────────────────────────────┐
│  高层：AVFoundation                                            │
│    AVAssetWriter(录文件) / AVAssetReader(读文件)               │
│    AVPlayer(播放) / AVCaptureSession(采集)                     │
│    特点：省心、几行代码搞定，但是"黑盒"——你拿不到/控不了比特流  │
│    适合："我只想录成 .mp4 / 播放一个文件"                       │
├──────────────────────────────────────────────────────────────┤
│  中层：VideoToolbox  ★ 本篇主角                                │
│    VTCompressionSession(编码) / VTDecompressionSession(解码)   │
│    特点：直接拿到/喂入编码后的比特流(CMSampleBuffer)、          │
│          能控低延迟、码率、关键帧、能逐帧拿数据发网络            │
│    适合："我要自己控制比特流、做实时推流/RTC/低延迟"            │
├──────────────────────────────────────────────────────────────┤
│  底层：Media Engine(硬件)                                      │
│    SoC / M 系芯片里的专用视频编解码电路(类比 NVENC/NVDEC)       │
│    VideoToolbox 是它的系统级软件接口                            │
└──────────────────────────────────────────────────────────────┘
```

一句话抓住分层逻辑：

- **AVFoundation 是"傻瓜相机"**：你按快门，它自己对焦、测光、存文件。你想要的就是结果文件，不关心中间比特流——录屏、本地播放、相册导出都走它。
- **VideoToolbox 是"手动挡相机"**：你要逐帧拿到压缩数据塞进 RTP 包、要把延迟压到一帧、要在弱网时动态降码率——这些 AVFoundation 给不了，必须下沉到 VT 自己开编解码会话。
- **Media Engine 是镜头和传感器**：真正干活的硬件，你不直接碰，VT 替你调。

> 类比 13 的 NVIDIA：VideoToolbox ≈ NVIDIA 的 Video Codec SDK（直接控编解码引擎），AVFoundation ≈ 更上层的封装。Media Engine ≈ NVENC/NVDEC 那块专用 ASIC（见 [13 §二](./13-NVIDIA硬件编解码.md)）——编解码不吃 CPU、也不吃 GPU 通用算力，是另一块专门电路。

### 1.1 为什么 iOS 和 macOS 能"一套代码两处跑"

这是 Apple 平台最大的省心点，也是高频面试点。

- **同源芯片**：Apple Silicon（M 系 Mac）和 iPhone/iPad 的 A 系芯片是**同一套架构**，Media Engine 是同一血统的硬件。
- **同一套 API**：`VideoToolbox.framework` 在 iOS 和 macOS 上**接口完全一致**（CoreFoundation 风格的 C API），`CVPixelBuffer` / `CMSampleBuffer` 这些 CoreMedia/CoreVideo 类型两端通用。
- **工程含义**：你可以**先在 M 系 Mac 上把 `VTCompressionSession` 那套调通、用命令行 ffmpeg 验证产出**，再原样移植到 iOS 真机——省掉真机调试的来回烧录。这正是 [10 §八 阶段2](./10-移动端硬件编解码.md) 推荐的路径，§十四会落到 iOS 具体步骤。

> macOS 桌面侧（Intel Mac 的软件回退、QuickSync）见 [07 §2.2 / §5.5](./07-硬件编解码.md)，§八会展开 Intel vs Apple Silicon 的差异。

---

## 二、核心对象与数据载体（先认全这些类型）

VideoToolbox 编程的一半工作量就是在这几个 CoreMedia / CoreVideo 类型之间倒腾数据。[10 §4.1](./10-移动端硬件编解码.md) 给了个基础表，这里**补全**——加上 `VTPixelTransferSession`、`CMBlockBuffer`、`CMTime`，并把"谁装未压缩、谁装压缩"这层关系说清。

| 对象 / 类型 | 大白话 | 装的是 | 关键点 |
|---|---|---|---|
| `VTCompressionSession` | 编码会话 | —（流程对象） | 喂未压缩帧 → 吐压缩数据，异步回调 |
| `VTDecompressionSession` | 解码会话 | —（流程对象） | 喂压缩数据 → 吐未压缩帧 |
| `VTPixelTransferSession` | 像素搬运/转换会话 | —（流程对象） | 硬件做**格式转换 + 缩放**（如 BGRA→NV12、改分辨率），喂编码器前的预处理；类比 FFmpeg 的 `scale_vt` 滤镜 |
| `CVPixelBuffer` | **一帧未压缩图像** | YUV(NV12) / BGRA 像素 | 底层是 **IOSurface**，可与 Metal/GL 零拷贝共享。iOS 上的"硬件帧"就是它 |
| `CVPixelBufferPool` | 像素帧复用池 | 一堆 `CVPixelBuffer` | 避免每帧重新分配；建池要带 IOSurface/Metal key（见 §七） |
| `CMSampleBuffer` | **一帧压缩数据的总包装** | 压缩比特流 + PTS/DTS + 格式描述 | 编码器吐出的、解码器喂入的都是它。是"数据 + 时间 + 元信息"三合一 |
| `CMBlockBuffer` | 裸字节缓冲 | 连续/分段的压缩字节本身 | `CMSampleBuffer` 内部装压缩数据的那层；要取 NALU 字节就从这里 `CMBlockBufferGetDataPointer` 拿 |
| `CMVideoFormatDescription` | 格式描述 | 宽高、codec、**SPS/PPS(/VPS)** | ★iOS 最经典坑的核心：参数集**单独存这里，不在比特流里**（见 §六） |
| `CMTime` | 有理数时间戳 | `value / timescale` | PTS/DTS 用它表示。`timescale` 是分母（如 600 或 90000），别用错（见 §十一） |

### 2.1 两条"装数据"的主线，先记死

把上表压成一句话心智模型：

```
未压缩侧：  CVPixelBuffer (像素, IOSurface 后端)  ← 相机/解码器产出, 编码器吃
压缩侧：    CMSampleBuffer = CMBlockBuffer(压缩字节, AVCC)
                          + CMTime(PTS/DTS)
                          + CMVideoFormatDescription(SPS/PPS 在这!)
```

- **编码**：喂 `CVPixelBuffer` → 拿 `CMSampleBuffer`。
- **解码**：喂 `CMSampleBuffer` → 拿 `CVPixelBuffer`。
- 两个 session 就是这两条线的方向相反的泵。

> `CVPixelBuffer` 为什么是"硬件帧"：相机（`AVCaptureSession`）吐出的就是包着 `CVPixelBuffer` 的 `CMSampleBuffer`，它底层是 IOSurface，可以零拷贝直接喂编码器或上传 Metal——这就是 [07 §4 硬件帧](./07-硬件编解码.md) 在 iOS 上的形态。注意：**采集吐的 `CMSampleBuffer` 装的是未压缩 `CVPixelBuffer`，编码吐的 `CMSampleBuffer` 装的是压缩 `CMBlockBuffer`**——同一个类型在采集/编码两端装的东西不同，别混。

---

## 三、编码完整生命周期

VT 编码是**异步**的：你喂帧进去，编码完成后通过**回调**把 `CMSampleBuffer` 交还给你。整个生命周期五步：

```
①Create → ②SetProperty → ③EncodeFrame(反复) → ④CompleteFrames(flush) → ⑤Invalidate
```

```
        ┌─────────────────────────────────────────────┐
喂入 →  │ VTCompressionSessionEncodeFrame              │
CVPixel │   (CVPixelBuffer + PTS + duration)           │
Buffer  └─────────────────────────────────────────────┘
                          │  (异步, 硬件流水线)
                          ▼
        ┌─────────────────────────────────────────────┐
        │ 输出回调 outputCallback                       │
        │   (status, CMSampleBuffer)  ← AVCC 压缩数据   │
回调 ←  │   在这里取数据: 转 Annex-B / 写文件 / 发网络    │
        └─────────────────────────────────────────────┘
```

### 3.1 精简调用序列

```c
// ① 创建会话: 指定宽高/codec/输出回调
VTCompressionSessionRef session;
VTCompressionSessionCreate(
    kCFAllocatorDefault,
    width, height,
    kCMVideoCodecType_H264,          // 或 _HEVC
    NULL,                            // 编码器规格(可强制硬件, 见 §八)
    NULL,                            // 源像素缓冲属性(可指定 pool)
    NULL,
    outputCallback,                  // ★ 异步输出回调
    userData,
    &session);

// ② 配置属性(见 §五, 实时编码这一步最关键)
VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
// ... AverageBitRate / MaxKeyFrameInterval / ProfileLevel ...
VTCompressionSessionPrepareToEncodeFrames(session);   // 可选: 预热

// ③ 反复喂帧 (每来一帧 CVPixelBuffer 就调一次)
CMTime pts = CMTimeMake(frameIndex, 30);              // 30 = timescale
VTCompressionSessionEncodeFrame(
    session, pixelBuffer, pts, kCMTimeInvalid,
    frameProperties,                 // 可逐帧传属性, 如强制关键帧(见 §五)
    userData, NULL);

// ④ 结束前 flush: 把流水线里还没吐的帧全逼出来
VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);

// ⑤ 销毁
VTCompressionSessionInvalidate(session);
CFRelease(session);
```

### 3.2 输出回调里干什么

```c
void outputCallback(void *userData, void *frameRef, OSStatus status,
                    VTEncodeInfoFlags flags, CMSampleBufferRef sampleBuffer) {
    if (status != noErr || !sampleBuffer) return;       // 注意检查
    // 1. 判断是不是关键帧(IDR): 查 sample attachments 的 NotSync
    // 2. 关键帧前要从 format description 取 SPS/PPS (见 §六)
    // 3. 从 CMBlockBuffer 取 AVCC 字节 → 转 Annex-B → 发网络/写文件
}
```

两个关键直觉：

1. **异步 + 回调**：`EncodeFrame` 返回不代表这帧编完了。编码器为了流水线效率会攒几帧再吐，吐出来的顺序在关 B 帧时和喂入一致（见 §五为什么关 B 帧）。所以**收尾必须 `CompleteFrames` flush**，否则最后几帧丢失——和 [01 解码器 send/receive 模型](./01-数据结构与生命周期.md) 一样"喂进去不等于立刻出来"。
2. **回调线程**：回调可能在 VT 内部线程触发，往网络/文件写数据要注意线程安全。

---

## 四、解码完整生命周期

解码方向相反——喂压缩 `CMSampleBuffer`、拿未压缩 `CVPixelBuffer`。但 iOS 解码有一道 Android 没有的**前置门槛**：必须先有 `CMVideoFormatDescription`（即先拿到 SPS/PPS），才能建解码会话。

```
①从 SPS/PPS 建 CMVideoFormatDescription
        │
        ▼
②VTDecompressionSessionCreate(formatDesc, 输出回调)
        │
        ▼
③把一帧压缩数据包成 CMSampleBuffer (AVCC 字节 + formatDesc + 时间)
        │
        ▼
④VTDecompressionSessionDecodeFrame → 回调拿 CVPixelBuffer → 渲染
```

### 4.1 精简调用序列

```c
// ① 从 SPS/PPS 建格式描述 (H.264)
const uint8_t *paramSetPointers[2] = { spsData, ppsData };
const size_t   paramSetSizes[2]    = { spsSize, ppsSize };
CMVideoFormatDescriptionRef formatDesc;
CMVideoFormatDescriptionCreateFromH264ParameterSets(
    kCFAllocatorDefault,
    2, paramSetPointers, paramSetSizes,
    4,                               // NAL 长度前缀字节数 = 4 (AVCC)
    &formatDesc);
// HEVC 用 CMVideoFormatDescriptionCreateFromHEVCParameterSets, 通常传 3 个(VPS/SPS/PPS)

// ② 建解码会话
VTDecompressionSessionRef session;
VTDecompressionSessionCreate(kCFAllocatorDefault, formatDesc,
    NULL, destPixelBufferAttrs, &decompCallbackRecord, &session);

// ③ 把一帧 AVCC 数据包成 CMSampleBuffer
//    (先把字节装进 CMBlockBuffer, 再带上 formatDesc + 时间戳)
CMSampleBufferRef sampleBuffer = /* CMSampleBufferCreate(blockBuffer, formatDesc, ...) */;

// ④ 解码 → 回调里拿 CVPixelBuffer
VTDecompressionSessionDecodeFrame(session, sampleBuffer,
    kVTDecodeFrame_EnableAsynchronousDecompression, userData, NULL);
```

### 4.2 关键点

- **格式描述是入场券**：解码前必须先有 SPS/PPS。从 RTP/裸流（Annex-B）拿数据时，要先把流里带 `00 00 00 01` 的 SPS/PPS NALU 拆出来建 `CMVideoFormatDescription`——这正是 §六转换的反向。对应 [10 §3.3](./10-移动端硬件编解码.md) Android 的 `csd-0/csd-1`，是同一件事的两种 API 形态。
- **喂入要 AVCC**：`VTDecompressionSession` 吃的是**长度前缀**格式（建 formatDesc 时指定了长度字段 4 字节），所以从 Annex-B 流来的数据要把起始码换回 4 字节长度（见 §六反向）。
- **回调拿 `CVPixelBuffer`**：解出的帧是 IOSurface 后端的 `CVPixelBuffer`，直接进 §七的渲染两条路。

---

## 五、实时编码关键属性（对照 06 的码控旋钮）

这是 VideoToolbox 编码的核心调参区。每个属性都是 [06-编码参数与码控.md](./06-编码参数与码控.md) 里某个概念在 VT 上的具体旋钮。**先记一句总纲：VT 的可调性远少于 x264——没有 `-preset veryslow` 那种精细搜索档，厂商只暴露下面这些 key。**

### 5.1 属性 ↔ 06 概念对照表

| VT 属性 Key（`kVTCompressionPropertyKey_` 前缀） | 作用 | 对照 [06](./06-编码参数与码控.md) | 实时取值 |
|---|---|---|---|
| `RealTime` = true | 编码器不做高延迟多遍处理 | ≈ `-tune zerolatency` | **true** |
| `AllowFrameReordering` = false | 关 B 帧（B 帧要等后续帧→引入重排序延迟） | "低延迟别用 B 帧"（06 §4.1/§5.2） | **false** |
| `ProfileLevel` | Profile + Level | 06 §四（Baseline/Main/High + Level） | 实时常 `Baseline_AutoLevel` 或 `ConstrainedHigh` |
| `AverageBitRate` | 目标平均码率 | 06 §3.1 Bitrate（VBR 取向） | 按带宽设 |
| `DataRateLimits` | 码率硬上限 `[字节数, 秒数]` | ≈ `-maxrate`/`-bufsize`（06 §6.3 VBV） | 配合 AverageBitRate 做 CBR 近似 |
| `ConstantBitRate` | 真 CBR（较新系统支持） | 06 §3.1 CBR | 需要严格 CBR 时 |
| `MaxKeyFrameInterval` | GOP 最大帧数 | 06 §6.3 `-g`（GOP） | 短 GOP（如 30/60） |
| `MaxKeyFrameIntervalDuration` | GOP 最大秒数 | GOP 的时间表达 | 如 2 秒；和上一条取**先到者** |
| `ExpectedFrameRate` | 告诉编码器预期帧率 | — | 实际帧率（助码控/GOP 估算） |
| `MaxH264SliceBytes` | 单 slice 字节上限 | — | 限制单 NALU 大小，配合 MTU/打包 |
| `AllowOpenGOP` | 是否允许开放 GOP | 06 GOP 结构 | 实时/可随机切入常 **false**（闭合 GOP） |

### 5.2 几个旋钮的深一点解释

- **CBR 近似 vs 真 CBR**：早期 VT 没有真 CBR，工程上用 `AverageBitRate`（定平均）+ `DataRateLimits`（定窗口内硬上限，例如"1 秒内不超过 X 字节"）逼近 CBR——这正是 [06 §6.3](./06-编码参数与码控.md) 的 VBV 漏桶模型在 VT 上的形态：`DataRateLimits` 的"秒数"就是桶的时间窗。较新系统多了 `ConstantBitRate` 才是真 CBR。直播/RTC 防止突发流量冲爆带宽时用这套。
- **GOP 两个 key 取先到者**：`MaxKeyFrameInterval`（帧数）和 `MaxKeyFrameIntervalDuration`（秒数）同时设时，**哪个先到就插关键帧**。变帧率（VFR，见 [05 §10.2](./05-H264-MP4-NALU.md)）下只设帧数会导致关键帧间隔时间漂移，所以实时场景两个都设。
- **为什么没有 preset 数字档**：x264 的 `-preset` 控制 RDO/运动搜索深度（[07 §7.1](./07-硬件编解码.md) 讲透了硬编为什么画质不如软编）。VT 是固定功能电路，搜索深度焊死在硅片里，所以**没有那个旋钮**——你只能给更高码率来追画质。

### 5.3 运行时动态控制（WebRTC 必用，对照 Android §3.7）

编码器跑起来后，**码率和关键帧不是定死的**——这正是 WebRTC 拥塞控制和丢包恢复要的两个旋钮：

```c
// (1) 拥塞控制要降码率: 中途直接改 AverageBitRate
VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, newBitrate);

// (2) 对端发来 PLI/FIR 请求关键帧: 在编码这一帧时传 frame properties 强制 IDR
CFDictionaryRef frameProps = /* { kVTEncodeFrameOptionKey_ForceKeyFrame: true } */;
VTCompressionSessionEncodeFrame(session, pixelBuffer, pts, dur, frameProps, ...);
```

- `kVTEncodeFrameOptionKey_ForceKeyFrame` 是 iOS 版的"立即产 IDR"，对应 [10 §3.7](./10-移动端硬件编解码.md) Android 的 `PARAMETER_KEY_REQUEST_SYNC_FRAME`。
- **没有这两个旋钮，弱网下 WebRTC 既不能自适应降码率、也不能在花屏后快速恢复**——它们是移动端 RTC 最实际的接口（详见 §十三）。

---

## 六、AVCC ↔ Annex-B：iOS 最经典坑（字节级深讲）

**这是 iOS 推流 / WebRTC 必踩、面试必问的坑**，比 [10 §4.3](./10-移动端硬件编解码.md) 更系统。先把根源说死：

> **VideoToolbox 编码器吐出的 `CMSampleBuffer` 是 AVCC 格式，而 RTP / RTSP / 裸 `.h264` 流要的是 Annex-B 格式。这两种格式不兼容，必须手动转。**

AVCC vs Annex-B 的本质区别见 [05 §四](./05-H264-MP4-NALU.md)，这里聚焦 VT 侧的两个致命差异。

### 6.1 两个差异：边界方式 + SPS/PPS 位置

```
VT 吐出 (AVCC):
  CMSampleBuffer
   ├─ CMBlockBuffer:  [4字节长度][NALU][4字节长度][NALU]...   ← 长度前缀切边界
   └─ CMVideoFormatDescription: { SPS, PPS }                 ← 参数集在这里, 不在流里!

RTP/裸流要 (Annex-B):
  [00 00 00 01][SPS][00 00 00 01][PPS][00 00 00 01][IDR]...   ← 起始码切边界
                ↑ 参数集作为普通 NALU 内联在流里
```

两件事都要做：

1. **边界**：4 字节长度前缀 → `00 00 00 01` 起始码。
2. **参数集**：SPS/PPS 不在 `CMBlockBuffer` 里，要从 `CMVideoFormatDescription` 单独取出来，在每个 IDR 前内联进流（[05 §6.3](./05-H264-MP4-NALU.md)：每个 IDR 前注入，工程首选）。

### 6.2 字节级示意：长度前缀 vs 起始码

假设一个 6 字节的 IDR NALU（实际会很大，这里示意）：

```
AVCC（VT 吐出, 大端长度）:
  00 00 00 06 | 65 88 84 21 9A BC
  └─4字节大端长度=6─┘ └──6字节NALU负载──┘
        ↑ 注意是"长度", 不是起始码; 这里恰好也以 00 00 00 开头, 极易被误当起始码

Annex-B（转换后）:
  00 00 00 01 | 65 88 84 21 9A BC
  └─起始码────┘ └──同样的6字节NALU──┘
```

**关键陷阱**：AVCC 的 4 字节长度若数值较小（如长度=6 → `00 00 00 06`），前三字节就是 `00 00 00`，肉眼/扫描器极易误判成起始码 `00 00 00 01` 的一部分——这就是 [05 §五](./05-H264-MP4-NALU.md) 说的"直接扫 MP4 字节找起始码会切错边界"的同款现场。**转换必须按长度字段精确跳转，不能扫起始码。**

### 6.3 转换步骤（编码方向：AVCC → Annex-B 发流）

```c
// 1. 关键帧前: 从 format description 取参数集, 转成带起始码的 NALU
CMVideoFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
size_t spsCount; const uint8_t *sps; size_t spsSize;
CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, &sps, &spsSize, &spsCount, NULL);
// index 1 取 PPS; 每个前面拼 00 00 00 01 写入输出流

// 2. 取压缩数据, 遍历每个 NALU: 读 4 字节大端长度 → 替换成起始码
CMBlockBufferRef block = CMSampleBufferGetDataBlock(sampleBuffer);
uint8_t *data; size_t totalLen;
CMBlockBufferGetDataPointer(block, 0, NULL, &totalLen, (char **)&data);
size_t offset = 0;
while (offset < totalLen) {
    uint32_t nalLen = (data[offset]<<24)|(data[offset+1]<<16)|(data[offset+2]<<8)|data[offset+3];
    // 把这 4 字节长度位置写成 00 00 00 01, 后面 nalLen 字节是 NALU 负载
    offset += 4 + nalLen;
}
```

判断是不是关键帧：查 `CMSampleBufferGetSampleAttachmentsArray`，看 `kCMSampleAttachmentKey_NotSync` 是否缺失/为 false（不是 NotSync = 是同步帧 = IDR）。

### 6.4 反向（解码方向：Annex-B → AVCC 喂解码器）

1. 扫 Annex-B 流，按起始码拆出各 NALU。
2. 把 SPS/PPS NALU 抽出来 → `CMVideoFormatDescriptionCreateFromH264ParameterSets`（§四①）。
3. 其余帧 NALU：起始码 → 4 字节大端长度，装进 `CMBlockBuffer` 包成 `CMSampleBuffer` 喂解码器。

### 6.5 漏了会怎样 + HEVC 区别

- **典型症状**：对端**只有声音没画面**（参数集没补，解码器建不起来），或**首帧后全是花屏**（边界切错/中途丢了参数集）。
- **HEVC 区别**：用 `CMVideoFormatDescriptionGetHEVCParameterSetAtIndex`，且 H.265 **多一个 VPS**（[05 §5.4](./05-H264-MP4-NALU.md)：VPS=32/SPS=33/PPS=34），通常要取 3 个参数集，每个 IDR 前注入 **VPS+SPS+PPS 三件**，漏 VPS 部分解码器直接解不出。

> 这一步在 FFmpeg 里对应 `h264_mp4toannexb` bsf（[05 §5.3](./05-H264-MP4-NALU.md)），但 iOS 上数据从 VT 出来、不走 FFmpeg muxer，所以**得自己手写这段**——这正是 WebRTC iOS 代码里那段 SPS/PPS 提取 + 起始码替换的来历（§十三）。

---

## 七、零拷贝（VT 侧提炼，底层 link 10）

> 零拷贝的**底层原理**——为什么 UMA 统一内存下回读还慢（tiling/swizzle、缓存一致性、fence 同步 stall）、IOSurface 是什么、桌面 PCIe 差异——在 [10 §4.5](./10-移动端硬件编解码.md) 已经讲透，**本节不重复**，只补 VideoToolbox 这一侧的落地要点。

VT 侧要记住的就这几条：

- **`CVPixelBuffer` 是 IOSurface 后端**：这是零拷贝成立的物理前提——一块 IOSurface 内存能同时被相机 ISP 写、VT 读写、Metal 当纹理采样（[10 §4.5.4](./10-移动端硬件编解码.md)）。VT 编码器内部的 pool 本身就是 IOSurface-backed。
- **NV12 双平面包成 Metal 纹理**：`CVMetalTextureCacheCreateTextureFromImage` 把 `CVPixelBuffer` 的**每个平面**alias 成一张 `MTLTexture`（同一块 IOSurface，零拷贝）：平面 0 = 亮度 `r8Unorm`，平面 1 = 色度 `rg8Unorm`，shader 里采两张纹理做 YUV→RGB。
- **建 pool 必带两个 key**：`kCVPixelBufferIOSurfacePropertiesKey`（拿 IOSurface 后端）+ `kCVPixelBufferMetalCompatibilityKey`（能直接给 Metal）。**漏了就退化成普通内存、零拷贝失效**。
- **`CVPixelBufferLockBaseAddress` 是零拷贝的"破坏者"**：它是"我要在 CPU 上读这块像素"的操作，会触发 detile（从 GPU tiled 布局重排成线性）+ 同步等待。热路径里别锁；只读时至少加 `kCVPixelBufferLock_ReadOnly`，且尽量挪出热路径。判据：**任何"想拿到 CPU 像素"的调用都会打断零拷贝**（同 [10 §4.5.11](./10-移动端硬件编解码.md) 的判据）。

### 7.1 渲染两条路

解码出 `CVPixelBuffer` 后，上屏有两条路（对照 [10 §4.4](./10-移动端硬件编解码.md)）：

| 路径 | 大白话 | 何时用 |
|---|---|---|
| `AVSampleBufferDisplayLayer` | 最省事——直接 enqueue `CMSampleBuffer` 让系统显示 | 只想把解出来的画面显示，不做后处理 |
| Metal + `CVMetalTextureCache` | 自己拿到纹理做渲染/滤镜/美颜 | 要后处理、自定义合成、美颜 |

> interop 横向总表（NVIDIA/Linux/Windows/macOS 各怎么把硬件帧零拷贝交给渲染 API）见 [07 §5.5.3](./07-硬件编解码.md)——macOS 这一行就是"CVPixelBuffer→Metal"，和 iOS 完全一样。

---

## 八、软件 vs 硬件编码器选择

VT **默认就走硬件**（Media Engine），但你可以通过创建会话时的 encoder specification 字典显式控制：

```c
// 创建会话时传入 encoderSpecification 字典:
// kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder = true  → 优先硬件
// kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder = true → 强制硬件,没有就失败
```

平台差异（高频面试点）：

| 平台 | 硬件编码 | 软件回退 |
|---|---|---|
| **iOS / iPadOS** | A 系 Media Engine，基本都是硬件 | 几乎没有软件回退，不支持的格式直接失败 |
| **Intel Mac** | 可能有 QuickSync（Intel 核显）| **有软件编码器回退**——VT 在 macOS 上可能悄悄用软件实现 |
| **Apple Silicon Mac** | 统一 Media Engine（同 iOS 血统） | 同 iOS，主要靠硬件 |

两个工程含义：

- **iOS 上不用纠结硬件与否**——基本就是硬件，你能做的是用 `Require...` key 确保不被悄悄回退到（不存在的）软件，从而尽早暴露"这格式/分辨率不支持"。
- **macOS 上要警惕"以为在用硬件、其实回退了软件"**：Intel Mac 上 VT 可能用软件编码器，CPU 占用会上去。要硬件就用 `Require...` key 强制，失败了再决策。

> 这也呼应 [07 §2.2 / §5.5](./07-硬件编解码.md)：M 系 Mac 无独显、只有统一 Media Engine（UMA）；Intel Mac 才有 QuickSync 这种核显路径。

---

## 九、FFmpeg 支持边界（iOS 最省心处）

iOS/macOS 在 FFmpeg 里是**编 + 解都有 wrapper**，这和 Android"只有解码 wrapper、没有编码 wrapper"形成鲜明对比（[10 §五](./10-移动端硬件编解码.md)）：

| | 解码 | 编码 |
|---|---|---|
| **iOS / macOS** | ✅ `h264_videotoolbox` / `hevc_videotoolbox` | ✅ `h264_videotoolbox` / `hevc_videotoolbox` |
| **Android** | ✅ `h264_mediacodec` 等 | ❌ 无 MediaCodec 编码 wrapper |

```bash
# macOS 上用 VideoToolbox 硬解硬编 (M 系跑 4K 飞快、几乎不耗电)
ffmpeg -hwaccel videotoolbox -i input.mp4 -c:v h264_videotoolbox -b:v 4M out.mp4
```

关键结论：**iOS 是 FFmpeg 硬件加速最省心的平台**——编解码都能直接用 VT wrapper，不像 Android 硬编必须绕开 FFmpeg 直接调系统 API。这也是为什么很多跨平台方案在 iOS 上能"FFmpeg 一把梭"，到 Android 就得混合架构（FFmpeg demux/mux + 系统 API 硬编）。

---

## 十、横向对比表

### 10.1 iOS VideoToolbox vs Android MediaCodec

| 维度 | iOS VideoToolbox | Android MediaCodec |
|---|---|---|
| 碎片化 | **低**（苹果自家芯片，行为统一） | **高**（高通/联发科/三星各异，坑多） |
| 比特流格式 | 吐/吃 **AVCC**（长度前缀） | 吐/吃 **Annex-B**（起始码）——**两端相反** |
| 未压缩帧载体 | `CVPixelBuffer`（IOSurface） | `Surface` / `SurfaceTexture`（GPU）或 `ByteBuffer`（CPU，颜色格式坑多） |
| SPS/PPS 位置 | 单独存 `CMVideoFormatDescription` | `csd-0`/`csd-1`（configure 时喂） |
| FFmpeg 编码 wrapper | ✅ 有 | ❌ 无 |
| API 风格 | C / CoreFoundation（现代、统一） | Java / NDK（`AMediaCodec`），缓冲区队列模型 |
| 强制关键帧 | frame property `ForceKeyFrame` | `setParameters` + `REQUEST_SYNC_FRAME` |

一句话记忆：**iOS 统一省心 + AVCC + CVPixelBuffer + FFmpeg 双向；Android 碎片化 + Annex-B + Surface/ByteBuffer + FFmpeg 只解不编。比特流格式两端正好相反——这是跨平台最容易翻车的点（§十一陷阱表）。**

### 10.2 VideoToolbox vs 桌面 NVENC

| 维度 | VideoToolbox（Apple） | NVENC（NVIDIA，见 [13](./13-NVIDIA硬件编解码.md)） |
|---|---|---|
| 可调性 | 少（无 preset 数字档，旋钮就 §五那些） | 多（`-preset p1~p7`、rc-lookahead、多参考帧等） |
| 内存架构 | **UMA**（统一内存，无独显无 PCIe） | 独显 **VRAM + PCIe 总线**（回读更贵，[07 §5.5.1](./07-硬件编解码.md)） |
| 能耗比 | **极高**（手机/笔记本场景王者） | 高吞吐，但功耗远大于移动 SoC |
| 并发路数 | 受设备 Media Engine 实例限制 | 消费卡有 NVENC 会话数限制，专业卡放开 |
| 典型场景 | iOS/Mac App、RTC、本地编辑 | 云游戏、直播服务器、云转码、AI 视频 |

记忆点：**VT 可调性更少、但能耗比极高且是 UMA（没有 PCIe 那条真总线）**；NVENC 旋钮多、吞吐猛，但功耗和总线开销是它的代价。

---

## 十一、iOS / Apple 特有陷阱表（症状 → 真相 / 对策）

| 症状 | 真相 / 对策 |
|---|---|
| 推流后对端只有声音没画面 / 首帧后花屏 | 直接把 VT 原始输出（AVCC）发了 RTP/裸流。必须转 Annex-B + 从 format description 补 SPS/PPS，每个 IDR 前注入（§六） |
| App 切后台回来花屏 / 编码崩 | 后台 VT session 可能失效。回前台要**重建 session + 重发 SPS/PPS + 强制一个关键帧**（§五 ForceKeyFrame） |
| 渲染卡、CPU 莫名很高 | 热路径调了 `CVPixelBufferLockBaseAddress` 锁像素，触发 detile + 同步 stall（§七）。别锁；只读加 `ReadOnly` 标志并挪出热路径 |
| 自定义 Metal 渲染拿不到纹理 / 性能退化 | 建 `CVPixelBufferPool` 漏了 `IOSurfaceProperties` / `MetalCompatibility` key，退化成普通内存。补上这两个 key（§七） |
| 某分辨率/像素格式编码失败或回退 | iOS 基本无软件回退，不支持的格式直接失败；分辨率要对齐（很多硬件要求偶数甚至 16 倍数，否则失败/绿边）。编码前协商好格式 |
| HEVC 在老设备上不可用 | 老芯片不支持 HEVC 硬编/硬解。编码前查询能力（`VTCopyVideoEncoderList` / 尝试创建会话看是否成功），不支持则降级 H.264 或软解兜底 |
| 跑一会儿就掉帧、卡住 | 持有 `CMSampleBuffer`/`CVPixelBuffer` 不释放 → **pool 耗尽**，编码器拿不到空 buffer 而 stall。用完立刻 `CFRelease`，别跨多帧攒着 |
| 播放速度异常 / 音画不同步 | `CMTime` 的 `timescale` 用错——把帧号当秒、或编解码两端 timescale 不一致。统一用一个明确的 timescale（如 600 或采集帧率），PTS = `CMTimeMake(帧号, timescale)`（§二 CMTime） |
| 编完最后几帧丢了 | 结束没调 `VTCompressionSessionCompleteFrames` flush。异步流水线里残留的帧要 flush 才会吐出来（§三） |

> 后台失效 + 重发关键帧、pool 耗尽这两条和 [10 §七陷阱表](./10-移动端硬件编解码.md) 是同源问题，本表给 iOS 的具体 API 对策。

---

## 十二、面试高频问答（带答题要点）

- **Q：VideoToolbox 和 AVFoundation 什么区别？什么时候用哪个？**
  AVFoundation 是高层、省心、黑盒——录文件（AVAssetWriter）、播放（AVPlayer）、采集（AVCaptureSession），你拿不到也控不了比特流。VideoToolbox 是中层、直接控编解码——能逐帧拿到压缩 `CMSampleBuffer`、控低延迟/码率/关键帧。**只想录文件/播放用 AVFoundation；要自己控比特流做实时推流/RTC/低延迟用 VideoToolbox。**

- **Q：VT 编码吐出什么格式？推流为什么要转换？怎么转？**
  吐 **AVCC**（4 字节长度前缀），且 SPS/PPS 单独在 `CMVideoFormatDescription` 里、不在流中。RTP/裸流要 **Annex-B**（`00 00 00 01` 起始码 + 参数集内联）。两件事：① 4 字节长度→起始码；② 从 format description 取 SPS/PPS（`...GetH264ParameterSetAtIndex`）每个 IDR 前注入（§六）。

- **Q：为什么实时编码要关 B 帧（AllowFrameReordering=false）？**
  B 帧双向预测、要参考后续帧才能解，引入**重排序延迟**（编码器得等未来帧）。实时场景延迟优先于压缩率，所以关掉。等价于 [06](./06-编码参数与码控.md) 的"低延迟别用 B 帧"、`-tune zerolatency`。

- **Q：CVPixelBuffer 和 IOSurface 什么关系？怎么零拷贝给 Metal？**
  `CVPixelBuffer` 的后端就是 IOSurface（内核共享、引用计数、可跨进程）。用 `CVMetalTextureCache` 把每个平面 alias 成 `MTLTexture`（NV12 = 亮度 r8 + 色度 rg8），shader 里做 YUV→RGB，全程同一块内存不下 CPU（§七，底层见 [10 §4.5.6](./10-移动端硬件编解码.md)）。

- **Q：怎么强制硬件编码 / 动态改码率 / 强制关键帧？**
  硬件：创建会话传 `RequireHardwareAcceleratedVideoEncoder=true`（§八）。改码率：运行时 `VTSessionSetProperty(..., AverageBitRate, ...)`。强制关键帧：编码该帧时传 frame property `kVTEncodeFrameOptionKey_ForceKeyFrame=true`（§五）。后两个是 WebRTC 拥塞控制 + 丢包恢复的旋钮。

- **Q：iOS 和 Android 比特流格式为什么相反？**
  VT 吐 AVCC（长度前缀，参数集分离），MediaCodec 吃/吐 Annex-B（起始码，参数集内联）。所以**同一套跨平台代码在两端的比特流处理逻辑正好相反**，是最容易翻车的点（§十）。

- **Q：为什么 iOS 比 Android 省心？**
  ① 芯片统一——苹果自家 Media Engine，行为一致，没有 Android 那种厂商碎片化/颜色格式坑；② FFmpeg 在 iOS 编解码 wrapper 都有，Android 只有解码 wrapper（硬编必须绕开 FFmpeg 直接调系统 API，§九）。

- **Q：SPS/PPS 在 iOS 存哪？**
  编码吐出的 `CMSampleBuffer` 里**不带** SPS/PPS——它们单独存在 `CMVideoFormatDescription` 里。要发流得用 `CMVideoFormatDescriptionGetH264ParameterSetAtIndex` 取出来手动注入（HEVC 还多个 VPS）。这和 MP4 的 `avcC`、Android 的 csd 是同一设计思想（[05 §6](./05-H264-MP4-NALU.md)）。

- **Q：编码是同步还是异步？收尾要注意什么？**
  异步——`EncodeFrame` 返回不代表编完，结果通过输出回调交还。结束前必须 `VTCompressionSessionCompleteFrames` flush，否则流水线里残留帧丢失（§三）。

---

## 十三、和 WebRTC 的衔接

libwebrtc 的 iOS 视频底座就是 VideoToolbox——本篇每一节都能在 WebRTC 源码里找到对应（源码在 `sdk/objc/`）：

- **编解码器实现**：`RTCVideoEncoder` / `RTCVideoDecoder` 的 H.264 实现底下就是 §三/§四 那套 `VTCompressionSession` / `VTDecompressionSession`，通过 `VideoEncoderFactory` / `VideoDecoderFactory` 注入。
- **帧载体**：WebRTC 的 `VideoFrame` 在 iOS 上包着 `RTCCVPixelBuffer`（即 `CVPixelBuffer`）——就是 §二/§七 的零拷贝采集→编码链。
- **§六的 AVCC→Annex-B 是必做项**：RTP 打包要 Annex-B，所以 iOS WebRTC 必然做这个转换。你在 WebRTC iOS 代码里看到的 SPS/PPS 提取 + 起始码替换，就是 §六那段。
- **动态码率 + PLI 强制 IDR**：拥塞控制（GCC）动态调 `AverageBitRate`、对端 PLI/FIR 时 `ForceKeyFrame`（§五 5.3）——这是弱网自适应 + 丢包恢复的实际接口。

> 零拷贝采集链在 WebRTC 的细节、两端对照见 [10 §六 / §4.5.10](./10-移动端硬件编解码.md)。一句话：**iOS WebRTC 视频性能/花屏问题，十有八九落在本篇的实时编码属性 + AVCC↔Annex-B + 零拷贝链上。**

---

## 十四、学习路径（iOS 具体化，呼应 10 §八 阶段2）

> 前提：先有 [07 硬件帧](./07-硬件编解码.md) + [05 AVCC/Annex-B](./05-H264-MP4-NALU.md) 的底子。

1. **在 M 系 Mac 上把 `VTCompressionSession` 调通**（和 iOS 同源，免真机烧录）：创建会话 → §五配低延迟属性 → 喂生成的 `CVPixelBuffer`（先用纯色/合成图像）→ 输出回调拿 `CMSampleBuffer`。
2. **做 AVCC → Annex-B 转换 → 写 `.h264` 裸流**（§六），然后用 ffmpeg 命令验证产出能正常解码播放——**这一步同时验证你的格式转换写对没有**：
   ```bash
   ffmpeg -i out.h264 -f null -        # 能正常解码不报错 = 转换 OK
   ffplay out.h264                     # 或直接播
   ```
3. **反向解码 + 显示**：读 `.h264` → Annex-B→AVCC + 建 `CMVideoFormatDescription`（§四）→ `VTDecompressionSession` 解出 `CVPixelBuffer` → `AVSampleBufferDisplayLayer` 上屏（§七，先走最省事的那条）。
4. **接 WebRTC**：把上面的理解对到 libwebrtc `sdk/objc/` 的 `RTCVideoEncoder/Decoder` 实现上，看它怎么封装 VT 并接进 RTP 链路（§十三）。

落地顺序的核心：**先 Mac 调通编码 + 转换 + ffmpeg 验证，再补解码显示，最后接 RTC。** 每一步都有可验证的产出，不会"写一堆代码不知道对不对"。

---

## 十五、自检

1. VideoToolbox 在 Apple 媒体栈处于哪一层？它和 AVFoundation 各适合什么场景？
2. 为什么能在 M 系 Mac 上开发 VT 代码再移植到 iOS？（同源芯片 + 同一 API）
3. 编码生命周期五步是哪五步？为什么结束前必须 `CompleteFrames`？
4. `CVPixelBuffer` 和 `CMSampleBuffer` 分别装什么？SPS/PPS 存在哪个对象里？
5. VT 编码吐出什么比特流格式？推 RTP 前必须做哪两件事？AVCC 的 4 字节长度为什么容易被误当起始码？
6. 实时编码为什么要把 `AllowFrameReordering` 设 false？`RealTime=true` 对应桌面哪个概念？
7. VT 怎么做 CBR 近似？对应 [06](./06-编码参数与码控.md) 哪个缓冲模型？为什么 VT 没有 x264 那种 preset 数字档？
8. 运行时怎么动态降码率 / 强制关键帧？这两个旋钮在 WebRTC 里解决什么问题？
9. 怎么把 `CVPixelBuffer` 零拷贝交给 Metal？建 pool 漏了哪两个 key 会失效？哪个调用会破坏零拷贝？
10. 怎么强制 VT 用硬件编码？iOS 和 Intel Mac 在软件回退上有什么差别？
11. FFmpeg 在 iOS 上能硬件**编码**吗？和 Android 有何不同？
12. iOS 和 Android 的比特流格式为什么相反？跨平台时这点为什么容易翻车？
13. HEVC 的参数集补齐和 H.264 有什么区别？（多了 VPS）
14. App 切后台回前台后编码花屏/崩，根因和对策是什么？

> 一句话收尾：**VideoToolbox 是 Apple 媒体栈里"想自己控比特流/低延迟"的那一层；它吐 AVCC、参数集分离存 format description，所以推流必做 AVCC→Annex-B + 补 SPS/PPS；实时编码靠 RealTime + 关 B 帧 + 动态码率/强制关键帧这几个有限旋钮；零拷贝靠 CVPixelBuffer(IOSurface)→Metal、别锁基址。iOS 因芯片统一 + FFmpeg 双向支持，是硬编解最省心的平台。**
