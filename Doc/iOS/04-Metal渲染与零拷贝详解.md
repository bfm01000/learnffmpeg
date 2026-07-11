# iOS Metal 渲染与零拷贝详解：从 CVPixelBuffer 到屏幕

> **适用方向**：iOS 移动端音视频 SDK 开发，视频渲染/预览/后处理方向
> **前置知识**：Metal 基础（MTLDevice / MTLCommandQueue / MTLTexture），了解 CVPixelBuffer 和 IOSurface 的基本概念
> **难度**：⭐⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 40 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[01-AVFoundation采集详解]] · [[../ffmpeg/15-iOS硬件编解码]] · [[03-GPUImage滤镜链详解]]
> **定位**：🟡 高级加分 —— Metal 是 iOS 上唯一的 GPU API（OpenGL ES 已废弃），渲染是采集→编码→显示的最后一步

---

## 一、全景导读：Metal 渲染在 iOS 媒体栈的位置

### 1.1 从场景说起

你已经从 AVCaptureSession 拿到了 NV12 的 CVPixelBuffer，也从 VideoToolbox 解出了 CVPixelBuffer。接下来怎么办？**你得把这些像素画到屏幕上**。

在 Android 上，你用 GLSurfaceView / TextureView + OpenGL ES 的 GL_OES_EGL_image_external 做零拷贝渲染。在 iOS 上，**OpenGL ES 从 2018 年起被 Apple 标记为 deprecated**，新项目只有一条路：**Metal**。

好消息是 Metal 的思路和 OpenGL ES 非常相似——你仍然有纹理、shader、FBO（Metal 叫 render pass descriptor）、纹理缓存——只是 API 从 GL 的全局状态机换成了 Metal 的面向对象风格。

### 1.2 Metal 在渲染管线里的位置

```
┌──────────────┐     ┌─────────────────┐     ┌──────────────┐
│ AVCapture    │     │ VideoToolbox    │     │ 网络接收      │
│ (采集)        │     │ (解码)          │     │ (拉流)        │
└──────┬───────┘     └────────┬────────┘     └──────┬───────┘
       │ CVPixelBuffer        │ CVPixelBuffer       │ CVPixelBuffer
       │ (NV12, IOSurface)    │ (NV12, IOSurface)   │ (手动构造)
       └──────────────────────┼────────────────────────┘
                              │
                    ┌─────────▼──────────┐
                    │ CVMetalTextureCache │  ← 零拷贝：alias IOSurface → MTLTexture
                    │ (纹理缓存)          │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │ Metal Shader       │  ← YUV→RGB 转换
                    │ (vertex + fragment) │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │ MTKView.drawable   │  ← 系统管理的可显示纹理
                    │ / CAMetalLayer     │
                    └─────────┬──────────┘
                              │
                    ┌─────────▼──────────┐
                    │ 屏幕               │
                    └────────────────────┘
```

### 1.3 为什么说这是"零拷贝"

理解零拷贝的关键在于 **IOSurface**：

```
CVPixelBuffer (摄像头/解码器输出)
    │
    └── 底层是 IOSurface（内核管理的 GPU 显存区域）
            │
            ├── VideoToolbox 编码器可以直接读写（同一个 IOSurface ID）
            │
            └── CVMetalTextureCache 通过 IOSurface ID "包装"成 MTLTexture
                而不是拷贝像素数据——CPU 不参与、没有 memcpy
```

**对比有拷贝方案**：
```
CVPixelBuffer → CVPixelBufferLockBaseAddress → memcpy → 新 MTLTexture → GPU
                                                          ↑
                                                     ~3-5ms for 1080p
                                                     + detile 开销
```

**零拷贝方案**：
```
CVPixelBuffer (IOSurface) → CVMetalTextureCache → MTLTexture (alias) → GPU
                                                       ↑
                                                   ~0.1ms（只是创建金属纹理的"视图"）
```

### 1.4 学习优先级

