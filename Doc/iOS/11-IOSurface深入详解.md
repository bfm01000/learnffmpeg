# IOSurface 深入详解（面试向）

## 0. 本篇定位

- 面试复习：先能解释 IOSurface 是跨框架共享图像内存的底座，`CVPixelBuffer`、Metal、VideoToolbox 可以通过它共享同一份底层资源。
- 深入学习：重点看 plane、stride、引用生命周期、跨进程/跨框架共享和缓存一致性。
- 工程落点：这篇偏底层机制，和 `10-iOS零拷贝深入详解.md` 互补：10 讲链路，11 讲底座。
> 适用方向：iOS/macOS 音视频 SDK 开发、跨进程渲染、零拷贝管线
> 前置：了解 CVPixelBuffer、Metal/OpenGL 基础、虚拟内存基本概念
> 难度：⭐⭐⭐⭐⭐
> 关联：[[10-iOS零拷贝深入详解]] · [[04-Metal渲染与零拷贝详解]] · [[../ffmpeg/15-iOS硬件编解码]]

---

## 一、面试问答

### Q1：IOSurface 到底是什么？用一句话说明白。

**话术**：

> "IOSurface 是 Apple 平台上的**内核级共享内存对象**。它最核心的价值是：**一块物理内存，多个消费者（CPU、GPU、不同进程、不同框架）都能直接读写，不需要拷贝。**"
>
> "你可以把它类比成 Linux 的 `shmget` / `memfd_create`，但 IOSurface 专门为图形/视频做了优化——它理解像素格式（pixel format）、stride、色彩空间这些图形概念，而且 GPU 能直接把它当纹理用。普通的 `shm` 做不到这一点。"
>
> "在 iOS 的整个音视频链路里，摄像头采集、VideoToolbox 编解码、Metal 渲染、CoreML 推理——所有这些模块之间传递的 CVPixelBuffer，底层全是同一块 IOSurface。这就是 iOS 零拷贝的物理基础。"

---

### Q2：IOSurface 和 CVPixelBuffer 是什么关系？

**话术**：

> "CVPixelBuffer 是 **CoreVideo 对 IOSurface 的上层封装**。它们的层级关系是："
>
> ```
> CVPixelBuffer      ← 面向应用开发者的 API（ObjC/C）
>     │
>     └── 内部持有一个 IOSurfaceRef
>            │
>            └── IOSurface  ← 内核对象，管理实际的物理内存
> ```
>
> "CVPixelBuffer 给你加了这些便利：自动管理 IOSurface 的引用计数、提供 Lock/Unlock 语义、管理 pixel format 和 plane 信息、和 CoreVideo 生态（CMSampleBuffer、VTCompressionSession 等）无缝对接。"
>
> "你可以绕过 CVPixelBuffer 直接用 IOSurface："
>
> ```objc
> // 创建一个 1920x1080 BGRA 的 IOSurface
> NSDictionary *props = @{
>     (id)kIOSurfaceWidth:  @1920,
>     (id)kIOSurfaceHeight: @1080,
>     (id)kIOSurfaceBytesPerElement: @4,
>     (id)kIOSurfacePixelFormat: @(kCVPixelFormatType_32BGRA),
> };
> IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
>
> // 拿到它的全局 ID
> uint32_t surfaceID = IOSurfaceGetID(surface);
>
> // 另一个进程通过这个 ID 就能拿到同一块内存
> IOSurfaceRef same = IOSurfaceLookup(surfaceID);
> ```
>
> "但在工程中几乎不会绕过——因为 CVPixelBuffer 和上层框架（AVFoundation、VideoToolbox、Metal）的对接太方便了。直接用 IOSurface 意味着你要自己处理很多底层细节（内存对齐、pixel format 兼容性、跨进程生命周期管理等）。"

---

### Q3：IOSurface 为什么能做到跨进程共享？机制是什么？

**面试官意图**：考察你对 iOS 进程间通信和内核对象的理解深度。

**话术**：

> "这要从 Mach 内核说起。iOS/macOS 的内核是 XNU，它提供了一种叫 **Mach port** 的进程间通信机制。IOSurface 底层是一个 **Mach 内核对象**。"
>
> "**完整的跨进程流程**："
>
> ```
> 进程 A                                             进程 B
>   │                                                  │
>   │ IOSurfaceCreate(props)                           │
>   │   → 内核分配物理内存                              │
>   │   → 返回 IOSurfaceRef (用户空间句柄)              │
>   │   → IOSurfaceGetID → surfaceID = 42              │
>   │                                                  │
>   │ 把 surfaceID 通过任意 IPC 传给 B:                  │
>   │ (XPC / mach_msg / URL scheme / 剪贴板)            │
>   │ ──────────────────────────────────────────────►  │
>   │                                                  │
>   │                                          IOSurfaceLookup(42)
>   │                                            → 内核查表: ID 42 的内存
>   │                                            → 映射到进程 B 的地址空间
>   │                                            → 返回一个新的 IOSurfaceRef
>   │                                            → 两块指针指向同一物理内存
>   │                                                  │
>   │ 进程 A 往 IOSurface 写 ──────── 同一块 ────────► 进程 B 立即可见
>   │                               物理内存
> ```
>
> "**为什么不能直接用共享内存（shm）**：
> - 普通 `shm_open` + `mmap` 只是 CPU 侧的映射，GPU 不认识这块内存的像素格式
> - IOSurface 内核对象记录了 pixel format、width、height、bytesPerRow、plane 信息——GPU 驱动可以直接读取这些元数据，把这块内存当作纹理来采样
> - IOSurface 的 tiled/swizzle 存储格式是 GPU 原生的，不需要 CPU 做格式转换
>
> "**几个关键 API**：
> - `IOSurfaceGetID(surface)` — 获取全局唯一 32 位 ID（不增加引用计数）
> - `IOSurfaceLookup(id)` — 通过 ID 查找并 retain IOSurface（增加引用计数，用完后要 CFRelease）
> - `IOSurfaceLookupFromMachPort(port)` — 通过 Mach port 查找（内核更安全的传递方式）"

