# VideoToolbox 硬解码实战：从 H.264 比特流到 CVPixelBuffer

## 0. 本篇定位

- 面试复习：先掌握 `VTDecompressionSession`、format description、Annex-B 到 AVCC、SPS/PPS 更新和解码失败恢复。
- 深入学习：重点看输出 `CVPixelBuffer` 的生命周期、`IOSurface` 后端、解码会话重建和渲染衔接。
- 工程落点：硬解码最好直接输出可被 Metal/显示层消费的 buffer，CPU 读回只作为特殊算法或调试路径。
> **适用方向**：iOS 移动端音视频 SDK 开发，播放器/RTC 拉流方向
> **前置知识**：H.264 NALU 基础、CMSampleBuffer / CVPixelBuffer / CMVideoFormatDescription
> **难度**：⭐⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 35 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[../ffmpeg/15-iOS硬件编解码]] · [[04-Metal渲染与零拷贝详解]] · [[05-VideoToolbox硬编码实战]]
> **定位**：🟢 中级必会 —— RTC 拉流、播放器、本地视频处理的核心

---

## 一、全景导读：VT 解码在 iOS 媒体栈的位置

### 1.1 从场景说起

你通过网络收到了 H.264 视频流（RTMP 拉流 / WebRTC 接收端 / 本地文件解封装），现在需要把它解码成画面显示到屏幕上。

在 iOS 上，解码是 VideoToolbox 的另一个核心能力——`VTDecompressionSession`。它和编码方向相反：喂压缩的 CMSampleBuffer → 异步回调拿未压缩的 CVPixelBuffer → Metal 渲染或 AVSampleBufferDisplayLayer 显示。

**iOS 解码有一个 Android 没有的硬门槛**：必须先有 CMVideoFormatDescription（即 SPS/PPS），才能创建解码会话。不像 Android 的 MediaCodec 可以在 configure 时直接把 csd-0/csd-1 传进去。这意味着从 Annex-B 裸流解码时，第一步是从流里提取 SPS/PPS 建格式描述。

### 1.2 解码管线全景图

```
输入: Annex-B H.264 裸流 / RTP packets / MP4 demux
  │
  ▼
① 提取 SPS/PPS → CMVideoFormatDescriptionCreateFromH264ParameterSets
  │
  ▼
② 创建 VTDecompressionSession(formatDesc, 输出回调)
  │
  ▼
③ Annex-B → AVCC 转换: 起始码 → 4 字节长度前缀
  │  (VideoToolbox 吃 AVCC，不吃 Annex-B)
  │
  ▼
④ 包成 CMSampleBuffer(CMBlockBuffer + formatDesc + PTS)
  │
  ▼
⑤ VTDecompressionSessionDecodeFrame(sampleBuffer)
  │  (异步)
  ▼
⑥ 输出回调: CVPixelBuffer (NV12, IOSurface)
  │
  ├──→ Metal 渲染（CVMetalTextureCache 零拷贝）
  └──→ AVSampleBufferDisplayLayer（最省事）
```

### 1.3 解码 vs 编码：关键差异一览

| 维度 | 编码 (VTCompressionSession) | 解码 (VTDecompressionSession) |
|------|---------------------------|------------------------------|
| 输入 | CVPixelBuffer | CMSampleBuffer（包着 CMBlockBuffer） |
| 输出 | CMSampleBuffer（包着 CMBlockBuffer） | CVPixelBuffer |
| 前置条件 | 宽高 + codec 即可 | **必须先有 CMVideoFormatDescription（SPS/PPS）** |
| 比特流格式 | 输出 AVCC | **输入必须是 AVCC** |
| 动态控制 | 改码率/强制关键帧 | 几乎没有运行时参数调整 |
| 会话复用 | 可复用以持续编码 | 格式变了必须重建（不要复用） |

---

