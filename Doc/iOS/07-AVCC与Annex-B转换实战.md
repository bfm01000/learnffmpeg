# AVCC 与 Annex-B 转换实战：iOS 推流必过的格式关

> **适用方向**：iOS 实时推流/RTC/WebRTC 开发
> **前置知识**：H.264 NALU 基础（NALU type / 起始码）、CMSampleBuffer / CMBlockBuffer / CMVideoFormatDescription 的基本操作
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 25 分钟
> **关联文档**：[[../ffmpeg/15-iOS硬件编解码]] §六 · [[05-VideoToolbox硬编码实战]] · [[06-VideoToolbox硬解码实战]] · [[../ffmpeg/05-H264-MP4-NALU]] §四/§六
> **定位**：🟢 中级必会 —— 这是 iOS 推流开发中"写对了就正常工作、写错了就没画面"的关键一步

---

## 一、为什么需要格式转换？

### 1.1 问题的根源

这是 iOS 音视频开发中**最经典、面试最常问、线上最常出事的坑**。根源只有一句话：

> **VideoToolbox 吐 AVCC 格式，而 RTP/RTMP/裸流要 Annex-B 格式。两个格式不兼容，必须手动转换。**

反过来的场景（拉流解码）也一样：
> **网络收到的 Annex-B 流，喂给 VideoToolbox 解码器必须转成 AVCC 格式。**

### 1.2 两种格式的直观对比

```
═══════════════════════════════════════════════════════════════
AVCC 格式 (VideoToolbox 编码输出 / MP4 存储):
═══════════════════════════════════════════════════════════════
  CMSampleBuffer
   ├─ CMBlockBuffer: [4B长度][NALU数据][4B长度][NALU数据]...
   └─ CMVideoFormatDescription: {SPS字节, PPS字节}
       ↑ 参数集在这里！不在数据流里！

  例: 00 00 01 A5 [65 88 84 21 9A BC ...]
      └─大端长度=421─┘ └──421字节的IDR数据──────┘

═══════════════════════════════════════════════════════════════
Annex-B 格式 (RTP / RTMP / .h264 裸流):
═══════════════════════════════════════════════════════════════
  [00 00 00 01][SPS NALU][00 00 00 01][PPS NALU][00 00 00 01][IDR NALU]...
  └─起始码────┘          └─起始码────┘          └─起始码────┘
  参数集作为普通 NALU 内联在流里

  例: 00 00 00 01 67 42 00 1E ...  (SPS)
      00 00 00 01 68 CE 01 F2 ...  (PPS)
      00 00 00 01 65 88 84 21 ...  (IDR)
```

### 1.3 两种格式的核心差异

| 维度 | AVCC | Annex-B |
|------|------|---------|
| NALU 边界标记 | 4 字节大端长度前缀 | `00 00 00 01` 或 `00 00 01` 起始码 |
| SPS/PPS 位置 | 单独在 CMVideoFormatDescription / avcC box | 内联在流里，作为普通 NALU |
| 使用场景 | MP4/QuickTime 容器、VideoToolbox 输入输出 | RTP/RTMP/RTSP、裸 .h264 文件、Android MediaCodec |
| FFmpeg 表示 | `avcC` codecpar extradata | 直接裸流 |
| FFmpeg BSF | 目标 → `h264_mp4toannexb` | 目标 → `h264_annexbtomp4` (提取 extradata) |

### 1.4 为什么 iOS 偏偏用 AVCC？

这是 Apple 的设计选择——VideoToolbox 的数据模型和 MP4 容器保持一致。MP4 的 `avcC` box 就是存 AVCC 格式的参数集，Apple 把同样的思想用到了编码器输出上。Android 的 MediaCodec 走的是另一条路，直接用 Annex-B。

**工程含义**：跨平台音视频 SDK 的比特流处理在两个平台上是**正好相反**的操作。

---