---

### Q4：IOSurface 的 tiled 内存布局是什么？为什么 GPU 需要 tiled 而不是 linear？

**话术**：

> "这是 IOSurface 最容易被忽略但最重要的设计。IOSurface 在物理内存里不是逐行线性排列的，而是以 **tile**（通常 16×16 或 32×32 像素块）为单位打散的。"
>
> "**线性布局 vs Tiled 布局**："
>
> ```
> 一张 32×32 的图像在内存里的排列:
>
> 线性 (Linear, CPU 友好):
>   地址 0~1023:   Row 0 的 32 个像素
>   地址 1024~2047: Row 1 的 32 个像素
>   ...
>   优点: CPU 逐行读很自然
>   缺点: 像素 (0,0) 和 (1,0) 在内存里差了一整行的 stride，
>         GPU 纹理缓存局部性极差
>
> Tiled (GPU 友好, Morton/Twiddled 等变体):
>   地址 0~1023:   Tile(0,0) = 左上角 16×16 块内的 256 个像素
>   地址 1024~2047: Tile(0,1) = 右边 16×16 块内的 256 个像素
>   地址 2048~3071: Tile(1,0) = 下面 16×16 块内的 256 个像素
>   ...
>   优点: GPU 纹理缓存是 2D 的，
>        采样一个点 (u,v) 时，它周围的像素大概率在同一个 tile 里 →
>        一次 cache line fetch 就能拿到 —— 缓存命中率极高
>   缺点: CPU 不重排的话根本读不了
> ```
>
> "**这就是 IOSurface 的核心矛盾——也是它强大的地方**：GPU 要以 tiled 格式读写才能高效；CPU 要 linear 格式才能逐行访问。IOSurface 在底层同时支持两种访问模式，但切换有代价——这就是 LockBaseAddress 需要 detile 操作的根本原因。"
>
> "**补充细节——tile 尺寸和 pixel format 的关系**：不同硬件/格式的 tile 大小不同。Apple Silicon 上 NV12 的 Y 平面通常是 64×16 的 tile，UV 平面是 32×16。BGRA 可能是 32×32。这些参数由 GPU 驱动决定，上层不需要关心——但理解它的存在，才能理解为什么 LockBaseAddress 突然变慢。"

---

### Q5：IOSurface 的内存是什么时候分配的？引用计数怎么管理？

**话术**：

> "**延迟分配**。`IOSurfaceCreate` 调用时只是创建了内核对象、记录了属性（宽高、pixel format），**物理内存并没有立即分配**。真正的内存分配发生在第一次访问时——无论是 CPU Lock 还是 GPU 绑定为纹理。这和 macOS/iOS 的虚拟内存管理一致。"
>
> "**引用计数**：IOSurface 由内核维护引用计数。每次 `IOSurfaceCreate` / `IOSurfaceLookup` / `CFRetain` 都会增加引用计数。每次 `CFRelease` 减少。当引用计数归零时，内核释放物理内存。"
>
> "**注意**：`IOSurfaceGetID` 不增加引用计数——它只是读一个整数。所以你不能凭 '知道 ID' 来保证 IOSurface 还活着。如果创建方已经 CFRelease 了，你拿这个 ID 去 IOSurfaceLookup 会返回 NULL。这是很多跨进程崩溃的根因——进程 A 释放了 IOSurface，进程 B 还在用。"
>
> "**iOS 上的特殊限制**：iOS 不开放 `IOSurfaceIncrementUseCount` / `IOSurfaceDecrementUseCount`（这是 macOS 的私有 API）。在 iOS 上，IOSurface 的生命周期完全靠引用计数自动管理。你唯一能做的就是 retain/release。跨进程时，发送方和接收方要自己约定好生命周期。"

---

## 二、深入原理

### 2.1 IOSurface 的内核架构

IOSurface 不是单纯的"一块共享内存"——它是一个完整的 IOKit 子系统，包含内核驱动和用户空间框架两层。

```
┌──────────────────────────────────────────────────────────┐
│  用户空间                                                 │
│                                                          │
│  App A                         App B                     │
│    │                             │                       │
│    │ IOSurface.framework         │ IOSurface.framework    │
│    │ (用户态封装)                 │ (用户态封装)            │
│    │                             │                       │
│    │ 创建 / Lookup / GetID       │ 渲染 / 编码 / 推理     │
│    │                             │                       │
├────┼─────────────────────────────┼───────────────────────┤
│    │  mach_msg (Mach IPC)        │                       │
│    ▼                             ▼                       │
├──────────────────────────────────────────────────────────┤
│  内核空间 (XNU)                                           │
│                                                          │
│  ┌──────────────────────────────────────────────┐        │
│  │ IOSurfaceRoot (IOKit 服务)                    │        │
│  │                                              │        │
│  │  ┌──────────────────────────────────────┐    │        │
│  │  │ IOSurface 对象 (每个 surface 一个)     │    │        │
│  │  │                                      │    │        │
│  │  │  • surfaceID: 全局唯一 32 位整数      │    │        │
│  │  │  • 属性: width, height, pixelFormat, │    │        │
│  │  │          bytesPerRow, planeCount ...  │    │        │
│  │  │  • 引用计数 (内核维护)                │    │        │
│  │  │  • 内存描述符 (IOMemoryDescriptor)    │    │        │
│  │  │    → 管理实际的物理页                 │    │        │
│  │  │  • plane 信息 (最多 3 个 plane)       │    │        │
│  │  │    → 每个 plane 有独立的 offset,      │    │        │
│  │  │       width, height, bytesPerRow     │    │        │
│  │  └──────────────────────────────────────┘    │        │
│  │                                              │        │
│  │  IOSurface ID → 对象映射表                    │        │
│  │  (全局, 跨所有进程)                           │        │
│  └──────────────────────────────────────────────┘        │
│                                                          │
│  IOMemoryDescriptor → 物理页映射                          │
│    → 可以同时映射到多个进程的虚拟地址空间                    │
│    → GPU 通过 IOMMU 直接访问同一物理页                     │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### 2.2 IOSurface 的内存布局：plane、stride、alignment

一个 IOSurface 可以有 1~3 个 plane（平面）。以最常见的 NV12 为例：

```
NV12 IOSurface (1920×1080):

