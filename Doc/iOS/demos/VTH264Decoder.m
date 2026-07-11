//  VTH264Decoder.m — 完整实现
//  详见 Doc/iOS/06-VideoToolbox硬解码实战.md 的完整讲解

#import "VTH264Decoder.h"

static const uint8_t kSC3[] = {0x00,0x00,0x01};
static const uint8_t kSC4[] = {0x00,0x00,0x00,0x01};

@implementation VTH264Decoder {
    VTDecompressionSessionRef _session;
    CMVideoFormatDescriptionRef _formatDesc;
    dispatch_queue_t _cbQueue;
}

- (instancetype)init {
    self = [super init];
    if (self) _cbQueue = dispatch_queue_create("com.vt.decoder.cb", DISPATCH_QUEUE_SERIAL);
    return self;
}
- (void)dealloc { [self invalidate]; }

// ---- 格式描述 ----
+ (CMVideoFormatDescriptionRef)createFormatDesc:(NSData *)sps pps:(NSData *)pps {
    if (!sps || !pps) return NULL;
    const uint8_t *p[2] = {sps.bytes, pps.bytes};
    const size_t   s[2] = {sps.length, pps.length};
    CMVideoFormatDescriptionRef fd = NULL;
    OSStatus st = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, p, s, 4, &fd);
    if (st != noErr) { NSLog(@"❌ formatDesc: %d", (int)st); return NULL; }
    return fd;
}

// ---- 创建解码会话 ----
- (BOOL)createSession:(CMVideoFormatDescriptionRef)fd {
    if (!fd) return NO;
    _formatDesc = fd; CFRetain(_formatDesc);
    NSDictionary *da = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    VTDecompressionOutputCallbackRecord cb = { .decompressionOutputCallback = OutCB, .decompressionOutputRefCon = (__bridge void *)self };
    OSStatus st = VTDecompressionSessionCreate(kCFAllocatorDefault, fd, NULL, (__bridge CFDictionaryRef)da, &cb, &_session);
    if (st != noErr) { NSLog(@"❌ VTDCreate: %d", (int)st); return NO; }
    return YES;
}

// ---- 解码 ----
- (BOOL)decode:(CMSampleBufferRef)sb {
    if (!_session || !sb) return NO;
    OSStatus st = VTDecompressionSessionDecodeFrame(_session, sb, kVTDecodeFrame_EnableAsynchronousDecompression, NULL, NULL);
    if (st != noErr) NSLog(@"❌ DecodeFrame: %d", (int)st);
    return st == noErr;
}

// ---- C 回调 ----
static void OutCB(void *ctx, void *src, OSStatus st, VTDecodeInfoFlags fl, CVImageBufferRef ib, CMTime pts, CMTime dur) {
    if (st != noErr) return;
    VTH264Decoder *dec = (__bridge VTH264Decoder *)ctx;
    if (ib && dec.outputCallback) {
        CVPixelBufferRetain(ib);
        dispatch_async(dec->_cbQueue, ^{ dec.outputCallback(ib, pts, nil); CVPixelBufferRelease(ib); });
    }
}

// ---- Annex-B → AVCC CMSampleBuffer ----
+ (CMSampleBufferRef)avccSampleBuffer:(NSData *)ab formatDesc:(CMVideoFormatDescriptionRef)fd pts:(CMTime)pts {
    NSData *avcc = [self ab2avcc:ab];
    if (!avcc) return NULL;
    CMBlockBufferRef bb = NULL;
    CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, (void *)avcc.bytes, avcc.length, kCFAllocatorNull, NULL, 0, avcc.length, 0, &bb);
    if (!bb) return NULL;
    CMSampleTimingInfo ti = { .duration = kCMTimeInvalid, .presentationTimeStamp = pts, .decodeTimeStamp = kCMTimeInvalid };
    CMSampleBufferRef sb = NULL;
    CMSampleBufferCreateReady(kCFAllocatorDefault, bb, fd, 1, &ti, 0, NULL, &sb);
    CFRelease(bb);
    return sb;
}

+ (NSData *)ab2avcc:(NSData *)ab {
    const uint8_t *b = ab.bytes; NSUInteger len = ab.length;
    if (len < 4) return nil;
    NSMutableData *o = [NSMutableData dataWithCapacity:len];
    NSUInteger i = 0;
    while (i < len) {
        NSUInteger sl = 0;
        if (i+4<=len && memcmp(b+i,kSC4,4)==0) sl=4; else if (i+3<=len && memcmp(b+i,kSC3,3)==0) sl=3;
        i+=sl; NSUInteger ns=i, ne=len;
        while (i+3<=len) { if (memcmp(b+i,kSC3,3)==0) { ne=i; break; } i++; }
        if (ne==len) i=len;
        NSUInteger nl=ne-ns; if (nl==0) continue;
        uint8_t lp[4]={(nl>>24)&0xFF,(nl>>16)&0xFF,(nl>>8)&0xFF,nl&0xFF};
        [o appendBytes:lp length:4]; [o appendBytes:b+ns length:nl];
    }
    return o;
}

// ---- SPS/PPS 提取 ----
+ (NSData *)spsFromAnnexB:(NSData *)d { return [self nalu:d type:7]; }
+ (NSData *)ppsFromAnnexB:(NSData *)d { return [self nalu:d type:8]; }

+ (NSData *)nalu:(NSData *)d type:(uint8_t)t {
    const uint8_t *b = d.bytes; NSUInteger len = d.length, i = 0;
    while (i+5 <= len) {
        if (memcmp(b+i,kSC4,4)==0) i+=4; else if (memcmp(b+i,kSC3,3)==0) i+=3; else { i++; continue; }
        NSUInteger ns=i, ne=len;
        while (i+3<=len) { if (memcmp(b+i,kSC3,3)==0) { ne=i; break; } i++; }
        if (ne==len) i=len;
        if ((b[ns]&0x1F)==t) return [NSData dataWithBytes:b+ns length:ne-ns];
        i=ne;
    }
    return nil;
}

- (void)invalidate {
    if (_session) { VTDecompressionSessionWaitForAsynchronousFrames(_session); VTDecompressionSessionInvalidate(_session); CFRelease(_session); _session=NULL; }
    if (_formatDesc) { CFRelease(_formatDesc); _formatDesc=NULL; }
}

@end