## 二、面试速记

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | VT 编码输出什么格式 | AVCC——4 字节长度前缀 + SPS/PPS 分离在 format description | 🔥🔥🔥 | 🟢 |
| 2 | 推流要转成什么格式 | Annex-B——起始码 `00 00 00 01` + SPS/PPS 内联 | 🔥🔥🔥 | 🟢 |
| 3 | SPS/PPS 在哪取 | CMVideoFormatDescriptionGetH264ParameterSetAtIndex | 🔥🔥🔥 | 🟢 |
| 4 | HEVC 比 H.264 多什么 | 多一个 VPS（取 3 个参数集而不是 2 个） | 🔥🔥 | 🟡 |
| 5 | 怎么判断关键帧 | 查 CMSampleBuffer attachments 的 NotSync key | 🔥🔥 | 🟢 |
| 6 | AvCC 的长度前缀为什么容易误判 | 长度值较小（如 < 256）时前三字节是 `00 00 00`，和起始码前三字节相同 | 🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：VideoToolbox 编码完直接推流会怎样？为什么要转格式？

**面试官想听什么：** 你真正理解两种格式的差异，而不是背答案。

**🗣️ 标准回答（可背诵）：**

> "如果不转格式直接推，对端会没有画面。原因有两个。第一是 NALU 边界标记不兼容——AVCC 用 4 字节大端长度前缀标记每个 NALU 的边界，而 RTP/RTMP 用 00 00 00 01 起始码。对端解码器按起始码切 NALU，读到长度前缀的字节会把它当成未知 NALU 头，解析失败。第二是 SPS/PPS 缺失——VideoToolbox 编码输出的 SPS/PPS 单独存放在 CMVideoFormatDescription 里，不在 CMBlockBuffer 的数据流里。直接推出去的数据流只有画面 NALU，没有 SPS/PPS。对端解码器没拿到参数集，根本建不起来解码上下文——典型表现是'只有声音没画面'，或者'首帧之后全是花屏'。所以推流前必须做两件事：用 CMVideoFormatDescriptionGetH264ParameterSetAtIndex 取出 SPS/PPS，在每个 IDR 关键帧前面手动注入；然后把所有 NALU 的 4 字节长度前缀替换成 00 00 00 01 起始码。"

**👨‍💻 追问预警：**
> Q: "FFmpeg 里怎么处理这个转换？为什么要自己写？"
> A: FFmpeg 有 `h264_mp4toannexb` bitstream filter 自动做这个转换。但在直调 VideoToolbox 的场景下，比特流从 VT 出来就直接进你的业务逻辑了，不经过 FFmpeg——所以必须自己手写这段转换逻辑。这也是为什么 WebRTC iOS 源码里有专门的 SPS/PPS 提取和格式转换代码。

---

## 三、核心 Demo：完整的格式转换工具类

### 3.1 H264FormatConverter.h

```objc
//  H264FormatConverter.h
//  AVCC ↔ Annex-B 双向格式转换工具
//
//  用法:
//   编码方向 (AVCC→Annex-B): [converter convertAVCCSampleBufferToAnnexB:sampleBuffer];
//   解码方向 (Annex-B→AVCC): [converter convertAnnexBDataToAVCCSampleBuffer:data formatDesc:fmt pts:time];

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// 转换后的 Annex-B 数据（包含 SPS/PPS + 所有 NALU）
@interface H264AnnexBPacket : NSObject
@property (nonatomic, strong) NSData *data;          // Annex-B 格式的完整字节
@property (nonatomic, assign) BOOL isKeyFrame;       // 是否关键帧
@property (nonatomic, assign) CMTime pts;             // 时间戳
@property (nonatomic, assign) CMTime dts;             // 解码时间戳
@end

@interface H264FormatConverter : NSObject

#pragma mark - 编码方向: AVCC → Annex-B（推流用）

/// 将编码输出的 CMSampleBuffer 转为 Annex-B 格式数据块
/// 自动处理 SPS/PPS 注入（仅关键帧时注入）
+ (H264AnnexBPacket *)convertToAnnexB:(CMSampleBufferRef)sampleBuffer;

/// 只做 AVCC → Annex-B 的 NALU 数据转换（不含 SPS/PPS）
/// 用于已经单独处理了参数集的场景
+ (NSData *)convertNALUsToAnnexB:(NSData *)avccData;

/// 从 CMSampleBuffer 提取 SPS 裸数据（不含起始码）
+ (NSData *)extractSPS:(CMSampleBufferRef)sampleBuffer;

/// 从 CMSampleBuffer 提取 PPS 裸数据（不含起始码）
+ (NSData *)extractPPS:(CMSampleBufferRef)sampleBuffer;

/// 获取带起始码的 SPS Annex-B 数据: [00 00 00 01] + SPS
+ (NSData *)spsAnnexBData:(CMSampleBufferRef)sampleBuffer;

/// 获取带起始码的 PPS Annex-B 数据: [00 00 00 01] + PPS
+ (NSData *)ppsAnnexBData:(CMSampleBufferRef)sampleBuffer;

#pragma mark - 解码方向: Annex-B → AVCC（喂解码器用）

/// 将 Annex-B 数据转为 AVCC 格式的 CMSampleBuffer
+ (CMSampleBufferRef _Nullable)createAVCCSampleBuffer:(NSData *)annexB
                                           formatDesc:(CMVideoFormatDescriptionRef)formatDesc
                                                  pts:(CMTime)pts;

/// 从 Annex-B 流提取 NALU 类型（7=SPS, 8=PPS, 5=IDR, 1=非IDR）
+ (uint8_t)naluTypeFromAnnexBNALU:(NSData *)naluData;

#pragma mark - 工具方法

/// 判断是否为 H.264 关键帧
+ (BOOL)isKeyFrame:(CMSampleBufferRef)sampleBuffer;

/// 判断是否为 HEVC 关键帧
+ (BOOL)isHEVCKeyFrame:(CMSampleBufferRef)sampleBuffer;

@end

NS_ASSUME_NONNULL_END
```