## 二、面试速记（考前 10 分钟）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | 解码前必须有什么 | CMVideoFormatDescription（从 SPS/PPS 创建），这是解码的入场券 | 🔥🔥🔥 | 🟢 |
| 2 | 解码器吃什么格式 | **AVCC**（4 字节长度前缀），不是 Annex-B | 🔥🔥🔥 | 🟡 |
| 3 | Annex-B 怎么喂给 VT | 先把起始码替换为 4 字节长度前缀（反向操作），再从 IDR 帧拆 SPS/PPS | 🔥🔥🔥 | 🟡 |
| 4 | 解码输出是什么 | CVPixelBuffer（NV12, IOSurface），可直接 Metal 渲染 | 🔥🔥🔥 | 🟢 |
| 5 | 解码会话能复用吗 | **不能**——格式变化（分辨率/SPS 变了）必须重建，Chromium 踩过这个坑 | 🔥🔥 | 🟡 |
| 6 | 怎么判断硬件解码是否支持 | VTIsHardwareDecodeSupported(kCMVideoCodecType_H264) | 🔥🔥 | 🟢 |
| 7 | PixelBuffer 的 attributes 要注意什么 | 必须带 kCVPixelBufferIOSurfacePropertiesKey，否则退化成普通内存 | 🔥🔥 | 🟡 |
| 8 | 解码失败怎么恢复 | 重建 session + 从下一个 IDR 开始喂帧 | 🔥🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：VideoToolbox 解码的完整流程是怎样的？

**面试官想听什么：** 你是否理解解码的前置条件（CMVideoFormatDescription）和数据格式（AVCC）要求。

**🗣️ 标准回答（可背诵）：**

> "iOS 上 VideoToolbox 解码分四步。第一步，从流里拿到 SPS 和 PPS——如果是 Annex-B 的 H.264 裸流，要从 IDR 前面的 SPS/PPS NALU 里提取；如果是 MP4，从 avcC box 里取。用这些参数集调 CMVideoFormatDescriptionCreateFromH264ParameterSets 创建格式描述对象——没有它解码会话根本建不起来，这是 iOS 解码的硬门槛。第二步，用这个 formatDescription 创建 VTDecompressionSession，并在创建时指定目标 CVPixelBuffer 的属性——必须带 IOSurface 和 Metal compatibility key 才能零拷贝给渲染层。第三步，喂帧——但注意 VideoToolbox 吃的是 AVCC 格式而不是 Annex-B，所以要把 Annex-B 流的起始码 00 00 00 01 替换成 4 字节大端长度前缀，然后包成 CMSampleBuffer 调 DecodeFrame。第四步，在输出回调里拿 CVPixelBuffer，直接给 Metal 渲染或 enqueue 到 AVSampleBufferDisplayLayer。这里有个关键——如果视频分辨率或 SPS 中途变了，必须重建解码会话，不能复用。Chromium 的 WebRTC 团队就踩过 VTDecompressionSessionCanAcceptFormatDescription 的坑，最终结论就是永远不要复用。"

**👨‍💻 追问预警：**
> Q: "DecodeFrame 是同步的还是异步的？"
> A: 可以选。传 `kVTDecodeFrame_EnableAsynchronousDecompression` 标志是异步——回调里收结果；不传是同步——DecodeFrame 返回后直接拿到解码后的 CVPixelBuffer。一般用异步，因为同步模式会让调用线程阻塞等硬件解码完成，影响上层调度。

---

#### Q2：Annex-B → AVCC 转换的具体字节操作是怎样的？

**面试官想听什么：** 你真正写过这段转换代码。

**🗣️ 标准回答（可背诵）：**

> "转换分两层。第一层是参数集——从 Annex-B 流扫出 SPS NALU (type=7) 和 PPS NALU (type=8)，去掉它们的起始码，把纯 SPS/PPS 字节传给 CMVideoFormatDescriptionCreateFromH264ParameterSets——注意这里的参数集是纯 NALU 数据，不带起始码也不带长度前缀。第二层是帧数据——把每个 NALU 前面的 00 00 00 01（或 00 00 01）起始码替换成 4 字节大端长度前缀——长度等于这个 NALU 的字节数（不含长度前缀本身）。注意 IDR 帧只有画面数据，SPS/PPS 不要再放进去了，因为已经在 format description 里了。实际代码中我是先扫一遍字节流，统计 SPS/PPS NALU 的起始和结束位置，然后把 VCL NALU 的起始码替换成长度前缀，装进 CMBlockBuffer 包成 CMSampleBuffer 喂解码器。"