| 层级 | 内容 | 重要度 |
|------|------|--------|
| 🟢 中级必会 | MTKView 基本使用：设置 device、实现 draw 回调 | 🔥🔥🔥 |
| 🟢 中级必会 | Metal shader 基础：vertex + fragment，YUV→RGB | 🔥🔥🔥 |
| 🟡 高级加分 | CVMetalTextureCache 零拷贝 NV12 → 两张 MTLTexture | 🔥🔥🔥 |
| 🟡 高级加分 | CVPixelBufferPool + IOSurface/Metal compatibility keys | 🔥🔥 |
| 🔵 专家深水区 | 多线程渲染（command queue 并发）、shader 合并优化 | 🔥 |

---

## 二、面试速记（考前 10 分钟扫一遍）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | iOS 怎么渲染 YUV 到屏幕 | MTKView + CVMetalTextureCache + Metal shader | 🔥🔥🔥 | 🟡 |
| 2 | 为什么 NV12 要两张纹理 | Y 平面一张(R8)、UV 平面一张(RG8)，shader 里分别采样再合并 | 🔥🔥🔥 | 🟡 |
| 3 | CVMetalTextureCache 的作用 | 把 CVPixelBuffer(IOSurface) 零拷贝映射为 MTLTexture | 🔥🔥🔥 | 🟡 |
| 4 | 零拷贝为什么快 | 全程 IOSurface alias，无 CPU memcpy，无 GPU↔CPU 回读 | 🔥🔥 | 🟡 |
| 5 | 创建 pixelBufferPool 漏了什么 key 零拷贝会失效 | kCVPixelBufferIOSurfacePropertiesKey + kCVPixelBufferMetalCompatibilityKey | 🔥🔥 | 🟡 |
| 6 | Metal 和 OpenGL ES 渲染的对应关系 | MTLDevice≈EAGLContext, MTLTexture≈GL texture, CVMetalTextureCache≈CVOpenGLESTextureCache | 🔥🔥 | 🟢 |
| 7 | MTKView 和 CAMetalLayer 的区别 | MTKView 是 UIView 子类，内部封装了 CAMetalLayer + display link | 🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：iOS 上怎么把摄像头采集的 NV12 画面渲染到屏幕上？

**面试官想听什么：** 你是否理解完整的 Metal 渲染管线，尤其是零拷贝纹理缓存的使用。

**🗣️ 标准回答（可背诵）：**

> "iOS 上用 Metal 渲染 NV12 画面，核心流程分四步。第一步创建 CVMetalTextureCache——这是 CoreVideo 和 Metal 之间的零拷贝桥梁。第二步，在每一帧拿到 CVPixelBuffer 后，用 CVMetalTextureCacheCreateTextureFromImage 把 NV12 的两个平面分别映射成两张 MTLTexture：Y 平面映射为 .r8Unorm 格式的单通道纹理，UV 平面映射为 .rg8Unorm 格式的双通道纹理——注意这里是 alias 而不是拷贝，底层是同一个 IOSurface。第三步，在 Metal shader 里，vertex shader 负责画一个全屏矩形，fragment shader 从两张纹理采样后用公式做 YUV→RGB 转换。第四步，把渲染结果写入 MTKView 的 currentDrawable.texture，commit 后就上屏了。整个过程中数据一直在 GPU 显存里，CPU 不参与像素搬运。"

**👨‍💻 追问预警：**
> Q: "为什么要用两张纹理分别映射 Y 和 UV？不能映射成一张吗？"
> A: NV12 是 BiPlanar——Y 和 UV 是两个独立的平面，在 IOSurface 里也是分开存储的。CVMetalTextureCache 一次只能映射一个平面，所以必须调两次。Metal 的 MTLPixelFormat 里没有原生 NV12 格式（不像 Vulkan 的 VK_FORMAT_G8_B8R8_2PLANE_420_UNORM），所以两张纹理是最优方案。shader 里采样 Y 纹理得到亮度（luminance），采样 UV 纹理得到色度（chrominance），用 BT.601 或 BT.709 矩阵合成 RGB。

---

#### Q2：CVPixelBufferLockBaseAddress 和 CVMetalTextureCache 的区别是什么？

**面试官想听什么：** 你理解 CPU 访问 vs GPU 零拷贝的性能差异。

**🗣️ 标准回答（可背诵）：**