┌────────────────────────────────────────────────────┐
│  Plane 0 (Y 平面)                                  │
│  • width:           1920                           │
│  • height:          1080                           │
│  • bytesPerRow:     1920 (或更大, 对齐后)           │
│  • bytesPerElement: 1 (每个像素 1 字节)             │
│  • offset:          0                              │
│  • 总大小:    bytesPerRow × height                  │
│            ≈ 1920 × 1080 = 2,073,600 bytes         │
│                                                    │
│  Plane 1 (UV 平面)                                 │
│  • width:           960  (= 1920/2, 4:2:0 降采样)  │
│  • height:          540  (= 1080/2)                │
│  • bytesPerRow:     1920 (每行 UV 交错 = 960×2)     │
│  • bytesPerElement: 2 (每像素 UV 一对 = 2 字节)     │
│  • offset:          2,080,768 (Plane 0 之后,       │
│                      按页面对齐, 至少 4096 的倍数)   │
│  • 总大小:    bytesPerRow × height                  │
│            ≈ 1920 × 540 = 1,036,800 bytes          │
│                                                    │
│  物理内存总大小: ≈ 3.1 MB (含对齐开销)                │
└────────────────────────────────────────────────────┘
```

**关键点**：

1. **bytesPerRow ≥ width × bytesPerElement**。因为对齐要求（通常是 64 或 128 字节），`bytesPerRow` 可能比实际需要的宽。额外的字节是 padding，不存储有效像素。

2. **plane 之间按页面大小（4096 字节）对齐**。Plane 1 的 offset 是 Plane 0 结束地址向上取整到 4KB 边界。

3. **为什么分开 plane 而不是 interleaved**：GPU 纹理可以直接绑定到单个 plane，不需要额外的视图切分。Y 纹理 = Plane 0 的内存，UV 纹理 = Plane 1 的内存。

### 2.3 IOSurface 与 GPU 纹理的绑定

这是 IOSurface 最关键的能力——**GPU 驱动可以直接把 IOSurface 的物理内存地址作为纹理的 backing store**。

```
CVMetalTextureCacheCreateTextureFromImage 的内部流程:

1. 传入 CVPixelBuffer → 获取底层 IOSurfaceRef → 拿到 IOSurfaceID

2. 查询 texture cache 是否有缓存:
   - 已缓存 (同 IOSurfaceID + 同 pixelFormat + 同 planeIndex)
     → 直接返回已有的 MTLTexture 引用
   - 未缓存 → 继续

3. Metal 驱动通过 IOSurfaceID 获取:
   - IOSurface 的物理页列表（由 IOMemoryDescriptor 提供）
   - 像素格式、宽高、bytesPerRow
   - plane offset

4. 创建一个 MTLTexture 对象，但**不分配新内存**:
   - MTLTexture 的 backing store = IOSurface 的 plane 内存
   - MTLTexture 的 storageMode = MTLStorageModeShared (UMA)
   - GPU 写入 = 直接写入 IOSurface 的物理页

5. 缓存这个映射关系（下次同一个 surface + format 直接复用）
```

**Metal 实际的绑定代码**：

```objc
// ================================================================
// Metal 版: 把 CVPixelBuffer 的 IOSurface 绑定为 MTLTexture
// ================================================================

// ① 创建 CVMetalTextureCache（一次, init 时）
CVMetalTextureCacheRef _textureCache;
CVMetalTextureCacheCreate(
    kCFAllocatorDefault,
    NULL,                     // cache attributes
    _device,                  // ★ MTLDevice（不需要 current，Metal 没有全局 context）
    NULL,                     // texture attributes
    &_textureCache
);

// ② 每帧: 把 NV12 两个 plane 映射为 Metal 纹理
- (void)bindNV12PixelBufferToMetal:(CVPixelBufferRef)pixelBuffer {

    size_t width  = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    // ---- Plane 0: Y 平面 ----
    CVMetalTextureRef cvTexY = NULL;
    CVReturn retY = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        _textureCache,
        pixelBuffer,                // ★ 源 CVPixelBuffer（底层 IOSurface）
        NULL,                       // texture attributes（nil = 默认）
        MTLPixelFormatR8Unorm,      // ★ Y 平面: 单通道 8 位，和 GL 的 GL_LUMINANCE 等价
        width,                      //    纹理宽度 = 图像宽度
        height,                     //    纹理高度 = 图像高度
        0,                          // ★ planeIndex = 0（Y 平面）
        &cvTexY
    );

    if (retY == kCVReturnSuccess && cvTexY) {
        // 从 CVMetalTextureRef 里取出底层的 MTLTexture 对象
        id<MTLTexture> textureY = CVMetalTextureGetTexture(cvTexY);
        //    ↑ textureY 的 backing store = IOSurface Plane 0 物理页
        //      没有拷贝，只是创建了一个 ObjC 的"视图"对象

        // 传给 fragment shader:
        //   [encoder setFragmentTexture:textureY atIndex:0];
        //   shader 里: texture2d<float> textureY [[texture(0)]];
        //              float y = textureY.sample(sampler, uv).r;  // .r = 亮度

        CFRelease(cvTexY);  // CVMetalTextureRef 只是包装，取出 MTLTexture 后可释放
    }

    // ---- Plane 1: UV 平面 ----
    CVMetalTextureRef cvTexUV = NULL;
    CVReturn retUV = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        _textureCache,
        pixelBuffer,
        NULL,
        MTLPixelFormatRG8Unorm,     // ★ UV 平面: 双通道 8 位，和 GL 的 GL_LUMINANCE_ALPHA 等价
        width / 2,                  //    4:2:0 降采样: 宽减半
        height / 2,                 //    4:2:0 降采样: 高减半
        1,                          // ★ planeIndex = 1（UV 平面）
        &cvTexUV
    );

    if (retUV == kCVReturnSuccess && cvTexUV) {
        id<MTLTexture> textureUV = CVMetalTextureGetTexture(cvTexUV);

        // 传给 fragment shader:
        //   [encoder setFragmentTexture:textureUV atIndex:1];
        //   shader 里: texture2d<float> textureUV [[texture(1)]];
        //              float u = textureUV.sample(sampler, uv).r;  // .r = U(Cb)
        //              float v = textureUV.sample(sampler, uv).g;  // .g = V(Cr) ★注意和 GL 不同!
        //              然后 YUV→RGB 矩阵合成

        CFRelease(cvTexUV);
    }
}