---

## 三、核心 Demo：完整的 VT H.264 解码器

### 3.1 VTH264Decoder.h

```objc
//  VTH264Decoder.h
//  完整的 iOS VideoToolbox H.264 硬件解码器

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// 解码输出回调
typedef void(^VTH264DecoderOutputCallback)(CVPixelBufferRef pixelBuffer,
                                            CMTime pts,
                                            NSError * _Nullable error);

@interface VTH264Decoder : NSObject

/// 输出回调（解码线程上调用，不要阻塞）
@property (nonatomic, copy, nullable) VTH264DecoderOutputCallback outputCallback;

/// 喂入一帧 H.264 AVCC 格式的 CMSampleBuffer 进行解码
/// @param sampleBuffer 必须包含 CMBlockBuffer（压缩数据）
///        + CMVideoFormatDescription + CMTime(PTS)
- (BOOL)decodeSampleBuffer:(CMSampleBufferRef)sampleBuffer;

/// 从 SPS/PPS 裸数据创建格式描述（解码器初始化前调用）
/// @return 成功返回格式描述，失败返回 nil
+ (nullable CMVideoFormatDescriptionRef)createFormatDescriptionFromSPS:(NSData *)sps
                                                                   PPS:(NSData *)pps;

/// 将 Annex-B 格式的 H.264 NALU 转为 AVCC CMSampleBuffer
+ (nullable CMSampleBufferRef)createSampleBufferFromAnnexBNALU:(NSData *)naluData
                                                   formatDesc:(CMVideoFormatDescriptionRef)formatDesc
                                                           pts:(CMTime)pts;

/// 从 Annex-B 流中提取 SPS 数据
+ (nullable NSData *)extractSPSFromAnnexB:(NSData *)annexBData;

/// 从 Annex-B 流中提取 PPS 数据
+ (nullable NSData *)extractPPSFromAnnexB:(NSData *)annexBData;

/// 销毁解码器
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
```

### 3.2 VTH264Decoder.m