### 3.2 H264FormatConverter.m

```objc
//  H264FormatConverter.m
//  完整的 AVCC ↔ Annex-B 双向格式转换实现

#import "H264FormatConverter.h"

// Annex-B 起始码
static const uint8_t kStartCode3[] = {0x00, 0x00, 0x01};
static const uint8_t kStartCode4[] = {0x00, 0x00, 0x00, 0x01};
static const size_t  kNALULengthSize = 4; // AVCC 长度字段字节数

@implementation H264AnnexBPacket
@end

// ============================================================
// MARK: - H264FormatConverter 实现
// ============================================================
@implementation H264FormatConverter

#pragma mark - 编码方向: AVCC → Annex-B

+ (H264AnnexBPacket *)convertToAnnexB:(CMSampleBufferRef)sampleBuffer {
    if (!sampleBuffer) return nil;

    H264AnnexBPacket *packet = [[H264AnnexBPacket alloc] init];
    packet.pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    packet.isKeyFrame = [self isKeyFrame:sampleBuffer];

    // 取得 CMBlockBuffer 中的 AVCC 格式 NALU 数据
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) return nil;

    size_t totalLen = 0;
    char *dataPtr = NULL;
    OSStatus ret = CMBlockBufferGetDataPointer(blockBuffer, 0, NULL,
                                                &totalLen, &dataPtr);
    if (ret != noErr || !dataPtr || totalLen == 0) return nil;

    NSMutableData *output = [NSMutableData dataWithCapacity:totalLen + 256];

    // ★ 步骤 1: 关键帧时，在前面注入 SPS + PPS
    if (packet.isKeyFrame) {
        NSData *spsAnnexB = [self spsAnnexBData:sampleBuffer]; // [起始码][SPS]
        NSData *ppsAnnexB = [self ppsAnnexBData:sampleBuffer]; // [起始码][PPS]
        if (spsAnnexB) [output appendData:spsAnnexB];
        if (ppsAnnexB) [output appendData:ppsAnnexB];
    }

    // ★ 步骤 2: 遍历 AVCC 的每个 NALU，把 4 字节长度前缀换成起始码
    NSData *avccData = [NSData dataWithBytesNoCopy:dataPtr
                                            length:totalLen
                                      freeWhenDone:NO];
    NSData *annexBNalus = [self convertNALUsToAnnexB:avccData];
    if (annexBNalus) {
        [output appendData:annexBNalus];
    }

    packet.data = output;
    return packet;
}

+ (NSData *)convertNALUsToAnnexB:(NSData *)avccData {
    if (!avccData || avccData.length < 4) return nil;

    const uint8_t *bytes = avccData.bytes;
    NSUInteger length = avccData.length;
    NSMutableData *output = [NSMutableData dataWithCapacity:length];

    NSUInteger offset = 0;
    while (offset + kNALULengthSize <= length) {
        // 读取 4 字节大端长度
        uint32_t nalLen =
            ((uint32_t)bytes[offset]     << 24) |
            ((uint32_t)bytes[offset + 1] << 16) |
            ((uint32_t)bytes[offset + 2] << 8)  |
            ((uint32_t)bytes[offset + 3]);

        offset += kNALULengthSize;

        if (nalLen == 0 || offset + nalLen > length) {
            // 长度不合法，跳过
            break;
        }

        // ★ 这里的陷阱：
        // 如果 nalLen 较小（如 < 256），长度前缀的前三字节是 00 00 00
        // 和起始码 00 00 00 01 的前三字节一模一样
        // 所以不能从数据里扫起始码！必须按长度字段精确跳转

        // 写入 4 字节起始码
        [output appendBytes:kStartCode4 length:4];

        // 写入 NALU 数据
        [output appendBytes:bytes + offset length:nalLen];

        offset += nalLen;
    }

    return output;
}

#pragma mark - SPS/PPS 提取

+ (NSData *)extractSPS:(CMSampleBufferRef)sampleBuffer {
    return [self parameterSetAtIndex:0 fromSampleBuffer:sampleBuffer];
}

+ (NSData *)extractPPS:(CMSampleBufferRef)sampleBuffer {
    return [self parameterSetAtIndex:1 fromSampleBuffer:sampleBuffer];
}

+ (NSData *)parameterSetAtIndex:(size_t)index
                fromSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    CMVideoFormatDescriptionRef fmt =
        CMSampleBufferGetFormatDescription(sampleBuffer);
    if (!fmt) return nil;

    const uint8_t *paramSet = NULL;
    size_t paramSetSize = 0;
    size_t paramSetCount = 0;
    int nalUnitHeaderLength = 0;

    OSStatus ret = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        fmt,
        index,
        &paramSet,
        &paramSetSize,
        &paramSetCount,
        &nalUnitHeaderLength
    );

    if (ret != noErr || !paramSet || paramSetSize == 0) return nil;
    return [NSData dataWithBytes:paramSet length:paramSetSize];
}

+ (NSData *)spsAnnexBData:(CMSampleBufferRef)sampleBuffer {
    NSData *sps = [self extractSPS:sampleBuffer];
    if (!sps) return nil;

    NSMutableData *data = [NSMutableData dataWithCapacity:sps.length + 4];
    [data appendBytes:kStartCode4 length:4];
    [data appendData:sps];
    return data;
}

+ (NSData *)ppsAnnexBData:(CMSampleBufferRef)sampleBuffer {
    NSData *pps = [self extractPPS:sampleBuffer];
    if (!pps) return nil;

    NSMutableData *data = [NSMutableData dataWithCapacity:pps.length + 4];
    [data appendBytes:kStartCode4 length:4];
    [data appendData:pps];
    return data;
}

#pragma mark - 解码方向: Annex-B → AVCC

+ (CMSampleBufferRef)createAVCCSampleBuffer:(NSData *)annexB
                                  formatDesc:(CMVideoFormatDescriptionRef)formatDesc
                                         pts:(CMTime)pts {
    return [self createAVCCSampleBuffer:annexB
                             formatDesc:formatDesc
                                    pts:pts
                                    dts:kCMTimeInvalid];
}

+ (CMSampleBufferRef)createAVCCSampleBuffer:(NSData *)annexB
                                  formatDesc:(CMVideoFormatDescriptionRef)formatDesc
                                         pts:(CMTime)pts
                                         dts:(CMTime)dts {
    if (!annexB || !formatDesc) return NULL;

    // ① Annex-B → AVCC：起始码 → 4 字节长度前缀
    NSData *avccData = [self convertAnnexBToAVCC:annexB];
    if (!avccData || avccData.length == 0) return NULL;

    // ② 装进 CMBlockBuffer
    CMBlockBufferRef blockBuffer = NULL;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        (void *)avccData.bytes,
        avccData.length,
        kCFAllocatorNull,
        NULL, 0, avccData.length, 0,
        &blockBuffer
    );
    if (status != noErr) return NULL;

    // ③ 组装 CMSampleBuffer
    CMSampleTimingInfo timing = {
        .duration              = kCMTimeInvalid,
        .presentationTimeStamp = pts,
        .decodeTimeStamp       = dts,
    };

    CMSampleBufferRef sampleBuffer = NULL;
    status = CMSampleBufferCreateReady(
        kCFAllocatorDefault,
        blockBuffer,
        formatDesc,
        1, &timing,
        0, NULL,
        &sampleBuffer
    );

    CFRelease(blockBuffer);
    return (status == noErr) ? sampleBuffer : NULL;
}

/// Annex-B → AVCC 的核心算法
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
            memcmp(bytes + offset, kStartCode4, 4) == 0) {
            startCodeLen = 4;
        } else if (offset + 3 <= length &&
                   memcmp(bytes + offset, kStartCode3, 3) == 0) {
            startCodeLen = 3;
        }
        offset += startCodeLen;

        // 找下一个起始码，确定当前 NALU 边界
        NSUInteger naluStart = offset;
        NSUInteger naluEnd   = length;
        while (offset + 3 <= length) {
            if (memcmp(bytes + offset, kStartCode3, 3) == 0) {
                naluEnd = offset;
                break;
            }
            offset++;
        }
        if (naluEnd == length) offset = length; // 最后一个 NALU

        NSUInteger naluLen = naluEnd - naluStart;
        if (naluLen == 0) continue;

        // 写入 4 字节大端长度前缀
        uint8_t lenPrefix[4] = {
            (uint8_t)((naluLen >> 24) & 0xFF),
            (uint8_t)((naluLen >> 16) & 0xFF),
            (uint8_t)((naluLen >> 8)  & 0xFF),
            (uint8_t)(naluLen         & 0xFF),
        };
        [avccData appendBytes:lenPrefix length:4];
        [avccData appendBytes:(bytes + naluStart) length:naluLen];
    }

    return avccData;
}

#pragma mark - NALU 类型解析

+ (uint8_t)naluTypeFromAnnexBNALU:(NSData *)naluData {
    if (!naluData || naluData.length < 1) return 0;

    const uint8_t *bytes = naluData.bytes;
    NSUInteger offset = 0;

    // 跳起始码
    if (naluData.length >= 4 && memcmp(bytes, kStartCode4, 4) == 0) {
        offset = 4;
    } else if (naluData.length >= 3 && memcmp(bytes, kStartCode3, 3) == 0) {
        offset = 3;
    }

    if (offset >= naluData.length) return 0;

    // NALU header 第一个字节的低 5 位
    uint8_t headerByte = bytes[offset];

    // 处理 H.264 NALU header（forbidden_zero_bit(1) + nal_ref_idc(2) + nal_unit_type(5)）
    return headerByte & 0x1F;
}

#pragma mark - 关键帧判断

+ (BOOL)isKeyFrame:(CMSampleBufferRef)sampleBuffer {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(
        sampleBuffer, true);
    if (!attachments || CFArrayGetCount(attachments) == 0) return NO;

    CFDictionaryRef dict = CFArrayGetValueAtIndex(attachments, 0);
    // kCMSampleAttachmentKey_NotSync = false 或不存在 → 同步帧（关键帧）
    CFBooleanRef notSync = CFDictionaryGetValue(dict,
        kCMSampleAttachmentKey_NotSync);
    return !notSync || !CFBooleanGetValue(notSync);
}

+ (BOOL)isHEVCKeyFrame:(CMSampleBufferRef)sampleBuffer {
    // HEVC 的判断逻辑相同，也查 NotSync
    return [self isKeyFrame:sampleBuffer];
}

@end
```

