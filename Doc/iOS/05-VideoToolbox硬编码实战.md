# VideoToolbox 硬编码实战：从 CVPixelBuffer 到 H.264 比特流

> **适用方向**：iOS 移动端音视频 SDK 开发，实时推流/RTC/录制方向
> **前置知识**：CVPixelBuffer / CMTime 基础，了解 H.264 编码基本概念（GOP/码率/Profile）
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 35 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[01-AVFoundation采集详解]] · [[../ffmpeg/15-iOS硬件编解码]] · [[04-Metal渲染与零拷贝详解]]
> **定位**：🟢 中级必会 —— 这是 iOS 端实时视频推流/RTC 的必经之路

---

## 零、VideoToolbox 是什么：概念与「全家桶」

> 本节先把 VideoToolbox（下文简称 **VT**）**是什么、在哪一层、由哪些东西组成**讲清楚，再进入编码实战。硬编码/硬解码两篇（05/06）共用这套概念。

### 0.1 一句话定义

**VideoToolbox 是 Apple 在 iOS / macOS / tvOS 上提供的一套底层 C 框架，让你直接调用设备里的专用视频编解码硬件（Media Engine / 编解码 ASIC），对单帧图像做 H.264 / HEVC / ProRes 等的压缩与解压。**

关键词拆开看：
- **底层**：纯 C API（`VTCompressionSessionCreate` 这种），不是 Objective-C/Swift 对象，需要手动管理引用计数（`CFRelease`）。
- **硬件**：真正干活的是 SoC 里独立的编解码单元，不占 CPU、不占 GPU 的通用算力，功耗极低、发热小——这正是移动端实时推流必须用它的原因。
- **单帧**：VT 只负责「一帧图像 ↔ 一段压缩数据」的转换。它**不做封装（mux）、不做网络传输、不做音视频同步**——那些是上层的事。

### 0.2 它在 iOS 媒体栈的位置

```
┌──────────────────────────────────────────────┐
│  应用 / SDK 层  (你的推流、RTC、录制逻辑)        │
├──────────────────────────────────────────────┤
│  AVFoundation   高层封装                        │
│   AVCaptureSession(采集) / AVAssetWriter(录制)  │  ← 傻瓜好用，但拿不到逐帧比特流
│   AVPlayer(播放) / AVAssetReader(读取)          │
├──────────────────────────────────────────────┤
│ ▶ VideoToolbox  ← 你在这一层                    │  ← 逐帧、可控、直通硬件
│   CoreMedia (CMSampleBuffer/CMTime/CMFormatDesc)│
│   CoreVideo  (CVPixelBuffer/IOSurface)          │
├──────────────────────────────────────────────┤
│  硬件驱动 / Media Engine (编解码 ASIC)           │  ← 真正压缩/解压像素的地方
└──────────────────────────────────────────────┘
```

- **往上**：AVFoundation 里的 `AVAssetWriter`、`AVCaptureVideoDataOutput` 等其实内部也是调 VT，只是替你把参数、比特流、封装都藏起来了。
- **往下**：VT 把你的请求翻译成对底层编解码硬件的调用，你不需要碰驱动。
- **平级**：FFmpeg 的 `h264_videotoolbox` / `h264_vt` 也是对 VT 的封装 wrapper。
- **一句话**：**VT 是「够底层能逐帧控制、又不用直接写驱动」的那个甜点层。**

### 0.3 「全家桶」：VideoToolbox 到底包含哪些东西

VT 本身很薄，它靠三个框架配合工作。真正要记的是这张「谁负责什么」的表：

| 类别 | 具体对象 | 属于哪个框架 | 干什么 |
|---|---|---|---|
| **① 会话 Session**（VT 的核心） | `VTCompressionSession` | VideoToolbox | **编码**：CVPixelBuffer → 压缩数据 |
| | `VTDecompressionSession` | VideoToolbox | **解码**：压缩数据 → CVPixelBuffer |
| | `VTPixelTransferSession` | VideoToolbox | 像素**格式转换/缩放**（如 BGRA→NV12） |
| **② 输入：未压缩帧** | `CVPixelBuffer` | CoreVideo | 一帧原始像素（NV12/BGRA…） |
| | `CVPixelBufferPool` | CoreVideo | 像素缓冲的**复用池**，避免反复分配 |
| | `IOSurface` | CoreVideo | 底层可被 GPU/编码器共享的物理内存（**零拷贝**基石） |
| **③ 输出：压缩帧** | `CMSampleBuffer` | CoreMedia | **一帧压缩数据的容器**（数据+时间+格式） |
| | `CMBlockBuffer` | CoreMedia | 真正装 H.264/HEVC **字节**的内存块 |
| | `CMFormatDescription` | CoreMedia | 帧的**格式描述**，编码后 **SPS/PPS 就藏在这里** |
| | `CMTime` | CoreMedia | 高精度时间戳（PTS/DTS） |
| **④ 配置/查询** | `VTSessionSetProperty` + `kVT*PropertyKey_*` | VideoToolbox | 设码率/GOP/RealTime/Profile 等**所有参数** |
| | `VTCopyVideoEncoderList` | VideoToolbox | 查当前设备**支持哪些硬件编码器** |
| | `VTSessionCopySupportedPropertyDictionary` | VideoToolbox | 查某个 session **支持哪些属性** |

