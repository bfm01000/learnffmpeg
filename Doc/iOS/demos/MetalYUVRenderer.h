//  MetalYUVRenderer.h
//  iOS Metal 零拷贝 YUV→RGB 渲染器 — 独立可用
//  依赖: Metal.framework, MetalKit.framework, CoreVideo.framework

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Metal 零拷贝 NV12→RGB 渲染器
/// 用法: 创建后绑定到 MTKView, 每帧调 render: 喂 CVPixelBuffer
@interface MetalYUVRenderer : NSObject <MTKViewDelegate>

/// 初始化并绑定到 MTKView (view.device 会被自动设置)
- (instancetype)initWithMetalView:(MTKView *)view;

/// 喂入一帧 NV12 CVPixelBuffer (线程安全, 零拷贝)
- (void)render:(CVPixelBufferRef)pixelBuffer;

@end

NS_ASSUME_NONNULL_END