> "两者是完全不同的数据通路。LockBaseAddress 是 CPU 通路——它把 GPU 显存里的像素数据映射到 CPU 地址空间，让你能用 memcpy 或指针读像素。这背后至少有两个开销：一是 detile——GPU 纹理在显存里是以 tiled/block-based 布局存储的（为了缓存局部性），CPU 要连续读就必须先重排成线性布局，这个操作耗时；二是 GPU↔CPU 同步——你得等 GPU 的所有操作完成才能读。1080p 的 NV12 这一趟下来大概 3-5ms。CVMetalTextureCache 是 GPU 通路——它在 GPU 端直接给 IOSurface 创建一个纹理'视图'，不触发任何 detile 和 CPU↔GPU 拷贝。用时不到 0.1ms。所以做实时视频渲染一定走 TextureCache，LockBaseAddress 只在调试、截图等非热路径上用。"

**👨‍💻 追问预警：**
> Q: "如果我必须在 CPU 上读像素怎么办？"
> A: 用 CVPixelBufferLockBaseAddress 加 kCVPixelBufferLock_ReadOnly 标志——只读标志可以让驱动少做一些同步。但仍然有 detile 开销。另外一定要配对的 unlock，否则 IOSurface 一直锁着会导致编码器/渲染管线卡住。这个操作永远不要放在音频线程或者主线程上。

---

#### Q3：MTKView 的 draw 回调里应该做什么、不应该做什么？

**面试官想听什么：** 你是否理解 Metal 的线程模型和 MTKView 的调用时机。

**🗣️ 标准回答（可背诵）：**

> "MTKView 的 draw 回调是由内部的 CADisplayLink 驱动的，默认在屏幕刷新时调用（60fps 就是每 16.67ms 一次），回调在主线程。draw 回调里应该只做 Metal 渲染操作：创建 command buffer、设置 render pass、绑定纹理、draw call、commit。不应该在 draw 回调里做耗时操作——比如编解码、网络 IO、甚至创建 texture cache（应该在外层初始化）。特别要注意：不要在 draw 回调里做同步等待（比如 waitUntilCompleted），这会直接卡住主线程导致 UI 无响应。另外，draw 回调的频率是固定的——如果你的帧源不是 60fps（比如 30fps 摄像头），要在外层做帧可用性判断，没有新帧就 skip draw。"

**👨‍💻 追问预警：**
> Q: "如果我想在后台线程渲染怎么办？"
> A: 可以不用 MTKView 的 draw 回调，直接用 CAMetalLayer 的nextDrawable 在任何线程调。但要自己管理刷新节奏——通常用 CADisplayLink 或 dispatch_source 驱动。而且不是所有 MTLCommandQueue 操作都线程安全，command buffer 的 commit 操作需要小心。

---

## 三、核心 Demo：完整的 Metal YUV 渲染器

### 3.1 Demo 架构

```
MetalRenderer.h/.m       ← 核心渲染器（CVPixelBuffer → 屏幕）
MetalShader.h/.m         ← Shader 管理（加载/编译 Metal shader）
MetalView.h/.m           ← MTKView 子类封装
```

### 3.2 MetalRenderer.h

```objc
//  MetalRenderer.h
//  iOS Metal 零拷贝 YUV 渲染器
//
//  用法:
//  1. 创建 MetalRenderer 实例
//  2. 每来一帧 CVPixelBuffer 就调用 renderPixelBuffer:
//  3. MTKView 的 draw 回调里自动渲染

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface MetalRenderer : NSObject

/// 初始化渲染器（绑定到 MTKView）
- (instancetype)initWithMetalView:(MTKView *)view;

/// 喂入一帧 NV12 的 CVPixelBuffer（线程安全，零拷贝）
- (void)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer;

@end

NS_ASSUME_NONNULL_END
```

### 3.3 MetalRenderer.m（完整实现）