一张图记住数据在这些对象间怎么流（以编码为例）：

```
CVPixelBuffer (CoreVideo, 未压缩)
      │  喂给
      ▼
VTCompressionSession (VideoToolbox, 调硬件压缩)
      │  异步回调吐出
      ▼
CMSampleBuffer (CoreMedia, 压缩帧容器)
   ├── CMBlockBuffer         → H.264/HEVC 字节流
   ├── CMFormatDescription   → SPS/PPS 参数集
   └── CMTime                → PTS/DTS
```

> 记忆口诀：**VideoToolbox 出「会话」，CoreVideo 管「进去的原始帧」，CoreMedia 管「出来的压缩帧」。** 三家分工，缺一不可。

### 0.4 VT 能做什么 / 不能做什么（划清边界）

| ✅ VT 负责 | ❌ VT 不负责（上层的事） |
|---|---|
| 单帧编码：像素 → H.264/HEVC/ProRes | 封装成 MP4/FLV/TS（那是 muxer 的活） |
| 单帧解码：压缩数据 → 像素 | 网络推流 RTMP/WebRTC（那是传输层） |
| 逐帧控码率、强制关键帧、改 GOP | 音频编码（音频走 AudioToolbox） |
| 像素格式转换/缩放（TransferSession） | 音视频同步、AVCC↔Annex-B 转换 |
| 直通硬件、零拷贝（配合 IOSurface） | 渲染上屏（那是 Metal/OpenGL 的活） |

**因此一条完整的推流管线是：** `采集(AVFoundation)` → `VT 编码` → `AVCC→Annex-B 转换` → `打包/推流(自己写或用库)`。VT 只是其中一环，但它是决定画质/延迟/功耗的关键一环。

### 0.5 常见误区先打预防针

- **「SPS/PPS 在比特流里」——错。** VT 编码把 SPS/PPS 放在 `CMFormatDescription` 里，不在 `CMBlockBuffer` 数据里，必须单独取出来拼到 Annex-B 流前面（详见 [[07-AVCC与Annex-B转换实战]]）。
- **「VT 是 Swift/OC 类」——错。** 是 C API + Core Foundation 对象，谁 `Create` 谁 `Release`，漏了就内存泄漏。
- **「编码是同步的」——错。** `VTCompressionSessionEncodeFrame` 立刻返回，压缩结果在**回调线程**异步吐出；结束前必须 `VTCompressionSessionCompleteFrames` 把管线里残留帧 flush 出来。
- **「VT 帮我发帧」——错。** 它只给你字节，怎么打包、怎么发全靠你。

---

## 一、全景导读：VideoToolbox 编码在 iOS 媒体栈的位置

### 1.1 从场景说起

你从 AVCaptureSession 拿到了 NV12 的 CVPixelBuffer。下一步要把它编码成 H.264 比特流，然后推 RTMP 或发 WebRTC。

三条路：
- **AVAssetWriter**：一行代码录 MP4，但你拿不到中间的比特流——没法推流
- **FFmpeg + h264_videotoolbox**：能用，但 FFmpeg 的 VT wrapper 封装了一层，多了一些不可控性
- **VideoToolbox 直调**：**最精细的控制**——能逐帧拿到压缩数据、动态调码率、强制关键帧

实时推流/RTC 唯一正确的选择是第三条路。

### 1.2 编码管线全景图

```
输入: CVPixelBuffer (NV12, IOSurface) + CMTime (PTS)
  │
  ▼
┌─────────────────────────────────────────────┐
│ VTCompressionSession                        │
│  ├─ RealTime = true (低延迟模式)             │
│  ├─ AllowFrameReordering = false (关B帧)    │
│  ├─ AverageBitRate = 2Mbps                  │
│  ├─ MaxKeyFrameInterval = 60 (GOP=2s@30fps)│
│  └─ ProfileLevel = Baseline_AutoLevel       │
└─────────────────────────────────────────────┘
  │  (异步, 硬件 Media Engine)
  ▼
输出回调: CMSampleBuffer
  ├─ CMBlockBuffer → H.264 AVCC 比特流
  ├─ CMVideoFormatDescription → SPS/PPS (单独存储!)
  └─ CMTime → PTS/DTS
  │
  ▼
AVCC → Annex-B 转换 → 推流/写文件
```

### 1.3 VideoToolbox vs AVAssetWriter vs FFmpeg h264_videotoolbox