### 3.3 使用示例

```objc
// ====== 编码方向：推流前转换 ======
H264FormatConverter *converter = [[H264FormatConverter alloc] init];

// 在 VT 编码回调里：
- (void)onEncodedSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    H264AnnexBPacket *packet = [H264FormatConverter convertToAnnexB:sampleBuffer];

    if (packet.isKeyFrame) {
        // packet.data 已经包含了 [SPS][PPS][IDR]
        NSLog(@"📦 IDR 帧: %lu bytes (含 SPS/PPS)", (unsigned long)packet.data.length);
    } else {
        // packet.data 只包含 P 帧 NALU
        NSLog(@"📦 P 帧: %lu bytes", (unsigned long)packet.data.length);
    }

    // 发送: packet.data (Annex-B 格式)
    [rtmpPublisher sendVideoData:packet.data pts:packet.pts isKeyFrame:packet.isKeyFrame];
}

// ====== 解码方向：收到 Annex-B 后转换 ======
- (void)onReceivedAnnexBFrame:(NSData *)annexBData pts:(CMTime)pts {
    // 判断是否是 IDR（第一个 NALU 的 type == 5）
    uint8_t naluType = [H264FormatConverter naluTypeFromAnnexBNALU:annexBData];

    if (naluType == 7 || naluType == 8) {
        // SPS 或 PPS → 缓存，等 IDR 来一起建格式描述
        return;
    }

    if (naluType == 5) {
        // IDR → 重建格式描述
        // ...提取 SPS/PPS 重建设备 _formatDesc...
    }

    CMSampleBufferRef avccBuffer =
        [H264FormatConverter createAVCCSampleBuffer:annexBData
                                         formatDesc:_formatDesc
                                                pts:pts];
    if (avccBuffer) {
        [_decoder decodeSampleBuffer:avccBuffer];
        CFRelease(avccBuffer);
    }
}
```