```objc
//  MetalRenderer.m
//  完整的 iOS Metal 零拷贝 YUV→RGB 渲染器

#import "MetalRenderer.h"
#import <simd/simd.h>

// ============================================================
// MARK: - Metal Shader 源码（内联，也可以放 .metal 文件）
// ============================================================
static NSString *const kShaderSource = @"\
#include <metal_stdlib>\n\
using namespace metal;\n\
\n\
// 顶点输入：全屏矩形的两个三角形\n\
struct VertexIn {\n\
    float2 position [[attribute(0)]];\n\
    float2 texCoord [[attribute(1)]];\n\
};\n\
\n\
// 顶点输出 → 片段着色器输入\n\
struct VertexOut {\n\
    float4 position [[position]];\n\
    float2 texCoord;\n\
};\n\
\n\
// ===== Vertex Shader =====\n\
vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {\n\
    VertexOut out;\n\
    out.position = float4(in.position, 0.0, 1.0);\n\
    out.texCoord = in.texCoord;\n\
    return out;\n\
}\n\
\n\
// ===== Fragment Shader: NV12 → RGB =====\n\
// 核心：从两张纹理采样后做 YUV→RGB 转换\n\
// Y 纹理: .r8Unorm (单通道，亮度)\n\
// UV 纹理: .rg8Unorm (双通道，色度)\n\
fragment float4 fragment_nv12(\n\
    VertexOut in [[stage_in]],\n\
    texture2d<float, access::sample> textureY [[texture(0)]],\n\
    texture2d<float, access::sample> textureUV [[texture(1)]],\n\
    sampler textureSampler [[sampler(0)]]\n\
) {\n\
    // 采样 Y 和 UV\n\
    float y  = textureY.sample(textureSampler, in.texCoord).r;\n\
    float2 uv = textureUV.sample(textureSampler, in.texCoord).rg;\n\
\n\
    // ITU-R BT.601 Full Range → RGB\n\
    // 这是 iOS 摄像头 NV12 (FullRange) 正确的转换矩阵\n\
    float u = uv.x - 0.5;\n\
    float v = uv.y - 0.5;\n\
\n\
    float r = y + 1.40200 * v;\n\
    float g = y - 0.34414 * u - 0.71414 * v;\n\
    float b = y + 1.77200 * u;\n\
\n\
    // 或 BT.709（大多数现代设备实际使用）:\n\
    // float r = y + 1.57480 * v;\n\
    // float g = y - 0.18733 * u - 0.46813 * v;\n\
    // float b = y + 1.85560 * u;\n\
\n\
    return float4(r, g, b, 1.0);\n\
}\n\
\n\
// ===== Fragment Shader: NV12 → RGB (VideoRange 16-235) =====\n\
// 如果输入是 VideoRange，用这个 shader\n\
fragment float4 fragment_nv12_videorange(\n\
    VertexOut in [[stage_in]],\n\
    texture2d<float, access::sample> textureY [[texture(0)]],\n\
    texture2d<float, access::sample> textureUV [[texture(1)]],\n\
    sampler textureSampler [[sampler(0)]]\n\
) {\n\
    float y  = textureY.sample(textureSampler, in.texCoord).r;\n\
    float2 uv = textureUV.sample(textureSampler, in.texCoord).rg;\n\
\n\
    // VideoRange → FullRange 归一化\n\
    y  = (y  - 16.0/255.0) * 255.0 / (235.0 - 16.0);\n\
    uv = (uv - 16.0/255.0) * 255.0 / (240.0 - 16.0);\n\
\n\
    float u = uv.x - 0.5;\n\
    float v = uv.y - 0.5;\n\
\n\
    float r = y + 1.40200 * v;\n\
    float g = y - 0.34414 * u - 0.71414 * v;\n\
    float b = y + 1.77200 * u;\n\
\n\
    return float4(r, g, b, 1.0);\n\
}\n\
";

// ============================================================
// MARK: - 全屏矩形的顶点数据（两个三角形 = 6 个顶点）
// ============================================================
typedef struct {
    simd_float2 position;
    simd_float2 texCoord;
} Vertex;

static const Vertex kQuadVertices[] = {
    // 三角形 1
    {{-1.0, -1.0}, {0.0, 1.0}},   // 左下
    {{ 1.0, -1.0}, {1.0, 1.0}},   // 右下
    {{-1.0,  1.0}, {0.0, 0.0}},   // 左上
    // 三角形 2
    {{ 1.0, -1.0}, {1.0, 1.0}},   // 右下
    {{ 1.0,  1.0}, {1.0, 0.0}},   // 右上
    {{-1.0,  1.0}, {0.0, 0.0}},   // 左上
};

// ============================================================
// MARK: - MetalRenderer 实现
// ============================================================
@implementation MetalRenderer {
    // Metal 核心对象
    id<MTLDevice>              _device;
    id<MTLCommandQueue>        _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLBuffer>              _vertexBuffer;
    id<MTLSamplerState>        _sampler;

    // 零拷贝纹理缓存 ★核心★
    CVMetalTextureCacheRef     _textureCache;

    // 当前帧的 Y/UV 纹理（由 renderPixelBuffer: 创建，draw 时使用）
    id<MTLTexture>             _textureY;
    id<MTLTexture>             _textureUV;

    // 线程安全
    dispatch_semaphore_t       _frameSemaphore; // 控制 inflight 帧数
    static const int           kMaxInflightFrames = 3;
}

// ============================================================
// MARK: - 初始化
// ============================================================
- (instancetype)initWithMetalView:(MTKView *)view {
    self = [super init];
    if (!self) return nil;

    _device = view.device;
    if (!_device) {
        // 如果 MTKView 没设 device，取系统默认
        _device = MTLCreateSystemDefaultDevice();
        view.device = _device;
    }

    _commandQueue = [_device newCommandQueue];
    _frameSemaphore = dispatch_semaphore_create(kMaxInflightFrames);

    // ① 创建零拷贝纹理缓存
    CVReturn cvRet = CVMetalTextureCacheCreate(
        kCFAllocatorDefault,
        NULL,                    // cache attributes
        _device,
        NULL,                    // texture attributes
        &_textureCache
    );
    if (cvRet != kCVReturnSuccess) {
        NSLog(@"❌ CVMetalTextureCacheCreate 失败: %d", cvRet);
        return nil;
    }

    // ② 编译 Shader → 创建渲染管线
    if (![self buildPipeline]) {
        return nil;
    }

    // ③ 创建顶点缓冲
    _vertexBuffer = [_device newBufferWithBytes:kQuadVertices
                                         length:sizeof(kQuadVertices)
                                        options:MTLResourceStorageModeShared];

    // ④ 创建采样器（双线性插值，边缘 clamp）
    MTLSamplerDescriptor *samplerDesc = [MTLSamplerDescriptor new];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    _sampler = [_device newSamplerStateWithDescriptor:samplerDesc];

    // ⑤ 配置 MTKView
    view.delegate = self;  // 让 renderer 成为 MTKViewDelegate
    view.framebufferOnly = NO;  // 我们需要读 drawable 的纹理信息
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;  // 标准输出格式
    view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    return self;
}

- (void)dealloc {
    if (_textureCache) {
        CVMetalTextureCacheFlush(_textureCache, kCVOptionFlags_None);
        CFRelease(_textureCache);
    }
}

// ============================================================
// MARK: - 编译 Shader
// ============================================================
- (BOOL)buildPipeline {
    NSError *error = nil;

    // 从源码编译
    id<MTLLibrary> library = [_device newLibraryWithSource:kShaderSource
                                                   options:nil
                                                     error:&error];
    if (!library) {
        NSLog(@"❌ Shader 编译失败: %@", error);
        return NO;
    }

    id<MTLFunction> vertexFunc   = [library newFunctionWithName:@"vertex_main"];
    id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"fragment_nv12"];
    // 如果要 VideoRange shader: @"fragment_nv12_videorange"

    // 配置渲染管线
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = vertexFunc;
    desc.fragmentFunction = fragmentFunc;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // ★关键★ 顶点布局：和 Vertex struct 对应
    MTLVertexDescriptor *vertexDesc = [MTLVertexDescriptor new];
    // Attribute 0: position (float2)
    vertexDesc.attributes[0].format   = MTLVertexFormatFloat2;
    vertexDesc.attributes[0].offset   = 0;
    vertexDesc.attributes[0].bufferIndex = 0;
    // Attribute 1: texCoord (float2)
    vertexDesc.attributes[1].format   = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].offset   = sizeof(simd_float2);
    vertexDesc.attributes[1].bufferIndex = 0;
    // Layout: 每个顶点 = position + texCoord = 两个 float2
    vertexDesc.layouts[0].stride      = sizeof(Vertex);
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    desc.vertexDescriptor = vertexDesc;

    _pipelineState = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!_pipelineState) {
        NSLog(@"❌ 创建 PipelineState 失败: %@", error);
        return NO;
    }
    return YES;
}

// ============================================================
// MARK: - 外部接口：喂入 CVPixelBuffer
// ============================================================
- (void)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer) return;

    // ★零拷贝核心★ 把 CVPixelBuffer 的两个平面映射为 MTLTexture

    size_t width  = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    // ---- 平面 0: Y（亮度）----
    // 注意：CVMetalTextureCache 内部会 flush 旧纹理，重复调用是高效的
    {
        CVMetalTextureRef cvTextureY = NULL;
        CVReturn cvRet = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,
            _textureCache,
            pixelBuffer,
            NULL,                          // texture attributes（nil = 默认）
            MTLPixelFormatR8Unorm,         // ★ Y 平面 = 单通道 8bit
            width,                         // Y 平面宽度 = 图像宽度
            height,                        // Y 平面高度 = 图像高度
            0,                             // planeIndex = 0
            &cvTextureY
        );
        if (cvRet == kCVReturnSuccess && cvTextureY) {
            _textureY = CVMetalTextureGetTexture(cvTextureY);
            CFRelease(cvTextureY);
        }
    }

    // ---- 平面 1: UV（色度，NV12 交错）----
    {
        CVMetalTextureRef cvTextureUV = NULL;
        CVReturn cvRet = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,
            _textureCache,
            pixelBuffer,
            NULL,
            MTLPixelFormatRG8Unorm,        // ★ UV 平面 = 双通道 8bit（U和V交错）
            width / 2,                     // UV 平面宽度 = 图像宽度 / 2（4:2:0 水平降采样）
            height / 2,                    // UV 平面高度 = 图像高度 / 2（4:2:0 垂直降采样）
            1,                             // planeIndex = 1
            &cvTextureUV
        );
        if (cvRet == kCVReturnSuccess && cvTextureUV) {
            _textureUV = CVMetalTextureGetTexture(cvTextureUV);
            CFRelease(cvTextureUV);
        }
    }
}

// ============================================================
// MARK: - MTKViewDelegate: 实际的渲染 draw call
// ============================================================
- (void)drawInMTKView:(MTKView *)view {
    // inflight 帧数控制：超过 3 帧在飞就等
    dispatch_semaphore_wait(_frameSemaphore, DISPATCH_TIME_FOREVER);

    if (!_textureY || !_textureUV) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;
    }

    // ① 创建 command buffer
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];

    // ② 拿当前帧的 drawable（系统管理的可显示纹理）
    id<CAMetalDrawable> drawable = [view currentDrawable];
    if (!drawable) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;
    }

    // ③ 配置 render pass
    MTLRenderPassDescriptor *passDesc = [view currentRenderPassDescriptor];
    if (!passDesc) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;
    }

    // ④ 创建 render encoder
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

    [encoder setRenderPipelineState:_pipelineState];
    [encoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];

    // ⑤ ★绑定两张纹理 + 采样器★
    [encoder setFragmentTexture:_textureY  atIndex:0];
    [encoder setFragmentTexture:_textureUV atIndex:1];
    [encoder setFragmentSamplerState:_sampler atIndex:0];

    // ⑥ 画全屏矩形（6 个顶点 = 两个三角形）
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6];

    [encoder endEncoding];

    // ⑦ 提交到屏幕（present 后系统负责 vsync 同步）
    [commandBuffer presentDrawable:drawable];

    // ⑧ 渲染完成后释放 semaphore（防止积压过多帧）
    __weak typeof(self) weakSelf = self;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buffer) {
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            dispatch_semaphore_signal(strongSelf->_frameSemaphore);
        }
    }];

    [commandBuffer commit];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // drawable 大小变化时（如旋转、分屏），这里可以做 viewport 调整
    // 由于我们画全屏矩形，用 vertex shader 的 clip space，基本不需要改
}

@end
```