```objc
//  VTH264Decoder.m
//  完整的 iOS VideoToolbox H.264 硬件解码器实现

#import "VTH264Decoder.h"

static const uint8_t kAnnexBStartCode3[] = {0x00, 0x00, 0x01};
static const uint8_t kAnnexBStartCode4[] = {0x00, 0x00, 0x00, 0x01};

@implementation VTH264Decoder {
    VTDecompressionSessionRef _session;
    CMVideoFormatDescriptionRef _formatDesc;
    dispatch_queue_t _decodeQueue;
    dispatch_queue_t _callbackQueue;
}

// ============================================================
// MARK: - 初始化
// ============================================================
- (instancetype)init {
    self = [super init];
    if (!self) return nil;
    _decodeQueue = dispatch_queue_create("com.vt.decoder.decode",
                                          DISPATCH_QUEUE_SERIAL);
    _callbackQueue = dispatch_queue_create("com.vt.decoder.callback",
                                            DISPATCH_QUEUE_SERIAL);
    return self;
}

- (void)dealloc {
    [self invalidate];
}

// ============================================================
// MARK: - ★ 从 SPS/PPS 创建 CMVideoFormatDescription ★
// ============================================================
+ (CMVideoFormatDescriptionRef)createFormatDescriptionFromSPS:(NSData *)sps
                                                          PPS:(NSData *)pps {
    if (!sps || !pps) return NULL;

    const uint8_t *paramSetPointers[2] = {
        sps.bytes,
        pps.bytes
    };
    const size_t paramSetSizes[2] = {
        sps.length,
        pps.length
    };

    CMVideoFormatDescriptionRef formatDesc = NULL;
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault,
        2,                          // 参数集数量（SPS + PPS）
        paramSetPointers,
        paramSetSizes,
        4,                          // NAL 长度字段字节数（4 = AVCC 标准）
        &formatDesc
    );

    if (status != noErr) {
        NSLog(@"❌ CMVideoFormatDescription 创建失败: %d", (int)status);
        return NULL;
    }

    // 从 format description 里读分辨率
    CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);
    NSLog(@"✅ FormatDescription 创建成功: %dx%d", dims.width, dims.height);
    return formatDesc;
}

// ============================================================
// MARK: - ★ 创建解码会话 ★
// ============================================================
- (BOOL)createDecompressionSession:(CMVideoFormatDescriptionRef)formatDesc {
    if (!formatDesc) return NO;

    _formatDesc = formatDesc;
    CFRetain(_formatDesc);

    // ★ 目标 pixel buffer 属性 ★
    // 必须带 IOSurface key + Metal compatibility 才能零拷贝
    NSDictionary *destAttrs = @{
        (id)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},  // ★ 零拷贝必须
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,   // ★ Metal 零拷贝必须
        (id)kCVPixelBufferWidthKey:  @(CMVideoFormatDescriptionGetDimensions(formatDesc).width),
        (id)kCVPixelBufferHeightKey: @(CMVideoFormatDescriptionGetDimensions(formatDesc).height),
    };

    // 解码输出回调记录
    VTDecompressionOutputCallbackRecord callbackRecord = {
        .decompressionOutputCallback = VTH264DecoderOutputCallback,
        .decompressionOutputRefCon   = (__bridge void *)self,
    };

    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        formatDesc,
        NULL,                               // video decoder specification
        (__bridge CFDictionaryRef)destAttrs,
        &callbackRecord,
        &_session
    );

    if (status != noErr) {
        NSLog(@"❌ VTDecompressionSessionCreate 失败: %d", (int)status);
        return NO;
    }

    NSLog(@"✅ VTDecompressionSession 创建成功");
    return YES;
}

// ============================================================
// MARK: - 解码一帧
// ============================================================
- (BOOL)decodeSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    if (!_session || !sampleBuffer) return NO;

    // ★ 异步解码：结果在回调里给
    VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;

    OSStatus status = VTDecompressionSessionDecodeFrame(
        _session,
        sampleBuffer,
        flags,
        NULL,       // sourceFrameRefCon
        NULL        // infoFlagsOut
    );

    if (status != noErr) {
        // 常见错误码:
        // -12909 (kVTVideoDecoderBadDataErr): 数据损坏/格式不对
        // -12911 (kVTVideoDecoderMalfunctionErr): 解码器内部错误，需重建
        // -8969  (badDataErr): SPS/PPS 不匹配
        NSLog(@"❌ DecodeFrame 失败: %d (frame may be corrupted)", (int)status);

        if (status == kVTVideoDecoderMalfunctionErr) {
            // 解码器内部错误，需要重建
            NSLog(@"⚠️ 解码器 malfunction，建议重建 session");
        }
        return NO;
    }

    return YES;
}

// ============================================================
// MARK: - ★ 解码输出回调（C 函数）★
// ============================================================
static void VTH264DecoderOutputCallback(
    void *decompressionOutputRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVImageBufferRef imageBuffer,    // 就是 CVPixelBufferRef
    CMTime pts,
    CMTime duration)
{
    if (status != noErr) {
        NSLog(@"❌ 解码回调错误: %d", (int)status);
        return;
    }

    VTH264Decoder *decoder = (__bridge VTH264Decoder *)decompressionOutputRefCon;

    if (imageBuffer && decoder.outputCallback) {
        CVPixelBufferRetain(imageBuffer);  // 保证异步处理时不被释放

        dispatch_async(decoder->_callbackQueue, ^{
            decoder.outputCallback(imageBuffer, pts, nil);
            CVPixelBufferRelease(imageBuffer);
        });
    }
}

// ============================================================
// MARK: - ★ Annex-B → AVCC 转换（核心算法）★
// ============================================================

/// 从 Annex-B 流创建 CMSampleBuffer（解码用）
+ (CMSampleBufferRef)createSampleBufferFromAnnexBNALU:(NSData *)naluData
                                           formatDesc:(CMVideoFormatDescriptionRef)formatDesc
                                                  pts:(CMTime)pts {
    // ① Annex-B → AVCC：把起始码替换为 4 字节长度前缀
    NSData *avccData = [self convertAnnexBToAVCC:naluData];
    if (!avccData) return NULL;

    // ② 把 AVCC 字节装进 CMBlockBuffer
    CMBlockBufferRef blockBuffer = NULL;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        (void *)avccData.bytes,
        avccData.length,
        kCFAllocatorNull,       // 不接管内存管理
        NULL,
        0,
        avccData.length,
        0,
        &blockBuffer
    );
    if (status != noErr) return NULL;

    // ③ 组装 CMSampleBuffer
    CMSampleTimingInfo timing = {
        .duration              = kCMTimeInvalid,
        .presentationTimeStamp = pts,
        .decodeTimeStamp       = kCMTimeInvalid,
    };

    CMSampleBufferRef sampleBuffer = NULL;
    status = CMSampleBufferCreateReady(
        kCFAllocatorDefault,
        blockBuffer,
        formatDesc,
        1,              // 1 个 sample
        1,              // 1 个 timing info
        &timing,
        0, NULL,
        &sampleBuffer
    );

    CFRelease(blockBuffer);
    return (status == noErr) ? sampleBuffer : NULL;
}

/// AVCC 格式：每个 NALU 前是 4 字节大端长度
+ (NSData *)convertAnnexBToAVCC:(NSData *)annexBData {
    const uint8_t *bytes = annexBData.bytes;
    NSUInteger length = annexBData.length;
    if (length < 4) return nil;

    NSMutableData *avccData = [NSMutableData dataWithCapacity:length];

    NSUInteger offset = 0;
    while (offset < length) {
        // 跳过起始码
        NSUInteger startCodeLen = 0;
        if (offset + 4 <= length &&
            memcmp(bytes + offset, kAnnexBStartCode4, 4) == 0) {
            startCodeLen = 4;
        } else if (offset + 3 <= length &&
                   memcmp(bytes + offset, kAnnexBStartCode3, 3) == 0) {
            startCodeLen = 3;
        }

        offset += startCodeLen;

        // 找下一个起始码（或数据末尾），确定当前 NALU 的边界
        NSUInteger naluStart = offset;
        NSUInteger naluEnd = length;
        while (offset + 3 <= length) {
            if (memcmp(bytes + offset, kAnnexBStartCode3, 3) == 0) {
                naluEnd = offset;
                break;
            }
            offset++;
        }
        if (naluEnd == length) offset = length;  // 最后一个 NALU

        NSUInteger naluLen = naluEnd - naluStart;
        if (naluLen == 0) continue;

        // ★ 写入 4 字节大端长度前缀 + NALU 数据
        uint8_t lenPrefix[4] = {
            (naluLen >> 24) & 0xFF,
            (naluLen >> 16) & 0xFF,
            (naluLen >> 8)  & 0xFF,
            naluLen & 0xFF
        };
        [avccData appendBytes:lenPrefix length:4];
        [avccData appendBytes:bytes + naluStart length:naluLen];
    }

    return avccData;
}

// ============================================================
// MARK: - Annex-B 解析工具
// ============================================================
+ (NSData *)extractSPSFromAnnexB:(NSData *)annexBData {
    return [self extractNALUOfType:7 fromAnnexB:annexBData];
}

+ (NSData *)extractPPSFromAnnexB:(NSData *)annexBData {
    return [self extractNALUOfType:8 fromAnnexB:annexBData];
}

+ (NSData *)extractNALUOfType:(uint8_t)naluType fromAnnexB:(NSData *)annexBData {
    const uint8_t *bytes = annexBData.bytes;
    NSUInteger length = annexBData.length;
    NSUInteger offset = 0;

    while (offset + 5 <= length) {  // 至少 起始码(3) + NALU_header(1) + type_mask(1)
        // 跳起始码
        if (memcmp(bytes + offset, kAnnexBStartCode4, 4) == 0) {
            offset += 4;
        } else if (memcmp(bytes + offset, kAnnexBStartCode3, 3) == 0) {
            offset += 3;
        } else {
            offset++;
            continue;
        }

        NSUInteger naluStart = offset;
        // NALU header 第一个字节的低 5 位是 NAL unit type
        // (nal_unit_type = first_byte & 0x1F)
        uint8_t headerByte = bytes[naluStart];
        uint8_t type = headerByte & 0x1F;

        // 找下一个起始码
        NSUInteger naluEnd = length;
        NSUInteger searchOffset = naluStart + 1;
        while (searchOffset + 3 <= length) {
            if (memcmp(bytes + searchOffset, kAnnexBStartCode3, 3) == 0) {
                naluEnd = searchOffset;
                break;
            }
            searchOffset++;
        }

        if (type == naluType) {
            // 返回纯 NALU 数据（不含起始码）
            return [NSData dataWithBytes:bytes + naluStart
                                  length:naluEnd - naluStart];
        }

        offset = naluEnd;
    }
    return nil;
}

// ============================================================
// MARK: - 销毁
// ============================================================
- (void)invalidate {
    if (_session) {
        // ★ 重要：不能在解码回调的线程上调 Invalidate，会死锁
        // Chromium 的经验：放在独立线程
        VTDecompressionSessionWaitForAsynchronousFrames(_session);
        VTDecompressionSessionInvalidate(_session);
        CFRelease(_session);
        _session = NULL;
    }
    if (_formatDesc) {
        CFRelease(_formatDesc);
        _formatDesc = NULL;
    }
}

@end
```

