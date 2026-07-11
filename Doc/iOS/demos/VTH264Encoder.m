//  VTH264Encoder.m — 完整实现
//  详见 Doc/iOS/05-VideoToolbox硬编码实战.md 的完整讲解

#import "VTH264Encoder.h"

@implementation VTH264EncoderConfig
- (instancetype)init {
    self = [super init];
    if (self) {
        _width = 1920; _height = 1080; _fps = 30;
        _bitrate = 2000000; _maxKeyFrameInterval = 60;
        _realTime = YES; _allowFrameReordering = NO;
        _profileLevel = kVTProfileLevel_H264_Baseline_AutoLevel;
    }
    return self;
}
@end

@implementation VTH264Encoder {
    VTH264EncoderConfig *_config;
    VTCompressionSessionRef _session;
    VTH264EncoderCallback _callback;
    dispatch_queue_t _callbackQueue;
    BOOL _forceKeyFrame;
}

- (instancetype)initWithConfig:(VTH264EncoderConfig *)config
                      callback:(VTH264EncoderCallback)callback {
    self = [super init];
    if (!self) return nil;
    _config = config;
    _callback = [callback copy];
    _callbackQueue = dispatch_queue_create("com.vt.encoder.cb", DISPATCH_QUEUE_SERIAL);
    if (![self createSession]) return nil;
    return self;
}

- (void)dealloc { [self invalidate]; }

- (BOOL)createSession {
    OSStatus s = VTCompressionSessionCreate(kCFAllocatorDefault,
        _config.width, _config.height, kCMVideoCodecType_H264,
        NULL, NULL, NULL, &OutputCB, (__bridge void *)self, &_session);
    if (s != noErr) { NSLog(@"❌ VTCompressionSessionCreate: %d", (int)s); return NO; }

    VTSessionSetProperty(_session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(_session, kVTCompressionPropertyKey_AllowFrameReordering,
                         _config.allowFrameReordering ? kCFBooleanTrue : kCFBooleanFalse);

    int br = _config.bitrate;
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &br);
    VTSessionSetProperty(_session, kVTCompressionPropertyKey_AverageBitRate, n);
    CFRelease(n);

    int fps = _config.fps;
    n = CFNumberCreate(NULL, kCFNumberIntType, &fps);
    VTSessionSetProperty(_session, kVTCompressionPropertyKey_ExpectedFrameRate, n);
    CFRelease(n);

    int gop = _config.maxKeyFrameInterval;
    n = CFNumberCreate(NULL, kCFNumberIntType, &gop);
    VTSessionSetProperty(_session, kVTCompressionPropertyKey_MaxKeyFrameInterval, n);
    CFRelease(n);

    VTSessionSetProperty(_session, kVTCompressionPropertyKey_ProfileLevel, _config.profileLevel);
    VTSessionSetProperty(_session, kVTCompressionPropertyKey_AllowOpenGOP, kCFBooleanFalse);
    VTCompressionSessionPrepareToEncodeFrames(_session);
    return YES;
}

- (BOOL)encode:(CVPixelBufferRef)pb pts:(CMTime)pts {
    if (!_session) return NO;
    CFDictionaryRef props = NULL;
    if (_forceKeyFrame) {
        props = (__bridge CFDictionaryRef)@{
            (__bridge id)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES
        };
        _forceKeyFrame = NO;
    }
    OSStatus s = VTCompressionSessionEncodeFrame(_session, pb, pts, kCMTimeInvalid, props, NULL, NULL);
    return s == noErr;
}

- (void)forceKeyFrame { _forceKeyFrame = YES; }

- (BOOL)setBitrate:(int)br {
    _config.bitrate = br;
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &br);
    OSStatus s = VTSessionSetProperty(_session, kVTCompressionPropertyKey_AverageBitRate, n);
    CFRelease(n);
    return s == noErr;
}

- (void)finish:(void(^)(void))completion {
    VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1 * NSEC_PER_SEC), _callbackQueue, ^{
        if (completion) completion();
    });
}

- (void)invalidate {
    if (_session) {
        VTCompressionSessionCompleteFrames(_session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(_session);
        CFRelease(_session); _session = NULL;
    }
}

// ---- C 回调 ----
static void OutputCB(void *ctx, void *src, OSStatus st, VTEncodeInfoFlags fl, CMSampleBufferRef sb) {
    if (st != noErr || !sb) return;
    VTH264Encoder *enc = (__bridge VTH264Encoder *)ctx;
    CFRetain(sb);
    dispatch_async(enc->_callbackQueue, ^{
        BOOL kf = [VTH264Encoder isKeyFrame:sb];
        if (enc->_callback) enc->_callback(sb, kf);
        CFRelease(sb);
    });
}

// ---- 工具方法 ----
+ (BOOL)isKeyFrame:(CMSampleBufferRef)sb {
    CFArrayRef a = CMSampleBufferGetSampleAttachmentsArray(sb, true);
    if (!a || CFArrayGetCount(a) == 0) return NO;
    CFBooleanRef ns = CFDictionaryGetValue(CFArrayGetValueAtIndex(a, 0), kCMSampleAttachmentKey_NotSync);
    return !ns || !CFBooleanGetValue(ns);
}

+ (NSData *)spsData:(CMSampleBufferRef)sb { return [self ps:sb idx:0]; }
+ (NSData *)ppsData:(CMSampleBufferRef)sb { return [self ps:sb idx:1]; }

+ (NSData *)ps:(CMSampleBufferRef)sb idx:(size_t)i {
    CMVideoFormatDescriptionRef f = CMSampleBufferGetFormatDescription(sb);
    if (!f) return nil;
    const uint8_t *d; size_t sz, cnt; int hdr;
    OSStatus s = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(f, i, &d, &sz, &cnt, &hdr);
    return (s == noErr && d && sz) ? [NSData dataWithBytes:d length:sz] : nil;
}

+ (NSData *)naluData:(CMSampleBufferRef)sb {
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    if (!bb) return nil;
    size_t len; char *p;
    OSStatus s = CMBlockBufferGetDataPointer(bb, 0, NULL, &len, &p);
    return (s == noErr && p && len) ? [NSData dataWithBytes:p length:len] : nil;
}

@end