---

## 四、HEVC 差异

### 4.1 HEVC 多一个 VPS

```objc
// H.264: 取 2 个参数集
CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, ...); // SPS
CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 1, ...); // PPS

// HEVC: 取 3 个参数集
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 0, ...); // VPS (新增!)
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 1, ...); // SPS
CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fmt, 2, ...); // PPS
```

### 4.2 HEVC 的 NALU Type

| Type | H.264 | HEVC |
|------|-------|------|
| VPS | — | 32 |
| SPS | 7 | 33 |
| PPS | 8 | 34 |
| IDR | 5 | 19 (IDR_W_RADL), 20 (IDR_N_LP) |
| Non-IDR | 1 | 1 (TRAIL_R), 0 (TRAIL_N) |

HEVC 的 NALU header 是 2 字节（不是 1 字节），type 在第一字节的高位 + 第二字节。`(header[0] >> 1) & 0x3F`。

---

## 五、常见坑

### 坑 1：AvCC 长度前缀被误判为起始码

长度值较小时（如 421 = `00 00 01 A5`），前 3 字节 `00 00 01` 和 3 字节起始码完全一样。如果用"扫起始码"的方式去解析 AVCC 数据，会切出错误的 NALU 边界。

**对策**：解析 AVCC 必须按长度字段精确跳转，不能扫起始码。