| 维度 | VTCompressionSession | AVAssetWriter | FFmpeg h264_videotoolbox |
|------|---------------------|---------------|--------------------------|
| 控制粒度 | 逐帧控编码参数 | 只设 preset/outputSettings | 中等，受 FFmpeg 封装限制 |
| 拿比特流 | 回调逐帧拿 CMSampleBuffer | 不能拿 | 通过 AVPacket 拿 |
| 动态改码率 | VTSessionSetProperty 实时改 | 不支持 | 不直接支持 |
| 强制关键帧 | 逐帧传 ForceKeyFrame | 不支持 | 不直接支持 |
| 适合场景 | 直播推流/RTC/自定义录制 | 本地快速录制 | 软硬编混合/跨平台复用 |
| 学习曲线 | 中（C API，需理解 CoreMedia 类型） | 低（OC/Swift 高层 API） | 中（FFmpeg 统一接口） |

**一句话判断**：需要逐帧控制比特流 → VideoToolbox 直调。只是录个文件 → AVAssetWriter。跨平台已经用 FFmpeg → 用 h264_videotoolbox wrapper 也行但不推荐用于实时场景。

---

## 二、面试速记（考前 10 分钟）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | VTCompressionSession 创建要哪些参数 | 宽高、codecType(H.264/HEVC)、输出回调、可选的 encoderSpec/pool | 🔥🔥🔥 | 🟢 |
| 2 | 低延迟编码的关键属性 | RealTime=true + AllowFrameReordering=false | 🔥🔥🔥 | 🟢 |
| 3 | 编码完怎么拿数据 | 异步回调里收 CMSampleBuffer → CMBlockBuffer → 取字节 | 🔥🔥🔥 | 🟢 |
| 4 | SPS/PPS 在哪 | **不在数据流里！** 在 CMVideoFormatDescription 里，用 GetH264ParameterSetAtIndex 取 | 🔥🔥🔥 | 🟡 |
| 5 | 怎么动态改码率 | VTSessionSetProperty(_, AverageBitRate, newValue) | 🔥🔥 | 🟡 |
| 6 | 怎么强制关键帧 | EncodeFrame 时传 frameProperties: {ForceKeyFrame: true} | 🔥🔥 | 🟡 |
| 7 | 结束编码前要做什么 | VTCompressionSessionCompleteFrames 把管线里残留帧 flush 出来 | 🔥🔥 | 🟡 |
| 8 | 怎么判断编码器是否支持某格式 | VTCopyVideoEncoderList 查可用编码器 | 🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：给我讲讲你怎么用 VideoToolbox 做 H.264 硬编码？

**面试官想听什么：** 你能不能说出完整生命周期，不只是 API 名字。

**🗣️ 标准回答（可背诵）：**

> "VideoToolbox 编码的标准流程是 Create → Config → Encode loop → CompleteFrames → Invalidate 五步。创建 VTCompressionSession 时要指定分辨率、H.264 编码类型、和输出回调——编码是异步的，结果通过回调给你。创建完立即设低延迟属性：RealTime 设 true 告诉编码器优先速度、AllowFrameReordering 设 false 关 B 帧、AverageBitRate 设目标码率、MaxKeyFrameInterval 设 GOP 大小。然后就可以循环调用 VTCompressionSessionEncodeFrame——每次喂一个 CVPixelBuffer 和它的 PTS，编码器异步处理后在回调里给你 CMSampleBuffer。在回调里要做三件事：一是从 CMVideoFormatDescription 里取 SPS/PPS（关键帧时），二是从 CMBlockBuffer 里取压缩字节，三是做 AVCC→Annex-B 转换（如果要推流）。结束前必须调 CompleteFrames 把管线里缓冲的帧全逼出来，否则最后几帧就丢了。整个 session 是引用计数管理的，用完调 Invalidate + CFRelease。"

**👨‍💻 追问预警：**
> Q: "VTCompressionSessionEncodeFrame 是同步还是异步？"
> A: 异步——调用后函数立即返回，编码结果通过输出回调异步给。这意味着喂帧的顺序和收帧的顺序在关 B 帧后是一致的。也意味着你不能在 EncodeFrame 后立即用编码结果——真正拿到数据是在回调里。

---

#### Q2：实时编码为什么必须关 B 帧？

**面试官想听什么：** 你理解编码器内部的重排序延迟。

**🗣️ 标准回答（可背诵）：**

> "B 帧是双向预测帧——编码时要参考它前面和**后面**的帧。这意味着编码器必须先拿到未来的帧，才能编码当前帧——这就引入了所谓的'重排序延迟'，通常是几帧到十几帧的量级。30fps 下一帧 33ms，10 帧就是 330ms——这个延时对直播和视频通话是不可接受的。关掉 B 帧后，编码器只使用 I 帧和 P 帧，每帧到达立即编码立即输出，延迟降低到一帧以内。在 VideoToolbox 里对应 AllowFrameReordering=false——这个名字暗示了 B 帧的本质：编码顺序和显示顺序不一样，需要重排序。"

---

## 三、核心 Demo：完整的 VT H.264 编码器

### 3.1 VTH264Encoder.h