### 3.4 关键设计决策解读

**1. 为什么用 R8Unorm 映射 Y 平面、RG8Unorm 映射 UV 平面？**

这是 Metal 的像素格式限制决定的：
- Y 平面：每个像素一个字节（灰度值 0-255），对应 `MTLPixelFormatR8Unorm`
- UV 平面：每两个字节一对（U 和 V 交错），对应 `MTLPixelFormatRG8Unorm`
- 如果映射成错误的格式（比如把 UV 也映射成 R8Unorm），shader 里采样结果完全错误
- Metal 没有类似 Vulkan 的原生 YUV 多平面格式，所以两张独立纹理是最佳实践

**2. 为什么 UV 纹理的宽高是 Y 纹理的一半？**

NV12 是 4:2:0 色度降采样——每 2×2 的 Y 像素共享一对 UV。所以 UV 平面的宽高各是 Y 平面的一半：

```
Y 平面:  width × height      个字节
UV 平面: (width/2) × (height/2) × 2 个字节
```

Metal shader 里采样 UV 时，GPU 自动做双线性插值，纹理坐标 0.0~1.0 正确映射到降采样后的 UV 数据。

**3. 为什么用 dispatch_semaphore 控制 inflight 帧数？**

Metal 的 command buffer 是异步执行的。如果不控制，CPU 可能一次性提交几十个 command buffer，而 GPU 来不及消费——导致：
- 内存暴涨（每个 command buffer 持有自己的资源）
- 延时增加（积压的帧越来越多，看到的画面是几百 ms 前的）

