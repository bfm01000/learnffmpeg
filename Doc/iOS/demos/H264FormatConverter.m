//  H264FormatConverter.m — 完整实现
//  详见 Doc/iOS/07-AVCC与Annex-B转换实战.md 的完整讲解

#import "H264FormatConverter.h"

static const uint8_t kSC3[] = {0x00,0x00,0x01};
static const uint8_t kSC4[] = {0x00,0x00,0x00,0x01};
static const size_t  kLS  = 4;

@implementation H264AnnexBPacket @end

@implementation H264FormatConverter

#pragma mark - AVCC → Annex-B

+ (H264AnnexBPacket *)toAnnexB:(CMSampleBufferRef)sb {
    if (!sb) return nil;
    H264AnnexBPacket *p = [H264AnnexBPacket new];
    p.pts = CMSampleBufferGetPresentationTimeStamp(sb);
    p.isKeyFrame = [self isKeyFrame:sb];
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    if (!bb) return nil;
    size_t len; char *dp;
    if (CMBlockBufferGetDataPointer(bb, 0, NULL, &len, &dp) != noErr || !dp) return nil;
    NSMutableData *o = [NSMutableData dataWithCapacity:len+256];
    if (p.isKeyFrame) {
        NSData *s=[self spsAnnexB:sb], *pp=[self ppsAnnexB:sb];
        if (s) [o appendData:s]; if (pp) [o appendData:pp];
    }
    NSData *nd = [self nalusToAnnexB:[NSData dataWithBytesNoCopy:dp length:len freeWhenDone:NO]];
    if (nd) [o appendData:nd];
    p.data = o; return p;
}

+ (NSData *)nalusToAnnexB:(NSData *)d {
    const uint8_t *b = d.bytes; NSUInteger len = d.length;
    if (len < 4) return nil;
    NSMutableData *o = [NSMutableData dataWithCapacity:len];
    NSUInteger i = 0;
    while (i + kLS <= len) {
        uint32_t nl = ((uint32_t)b[i]<<24)|((uint32_t)b[i+1]<<16)|((uint32_t)b[i+2]<<8)|b[i+3];
        i += kLS; if (nl == 0 || i + nl > len) break;
        [o appendBytes:kSC4 length:4]; [o appendBytes:b+i length:nl]; i += nl;
    }
    return o;
}

+ (NSData *)sps:(CMSampleBufferRef)sb { return [self ps:sb idx:0]; }
+ (NSData *)pps:(CMSampleBufferRef)sb { return [self ps:sb idx:1]; }

+ (NSData *)ps:(CMSampleBufferRef)sb idx:(size_t)idx {
    CMVideoFormatDescriptionRef f = CMSampleBufferGetFormatDescription(sb);
    if (!f) return nil;
    const uint8_t *d; size_t sz, cnt; int hdr;
    OSStatus s = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(f, idx, &d, &sz, &cnt, &hdr);
    return (s == noErr && d && sz) ? [NSData dataWithBytes:d length:sz] : nil;
}

+ (NSData *)spsAnnexB:(CMSampleBufferRef)sb {
    NSData *d = [self sps:sb]; if (!d) return nil;
    NSMutableData *o = [NSMutableData dataWithCapacity:d.length+4];
    [o appendBytes:kSC4 length:4]; [o appendData:d]; return o;
}
+ (NSData *)ppsAnnexB:(CMSampleBufferRef)sb {
    NSData *d = [self pps:sb]; if (!d) return nil;
    NSMutableData *o = [NSMutableData dataWithCapacity:d.length+4];
    [o appendBytes:kSC4 length:4]; [o appendData:d]; return o;
}

#pragma mark - Annex-B → AVCC

+ (CMSampleBufferRef)avccSampleBuffer:(NSData *)ab formatDesc:(CMVideoFormatDescriptionRef)fd pts:(CMTime)pts {
    NSData *avcc = [self ab2avcc:ab]; if (!avcc) return NULL;
    CMBlockBufferRef bb = NULL;
    CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, (void *)avcc.bytes, avcc.length, kCFAllocatorNull, NULL, 0, avcc.length, 0, &bb);
    if (!bb) return NULL;
    CMSampleTimingInfo ti = {.duration=kCMTimeInvalid, .presentationTimeStamp=pts, .decodeTimeStamp=kCMTimeInvalid};
    CMSampleBufferRef sb = NULL;
    CMSampleBufferCreateReady(kCFAllocatorDefault, bb, fd, 1, &ti, 0, NULL, &sb);
    CFRelease(bb); return sb;
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

+ (uint8_t)naluType:(NSData *)d {
    if (!d || d.length < 4) return 0;
    const uint8_t *b = d.bytes; NSUInteger i=0;
    if (d.length>=4 && memcmp(b,kSC4,4)==0) i=4; else if (d.length>=3 && memcmp(b,kSC3,3)==0) i=3;
    return (i < d.length) ? (b[i] & 0x1F) : 0;
}

+ (BOOL)isKeyFrame:(CMSampleBufferRef)sb {
    CFArrayRef a = CMSampleBufferGetSampleAttachmentsArray(sb, true);
    if (!a || CFArrayGetCount(a) == 0) return NO;
    CFBooleanRef ns = CFDictionaryGetValue(CFArrayGetValueAtIndex(a, 0), kCMSampleAttachmentKey_NotSync);
    return !ns || !CFBooleanGetValue(ns);
}

@end