### 3.3 使用示例

```objc
// ---- 1. 从 Annex-B 流初始化解码器 ----

// 接收的第一个 IDR 数据（包含 SPS + PPS + IDR）
NSData *firstIDRData = ...; // 从网络/UDP 收到的 Annex-B 数据

// 提取 SPS 和 PPS
NSData *spsData = [VTH264Decoder extractSPSFromAnnexB:firstIDRData];
NSData *ppsData = [VTH264Decoder extractPPSFromAnnexB:firstIDRData];

// 创建格式描述
CMVideoFormatDescriptionRef formatDesc =
    [VTH264Decoder createFormatDescriptionFromSPS:spsData PPS:ppsData];

// 创建解码器
VTH264Decoder *decoder = [[VTH264Decoder alloc] init];
[decoder createDecompressionSession:formatDesc];

decoder.outputCallback = ^(CVPixelBufferRef pixelBuffer, CMTime pts, NSError *error) {
    // 解码成功，渲染 pixelBuffer
    [metalRenderer renderPixelBuffer:pixelBuffer];
};

// ---- 2. 循环喂帧 ----
// 每收到一个 Annex-B NALU：
CMTime pts = CMTimeMake(frameCount, 30);
CMSampleBufferRef sampleBuffer =
    [VTH264Decoder createSampleBufferFromAnnexBNALU:naluData
                                         formatDesc:formatDesc
                                                pts:pts];
if (sampleBuffer) {
    [decoder decodeSampleBuffer:sampleBuffer];
    CFRelease(sampleBuffer);
}

// ---- 3. 结束时销毁 ----
[decoder invalidate];
CFRelease(formatDesc);
```

