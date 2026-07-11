//  MetalYUVRenderer.m — 完整实现
//  详见 Doc/iOS/04-Metal渲染与零拷贝详解.md 的完整讲解

#import "MetalYUVRenderer.h"
#import <simd/simd.h>

static NSString *const kShaderSrc = @"\
#include <metal_stdlib>\n\
using namespace metal;\n\
struct VI { float2 p [[attribute(0)]]; float2 t [[attribute(1)]]; };\n\
struct VO { float4 p [[position]]; float2 t; };\n\
vertex VO vs(VI in [[stage_in]]) { VO o; o.p=float4(in.p,0.0,1.0); o.t=in.t; return o; }\n\
fragment float4 fs(VO in [[stage_in]], texture2d<float,access::sample> tY [[texture(0)]], texture2d<float,access::sample> tUV [[texture(1)]], sampler s [[sampler(0)]]) {\n\
    float y=tY.sample(s,in.t).r, u=tUV.sample(s,in.t).r-0.5, v=tUV.sample(s,in.t).g-0.5;\n\
    return float4(y+1.402*v, y-0.34414*u-0.71414*v, y+1.772*u, 1.0);\n\
}\n\
";

typedef struct { simd_float2 p; simd_float2 t; } Vertex;
static const Vertex kQuad[] = {
    {{-1,-1},{0,1}}, {{1,-1},{1,1}}, {{-1,1},{0,0}},
    {{1,-1},{1,1}}, {{1,1},{1,0}}, {{-1,1},{0,0}},
};

@implementation MetalYUVRenderer {
    id<MTLDevice> _dev; id<MTLCommandQueue> _q; id<MTLRenderPipelineState> _ps;
    id<MTLBuffer> _vb; id<MTLSamplerState> _sampler;
    CVMetalTextureCacheRef _tc;
    id<MTLTexture> _tY, _tUV;
    dispatch_semaphore_t _sem;
}

- (instancetype)initWithMetalView:(MTKView *)view {
    self = [super init]; if (!self) return nil;
    _dev = view.device ?: MTLCreateSystemDefaultDevice();
    view.device = _dev; _q = [_dev newCommandQueue];
    _sem = dispatch_semaphore_create(3);
    CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, _dev, NULL, &_tc);
    if (![self buildPipeline]) return nil;
    _vb = [_dev newBufferWithBytes:kQuad length:sizeof(kQuad) options:MTLResourceStorageModeShared];
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    _sampler = [_dev newSamplerStateWithDescriptor:sd];
    view.delegate = self; view.framebufferOnly = NO; view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    return self;
}

- (void)dealloc { if (_tc) { CVMetalTextureCacheFlush(_tc, 0); CFRelease(_tc); } }

- (BOOL)buildPipeline {
    NSError *e; id<MTLLibrary> lib = [_dev newLibraryWithSource:kShaderSrc options:nil error:&e];
    if (!lib) { NSLog(@"❌ shader: %@", e); return NO; }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"vs"];
    d.fragmentFunction = [lib newFunctionWithName:@"fs"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor new];
    vd.attributes[0].format=MTLVertexFormatFloat2; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
    vd.attributes[1].format=MTLVertexFormatFloat2; vd.attributes[1].offset=sizeof(simd_float2); vd.attributes[1].bufferIndex=0;
    vd.layouts[0].stride=sizeof(Vertex); vd.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;
    d.vertexDescriptor = vd;
    _ps = [_dev newRenderPipelineStateWithDescriptor:d error:&e];
    return _ps != nil;
}

- (void)render:(CVPixelBufferRef)pb {
    if (!pb) return;
    size_t w = CVPixelBufferGetWidth(pb), h = CVPixelBufferGetHeight(pb);
    { CVMetalTextureRef t=NULL; CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,_tc,pb,NULL,MTLPixelFormatR8Unorm,w,h,0,&t); if(t){_tY=CVMetalTextureGetTexture(t); CFRelease(t);} }
    { CVMetalTextureRef t=NULL; CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,_tc,pb,NULL,MTLPixelFormatRG8Unorm,w/2,h/2,1,&t); if(t){_tUV=CVMetalTextureGetTexture(t); CFRelease(t);} }
}

- (void)drawInMTKView:(MTKView *)view {
    dispatch_semaphore_wait(_sem, DISPATCH_TIME_FOREVER);
    if (!_tY || !_tUV) { dispatch_semaphore_signal(_sem); return; }
    id<MTLCommandBuffer> cb = [_q commandBuffer];
    id<CAMetalDrawable> dr = [view currentDrawable];
    MTLRenderPassDescriptor *pd = [view currentRenderPassDescriptor];
    if (!dr || !pd) { dispatch_semaphore_signal(_sem); return; }
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:pd];
    [e setRenderPipelineState:_ps]; [e setVertexBuffer:_vb offset:0 atIndex:0];
    [e setFragmentTexture:_tY atIndex:0]; [e setFragmentTexture:_tUV atIndex:1]; [e setFragmentSamplerState:_sampler atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [e endEncoding]; [cb presentDrawable:dr];
    __weak typeof(self) ws = self;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> _) { dispatch_semaphore_signal(ws->_sem); }];
    [cb commit];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {}
@end