设 `kMaxInflightFrames = 3` 意味着 GPU 管线里最多 3 帧。超过后 CPU 会等待——这是所有 Metal 应用的标准实践（Metal Best Practices Guide 明确推荐）。

**4. 为什么 framebufferOnly = NO？**

默认 `framebufferOnly = YES`，意味着 MTKView 的 drawable 纹理只写不读。如果你的渲染管线需要读回（比如做后处理、截图），要设成 NO。纯渲染场景可以保持 YES 以获得微小性能提升。

---

## 四、进阶话题

### 4.1 CVPixelBufferPool 的正确创建（保证零拷贝）

```objc
- (CVPixelBufferPoolRef)createPixelBufferPool:(int)width
                                       height:(int)height
                                       format:(OSType)format
                                   minBuffers:(int)minCount {
    NSDictionary *attrs = @{
        // ★ 这两个 key 是零拷贝的前提！
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{},  // 拿 IOSurface 后端
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,   // 能作为 Metal 纹理

        (id)kCVPixelBufferWidthKey:             @(width),
        (id)kCVPixelBufferHeightKey:            @(height),
        (id)kCVPixelBufferPixelFormatTypeKey:   @(format),
        (id)kCVPixelBufferMinimumBufferCountKey: @(minCount),
        (id)kCVPixelBufferBytesPerRowAlignmentKey: @(64), // Metal 推荐 64 字节对齐
    };

    CVPixelBufferPoolRef pool = NULL;
    CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL,
                            (__bridge CFDictionaryRef)attrs, &pool);
    return pool;
}

// 使用：
// CVPixelBufferPoolRef pool = [self createPixelBufferPool:1920 height:1080
//    format:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange minBuffers:6];
// CVPixelBufferRef buf;
// CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &buf);
```