---

## 四、关键设计决策解读

### 4.1 为什么解码会话不能复用？

Apple 文档里有一个 `VTDecompressionSessionCanAcceptFormatDescription()` API，理论上可以用来判断能不能复用。但 **Chromium 团队在 2015 年的实践中发现了这个 API 的 bug**——某些情况下它返回 YES（能复用），但实际上解码会花屏或崩溃。

**最佳实践**：分辨率变 / SPS 变 / Profile 变 → 销毁旧 session，重建新 session。切换只发生在 IDR 帧处（切在非 IDR 处对端解码器也建不起来）。`VTDecompressionSessionWaitForAsynchronousFrames` 等所有异步帧完成后再 Invalidate。

### 4.2 为什么 Invalidate 不能在其他回调线程上调？

`VTDecompressionSessionInvalidate` 内部会等待所有 pending 的异步解码完成。如果你在解码输出回调的线程上调它，就会造成**自己等自己**的死锁。正确做法是 dispatch 到独立线程或主线程执行销毁。

### 4.3 为什么 CVPixelBufferIOSurfacePropertiesKey 是必须的？

不带这个 key 创建的 destination pixel buffer attributes，会让解码器把像素输出到普通内存（非 IOSurface）。这意味着你后续：
- 无法用 CVMetalTextureCache 做零拷贝渲染
- 每次渲染都要做一次 GPU upload（3-5ms/帧 for 1080p）

加上 `@{kCVPixelBufferIOSurfacePropertiesKey: @{}}` 后，解码输出的 CVPixelBuffer 后端就是 IOSurface，可以直接走零拷贝渲染链路。