```objc
//  VTH264Encoder.h
//  完整的 iOS VideoToolbox H.264 实时编码器
//
//  特性:
//  - 异步编码，输出回调
//  - 低延迟配置（RealTime + 关B帧）
//  - 动态码率调整
//  - 强制关键帧
//  - CVPixelBufferPool 复用

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// 编码器配置
@interface VTH264EncoderConfig : NSObject
@property (nonatomic, assign) int width;                    // 默认 1920
@property (nonatomic, assign) int height;                   // 默认 1080
@property (nonatomic, assign) int fps;                      // 默认 30
@property (nonatomic, assign) int bitrate;                  // 默认 2000000 (2Mbps)
@property (nonatomic, assign) int maxKeyFrameInterval;     // 默认 60 (2s@30fps)
@property (nonatomic, assign) BOOL realTime;               // 默认 YES
@property (nonatomic, assign) BOOL allowFrameReordering;   // 默认 NO
@property (nonatomic, assign) CFStringRef profileLevel;    // 默认 kVTProfileLevel_H264_Baseline_AutoLevel
@end

/// 编码输出回调（每个编码帧回调一次）
typedef void(^VTH264EncoderOutputCallback)(CMSampleBufferRef sampleBuffer, BOOL isKeyFrame);

/// 编码器
@interface VTH264Encoder : NSObject

- (instancetype)initWithConfig:(VTH264EncoderConfig *)config
               outputCallback:(VTH264EncoderOutputCallback)callback;

/// 喂入一帧原始 YUV 数据进行编码（线程安全）
- (BOOL)encodePixelBuffer:(CVPixelBufferRef)pixelBuffer
              presentationTime:(CMTime)pts;

/// 强制下一帧为 IDR 关键帧（对端 PLI 时调用）
- (void)forceKeyFrame;

/// 动态调整码率（拥塞控制时调用）
- (BOOL)setBitrate:(int)newBitrate;

/// 结束编码：flush 管线中的所有帧
- (void)finishWithCompletion:(void(^)(void))completion;

/// 销毁编码器
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
```

### 3.2 VTH264Encoder.m

