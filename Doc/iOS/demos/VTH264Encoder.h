//  VTH264Encoder.h
//  iOS VideoToolbox H.264 硬件编码器 — 独立可用
//  依赖: VideoToolbox.framework, CoreVideo.framework, CoreMedia.framework

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// 编码器配置
@interface VTH264EncoderConfig : NSObject
@property (nonatomic, assign) int width;
@property (nonatomic, assign) int height;
@property (nonatomic, assign) int fps;
@property (nonatomic, assign) int bitrate;               // bps, 默认 2M
@property (nonatomic, assign) int maxKeyFrameInterval;  // 帧数, 默认 60 (2s@30fps)
@property (nonatomic, assign) BOOL realTime;             // 默认 YES
@property (nonatomic, assign) BOOL allowFrameReordering; // 默认 NO
@property (nonatomic, assign) CFStringRef profileLevel;  // 默认 Baseline_AutoLevel
@end

/// 编码输出回调: sampleBuffer (AVCC格式) + isKeyFrame
typedef void(^VTH264EncoderCallback)(CMSampleBufferRef sampleBuffer, BOOL isKeyFrame);

@interface VTH264Encoder : NSObject

- (instancetype)initWithConfig:(VTH264EncoderConfig *)config
                      callback:(VTH264EncoderCallback)callback;

/// 编码一帧 (CVPixelBuffer NV12, pts 注意 timescale)
- (BOOL)encode:(CVPixelBufferRef)pixelBuffer pts:(CMTime)pts;

/// 强制下一帧为 IDR (对端 PLI 时调用)
- (void)forceKeyFrame;

/// 动态调整码率 (拥塞控制时调用)
- (BOOL)setBitrate:(int)newBitrate;

/// Flush 管线中所有帧, completion 在所有帧输出后回调
- (void)finish:(void(^)(void))completion;

/// 销毁编码器
- (void)invalidate;

// ---- 工具方法 (静态) ----
+ (BOOL)isKeyFrame:(CMSampleBufferRef)sampleBuffer;
+ (nullable NSData *)spsData:(CMSampleBufferRef)sampleBuffer;   // 纯 SPS, 无起始码
+ (nullable NSData *)ppsData:(CMSampleBufferRef)sampleBuffer;   // 纯 PPS, 无起始码
+ (nullable NSData *)naluData:(CMSampleBufferRef)sampleBuffer;  // AVCC 格式 NALU 数据

@end

NS_ASSUME_NONNULL_END