### 4.2 BT.601 vs BT.709 的选择

- **BT.601**：标清（SD）色彩空间，老设备和某些摄像头使用
- **BT.709**：高清（HD）色彩空间，现代 iPhone 的默认色彩空间
- **BT.2020**：HDR 色彩空间

实际工程中，现代 iPhone 摄像头吐出的 NV12 是 BT.709。但从 CVPixelBuffer 的 attachments 里可以读取确切的色彩矩阵：

```objc
CFStringRef matrix = CVBufferGetAttachment(pixelBuffer,
    kCVImageBufferYCbCrMatrixKey, NULL);
// kCVImageBufferYCbCrMatrix_ITU_R_709_2   → BT.709
// kCVImageBufferYCbCrMatrix_ITU_R_601_4   → BT.601
// kCVImageBufferYCbCrMatrix_ITU_R_2020    → BT.2020
```

### 4.3 处理旋转和镜像

摄像头采集的 CVPixelBuffer 可能带旋转。你可以在 vertex shader 里处理旋转和镜像：

```metal
// vertex shader 中用 rotateMatrix 乘 position
constant float2x2 rotateMatrix = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);
// 镜像：texCoord.x = 1.0 - texCoord.x;  （前置摄像头需要）
```

或者在设置 vertex buffer 时直接调整纹理坐标。前置摄像头通常需要水平翻转（镜像）。