```objc
//  VTH264Encoder.m
//  完整的 iOS VideoToolbox H.264 实时编码器实现

#import "VTH264Encoder.h"

@implementation VTH264EncoderConfig
- (instancetype)init {
    self = [super init];
    if (self) {
        _width  = 1920;
        _height = 1080;
        _fps    = 30;
        _bitrate = 2000000;
        _maxKeyFrameInterval = 60;
        _realTime = YES;
        _allowFrameReordering = NO;
        _profileLevel = kVTProfileLevel_H264_Baseline_AutoLevel;
    }
    return self;
}
@end

// ============================================================
// MARK: - VTH264Encoder 实现
// ============================================================
@implementation VTH264Encoder {
    VTH264EncoderConfig    *_config;
    VTCompressionSessionRef _session;
    CVPixelBufferPoolRef    _pixelBufferPool;
    dispatch_queue_t        _encodeQueue;      // 编码调度队列
    dispatch_queue_t        _callbackQueue;    // 输出回调队列
    VTH264EncoderOutputCallback _outputCallback;
    BOOL                    _forceKeyFrame;    // 下一帧强制 IDR
    BOOL                    _isFlushing;
    int                     _frameCount;
}

// ============================================================
// MARK: - 初始化
// ============================================================
- (instancetype)initWithConfig:(VTH264EncoderConfig *)config
               outputCallback:(VTH264EncoderOutputCallback)callback {
    self = [super init];
    if (!self) return nil;

    _config = config;
    _outputCallback = [callback copy];
    _encodeQueue = dispatch_queue_create("com.vt.encoder.encode",
                                          DISPATCH_QUEUE_SERIAL);
    _callbackQueue = dispatch_queue_create("com.vt.encoder.callback",
                                            DISPATCH_QUEUE_SERIAL);

    if (![self createCompressionSession]) return nil;
    if (![self createPixelBufferPool])    return nil;

    return self;
}

- (void)dealloc {
    [self invalidate];
}

// ============================================================
// MARK: - 创建 VTCompressionSession
// ============================================================
- (BOOL)createCompressionSession {
    OSStatus status;

    // ① 创建编码会话
    status = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        _config.width,
        _config.height,
        kCMVideoCodecType_H264,             // H.264 编码
        NULL,                                // encoderSpecification（NULL = 默认）
        NULL,                                // sourceImageBufferAttributes
        NULL,                                // compressedDataAllocator（NULL = 默认）
        &VTH264EncoderOutputCallback,        // ★ 输出回调函数
        (__bridge void *)self,               // 回调上下文（self）
        &_session
    );
    if (status != noErr) {
        NSLog(@"❌ VTCompressionSessionCreate 失败: %d", (int)status);
        return NO;
    }

    // ② 配置低延迟实时编码属性
    //    这些属性是「实时推流 / RTC」的标准配置
    [self setSessionProperty:kVTCompressionPropertyKey_RealTime
                       value:kCFBooleanTrue];
    [self setSessionProperty:kVTCompressionPropertyKey_AllowFrameReordering
                       value:_config.allowFrameReordering ? kCFBooleanTrue : kCFBooleanFalse];

    // ③ 码率控制
    int bitrate = _config.bitrate;
    CFNumberRef bitrateNum = CFNumberCreate(NULL, kCFNumberIntType, &bitrate);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
    CFRelease(bitrateNum);

    // DataRateLimits: [字节数/秒, 秒数] 硬上限，配合 AverageBitRate 做 CBR 近似
    // 这里设 1.5 × bitrate 作为 1 秒内的硬上限
    int64_t dataRateBytes = (int64_t)(bitrate * 1.5 / 8);
    int64_t dataRateSeconds = 1;
    CFNumberRef limits[2] = {
        CFNumberCreate(NULL, kCFNumberSInt64Type, &dataRateBytes),
        CFNumberCreate(NULL, kCFNumberSInt64Type, &dataRateSeconds)
    };
    CFArrayRef dataRateLimits = CFArrayCreate(NULL, (const void **)limits, 2, NULL);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_DataRateLimits, dataRateLimits);
    for (int i = 0; i < 2; i++) CFRelease(limits[i]);
    CFRelease(dataRateLimits);

    // ④ 帧率 & GOP
    int fps = _config.fps;
    CFNumberRef fpsNum = CFNumberCreate(NULL, kCFNumberIntType, &fps);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_ExpectedFrameRate, fpsNum);
    CFRelease(fpsNum);

    int gop = _config.maxKeyFrameInterval;
    CFNumberRef gopNum = CFNumberCreate(NULL, kCFNumberIntType, &gop);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_MaxKeyFrameInterval, gopNum);
    CFRelease(gopNum);

    // GOP 时间上限（2 秒），和帧数上限取先到者
    int gopDuration = 2;
    CFNumberRef gopDur = CFNumberCreate(NULL, kCFNumberIntType, &gopDuration);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, gopDur);
    CFRelease(gopDur);

    // ⑤ Profile & Level
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_ProfileLevel, _config.profileLevel);

    // ⑥ 关 Open GOP（实时场景需要闭合 GOP）
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_AllowOpenGOP, kCFBooleanFalse);

    // ⑦ 可选：预热 session
    VTCompressionSessionPrepareToEncodeFrames(_session);

    NSLog(@"✅ VTCompressionSession 创建成功: %dx%d @ %d fps, %d bps",
          _config.width, _config.height, fps, bitrate);
    return YES;
}

- (void)setSessionProperty:(CFStringRef)key value:(CFTypeRef)value {
    VTSessionSetProperty(_session, key, value);
}

// ============================================================
// MARK: - 创建 PixelBufferPool（保证零拷贝 + 复用）
// ============================================================
- (BOOL)createPixelBufferPool {
    NSDictionary *attrs = @{
        (id)kCVPixelBufferWidthKey:  @(_config.width),
        (id)kCVPixelBufferHeightKey: @(_config.height),
        (id)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
        (id)kCVPixelBufferMinimumBufferCountKey: @(6),
        // ★ 两个零拷贝 key
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    CVReturn ret = CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL,
        (__bridge CFDictionaryRef)attrs, &_pixelBufferPool);
    if (ret != kCVReturnSuccess) {
        NSLog(@"❌ CVPixelBufferPool 创建失败: %d", ret);
        return NO;
    }
    return YES;
}

// ============================================================
// MARK: - 编码一帧
// ============================================================
- (BOOL)encodePixelBuffer:(CVPixelBufferRef)pixelBuffer
              presentationTime:(CMTime)pts {
    if (!_session) return NO;

    // 准备逐帧属性（强制关键帧）
    CFMutableDictionaryRef frameProps = NULL;
    if (_forceKeyFrame) {
        frameProps = CFDictionaryCreateMutable(kCFAllocatorDefault, 1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(frameProps,
            kVTEncodeFrameOptionKey_ForceKeyFrame, kCFBooleanTrue);
        _forceKeyFrame = NO;
    }

    // ★ 异步编码：EncodeFrame 立即返回，结果在回调里给
    OSStatus status = VTCompressionSessionEncodeFrame(
        _session,
        pixelBuffer,
        pts,                    // PTS
        kCMTimeInvalid,         // duration（硬件自己算）
        frameProps,             // 逐帧属性（可为 NULL）
        NULL,                   // sourceFrameRefCon
        NULL                    // infoFlagsOut
    );

    if (frameProps) CFRelease(frameProps);

    if (status != noErr) {
        NSLog(@"❌ EncodeFrame 失败: %d", (int)status);
        return NO;
    }

    _frameCount++;
    return YES;
}

// ============================================================
// MARK: - 强制关键帧（对端 PLI 时调用）
// ============================================================
- (void)forceKeyFrame {
    _forceKeyFrame = YES;
}

// ============================================================
// MARK: - 动态改码率（拥塞控制时调用）
// ============================================================
- (BOOL)setBitrate:(int)newBitrate {
    if (!_session) return NO;
    _config.bitrate = newBitrate;

    CFNumberRef bitrateNum = CFNumberCreate(NULL, kCFNumberIntType, &newBitrate);
    OSStatus status = VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_AverageBitRate, bitrateNum);
    CFRelease(bitrateNum);

    if (status != noErr) {
        NSLog(@"❌ 动态改码率失败: %d", (int)status);
        return NO;
    }

    // 同步更新 DataRateLimits
    int64_t dataRateBytes = (int64_t)(newBitrate * 1.5 / 8);
    int64_t dataRateSeconds = 1;
    CFNumberRef limits[2] = {
        CFNumberCreate(NULL, kCFNumberSInt64Type, &dataRateBytes),
        CFNumberCreate(NULL, kCFNumberSInt64Type, &dataRateSeconds)
    };
    CFArrayRef dataRateLimits = CFArrayCreate(NULL, (const void **)limits, 2, NULL);
    VTSessionSetProperty(_session,
        kVTCompressionPropertyKey_DataRateLimits, dataRateLimits);
    for (int i = 0; i < 2; i++) CFRelease(limits[i]);
    CFRelease(dataRateLimits);

    NSLog(@"📊 码率调整: %d bps", newBitrate);
    return YES;
}

// ============================================================
// MARK: - 结束 & 销毁
// ============================================================
- (void)finishWithCompletion:(void(^)(void))completion {
    if (!_session) {
        if (completion) completion();
        return;
    }

    // ★ 关键：flush 管线里残留的帧
    // 不调这个的话，最后几帧（还在硬件管线里) 永远不会吐到回调
    OSStatus status = VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
    if (status != noErr) {
        NSLog(@"⚠️ CompleteFrames 失败: %d", (int)status);
    }

    // 给回调一点时间收到 flush 的帧
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1 * NSEC_PER_SEC),
                   _callbackQueue, ^{
        if (completion) completion();
    });
}

- (void)invalidate {
    if (_session) {
        VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(_session);
        CFRelease(_session);
        _session = NULL;
    }
    if (_pixelBufferPool) {
        CFRelease(_pixelBufferPool);
        _pixelBufferPool = NULL;
    }
}

// ============================================================
// MARK: - ★ 输出回调（C 函数）★
// ============================================================
static void VTH264EncoderOutputCallback(
    void *outputCallbackRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTEncodeInfoFlags infoFlags,
    CMSampleBufferRef sampleBuffer)
{
    if (status != noErr) {
        NSLog(@"❌ 编码回调错误: %d", (int)status);
        return;
    }
    if (!sampleBuffer) return;

    VTH264Encoder *encoder = (__bridge VTH264Encoder *)outputCallbackRefCon;

    // 判断是否关键帧
    BOOL isKeyFrame = [VTH264Encoder isKeyFrame:sampleBuffer];

    // 把数据交给外部回调
    if (encoder->_outputCallback) {
        // ⚠️ retain 一下，防止调用者还没处理完就被释放
        CFRetain(sampleBuffer);

        dispatch_async(encoder->_callbackQueue, ^{
            encoder->_outputCallback(sampleBuffer, isKeyFrame);
            CFRelease(sampleBuffer);
        });
    }
}

// ============================================================
// MARK: - 工具方法
// ============================================================
+ (BOOL)isKeyFrame:(CMSampleBufferRef)sampleBuffer {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(
        sampleBuffer, true); // true = create if needed
    if (!attachments || CFArrayGetCount(attachments) == 0) return NO;

    CFDictionaryRef attachment = CFArrayGetValueAtIndex(attachments, 0);
    // NotSync = false/不存在 → 是关键帧
    // NotSync = true  → 不是关键帧
    CFBooleanRef notSync = CFDictionaryGetValue(attachment,
        kCMSampleAttachmentKey_NotSync);
    return !notSync || !CFBooleanGetValue(notSync);
}

+ (NSData *)spsData:(CMSampleBufferRef)sampleBuffer {
    return [self parameterSetData:sampleBuffer index:0];
}

+ (NSData *)ppsData:(CMSampleBufferRef)sampleBuffer {
    return [self parameterSetData:sampleBuffer index:1];
}

+ (NSData *)parameterSetData:(CMSampleBufferRef)sampleBuffer index:(size_t)idx {
    CMVideoFormatDescriptionRef fmt =
        CMSampleBufferGetFormatDescription(sampleBuffer);
    if (!fmt) return nil;

    const uint8_t *paramSet = NULL;
    size_t paramSetSize = 0;
    size_t paramSetCount = 0;
    int nalUnitHeaderLength = 0;

    OSStatus ret = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        fmt, idx, &paramSet, &paramSetSize, &paramSetCount, &nalUnitHeaderLength);
    if (ret != noErr || !paramSet || paramSetSize == 0) return nil;

    return [NSData dataWithBytes:paramSet length:paramSetSize];
}

+ (NSData *)naluData:(CMSampleBufferRef)sampleBuffer {
    // 从 CMBlockBuffer 取出完整的 AVCC 数据
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!block) return nil;

    size_t totalLen = 0;
    char *dataPtr = NULL;
    OSStatus ret = CMBlockBufferGetDataPointer(block, 0, NULL, &totalLen, &dataPtr);
    if (ret != noErr || !dataPtr || totalLen == 0) return nil;

    return [NSData dataWithBytes:dataPtr length:totalLen];
}

@end
```