---

## 五、进阶话题

### 5.1 解码失败恢复策略

```objc
- (void)handleDecodeError:(OSStatus)status {
    if (status == kVTVideoDecoderMalfunctionErr ||
        status == kVTVideoDecoderBadDataErr) {
        // ① 等所有异步帧完成
        VTDecompressionSessionWaitForAsynchronousFrames(_session);
        VTDecompressionSessionInvalidate(_session);
        CFRelease(_session);
        _session = NULL;

        // ② 重建 session（格式描述不变的话）
        [self createDecompressionSession:_formatDesc];

        // ③ 从下一个 IDR 开始喂（跳过 P 帧，等新的 IDR）
        _waitingForKeyFrame = YES;
    }
}
```

### 5.2 HEVC 解码差异

```objc
// HEVC 多一个 VPS，传 3 个参数集
const uint8_t *paramSets[3] = { vpsData, spsData, ppsData };
const size_t paramSizes[3] = { vpsSize, spsSize, ppsSize };
CMVideoFormatDescriptionCreateFromHEVCParameterSets(
    kCFAllocatorDefault, 3, paramSets, paramSizes,
    4,  // NAL length size = 4
    NULL,  // extensions
    &formatDesc
);
```

### 5.3 AVSampleBufferDisplayLayer（最省事的渲染）

如果不需要 Metal 后处理（美颜/滤镜），直接 `AVSampleBufferDisplayLayer` 渲染解码输出：

```objc
// 创建
AVSampleBufferDisplayLayer *displayLayer = [[AVSampleBufferDisplayLayer alloc] init];
displayLayer.videoGravity = AVLayerVideoGravityResizeAspect;

// 直接把解码后的 CMSampleBuffer enqueue
// (如果是自己 VT 解码，需要把 CVPixelBuffer 包回 CMSampleBuffer)
[displayLayer enqueueSampleBuffer:sampleBuffer];

// ★ 必须手动 flush CATransaction，否则画面不更新
[CATransaction flush];
```

注意 `AVSampleBufferDisplayLayer` 有自己的内部缓冲队列，适合播放场景。实时低延迟场景还是走 Metal 直接渲染。

---

## 六、常见坑

### 坑 1：喂入 Annex-B 花屏/解不出

**根因**：VideoToolbox 吃 AVCC（4 字节长度前缀），不是 Annex-B（起始码）。直接喂 Annex-B 会导致解码器把起始码当数据解析。

**对策**：必须在喂入前做 Annex-B → AVCC 转换。

### 坑 2：解码器复用花屏

**根因**：`VTDecompressionSessionCanAcceptFormatDescription` API 有已知 bug（Chromium 验证）。

**对策**：格式变 → 直接重建 session。

### 坑 3：Invalidate 死锁

**根因**：在解码回调线程里调了 Invalidate。

**对策**：`VTDecompressionSessionWaitForAsynchronousFrames` → dispatch 到其他线程 → `Invalidate`。

### 坑 4：SPS/PPS 带起始码传给 CreateFormatDescription

**根因**：`CMVideoFormatDescriptionCreateFromH264ParameterSets` 期望的参数集是纯 NALU 数据（不含起始码）。如果带了 00 00 00 01，创建会失败或者格式描述错误。

**对策**：提取 SPS/PPS 时去掉起始码。

---

## 一句话总结
> iOS 解码 = SPS/PPS → CMVideoFormatDescription（硬门槛） → VTDecompressionSession（不复用） → Annex-B→AVCC（格式转换） → DecodeFrame（异步） → CVPixelBuffer（IOSurface） → Metal/DisplayLayer。坑集中在格式转换、会话复用、和销毁死锁三点。

## 关联文档
- [[../ffmpeg/15-iOS硬件编解码]] — VT 编解码的全文深讲
- [[05-VideoToolbox硬编码实战]] — 编码端
- [[04-Metal渲染与零拷贝详解]] — 解码输出后的渲染
- [[07-AVCC与Annex-B转换实战]] — Annex-B ↔ AVCC 转换的完整实现
- [[08-端到端采集编码推流管线]] — 拉流解码的完整管线