// ③ 释放缓存（dealloc 时）
CVMetalTextureCacheFlush(_textureCache, kCVOptionFlags_None);
CFRelease(_textureCache);
```

**OpenGL ES 的绑定流程**（虽然 OpenGL ES 已在 iOS 上废弃，但 macOS 仍有大量 GL 代码，且面试会考两者的对应关系）：

```
CVOpenGLESTextureCacheCreateTextureFromImage 的内部流程:

1. 传入 CVPixelBuffer → 获取底层 IOSurfaceRef → 拿到 IOSurfaceID
   （和 Metal 完全一样的起点）

2. 查询 CVOpenGLESTextureCache 是否有缓存:
   - 已缓存 (同 IOSurfaceID + 同 GL_TEXTURE_2D/GL_TEXTURE_RECTANGLE + 同 plane)
     → 直接返回已有的 CVOpenGLESTextureRef
   - 未缓存 → 继续

3. OpenGL ES 驱动通过 IOSurfaceID 获取:
   - IOSurface 的物理页列表
   - 像素格式、宽高、bytesPerRow、plane offset
   - ★ 关键差异: GL 需要根据 IOSurface 的 pixel format 选择 GL 内部格式
     - NV12 Y plane (1 byte/pixel)  → GL_LUMINANCE / GL_R8
     - NV12 UV plane (2 bytes/pixel) → GL_LUMINANCE_ALPHA / GL_RG8
     - BGRA (4 bytes/pixel)          → GL_RGBA / GL_BGRA

4. 创建一个 GL 纹理对象 (glGenTextures 已在 cache 层内部完成):
   - 纹理的 backing store = IOSurface 的 plane 物理页
   - iosurface backing 保证 glTexImage2D 不会触发数据拷贝
   - 纹理类型取决于调用时指定的 target:
     · GL_TEXTURE_2D         — 普通纹理, 需要归一化坐标 (0~1)
     · GL_TEXTURE_RECTANGLE  — IOSurface 常用, 像素坐标 (0~W, 0~H)

5. 绑定到 GL 的纹理单元后，shader 里用 sampler2D / sampler2DRect 采样:
   - GPU 纹理单元通过 IOMMU 直接读 IOSurface 物理页
   - 零拷贝 ✓
```

**OpenGL ES 实际的绑定代码**（对比 Metal 版本）：

```objc
// ================================================================
// OpenGL ES 版: 把 CVPixelBuffer 的 IOSurface 绑定为 GL 纹理
// ================================================================

// ① 创建 CVOpenGLESTextureCache（一次, init 时）
CVOpenGLESTextureCacheRef _glTextureCache;
CVOpenGLESTextureCacheCreate(
    kCFAllocatorDefault,
    NULL,                     // cache attributes
    _eaglContext,             // ★ EAGLContext 当前必须 current
    NULL,                     // texture attributes
    &_glTextureCache
);

// ② 每帧: 把 NV12 两个 plane 映射为 GL 纹理
- (void)bindNV12PixelBufferToGL:(CVPixelBufferRef)pixelBuffer {

    // ---- Plane 0: Y 平面 ----
    CVOpenGLESTextureRef cvTexY = NULL;
    CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        _glTextureCache,
        pixelBuffer,
        NULL,                                    // texture attributes
        GL_TEXTURE_2D,                          // target: 普通 2D 纹理
        GL_LUMINANCE,                            // ★ internalFormat: 单通道亮度
        (GLsizei)CVPixelBufferGetWidth(pixelBuffer),          // Y 宽 = 图像宽
        (GLsizei)CVPixelBufferGetHeight(pixelBuffer),         // Y 高 = 图像高
        GL_LUMINANCE,                            // format
        GL_UNSIGNED_BYTE,                        // type
        0,                                       // ★ planeIndex = 0
        &cvTexY
    );

    GLuint textureY = CVOpenGLESTextureGetName(cvTexY);

    // 绑定到纹理单元 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureY);
    // 设置采样参数（IOSurface 纹理边缘 sample 不会越界，用 clamp）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


    // ---- Plane 1: UV 平面 ----
    CVOpenGLESTextureRef cvTexUV = NULL;
    CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        _glTextureCache,
        pixelBuffer,
        NULL,
        GL_TEXTURE_2D,
        GL_LUMINANCE_ALPHA,                      // ★ internalFormat: 双通道 (U,V)
        (GLsizei)CVPixelBufferGetWidth(pixelBuffer) / 2,    // UV 宽 = 图像宽/2
        (GLsizei)CVPixelBufferGetHeight(pixelBuffer) / 2,   // UV 高 = 图像高/2
        GL_LUMINANCE_ALPHA,
        GL_UNSIGNED_BYTE,
        1,                                       // ★ planeIndex = 1
        &cvTexUV
    );

    GLuint textureUV = CVOpenGLESTextureGetName(cvTexUV);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureUV);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // shader 里:
    //   uniform sampler2D textureY;   // Y  = texture2D(textureY, uv).r
    //   uniform sampler2D textureUV;  // U  = texture2D(textureUV, uv).r
    //                                  // V  = texture2D(textureUV, uv).a
    //   然后用 YUV→RGB 矩阵合成

    // 用完后释放 CVOpenGLESTextureRef（底层 IOSurface 不受影响）
    CFRelease(cvTexY);
    CFRelease(cvTexUV);
}