### 4.4 自定义滤镜链（Metal 版 GPUImage）

借鉴 GPUImage 的滤镜链设计，用 Metal 实现：

```objc
// Metal 滤镜链的核心协议
@protocol MetalFilterInput <NSObject>
- (void)setInputTexture:(id<MTLTexture>)texture;
@end

@protocol MetalFilterOutput <NSObject>
- (void)addTarget:(id<MetalFilterInput>)target;
@end

// 每个滤镜内部管理自己的 render pass（不需要 FBO，Metal 用 descriptor 描述）
@interface MetalBeautyFilter : NSObject <MetalFilterInput, MetalFilterOutput>
@property (nonatomic, strong) id<MTLTexture> outputTexture;
- (void)renderToTexture:(id<MTLTexture>)destTexture
          commandBuffer:(id<MTLCommandBuffer>)commandBuffer;
@end
```

这与 GPUImage 的 GPUImageOutput / GPUImageInput / GPUImageFilter 三层抽象一一对应，但底层 API 从 OpenGL ES 换成了 Metal。

---

## 五、常见坑与实践经验

### 坑 1：CVMetalTextureCache 返回 -6660（kCVReturnInvalidArgument）

**根因**：CVPixelBuffer 的 pixel format 不支持——比如尝试用 `MTLPixelFormatRGBA8Unorm` 映射 NV12 的 Y 平面。检查 pixel format 和 planeIndex 的对应关系。

### 坑 2：渲染画面颜色发绿/偏紫

**根因**：YUV→RGB 转换矩阵用反了。BT.601 和 BT.709 的系数不同：
- 画面发绿 → UV 分量用反了（texture mapping 时 UV 纹理的格式不对，比如用 R8Unorm 映射了必须用 RG8Unorm 的平面）
- 画面偏紫 → 可能是 FullRange 的数据用 VideoRange shader 解码了（或者反过来）

### 坑 3：渲染后画面闪烁

**根因**：多帧共享了同一个 _textureY/_textureUV 引用，前一帧的纹理还没用完就被新帧覆盖。正确做法是用 inflight semaphore 限制——如代码中的 `kMaxInflightFrames = 3`。

### 坑 4：MTKView 不显示内容但数据存在

**排查清单**：
1. `mtkView.delegate` 设了吗？
2. `mtkView.enableSetNeedsDisplay = NO`（默认是 `NO`，即持续刷新）？
3. `paused` 属性是 `NO`？
4. `drawInMTKView:` 被调了吗？（打断点确认）
5. commit 之前 `endEncoding` 了吗？
6. texture 的宽高是 0 吗？（检查 CVMetalTextureCache 是否成功）

---

## 🎯 一句话总结

> iOS Metal 渲染的核心是 CVMetalTextureCache——它把 CVPixelBuffer 底层的 IOSurface 零拷贝 alias 成 Metal 纹理，NV12 两个平面分别映射为 R8Unorm 和 RG8Unorm，在 shader 里用 YUV→RGB 矩阵合成输出。全程数据不离开 GPU 显存、CPU 零参与。

## 🔗 关联文档

- [[00-iOS音视频开发全景导读]] — iOS 媒体栈全景图
- [[01-AVFoundation采集详解]] — 拿到 CVPixelBuffer 的来源
- [[../ffmpeg/15-iOS硬件编解码]] — 编解码产出的 CVPixelBuffer
- [[03-GPUImage滤镜链详解]] — GPUImage 的滤镜链设计（Metal版同理）
- [[05-VideoToolbox硬编码实战]] — 编码输出接入 Metal 渲染
- [[06-VideoToolbox硬解码实战]] — 解码输出接入 Metal 渲染