### 坑 2：忘记在非关键帧也做格式转换

只对关键帧做了 AVCC→Annex-B 转换，P 帧也必须是 Annex-B 格式。转换操作对所有帧都需要。

### 坑 3：SPS/PPS 注入时机不对

SPS/PPS 应该在**每一个 IDR 前面**注入，而不是只在第一个 IDR 前注入一次。原因：中间加入的观看者需要 SPS/PPS 才能开始解码。WebRTC 每个 IDR 前都带 SPS/PPS。

### 坑 4：起始码用 3 字节还是 4 字节

- Annex-B 标准允许 `00 00 01`（3字节）和 `00 00 00 01`（4字节）两种起始码
- **推荐统一用 4 字节** `00 00 00 01`——某些解码器对 3 字节起始码的兼容性差
- SPS/PPS/IDR 的第一个 NALU 前面**必须**用 4 字节起始码

---

## 🎯 一句话总结

> AVCC ↔ Annex-B 转换是 iOS 推流/拉流的必经关卡：编码方向把 4 字节长度前缀换起始码 + 从 CMVideoFormatDescription 取 SPS/PPS 注入 IDR 前；解码方向反向操作。HEVC 比 H.264 多一个 VPS 参数集。核心陷阱是 AVCC 长度前缀可能和起始码字节序列相同——必须按长度字段精确跳转，不能扫起始码。

## 🔗 关联文档

- [[../ffmpeg/15-iOS硬件编解码]] §六 — AVCC vs Annex-B 字节级深讲
- [[../ffmpeg/05-H264-MP4-NALU]] — H.264 NALU 基础
- [[05-VideoToolbox硬编码实战]] — 编码输出 → 转换 → 推流
- [[06-VideoToolbox硬解码实战]] — 拉流 → 转换 → 解码