// ③ 释放缓存（dealloc 时）
CVOpenGLESTextureCacheFlush(_glTextureCache, 0);
CFRelease(_glTextureCache);
```

**Metal 与 OpenGL ES 绑定的关键差异**：

| 维度 | Metal (`CVMetalTextureCache`) | OpenGL ES (`CVOpenGLESTextureCache`) |
|------|------------------------------|--------------------------------------|
| 缓存对象 | `CVMetalTextureCacheRef`（绑定 `MTLDevice`） | `CVOpenGLESTextureCacheRef`（绑定 `EAGLContext`） |
| 纹理对象 | `MTLTexture`，通过 `CVMetalTextureGetTexture` 取 | `GLuint`，通过 `CVOpenGLESTextureGetName` 取 |
| Y 平面 internalFormat | `MTLPixelFormatR8Unorm` | `GL_LUMINANCE`（老）/ `GL_R8`（ES 3.0+） |
| UV 平面 internalFormat | `MTLPixelFormatRG8Unorm` | `GL_LUMINANCE_ALPHA`（老）/ `GL_RG8`（ES 3.0+） |
| shader 中 UV 的 .g 分量 | `.g` = V（Cr） | `.a` = V（Cr）← **注意! 不一样!** |
| 纹理坐标 | 归一化 0~1（TEXTURE_2D 都支持，RECTANGLE 用像素坐标） | 归一化 0~1（GL_TEXTURE_2D）/ 像素坐标（GL_TEXTURE_RECTANGLE） |
| Context 要求 | 创建时 `MTLDevice` 不需要是 current（Metal 没有全局 context 概念） | 创建时 `EAGLContext` **必须 current**（GL 的全局状态机特性） |
| 生命周期 | ARC 可管理（ObjC 对象） | `CFRelease` 手动管理（C 对象，类似 IOSurface） |

> **面试注意**：如果在面试中被问到 GL 版本，一定要补充"OpenGL ES 已在 iOS 12+ 废弃，但 macOS 的 OpenGL 仍然支持，且很多老旧代码库还在用 CVOpenGLESTextureCache。Metal 是 Apple 平台的未来。"

### 2.4 IOSurface 的 ID 机制：为什么是 32 位

```
IOSurfaceID 的结构 (非公开, 根据逆向和头文件推断):

  bit 31      bit 0
  ┌────────────┬──────────────────────────────────┐
  │ 标志/类型?  │ 内核分配的序号                      │
  └────────────┴──────────────────────────────────┘

特性:
- 全局唯一: 整个系统 (所有进程) 只有一个 IOSurfaceID 命名空间
- 单调递增: 每次创建新 surface, ID 递增 (类似 Linux PID)
- 32 位: 理论上可创建 40 亿个, 实际循环使用
- 不会被回收后立即复用: 防止 ABA 问题
```

**为什么其他框架都接受 IOSurfaceID**：

VideoToolbox、CoreML、Metal、AVFoundation 内部的 IPC 传递，传的往往就是一个 IOSurfaceID 整数和 pixel format 等少量元信息。接收方自己调 `IOSurfaceLookup` 拿到内存引用。这就是为什么 iOS 的媒体管线能做到"零拷贝"——每个环节看到的都是同一个 IOSurfaceID。

---

## 三、编程实战：直接操作 IOSurface

绝大多数情况下你通过 CVPixelBuffer 间接使用 IOSurface。但了解原生 API 对理解底层机制至关重要。

### 3.1 创建和基本操作

```objc
#import <IOSurface/IOSurface.h>
#import <CoreVideo/CoreVideo.h>

// ================================================================
// 1. 创建一个 IOSurface
// ================================================================
- (IOSurfaceRef)createBGRASurface:(int)width height:(int)height {
    // IOSurface 的属性用 CFDictionary 传递
    NSDictionary *props = @{
        // ---- 尺寸 ----
        (id)kIOSurfaceWidth:            @(width),
        (id)kIOSurfaceHeight:           @(height),

        // ---- 像素格式 ----
        // kCVPixelFormatType_32BGRA = 'BGRA' (4 字节, B G R A 顺序)
        (id)kIOSurfacePixelFormat:      @(kCVPixelFormatType_32BGRA),

        // ---- 每像素字节数 ----
        (id)kIOSurfaceBytesPerElement:  @4,       // BGRA = 4 字节

        // ---- 每行字节数 (需 ≥ width × bytesPerElement, 且满足对齐) ----
        (id)kIOSurfaceBytesPerRow:      @(width * 4),  // 1920×4=7680, 64字节对齐 ✓

        // ---- 对齐 ----
        (id)kIOSurfaceElementWidth:     @(width),   // 实际像素宽度
        (id)kIOSurfaceElementHeight:    @(height),  // 实际像素高度

        // ---- 跨进程共享 ----
        (id)kIOSurfaceIsGlobal:         @YES,       // ★ 设为 YES 才能被 Lookup

        // ---- 内存分配策略 (可选) ----
        // IOSurfaceMemoryManager 会自动处理
    };

    IOSurfaceRef surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!surface) {
        NSLog(@"❌ IOSurfaceCreate failed");
        return NULL;
    }

    // 拿到全局 ID（传给其他进程用）
    uint32_t surfaceID = IOSurfaceGetID(surface);
    NSLog(@"✅ Created IOSurface with ID: %u", surfaceID);

    // 验证属性
    NSLog(@"   width=%zu, height=%zu, bytesPerRow=%zu",
          IOSurfaceGetWidth(surface),
          IOSurfaceGetHeight(surface),
          IOSurfaceGetBytesPerRow(surface));
    // 注意: bytesPerRow 可能不等于 width×4，系统会根据对齐需要向上调整

    return surface;  // 调用方负责 CFRelease
}