### 3.3 使用示例

```objc
// ---- 1. 创建编码器 ----
VTH264EncoderConfig *config = [[VTH264EncoderConfig alloc] init];
config.width  = 1920;
config.height = 1080;
config.fps    = 30;
config.bitrate = 2000000; // 2Mbps

VTH264Encoder *encoder = [[VTH264Encoder alloc] initWithConfig:config
    outputCallback:^(CMSampleBufferRef sampleBuffer, BOOL isKeyFrame) {

        if (isKeyFrame) {
            // 取出 SPS/PPS
            NSData *sps = [VTH264Encoder spsData:sampleBuffer];
            NSData *pps = [VTH264Encoder ppsData:sampleBuffer];
            // 写入 Annex-B 流: [00 00 00 01] + SPS + [00 00 00 01] + PPS
        }

        // 取出 NALU 数据（AVCC 格式，需转 Annex-B 后再推流）
        NSData *naluData = [VTH264Encoder naluData:sampleBuffer];
        // 转 Annex-B → 发送
    }];

// ---- 2. 循环编码 ----
// 在摄像头回调里：
CMTime pts = CMTimeMake(frameIndex, 30); // timescale = 30
[encoder encodePixelBuffer:pixelBuffer presentationTime:pts];

// ---- 3. 对端 PLI → 强制 IDR ----
[encoder forceKeyFrame];

// ---- 4. 拥塞控制 → 降码率 ----
[encoder setBitrate:1500000]; // 降到 1.5Mbps

// ---- 5. 结束时 flush ----
[encoder finishWithCompletion:^{
    // 所有帧已输出完毕，可以安全销毁
    [encoder invalidate];
}];
```

