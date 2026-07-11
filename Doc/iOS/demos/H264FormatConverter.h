//  H264FormatConverter.h
//  AVCC ↔ Annex-B 双向格式转换 — 独立可用
//  依赖: CoreMedia.framework, VideoToolbox.framework

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/// Annex-B 格式数据包
@interface H264AnnexBPacket : NSObject
@property (nonatomic, strong) NSData *data;
@property (nonatomic, assign) BOOL isKeyFrame;
@property (nonatomic, assign) CMTime pts;
@end

@interface H264FormatConverter : NSObject

#pragma mark - AVCC → Annex-B (推流方向)

/// 编码 CMSampleBuffer → Annex-B 数据包 (自动注入 SPS/PPS 到 IDR 前)
+ (H264AnnexBPacket *)toAnnexB:(CMSampleBufferRef)sampleBuffer;

/// AVCC NALU 数据 → Annex-B (不处理 SPS/PPS)
+ (NSData *)nalusToAnnexB:(NSData *)avccData;

/// 提取 SPS/PPS (纯 NALU, 无起始码)
+ (nullable NSData *)sps:(CMSampleBufferRef)sampleBuffer;
+ (nullable NSData *)pps:(CMSampleBufferRef)sampleBuffer;

/// 提取 SPS/PPS (带起始码, 可直接写入 Annex-B 流)
+ (nullable NSData *)spsAnnexB:(CMSampleBufferRef)sampleBuffer;
+ (nullable NSData *)ppsAnnexB:(CMSampleBufferRef)sampleBuffer;

#pragma mark - Annex-B → AVCC (解码方向)

/// Annex-B 数据 → AVCC CMSampleBuffer
+ (nullable CMSampleBufferRef)avccSampleBuffer:(NSData *)annexB
                                     formatDesc:(CMVideoFormatDescriptionRef)fd
                                            pts:(CMTime)pts;

/// 判 NALU 类型 (5=IDR, 7=SPS, 8=PPS, 1=非IDR)
+ (uint8_t)naluType:(NSData *)annexBNalu;

#pragma mark - 工具

+ (BOOL)isKeyFrame:(CMSampleBufferRef)sampleBuffer;

@end

NS_ASSUME_NONNULL_END