// ================================================================
// 2. CPU 读写 IOSurface
// ================================================================
- (void)writePixelsToSurface:(IOSurfaceRef)surface {
    // ① 获取基础地址
    void *baseAddr = IOSurfaceGetBaseAddress(surface);
    //    ↑ 这个调用内部会:
    //      1. 如果 IOSurface 的物理内存还没分配 → 现在分配
    //      2. 把物理页映射到当前进程的虚拟地址空间
    //      3. 返回虚拟地址指针
    //      注意: 返回的地址在 surface 生命周期内保持不变
    //      但如果你期望"每次 Lock 才映射"的行为,
    //      需要用 IOSurfaceGetBaseAddressOfPlane + 手动同步

    size_t width       = IOSurfaceGetWidth(surface);
    size_t height      = IOSurfaceGetHeight(surface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(surface);

    // ② 写入像素（示例：红色）
    //    注意必须用 bytesPerRow 做行步进，不能用 width × 4!
    //    因为可能有 padding 字节。
    uint8_t *row = (uint8_t *)baseAddr;
    for (size_t y = 0; y < height; y++) {
        uint8_t *pixel = row;
        for (size_t x = 0; x < width; x++) {
            pixel[0] = 0xFF;  // B
            pixel[1] = 0x00;  // G
            pixel[2] = 0x00;  // R
            pixel[3] = 0xFF;  // A
            pixel += 4;
        }
        row += bytesPerRow;   // ★ 用 bytesPerRow，不是 width*4!
    }

    // ③ 确保 CPU 写入对 GPU 可见（UMA 架构下通常不需要，但写一下最安全）
    //    iOS 上没有 IOSurfaceFlush (macOS 私有 API)
    //    在 iOS 上，UMA 架构下 CPU 写入自动对 GPU 可见
    //    如果是 GPU 写入后 CPU 要读：
    //      需要 glFlush / MTLCommandBuffer.waitUntilCompleted 等同步
}

// ================================================================
// 3. 从另一个 IOSurfaceID 查找并读取
// ================================================================
- (void)readFromSurfaceID:(uint32_t)surfaceID {
    IOSurfaceRef surface = IOSurfaceLookup(surfaceID);
    //                          ↑ 内核通过 ID 查找 IOSurface 对象
    //                            如果找到 → retain + 映射到当前进程 → 返回引用
    //                            如果找不到 → 返回 NULL
    //                            （可能已经被释放，或者根本没创建）

    if (!surface) {
        NSLog(@"❌ Surface ID %u not found", surfaceID);
        return;
    }

    // 读像素
    void *baseAddr = IOSurfaceGetBaseAddress(surface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(surface);
    // ... 读写 ...

    // ★ 必须释放！IOSurfaceLookup 会 retain
    CFRelease(surface);
}

// ================================================================
// 4. 用 IOSurface 构造 CVPixelBuffer（桥接到上层框架）
// ================================================================
- (CVPixelBufferRef)wrapSurfaceInPixelBuffer:(IOSurfaceRef)surface {
    // 这种创建方式下，CVPixelBuffer 不拥有底层内存
    // 你需要保证 IOSurface 在 CVPixelBuffer 使用期间不被释放

    CVPixelBufferRef pixelBuffer = NULL;
    CVReturn ret = CVPixelBufferCreateWithIOSurface(
        kCFAllocatorDefault,
        surface,          // ★ 直接传入 IOSurface
        NULL,             // attributes（可以加 MetalCompatibility 等）
        &pixelBuffer
    );
    if (ret != kCVReturnSuccess) {
        NSLog(@"❌ CVPixelBufferCreateWithIOSurface failed: %d", ret);
        return NULL;
    }
    return pixelBuffer;
}
```

### 3.2 跨进程传递（macOS 示例）

iOS 上第三方 App 无法直接使用 XPC 传递 IOSurfaceID（沙盒限制），但 macOS 上可以。此处展示原理：

```objc
// ================================================================
// 发送方 (Server)
// ================================================================

// 1. 创建全局 IOSurface
IOSurfaceRef surface = [self createBGRASurface:1920 height:1080];
uint32_t surfaceID = IOSurfaceGetID(surface);

// 2. 通过 XPC 传递 ID（一个 32 位整数）
//    xpc_dictionary_set_uint64(message, "surfaceID", surfaceID);

// 3. 发送方保持 IOSurface 的引用，直到接收方确认收到
//    (否则 IOSurface 可能在传递过程中被释放)


// ================================================================
// 接收方 (Client)
// ================================================================

// 1. 从 XPC 消息中取出 surfaceID
//    uint32_t surfaceID = xpc_dictionary_get_uint64(message, "surfaceID");

// 2. 通过 ID 查找并映射
IOSurfaceRef surface = IOSurfaceLookup(surfaceID);
if (surface) {
    // 3. 用 CVMetalTextureCache 包装为 Metal 纹理，直接渲染
    // 或者 wrap 成 CVPixelBuffer 喂给 VideoToolbox
    // 全程零拷贝

    // 4. 用完后释放
    CFRelease(surface);
}
```

### 3.3 NV12 IOSurface 的 plane 操作

```objc
// ================================================================
// 创建 NV12 IOSurface
// ================================================================
- (IOSurfaceRef)createNV12Surface:(int)width height:(int)height {
    NSDictionary *props = @{
        (id)kIOSurfaceWidth:             @(width),
        (id)kIOSurfaceHeight:            @(height),
        (id)kIOSurfacePixelFormat:       @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange), // NV12
        (id)kIOSurfaceIsGlobal:          @YES,

        // NV12 不需要显式设 BytesPerElement 和 BytesPerRow
        // 系统会根据 pixel format 自动计算两个 plane 的布局
    };
    return IOSurfaceCreate((__bridge CFDictionaryRef)props);
}

// ================================================================
// 分别操作 Y 和 UV plane
// ================================================================
- (void)processNV12Planes:(IOSurfaceRef)surface {
    // 获取 plane 数量（NV12 = 2）
    size_t planeCount = IOSurfaceGetPlaneCount(surface);
    NSLog(@"Plane count: %zu", planeCount);  // 输出: 2

    // ---- Plane 0: Y ----
    void *yBase = IOSurfaceGetBaseAddressOfPlane(surface, 0);
    size_t yWidth       = IOSurfaceGetWidthOfPlane(surface, 0);       // = width
    size_t yHeight      = IOSurfaceGetHeightOfPlane(surface, 0);      // = height
    size_t yBytesPerRow = IOSurfaceGetBytesPerRowOfPlane(surface, 0); // ≥ width
    size_t yOffset      = IOSurfaceGetPlaneOffset(surface, 0);        // 0

    // ---- Plane 1: UV ----
    void *uvBase = IOSurfaceGetBaseAddressOfPlane(surface, 1);
    size_t uvWidth       = IOSurfaceGetWidthOfPlane(surface, 1);       // = width / 2
    size_t uvHeight      = IOSurfaceGetHeightOfPlane(surface, 1);      // = height / 2
    size_t uvBytesPerRow = IOSurfaceGetBytesPerRowOfPlane(surface, 1); // ≥ width
    size_t uvOffset      = IOSurfaceGetPlaneOffset(surface, 1);        // Plane 0 结束后的对齐偏移

    // 验证: 两块内存是否物理连续
    // baseAddr + offset 应该指向同一个 IOSurface 分配的不同区域
    void *baseAddr = IOSurfaceGetBaseAddress(surface);  // = plane 0 的基地址
    assert((uint8_t *)baseAddr + yOffset == (uint8_t *)yBase);
    assert((uint8_t *)baseAddr + uvOffset == (uint8_t *)uvBase);
}
```

---

## 四、IOSurface 全链路追踪：一帧画面的旅程

用一个具体例子追踪 IOSurface 在一帧画面处理中的完整生命周期：

```
═══════════════════════════════════════════════════════════════════
 阶段 1: 摄像头采集
═══════════════════════════════════════════════════════════════════

mediaserverd (系统摄像头 daemon):
  │
  │ IOSurfaceCreate(props: NV12, 1920×1080)
  │   → 内核创建 IOSurface 对象, 分配 surfaceID = 12345
  │   → 物理内存尚未分配 (lazy allocation)
  │
  │ 摄像头硬件 → DMA 写入 IOSurface 物理内存
  │   → 第一次写入时触发物理页分配
  │   → 数据是 tiled NV12 (GPU 原生格式)
  │
  │ 把 surfaceID = 12345 封在 CMSampleBuffer 里,
  │ 通过 mach_msg 发给你 App 的 AVCaptureVideoDataOutput 回调


═══════════════════════════════════════════════════════════════════
 阶段 2: App 收到帧
═══════════════════════════════════════════════════════════════════

你的 App (AVCaptureVideoDataOutput 回调):
  │
  │ CMSampleBuffer → CVPixelBuffer
  │   → CVPixelBuffer 内部 IOSurfaceID = 12345
  │   → 物理内存: 还是 daemon 分配的那块，零拷贝!
  │
  │ 此时 surface 的引用计数 ≥ 2:
  │   ref 1: mediaserverd 持有 (待回收)
  │   ref 2: 你的 CVPixelBuffer retain 持有


═══════════════════════════════════════════════════════════════════
 阶段 3: 渲染预览 (Metal)
═══════════════════════════════════════════════════════════════════

  │ CVMetalTextureCacheCreateTextureFromImage(pixelBuffer, ...)
  │   → Metal 驱动查: IOSurfaceID = 12345
  │   → 给 Plane 0 创建 MTLTexture (R8Unorm, 1920×1080)
  │     backing store = IOSurface Plane 0 的物理页
  │   → 给 Plane 1 创建 MTLTexture (RG8Unorm, 960×540)
  │     backing store = IOSurface Plane 1 的物理页
  │
  │ Metal shader 采样 Y 纹理:
  │   → GPU 纹理单元通过 IOMMU 直接访问 IOSurface 物理页
  │   → tiled 格式, 纹理缓存高效
  │   → 零拷贝 ✓
  │
  │ 渲染到 MTKView.drawable
  │   → drawable.texture 也是 IOSurface (CAMetalLayer 管理)
  │   → commit → 屏幕


═══════════════════════════════════════════════════════════════════
 阶段 4: 编码 (VideoToolbox)
═══════════════════════════════════════════════════════════════════

  │ VTCompressionSessionEncodeFrame(pixelBuffer, ...)
  │   → VideoToolbox 获取 IOSurfaceID = 12345
  │   → 编码器硬件 (ISP/ANE/Video Encoder) 通过 IOMMU 直接读物理页
  │   → tiled NV12 → 硬件编码器原生支持
  │   → 零拷贝 ✓
  │
  │ 编码器输出 → 新的 CVPixelBuffer (压缩后数据, CMBlockBuffer)
  │   注意: 输出不再是 IOSurface (压缩数据不需要 pixel layout)


═══════════════════════════════════════════════════════════════════
 阶段 5: 释放
═══════════════════════════════════════════════════════════════════

  │ 编码和渲染都完成后:
  │   CVPixelBuffer CFRelease → 内部 IOSurface CFRelease
  │   mediaserverd 也释放了自己的引用
  │   → IOSurface 引用计数归零
  │   → 内核释放物理页
  │   → surfaceID = 12345 标记为可用 (可能被后续创建复用)
```

---

## 五、常见坑与调试

### 坑 1：以为拿到了 IOSurface 就可以不 Lock，结果读写错乱

```objc
// ❌ 错误: 多线程同时访问同一个 IOSurface 但没同步
// 线程 A: Metal 正在渲染到这个 surface
// 线程 B: 直接 IOSurfaceGetBaseAddress 然后 memset
// → GPU 和 CPU 同时写同一块内存 → 画面撕裂、花屏

// ✅ 正确: 用信号量/锁协调访问
// 或者: CPU 只写、GPU 只读（单向数据流）
// 或者: 使用 IOSurface 的读写锁机制（macOS 上的 IOSurfaceLock / IOSurfaceUnlock）
```

### 坑 2：bytesPerRow ≠ width × bytesPerElement

```objc
// ❌ 常见错误: 假设每行刚好 width × bytesPerElement 字节
void *base = IOSurfaceGetBaseAddress(surface);
for (int y = 0; y < height; y++) {
    processRow(base + y * width * 4);  // ← 如果 bytesPerRow > width*4，错位!
}

// ✅ 正确: 始终用 bytesPerRow
size_t bytesPerRow = IOSurfaceGetBytesPerRow(surface);
for (int y = 0; y < height; y++) {
    processRow((uint8_t *)base + y * bytesPerRow);
}
```

### 坑 3：跨进程时 IOSurface 被提前释放

```objc
// 发送方:
uint32_t sid = IOSurfaceGetID(surface);
sendToClient(sid);
CFRelease(surface);  // ← 如果客户端还没 Lookup，IOSurface 就没了!

// ✅ 正确做法: 发送方保持引用直到客户端确认收到
uint32_t sid = IOSurfaceGetID(surface);
sendToClient(sid);
// 等待客户端的 ACK (或使用 XPC 的 reply handler)
waitForAck();
CFRelease(surface);
```

### 坑 4：误以为 LockBaseAddress 后 IOSurface 会自动同步

```objc
// 整个调用链中:
// 1. GPU 渲染到 IOSurface (通过 CVMetalTextureCache alias)
// 2. [commandBuffer commit] → GPU 异步执行
// 3. 立刻 CPU Lock → 可能读到旧数据（GPU 还没写完）

// ✅ 修复: commit 后等待 GPU 完成
[commandBuffer waitUntilCompleted];
// 或者:
[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buf) {
    // 现在 CPU 可以安全地 Lock + 读
}];
```

### 坑 5：创建 IOSurface 时没设 kIOSurfaceIsGlobal

```objc
// ❌ 默认创建的 IOSurface 不是全局的
IOSurfaceRef surface = IOSurfaceCreate(props);
uint32_t sid = IOSurfaceGetID(surface);  // 能拿到 ID
// 但另一个进程 IOSurfaceLookup(sid) → NULL!

