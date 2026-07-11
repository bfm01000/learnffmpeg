//  VTH264Decoder.h
//  iOS VideoToolbox H.264 硬件解码器 — 独立可用
//  依赖: VideoToolbox.framework, CoreVideo.framework, CoreMedia.framework

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

typedef void(^VTH264DecoderCallback)(CVPixelBufferRef pixelBuffer, CMTime pts, NSError *_Nullable error);

@interface VTH264Decoder : NSObject

@property (nonatomic, copy, nullable) VTH264DecoderCallback outputCallback;

/// 从 SPS/PPS 裸数据创建格式描述 (解码器初始化前置步骤)
+ (nullable CMVideoFormatDescriptionRef)createFormatDesc:(NSData *)sps pps:(NSData *)pps;

/// 用格式描述初始化解码会话
- (BOOL)createSession:(CMVideoFormatDescriptionRef)formatDesc;

/// 解码一帧 AVCC 格式 CMSampleBuffer
- (BOOL)decode:(CMSampleBufferRef)sampleBuffer;

/// 将 Annex-B 数据转为 AVCC CMSampleBuffer (方便喂入解码器)
+ (nullable CMSampleBufferRef)avccSampleBuffer:(NSData *)annexB
                                    formatDesc:(CMVideoFormatDescriptionRef)fd
                                           pts:(CMTime)pts;

/// 从 Annex-B 流提取 SPS/PPS
+ (nullable NSData *)spsFromAnnexB:(NSData *)data;
+ (nullable NSData *)ppsFromAnnexB:(NSData *)data;

/// 销毁
- (void)invalidate;

@end

NS_ASSUME_NONNULL_END