---

## 四、关键设计决策解读

### 4.1 为什么 RealTime=true + AllowFrameReordering=false 是实时编码的标配？

这是一个"用压缩率换延迟"的取舍：

| 参数 | 设 true/false 的影响 |
|------|---------------------|
| `RealTime = true` | 编码器不做耗时的多 pass 搜索、不用 lookahead——压缩率下降 10-20%，但延迟从 200ms+ 降到 < 5ms |
| `AllowFrameReordering = false` | 禁用 B 帧，编码顺序 = 显示顺序——消除重排序延迟 |

两者合在一起的效果 ≈ x264 的 `-tune zerolatency`。如果没有这两个设置，VT 默认可能用 B 帧 + lookahead，导致编码延迟 200-500ms——对实时通信是致命的。

### 4.2 为什么编码输出是异步的？

硬件编码器的内部是流水线结构：帧进入后先做运动估计、再做变换量化、最后熵编码输出。流水线长度可能是 3-5 帧。同步等待会让 CPU 线程白白阻塞。异步模型让 CPU 可以立即回去处理下一帧——采集线程不被阻塞，丢帧率降低。

这也是为什么 `CompleteFrames` 很重要——关 session 前必须等管线排空。

### 4.3 为什么 DataRateLimits 要设成 1.5× bitrate？

`DataRateLimits` 是一个硬帽——[字节数, 秒数] 表示"在 X 秒内最多输出 Y 字节"。设成 1.5× AverageBitRate 给瞬时码率留一些弹性（场景运动剧烈时会有码率尖峰），但又不让它无限制冲高。这个 1.5 是个经验值，要根据实际网络带宽和应用场景调整。如果是严格 CBR 需求，用较新系统的 `ConstantBitRate` 属性。

---

## 五、进阶话题

### 5.1 HEVC 编码

H.264 → HEVC 只需改三个地方：

```objc
// ① Codec 类型
kCMVideoCodecType_HEVC  // 替代 kCMVideoCodecType_H264

// ② Profile
kVTProfileLevel_HEVC_Main_AutoLevel   // 8-bit SDR
kVTProfileLevel_HEVC_Main10_AutoLevel // 10-bit HDR

// ③ 取了参数集（多一个 VPS）
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 0, ...); // VPS
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 1, ...); // SPS
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 2, ...); // PPS
```

### 5.2 编码前格式转换（可选：VT 会自动转，但有更优解）

> ⚠️ **先纠正一个常见误解**：并不是「必须手动把 BGRA 转成 NV12 才能编码」。`VTCompressionSession` 接受多种输入像素格式，BGRA(`kCVPixelFormatType_32BGRA`)通常也被硬件编码器接受——**你喂进去非原生格式时，VT 会在内部自动插入一次转换**（走 GPU/ISP），直接编也不会报错。所以下面这段 `VTPixelTransferSession` **不是功能上必需的**。

**但「能自动转」≠「应该让它自动转」**，隐式转换有代价：

| 做法 | 代价 |
|---|---|
| 喂 **NV12**（编码器原生格式） | 无转换，最快、最省电、可零拷贝 |
| 喂 **BGRA** 让 VT 隐式转 | 多一次 RGB→YUV，吃功耗/延迟，且是看不见的「黑箱」 |
| 手动 `VTPixelTransferSession` | 自己控转换，但也多一次显式 pass |