// ✅ 必须显式设为全局
NSDictionary *props = @{
    // ... 其他属性 ...
    (id)kIOSurfaceIsGlobal: @YES,  // ★ 别忘了这个!
};
```

---

## 六、与 Android 对应机制对比

| 维度 | iOS/macOS (IOSurface) | Android (GraphicBuffer / AHardwareBuffer) |
|------|----------------------|-------------------------------------------|
| 内核对象 | `IOSurface` (IOKit, Mach 内核对象) | `GraphicBuffer` (HAL 层, gralloc) / `AHardwareBuffer` |
| 全局 ID | `IOSurfaceID` (32 位, 直接可用) | 无统一全局 ID, 通过 `AHardwareBuffer` 序列化传递 |
| 跨进程共享 | `IOSurfaceLookup(id)` 直接映射 | 需通过 Binder + `AHardwareBuffer` 序列化, 更重 |
| GPU 纹理绑定 | `CVMetalTextureCache` / `CVOpenGLESTextureCache` | `EGLImageKHR` + `OES_EGL_image_external` (GLES) / `VK_ANDROID_external_memory` (Vulkan) |
| Pixel format 感知 | ✅ 内置, 所有框架通用 | ⚠️ 依赖 HAL 实现, 不同厂商行为可能有差异 |
| CPU 映射 | `IOSurfaceGetBaseAddress` (持久映射) | `AHardwareBuffer_lock` (需显式 lock/unlock) |
| Tiled 格式 | Apple 统一 (Apple Silicon 驱动), 行为可预测 | 各厂商不同 (Qualcomm Adreno / ARM Mali / ... ), 碎片化严重 |
| 生命周期 | 引用计数, 内核管理 | `AHardwareBuffer` 引用计数, 但 GraphicBuffer 可能受 HAL 管理 |
| 调试工具 | `IOSurface` CLI 命令(macOS) / Instruments | `dumpsys SurfaceFlinger` / systrace / RenderDoc |

---

## 七、一句话总结

> IOSurface 是 Apple 平台音视频零拷贝的物理基础——一块内核管理的、CPU 和 GPU 均可直接访问的共享内存。它通过全局 32 位 ID 实现跨进程零拷贝，通过 tiled 内存布局优化 GPU 纹理缓存效率，通过 plane 机制支持 YUV 多平面格式。CoreVideo、Metal、VideoToolbox、CoreML——整个媒体栈的"零拷贝"本质上都是同一块 IOSurface 在不同框架手中传递。

---

## 八、自检清单

- IOSurface 和 CVPixelBuffer 的层级关系是什么？为什么通常不直接操作 IOSurface？
- IOSurfaceID 是全局唯一的吗？怎么跨进程传递和查找？
- `IOSurfaceGetID` 和 `IOSurfaceLookup` 对引用计数的影响有什么不同？
- 为什么 IOSurface 使用 tiled 内存布局而不是 linear？这和 GPU 纹理缓存有什么关系？
- NV12 IOSurface 有几个 plane？每个 plane 的宽高和 bytesPerRow 怎么计算？
- 为什么 GPU 写入 IOSurface 后，CPU 立刻读可能读到旧数据？怎么解决？
- 创建 IOSurface 时忘记设 `kIOSurfaceIsGlobal: @YES` 会有什么后果？
- `bytesPerRow` 为什么可能不等于 `width × bytesPerElement`？遍历像素时该用哪个？
- iOS 为什么禁用了 `IOSurfaceIncrementUseCount`？这给跨进程场景带来什么挑战？
- IOSurface 的内存是创建时分配还是首次访问时分配？