**✅ 最佳实践：从编码器给的 PixelBufferPool 拿「对的格式」的缓冲，从源头就不发生转换：**

```objc
// 从 compression session 拿它「亲儿子」格式的缓冲池
CVPixelBufferPoolRef pool =
    VTCompressionSessionGetPixelBufferPool(session);

// 从池里要 buffer——已是编码器要的格式 + IOSurface 背衬(零拷贝)
CVPixelBufferRef dst;
CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &dst);
// 让 Metal 渲染 / 采集 直接写进 dst，再喂给编码器，全程零转换
```

另外：如果直接从 `AVCaptureVideoDataOutput` 采集，把 `videoSettings` 设成
`kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`（NV12），**采集端就直接给 NV12，这一步根本不存在**。BGRA 基本只在「从 Metal/GPU 渲染结果拿帧」时才遇到。

**那 `VTPixelTransferSession` 什么时候才真正需要？** 三种场景：① 编码前要**缩放**（如 1080p 采集编 720p），顺带转格式；② 源格式编码器**确实不接受**，需显式桥接；③ 需要**精确控制色彩范围/矩阵**（video range ↔ full range）。此时才用它：

```objc
// 创建转换 session
VTPixelTransferSessionRef transferSession;
VTPixelTransferSessionCreate(kCFAllocatorDefault, &transferSession);

// 转换：srcBuffer(BGRA) → dstBuffer(NV12)
VTPixelTransferSessionTransferImage(transferSession,
    srcBuffer, dstBuffer);
```

这在功能上等价于 FFmpeg 的 `scale_vt` 滤镜。

> **一句话**：优先用 `GetPixelBufferPool` 从源头拿对的格式（零转换）；`VTPixelTransferSession` 只在**要缩放或强控色彩**时才用；实在都没做，VT 也会替你隐式转，只是多花点功耗。

### 5.3 编码器能力查询

```objc
// 查当前设备所有可用视频编码器
CFArrayRef encoderList = NULL;
VTCopyVideoEncoderList(NULL, &encoderList);
// 遍历，看是否支持 HEVC、特定分辨率等

// 快速检测 HEVC 硬编支持
BOOL supportsHEVC = VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC);
```

### 5.4 后台处理

App 进后台后，VTCompressionSession 可能被系统暂停或失效。回到前台时：

```objc
- (void)handleApplicationDidBecomeActive {
    // 1. 如果 session 已失效，重建
    if (![self isSessionValid]) {
        [self invalidate];
        [self createCompressionSession];
    }
    // 2. 强制输出一个 IDR（对端解码器才能重新同步）
    [self forceKeyFrame];
}
```

---

## 六、常见坑

### 坑 1：回调里阻塞导致管线卡死

回调是 VT 内部线程调用的。如果在回调里做耗时操作（网络 IO、同步锁），会阻塞整个 VT 管线，后续帧无法输出。

**对策**：回调里只做最轻量的操作——CFRetain sampleBuffer → 分发到异步队列 → 立即返回。

### 坑 2：忘记 CompleteFrames 丢最后几帧

编码器的硬件管线里有 3-5 帧在排队。直接 Invalidate 会丢掉它们。

**对策**：先调 `CompleteFrames`，等一小段时间（0.1-0.2s）让回调走完，再 `Invalidate`。

### 坑 3：CVPixelBuffer 引用计数未释放

如果 `CVPixelBufferPool` 里分配的 buffer 被业务层持有不放（比如 retain 了没有 release），池子里可用的 buffer 会越来越少，最终导致 `CVPixelBufferPoolCreatePixelBuffer` 失败——编码器拿不到输入 buffer 而卡住。

**对策**：用完后立即 `CFRelease`。或者用 `@autoreleasepool`。

### 坑 4：分辨率/像素格式不支持

某些分辨率（如非 16 倍数）可能导致硬件编码失败。iOS 基本没有软编回退，不支持的会直接失败。

**对策**：编码前对宽高做 16 对齐（或至少偶数对齐）；创建 session 时检查返回值。

---

## 🎯 一句话总结

> VideoToolbox 编码 = 创建 VTCompressionSession + 配好 RealTime + 关 B 帧 + 异步回调里取数据。关键帧前自己补 SPS/PPS、实时改码率用 VTSessionSetProperty、强制 IDR 在 EncodeFrame 时传 ForceKeyFrame、结束前调 CompleteFrames flush。

## 🔗 关联文档

- [[../ffmpeg/15-iOS硬件编解码]] — VT 编解码的全文深讲
- [[01-AVFoundation采集详解]] — 编码输入 CVPixelBuffer 的来源
- [[04-Metal渲染与零拷贝详解]] — 编码前的 Metal 预处理（美颜/滤镜）
- [[06-VideoToolbox硬解码实战]] — 解码端
- [[07-AVCC与Annex-B转换实战]] — 拿到编码输出后的格式转换
- [[08-端到端采集编码推流管线]] — 把编码器接入完整推流管线
