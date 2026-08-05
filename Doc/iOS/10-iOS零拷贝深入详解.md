# iOS 零拷贝深入详解（面试向）

## 0. 本篇定位

- 面试复习：先掌握 `CVPixelBuffer`、`IOSurface`、`CVMetalTextureCache`、`LockBaseAddress` 的边界，以及什么情况会破坏零拷贝。
- 深入学习：重点看 NV12 plane 映射、纹理缓存生命周期、颜色空间、跨 Android 对比和零拷贝验证方法。
- 工程落点：这篇偏实战链路，适合回答“采集/解码帧如何不经过 CPU 进入 Metal、滤镜、编码或显示”。
> 适用方向：iOS 音视频 SDK 开发、渲染/编解码/推流链路优化
> 前置：了解 CVPixelBuffer、Metal/OpenGL 基础概念
> 难度：⭐⭐⭐⭐⭐
> 关联：[[04-Metal渲染与零拷贝详解]] · [[../ffmpeg/15-iOS硬件编解码]] · [[01-AVFoundation采集详解]]

---

## 一、面试问答（口语化，可直接背）

### Q1：说说你对 iOS 零拷贝的理解？

**面试官意图**：考察你是否真正理解"零拷贝"到底零了什么，而不是背名词。

**话术**：

> "iOS 上的零拷贝，核心就是一句话：**让数据从头到尾待在 GPU 显存里，CPU 永远不参与像素搬运。**"
>
> "具体来说，iOS 的摄像头采集、VideoToolbox 编解码产出的都是 CVPixelBuffer。这个 CVPixelBuffer 底层不是普通的 malloc 内存——它是一块 IOSurface，由内核管理，CPU 和 GPU 都能直接通过总线访问。"
>
> "零拷贝的关键 API 是 **CVMetalTextureCache**（Metal 侧）或者 **CVOpenGLESTextureCache**（OpenGL ES 侧）。这两个 API 做的事情一样：不分配新内存、不 memcpy，而是直接给 IOSurface 在 GPU 侧建一个'纹理视图'——GPU 往这个纹理上读写，就等于在操作原来的 IOSurface。"
>
> "举个例子：摄像头吐出一帧 NV12 的 CVPixelBuffer → 我用 CVMetalTextureCacheCreateTextureFromImage 把它的 Y 平面映射为一张 R8Unorm 的 MTLTexture，UV 平面映射为 RG8Unorm → 在 Metal shader 里做完 YUV→RGB 直接渲到 MTKView。整个过程没有一次 memcpy，没有一次 CVPixelBufferLockBaseAddress。这就是零拷贝。"
>
> "打破零拷贝的操作有哪些？第一，CVPixelBufferLockBaseAddress + memcpy——CPU 强行读 GPU 数据。第二，创建 CVPixelBufferPool 时漏了 IOSurfacePropertiesKey——底层走的普通内存。第三，采集时设了非 NV12 的格式——系统内部做格式转换，那也是拷贝。"

**👨‍💻 追问 1：UMA（统一内存）架构下，GPU 和 CPU 共享物理内存，为什么 LockBaseAddress 还有开销？**

> "这是个好问题。虽然 M 系列芯片和 A 系列芯片都是 UMA 架构，CPU 和 GPU 确实共享同一块物理 LPDDR 内存——但 GPU 纹理在这块内存里的**存储格式**和 CPU 看到的**线性地址**不一样。"
>
> "GPU 为了纹理缓存的局部性，会把纹理以 **tiled / swizzle** 格式存储——相邻的像素在内存里不是连续排列的，而是按 16×16 或 32×32 的 block 打散的。CPU 不认识这种格式，它只能按线性地址逐行读。所以 LockBaseAddress 背后，硬件必须做一个叫 **detile** 的操作——把 tiled 格式实时转成线性格式——这个操作有硬件加速，但仍然需要同步等待，1080p 一帧大约 1-3ms。"
>
> "CVMetalTextureCache 就完全不需要 detile——因为 GPU 原生就理解 tiled 格式，它可以直接采样。这就是 UMA 下零拷贝依然有意义的根本原因。"

**👨‍💻 追问 2：你说 IOSurface 是'内核管理的共享内存'，具体是什么机制？**

> "IOSurface 是 IOKit 框架里的一个内核对象。它的核心是一个 `IOSurfaceID`——一个全局唯一的 32 位整数。任何进程、任何框架（CoreVideo、Metal、OpenGL、VideoToolbox），只要知道这个 ID，就能通过 IOSurfaceLookup 拿到同一块内存的引用。"
>
> "这就是 iOS 上跨进程零拷贝的基础。比如：摄像头 daemon（mediaserverd）采集的帧 → 通过 IOSurface ID 传给你的 App → 你的 App 拿到 CVPixelBuffer → 再通过同样的 IOSurface ID 传给 VideoToolbox 编码器。全程是一块内存在不同组件之间'换手'，像素数据一次都没搬过。"

---

### Q2：CVPixelBufferPool 怎么创建才能保证零拷贝？漏了哪些 key 会断？

**面试官意图**：考察你有没有实际踩过坑，知不知道每个 key 的作用。

**话术**：

> "创建零拷贝的 CVPixelBufferPool，**两个 key 是必须的，少一个零拷贝就断了**。"
>
> "第一个：`kCVPixelBufferIOSurfacePropertiesKey: @{}`。这个 key 指定 CVPixelBuffer 底层用 IOSurface 而不是普通的 CPU malloc 内存。不加的话，buffer 就是纯 CPU 内存，CVTextureCache 根本没法 alias。"
>
> "第二个：`kCVPixelBufferMetalCompatibilityKey: @YES`（Metal 场景）或者 `kCVPixelBufferOpenGLCompatibilityKey: @YES`（OpenGL 场景）。这个 key 确保 IOSurface 的像素格式和内存对齐方式兼容 GPU 纹理。不加的话，CVMetalTextureCacheCreateTextureFromImage 会返回 -6660（kCVReturnInvalidArgument）之类的错误。"
>
> "还有一个非必须但强烈推荐的：`kCVPixelBufferBytesPerRowAlignmentKey: @64`。Metal 要求纹理的每行字节数 64 字节对齐。不对齐的话可能触发内部拷贝。"

> （这里深挖一下——面试官很可能会追问"为什么 64 字节？不对齐到底发生了什么？"）
> 
> **什么是"64 字节对齐"**：拿 1080p 的 BGRA 来说，width=1920，每像素 4 字节，一行原始数据 = 1920×4 = **7680 字节**。7680 ÷ 64 = 120，正好是 64 的整数倍——这种情况天然对齐，不需要任何 padding。但如果 width=1921，一行 = 1921×4 = 7684 字节，7684 ÷ 64 = 120.0625，**不是整数倍**。系统就会在每行末尾自动补 60 个字节的 padding（7684 → 7744 = 64×121），让每行起始地址都在 64 字节边界上。这就是 `bytesPerRow` 可能大于 `width × bytesPerElement` 的原因。
> 
> **为什么 Metal 要求 64 字节对齐**：Metal 的纹理数据在 GPU 端是按 **block/tile** 方式组织的。GPU 的纹理单元（Texture Unit）每次从显存 fetch 数据时，是按 cache line 粒度（通常是 64 或 128 字节）一整块读上来的。如果每行起始地址不是 64 字节对齐：
> - GPU 内部要额外做一次内存重排（把不对齐的数据 shuffle 成对齐的 block）
> - 这会触发 Metal 驱动的 **hidden copy**——在你的 CVPixelBuffer 之外，驱动偷偷 malloc 一块对齐的内存，把数据拷过去，再上传到 GPU。IOMMU 的页表映射也要求 4KB 对齐，但 64 字节是 Metal 纹理 cache line 的更细粒度约束
> - 这个隐藏拷贝和你显式调用 LockBaseAddress 一样，打破了零拷贝
> 
> **怎么验证**：建一个 1921 宽的 buffer，不加 64 对齐 key → `CVPixelBufferGetBytesPerRow` 返回 7684 → 用 Metal System Trace 抓一下 → 能看到 driver internal texture upload 的耗时。同样的 buffer 加上 64 对齐 key → `bytesPerRow` 变成 7744 → trace 里没有 upload。多出来的 60 字节 padding 换来了 GPU 直接 alias 的能力——完全不亏。
> 
> **什么人可以忽略**：如果你的宽高是 16/32/64 的整数倍（绝大多数视频分辨率：1920、1280、3840 都是），BGRA/NV12 下 bytesPerRow 天然就是 64 对齐的，这个 key 加了也不起作用（但加了无害）。如果你的场景里有非标准分辨率（比如从某个 SDK 吐出来一个奇怪尺寸），就一定要加。
>
> "完整的正确创建代码是："

```objc
NSDictionary *attrs = @{
    // ★ 必须 1: IOSurface 后端
    (id)kCVPixelBufferIOSurfacePropertiesKey: @{},

    // ★ 必须 2: Metal 兼容
    (id)kCVPixelBufferMetalCompatibilityKey: @YES,

    // 推荐: 64 字节对齐（Metal 要求）
    (id)kCVPixelBufferBytesPerRowAlignmentKey: @(64),

    // 其他常规配置
    (id)kCVPixelBufferWidthKey: @(1920),
    (id)kCVPixelBufferHeightKey: @(1080),
    (id)kCVPixelBufferPixelFormatTypeKey:
        @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange), // NV12
    (id)kCVPixelBufferMinimumBufferCountKey: @(6),
};

CVPixelBufferPoolRef pool = NULL;
CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL,
                        (__bridge CFDictionaryRef)attrs, &pool);
```

**👨‍💻 追问：怎么验证当前拿到的一个 CVPixelBuffer 是不是 IOSurface 后端？**

> "一行代码：`CVPixelBufferGetIOSurface(pixelBuffer)`。返回非 NULL 就是 IOSurface 后端，返回 NULL 就是普通 CPU 内存。另外用 Instruments 的 Allocations 抓一下有没有 3MB 左右的 malloc/memcpy——1080p NV12 的 Y 平面约 2MB、UV 平面约 1MB——如果有就说明有人在 CPU 上拷像素。"

---

### Q3：NV12 的 Y 平面和 UV 平面分别怎么映射到 Metal 纹理？为什么是 R8Unorm 和 RG8Unorm？

**面试官意图**：考察你对像素格式和 GPU 纹理映射的理解是否精确。

**话术**：

> "NV12 是 BiPlanar 格式，总共两个平面。用 CVMetalTextureCacheCreateTextureFromImage 要调两次，一次映射一个平面。"
>
> "**Y 平面**：planeIndex=0，每个像素一个字节，存的是亮度值（0~255）。对应 Metal 的 `MTLPixelFormatR8Unorm`——单通道、8 位无符号归一化。纹理的宽高 = 图像的原始宽高。"
>
> "**UV 平面**：planeIndex=1，每两个字节一对 UV，U 和 V 交错存储。对应 Metal 的 `MTLPixelFormatRG8Unorm`——双通道、每通道 8 位。纹理的宽 = 图像宽度/2，高 = 图像高度/2——因为 NV12 是 4:2:0 色度降采样，每 2×2 个 Y 像素共享一对 UV。"

```
Y 平面:  W × H      字节，MTLPixelFormatR8Unorm，  planeIndex=0
UV 平面: (W/2)×(H/2)×2 字节，MTLPixelFormatRG8Unorm，planeIndex=1
```

> "为什么不用一张纹理？Metal 没有原生的 NV12 多平面格式（不像 Vulkan 有 `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`），所以只能分成两张。shader 里分别声明两个 `texture2d<float, access::sample>`，各自采样后再用矩阵合成 RGB。"

**👨‍💻 追问：如果用错了 pixel format，比如把 UV 也映射成 R8Unorm，会怎么样？**

> "UV 平面每像素是 2 字节（U 和 V 交错），如果用 R8Unorm 映射——它只读一个字节——那就只能读到 U 的值，V 全被当成了下一个像素的 U。shader 里做 YUV→RGB 转换时，色度信息完全错乱。画面表现就是颜色诡异——偏绿、偏紫、饱和度完全不对。而且 `CVMetalTextureCacheCreateTextureFromImage` 可能会直接返回错误（比如 stride 对不上），也可能在某些驱动上'勉强成功'但采样结果错。"

**👨‍💻 追问 2：为什么 UV 纹理用 RG8Unorm 而不是其他格式？**

> "因为 UV 是两个独立的值（Cb 和 Cr），天然对应 RG 两个通道。R8Unorm 只有一个通道承载不了两个值。RGBA8Unorm 有四个通道，多了两个浪费，而且可能让驱动做额外对齐处理。RG8Unorm 是最精确的映射：两个 8 位通道、交错排列、大小刚好。"

---

### Q4：LockBaseAddress 和 CVMetalTextureCache 分别适用于什么场景？为什么实时渲染不能用 LockBaseAddress？

**面试官意图**：考察你是否理解 CPU/GPU 两条通路的区别和各自适用场景。

**话术**：

> "这两者是完全不同的数据通路。"
>
> "**CVMetalTextureCache 是 GPU 通路**——在 GPU 侧给 IOSurface 建纹理视图，GPU 直接采样。整个过程 CPU 不参与，没有 detile 开销，没有 memcpy。耗时 < 0.1ms。适用于**所有实时视频链路**：渲染预览、编码输入、滤镜处理。"
>
> "**LockBaseAddress 是 CPU 通路**——把 GPU 的 tiled 内存映射成 CPU 可读的线性地址。涉及三个开销：第一，detile——把 tiled/swizzle 存储转成线性排列；第二，同步——CPU 必须等 GPU 当前所有操作完成（隐式的 GPU flush）；第三，如果是非 UMA 架构（如 Intel Mac），还要过一次 PCIe 总线。"
>
> "1080p NV12 的 LockBaseAddress 一趟下来：
> - UMA（M 系列/A 系列）：约 1-3ms（只有 detile 开销）
> - 非 UMA（Intel Mac）：约 3-8ms（detile + PCIe + GPU stall）
> - 30fps 下累计：30~240ms/s 被浪费"


场景速查表:

| 场景 | 应该用什么 | 原因 |
|------|-----------|------|
| 摄像头预览渲染 | CVMetalTextureCache | 每帧 16.7ms 窗口，Lock 一下就超了 |
| 视频滤镜/美颜 | CVMetalTextureCache | 输入输出都是 GPU 纹理，全程 GPU |
| 编码器输入 | 直接传 CVPixelBuffer | VideoToolbox 内部自己走零拷贝 |
| 截图/snapshot | LockBaseAddress | 非热路径，偶尔一次可以接受 |
| dump 像素调试 | LockBaseAddress | 需要 CPU 直接看像素值 |
| 上传到非 Metal 框架 | LockBaseAddress | 如果目标框架不能接受 IOSurface |
| AI/推理(CoreML) | CVPixelBuffer 直传 | CoreML 支持 IOSurface 零拷贝 |


> "一句话：热路径（每帧都走的）必须零拷贝；冷路径（偶尔一次的）随便。"

**👨‍💻 追问：如果我真的需要在 CPU 上处理像素（比如 CPU 美颜算法），怎么尽量减少开销？**

> "三个优化点。第一，Lock 的时候加 `kCVPixelBufferLock_ReadOnly` 标志——只读锁定可以减少同步开销（不需要等 GPU 完全写完）。第二，尽量在采集时就设好格式——比如采集直接用 BGRA 而不是 NV12，省去 YUV→RGB 转换。第三，如果不是每帧都要处理，用 `CVPixelBufferPool` 做对象复用，减少 alloc/free。但要清楚一点：只要走 LockBaseAddress，detile 开销是逃不掉的——那是硬件层面的操作。所以 CPU 美颜在移动端基本已经绝迹了，大家都走 GPU。"

---

### Q5：iOS 零拷贝和 Android 零拷贝有什么区别？

**面试官意图**：考察跨平台视野。

**话术**：

> "两边底层机制完全不一样，但思路相通。"
>
> "**iOS**：底层是 **IOSurface**（内核对象，全局 ID），API 是 **CVMetalTextureCache**。整个系统高度统一——AVCaptureSession、VideoToolbox、CoreML、Metal 全通过 IOSurface ID 共享内存。你只需要确保 pixelBuffer 是 IOSurface 后端、带了 MetalCompatibilityKey，零拷贝自动生效。Apple 帮你做了大部分脏活。"
>
> "**Android**：底层是 **gralloc/GraphicBuffer**（HAL 层），强绑定硬件厂商。API 路径有很多条——可以用 **AHardwareBuffer**（最现代，跨进程、跨 API）、可以用 **SurfaceTexture + GL_OES_EGL_image_external**（OpenGL ES 零拷贝纹理）、也可以用 **ImageReader** 拿 CPU 可读的 Image。碎片化严重，不同厂商实现质量参差不齐。"
>
> "关键差异：
> - iOS 一条路走到黑（IOSurface），Android 条条大路但每条都有坑
> - iOS 的 CVMetalTextureCache 几乎零失败率，Android 的 AHardwareBuffer 跨 GPU 厂商兼容性要自己测
> - iOS 的 IOSurface ID 跨进程是系统原生支持的，Android 需要 AHardwareBuffer + binder 序列化
> - Android 有 Vulkan 的原生 YUV 格式（`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`），可以直接映射单张纹理，比 iOS 两张纹理更优雅"

---

## 二、深入原理

面试问答能让你"说对"，但大厂面试官会追问到你"说不下去"为止。下面的内容就是帮你撑过追问的。

### 2.1 IOSurface 的内核视角

IOSurface 不是简单的"一块共享内存"——它是 IOKit 框架里的一个 **内核对象**（kernel object），由 `IOSurface.framework` 管理。

```
用户空间 (App)
  │
  │ CVPixelBuffer (CoreVideo 封装)
  │ CVMetalTextureCache (Metal ↔ CoreVideo 桥)
  │
  ├── 所有这些操作最终都变成 mach_msg 发给 IOKit
  │
  │
内核空间 (Kernel / IOKit)
  │
  │ IOSurfaceRoot (IOSurface 的内核服务)
  │   ├── IOSurface 对象：跟踪引用计数、内存映射、pixel format
  │   ├── IOSurfaceID：全局 32 位 ID，跨进程查找
  │   └── 实际内存：由 IOMemoryDescriptor 管理，可以映射到不同进程的地址空间
  │
  ▼
物理内存 (LPDDR)
```

**关键特性**：

1. **全局 ID 机制**：`IOSurfaceGetID(surface)` 返回一个全局唯一的 32 位整数。任何进程通过 `IOSurfaceLookup(id)` 就能拿到同一块内存。这是 iOS 上**跨进程零拷贝**的基石——摄像头 daemon 采集的帧和你 App 看到的 CVPixelBuffer 物理上是同一块内存。

2. **引用计数**：IOSurface 由内核维护引用计数，iOS 上不能直接 `IOSurfaceIncrementUseCount`（那是 macOS 私有 API），但 CVPixelBuffer 的 retain/release 内部会 sync 到 IOSurface 的引用计数。这就是为什么你不 Lock + Unlock 会导致引用计数泄漏——底层 IOSurface 一直以为有人在使用，不会被释放。

3. **内存布局的秘密——为什么 GPU 用 tiled 格式**：

```
线性布局 (CPU 友好):
  Row 0: Pixel(0,0) Pixel(0,1) Pixel(0,2) ... Pixel(0,W-1)
  Row 1: Pixel(1,0) Pixel(1,1) Pixel(1,2) ... Pixel(1,W-1)
  ...
  → 逐行连续，跨行按 stride 跳转

Tiled 布局 (GPU 友好, 示意):
  Block(0,0) 包含: Pixel(0..15, 0..15)   在内存里连续
  Block(0,1) 包含: Pixel(0..15, 16..31)  在内存里连续
  Block(1,0) 包含: Pixel(16..31, 0..15)  在内存里连续
  ...
  → 每个 16×16 (或 32×32) 的 tile 内连续，tile 之间乱序

好处: GPU 纹理缓存是 2D 的，采一个纹素时，它周围的纹素大概率在同一个 tile 里
——缓存局部性极高。线性布局下，上下两个像素在内存里差了整行 stride，缓存命中率低。
```

这就是为什么 `CVPixelBufferLockBaseAddress` 需要 detile——硬件必须把这个 tiled 布局实时转成线性布局，CPU 才能按行读取。`CVMetalTextureCache` 不需要这个操作，因为 GPU 原生理解 tiled 格式。

### 2.2 CVMetalTextureCache 的创建时机和线程安全

```objc
// 在 init 里创建一次，不是每帧创建！

CVMetalTextureCacheRef _textureCache;

- (instancetype)init {
    // 1. 必须先有 MTLDevice
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();

    // 2. 创建 texture cache（一次性，别每帧调！）
    CVReturn ret = CVMetalTextureCacheCreate(
        kCFAllocatorDefault,   // 分配器
        NULL,                  // cache 属性字典
        device,                // MTLDevice（必须！用来创建 Metal 纹理）
        NULL,                  // 纹理属性字典
        &_textureCache         // 输出
    );

    // 3. 每帧调用 CVMetalTextureCacheCreateTextureFromImage
    //    内部会复用之前的映射，不是每次新建纹理
}
```

**线程安全**：`CVMetalTextureCache` 不是线程安全的。它跟 `MTLDevice` 绑定，通常和渲染在同一个线程操作。多线程场景下要加锁。

**flush 的作用**：

```objc
// 确保 texture cache 中所有 pending 的 GPU 写入都完成
// 相当于一道 GPU→CPU 的内存屏障
CVMetalTextureCacheFlush(_textureCache, kCVOptionFlags_None);

// 什么时候要调 flush?
// 1. 在 dealloc 释放 cache 之前
// 2. 需要确保 CPU 侧读到 GPU 最新写入时
// 3. 热路径（每帧）不需要调——渲染 submit/commit 本身就隐含了同步
```

### 2.3 完整的零拷贝链路（从采集到编码到渲染）

```
┌─────────────────────────────────────────────────────────────┐
│  iOS 端到端零拷贝链路（理想情况）                              │
│                                                             │
│  mediaserverd (摄像头 daemon)                                │
│    │ 采集, NV12, IOSurface ID=42                            │
│    │ 通过 mach_msg 传递 IOSurface ID                        │
│    ▼                                                        │
│  你的 App: AVCaptureVideoDataOutput 回调                     │
│    │ CMSampleBuffer → CVPixelBuffer (IOSurface ID=42)       │
│    │ 物理上就是同一块内存！没有拷贝！                          │
│    │                                                        │
│    ├──→ 预览路线: CVMetalTextureCache alias → Metal shader   │
│    │      YUV→RGB → MTKView drawable (还是一次拷贝都没有)     │
│    │                                                        │
│    └──→ 编码路线: VTCompressionSessionEncodeFrame            │
│           (直接传 CVPixelBuffer, VideoToolbox 内部 alias)     │
│           编码器直接读 IOSurface ID=42 的数据，零拷贝          │
│                                                             │
│  链路上每个环节被打破的条件（都有对应的规避方式）:             │
│  ① 采集设了非 NV12 格式 → 系统内部 format convert，有拷贝    │
│  ② LockBaseAddress + memcpy → CPU detile + 拷贝             │
│  ③ CVPixelBufferPool 漏了 IOSurfacePropertiesKey → 纯 CPU  │
│  ④ 手动 newTextureWithDescriptor + replaceRegion → 拷贝    │
└─────────────────────────────────────────────────────────────┘
```

---

## 三、完整代码：零拷贝 Metal 渲染器（逐行注释）

这一节把上面所有的理论落到代码上。**每行都注释**，适合面试前逐行过一遍。

### 3.1 头文件

```objc
//  MetalRenderer.h —— 接口简洁，只暴露必要的

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

@class MTKView;  // 前向声明，避免引入整个 MetalKit

@interface MetalRenderer : NSObject

/// 初始化，绑定到一个 MTKView 上
- (instancetype)initWithMetalView:(MTKView *)view;

/// 每帧调用一次，传入 NV12 的 CVPixelBuffer（零拷贝，线程安全）
- (void)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer;

@end
```

### 3.2 实现文件（完整注释）

```objc
//  MetalRenderer.m
//  iOS Metal 零拷贝 YUV→RGB 渲染器（面试版，逐行注释）

#import "MetalRenderer.h"
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

// ================================================================
// MARK: - 1. Metal Shader 源码
// ================================================================
// 一段完整的 Metal Shading Language 代码。
// 两种写法: 内联字符串（下面这样，方便面试展示）或 .metal 文件（工程更干净）。

static NSString *const kShaderSource = @"\
#include <metal_stdlib>\n\
using namespace metal;\n\
\n\
// --- 顶点着色器输入 ---
struct VertexIn {\n\
    float2 position [[attribute(0)]];   // 顶点坐标（NDC: -1~1）
    float2 texCoord [[attribute(1)]];    // 纹理坐标（0~1）
};\n\
\n\
// --- 顶点着色器输出 = 片段着色器输入 ---
struct VertexOut {\n\
    float4 position [[position]];        // 裁剪空间坐标（必须，GPU 用这个做光栅化）
    float2 texCoord;                     // 传给 fragment shader 的纹理坐标
};\n\
\n\
// ★ Vertex Shader: 几乎是最简单的——只是把顶点坐标和纹理坐标原样传下去
vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {\n\
    VertexOut out;\n\
    out.position = float4(in.position, 0.0, 1.0);\n\
    out.texCoord = in.texCoord;\n\
    return out;\n\
}\n\
\n\
// ★ Fragment Shader: NV12 → RGB 的核心
//   textureY:  Y 平面，格式 R8Unorm  → shader 里 sample 得到 .r = 亮度
//   textureUV: UV 平面，格式 RG8Unorm → shader 里 sample 得到 .r=U, .g=V
fragment float4 fragment_nv12(\n\
    VertexOut in [[stage_in]],\n\
    texture2d<float, access::sample> textureY  [[texture(0)]],\n\
    texture2d<float, access::sample> textureUV [[texture(1)]],\n\
    sampler textureSampler [[sampler(0)]]\n\
) {\n\
    // 采样 Y: 单通道, 取 .r 分量得到亮度值 (0~1 归一化)\n\
    float y  = textureY.sample(textureSampler, in.texCoord).r;\n\
    // 采样 UV: 双通道, .r = U(Cb), .g = V(Cr)\n\
    float2 uv = textureUV.sample(textureSampler, in.texCoord).rg;\n\
\n\
    // 把 UV 从 [0,1] 映射到 [-0.5, 0.5] —— YUV→RGB 矩阵的标准前处理\n\
    float u = uv.x - 0.5;\n\
    float v = uv.y - 0.5;\n\
\n\
    // ITU-R BT.601 Full Range → RGB 矩阵\n\
    float r = y + 1.40200 * v;\n\
    float g = y - 0.34414 * u - 0.71414 * v;\n\
    float b = y + 1.77200 * u;\n\
\n\
    return float4(r, g, b, 1.0);\n\
}\n\
\n\
// --- BT.709 (HD 色彩空间，现代 iPhone 默认) ---
// 如果你的摄像头输出是 BT.709，用下面这个:
fragment float4 fragment_nv12_bt709(\n\
    VertexOut in [[stage_in]],\n\
    texture2d<float, access::sample> textureY  [[texture(0)]],\n\
    texture2d<float, access::sample> textureUV [[texture(1)]],\n\
    sampler textureSampler [[sampler(0)]]\n\
) {\n\
    float y  = textureY.sample(textureSampler, in.texCoord).r;\n\
    float2 uv = textureUV.sample(textureSampler, in.texCoord).rg;\n\
    float u = uv.x - 0.5;\n\
    float v = uv.y - 0.5;\n\
    // BT.709 系数（和 601 不同！）\n\
    float r = y + 1.57480 * v;\n\
    float g = y - 0.18733 * u - 0.46813 * v;\n\
    float b = y + 1.85560 * u;\n\
    return float4(r, g, b, 1.0);\n\
}\n\
";

// ================================================================
// MARK: - 2. 顶点数据（全屏矩形 = 两个三角形）
// ================================================================
// GPU 画一个覆盖全屏的矩形，顶点坐标在 NDC（Normalized Device Coordinates）
// 范围是 -1 到 1：(-1,-1) 是左下，(1,1) 是右上

typedef struct {
    simd_float2 position;   // NDC 坐标
    simd_float2 texCoord;   // 纹理坐标
} Vertex;

// 两个三角形拼成一个矩形，共 6 个顶点
//  三角形1: 左下→右下→左上
//  三角形2: 右下→右上→左上
static const Vertex kQuadVertices[] = {
    // 三角形 1
    {{-1.0, -1.0}, {0.0, 1.0}},   // 左下: NDC 左下 = 纹理左下
    {{ 1.0, -1.0}, {1.0, 1.0}},   // 右下
    {{-1.0,  1.0}, {0.0, 0.0}},   // 左上
    // 三角形 2
    {{ 1.0, -1.0}, {1.0, 1.0}},   // 右下
    {{ 1.0,  1.0}, {1.0, 0.0}},   // 右上
    {{-1.0,  1.0}, {0.0, 0.0}},   // 左上
};
// 注意纹理坐标 Y 方向: 这里 Y=0 对应 NDC 上方, Y=1 对应 NDC 下方。
// Metal 里纹理原点在左上角，NDC 原点在中心——但 CVMetalTextureCache
// 创建的纹理已经是 Metal 坐标系，不需要翻转。如果画面上下颠倒才需要调。

// ================================================================
// MARK: - 3. MetalRenderer 实现
// ================================================================
@implementation MetalRenderer {
    // ---- Metal 核心对象（init 时创建，一次）----
    id<MTLDevice>              _device;            // GPU 设备
    id<MTLCommandQueue>        _commandQueue;      // 命令队列（线程安全）
    id<MTLRenderPipelineState> _pipelineState;     // 渲染管线（shader 编译后固化）
    id<MTLBuffer>              _vertexBuffer;      // 顶点缓冲（6 个全屏矩形顶点）
    id<MTLSamplerState>        _sampler;           // 采样器（双线性 + clamp）

    // ---- ★零拷贝核心★ ----
    CVMetalTextureCacheRef     _textureCache;      // IOSurface → MTLTexture 映射器

    // ---- 当前帧的纹理（每帧由 renderPixelBuffer: 更新）----
    id<MTLTexture>             _textureY;          // Y 平面纹理 (R8Unorm)
    id<MTLTexture>             _textureUV;         // UV 平面纹理 (RG8Unorm)

    // ---- inflight 帧数控制 ----
    dispatch_semaphore_t       _frameSemaphore;    // 限制 GPU 中排队的帧数 ≤ 3
    static const int           kMaxInflightFrames = 3;
}

// ================================================================
// MARK: - 初始化
// ================================================================
- (instancetype)initWithMetalView:(MTKView *)view {
    self = [super init];
    if (!self) return nil;

    // ① 获取 MTLDevice（如果没有就用系统默认）
    _device = view.device ?: MTLCreateSystemDefaultDevice();
    view.device = _device;

    // ② 创建命令队列
    _commandQueue = [_device newCommandQueue];

    // ③ ★ 创建零拷贝纹理缓存（最关键的初始化步骤）★
    //    参数详解:
    //      kCFAllocatorDefault:  默认内存分配器
    //      NULL:                 cache 属性（默认即可）
    //      _device:              MTLDevice —— 必须！用来创建 Metal 纹理对象
    //      NULL:                 texture 属性（默认即可，系统会根据 IOSurface 推断格式）
    //      &_textureCache:       输出
    CVReturn cvRet = CVMetalTextureCacheCreate(
        kCFAllocatorDefault,
        NULL,               // cache attributes
        _device,            // ★ 关键: 绑定到 Metal 设备
        NULL,               // texture attributes
        &_textureCache
    );
    if (cvRet != kCVReturnSuccess) {
        NSLog(@"❌ CVMetalTextureCacheCreate failed: %d", cvRet);
        return nil;
    }

    // ④ 编译 shader → 创建渲染管线
    if (![self _buildPipeline]) return nil;

    // ⑤ 创建顶点缓冲（全屏矩形的 6 个顶点）
    _vertexBuffer = [_device newBufferWithBytes:kQuadVertices
                                         length:sizeof(kQuadVertices)
                                        options:MTLResourceStorageModeShared];
    //      Shared 模式: CPU 和 GPU 共享同一块内存。因为顶点数据不变，
    //      创建一次后 GPU 只读，Shared 是最佳选择。

    // ⑥ 创建采样器
    MTLSamplerDescriptor *samplerDesc = [MTLSamplerDescriptor new];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;  // 缩小时双线性插值
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;  // 放大时双线性插值
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge; // 水平方向边缘 clamp
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge; // 垂直方向边缘 clamp
    _sampler = [_device newSamplerStateWithDescriptor:samplerDesc];

    // ⑦ 配置 MTKView + 设置 delegate
    view.delegate = self;
    view.framebufferOnly = NO;  // 默认 YES。设为 NO 允许读 drawable（如截图）
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;  // 标准 8-bit BGRA 输出
    view.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0); // 清屏色: 黑色

    // ⑧ inflight 帧数信号量
    _frameSemaphore = dispatch_semaphore_create(kMaxInflightFrames);

    return self;
}

- (void)dealloc {
    if (_textureCache) {
        // 先 flush: 确保所有 pending 的 GPU 写入都落盘
        CVMetalTextureCacheFlush(_textureCache, kCVOptionFlags_None);
        CFRelease(_textureCache);  // CoreFoundation 对象用 CFRelease
        _textureCache = NULL;
    }
    // MTKView.delegate 记得置 nil 防止野指针
}

// ================================================================
// MARK: - Shader 编译
// ================================================================
- (BOOL)_buildPipeline {
    NSError *error = nil;

    // 从源码字符串编译 Metal Library
    id<MTLLibrary> library = [_device newLibraryWithSource:kShaderSource
                                                   options:nil
                                                     error:&error];
    if (!library) {
        NSLog(@"❌ Shader compile error: %@", error);
        return NO;
    }

    // 取 vertex 和 fragment 函数
    id<MTLFunction> vertFunc = [library newFunctionWithName:@"vertex_main"];
    id<MTLFunction> fragFunc = [library newFunctionWithName:@"fragment_nv12_bt709"];
    //                            ↑ 选 BT.709（现代 iPhone 默认色彩空间）
    //                              如果是老设备/模拟器用 fragment_nv12（BT.601）

    // 配置渲染管线描述符
    MTLRenderPipelineDescriptor *pipelineDesc = [MTLRenderPipelineDescriptor new];
    pipelineDesc.vertexFunction   = vertFunc;
    pipelineDesc.fragmentFunction = fragFunc;
    // 输出到 MTKView 的 drawable，格式必须和 view.colorPixelFormat 一致
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // ★ 配置顶点布局: 告诉 Metal 怎么从 vertex buffer 里读数据
    MTLVertexDescriptor *vertDesc = [MTLVertexDescriptor new];
    //   Attribute 0: position (float2)，偏移 0
    vertDesc.attributes[0].format      = MTLVertexFormatFloat2;
    vertDesc.attributes[0].offset      = 0;
    vertDesc.attributes[0].bufferIndex = 0;
    //   Attribute 1: texCoord (float2)，偏移 sizeof(float2) = 8 字节
    vertDesc.attributes[1].format      = MTLVertexFormatFloat2;
    vertDesc.attributes[1].offset      = sizeof(simd_float2);
    vertDesc.attributes[1].bufferIndex = 0;
    //   Layout: 每个顶点的步长 = sizeof(Vertex) = 16 字节
    vertDesc.layouts[0].stride       = sizeof(Vertex);
    vertDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    pipelineDesc.vertexDescriptor = vertDesc;

    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                             error:&error];
    if (!_pipelineState) {
        NSLog(@"❌ Pipeline state error: %@", error);
        return NO;
    }
    return YES;
}

// ================================================================
// MARK: - ★ 零拷贝核心：把 CVPixelBuffer 映射为 Metal 纹理 ★
// ================================================================
- (void)renderPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer) return;

    size_t width  = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);

    // ---- 映射平面 0: Y（亮度）----
    {
        CVMetalTextureRef cvTexY = NULL;
        CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,        // 分配器
            _textureCache,              // ★ 纹理缓存
            pixelBuffer,                // ★ 源 CVPixelBuffer（底层 IOSurface）
            NULL,                       // 纹理属性（nil = 用默认值）
            MTLPixelFormatR8Unorm,      // ★ Y 平面: 单通道 8 位
            width,                      // 纹理宽度 = 图像宽度
            height,                     // 纹理高度 = 图像高度
            0,                          // ★ planeIndex = 0（Y 平面）
            &cvTexY                     // 输出
        );
        if (ret == kCVReturnSuccess && cvTexY) {
            // 从 CVMetalTextureRef 里取出底层的 MTLTexture
            _textureY = CVMetalTextureGetTexture(cvTexY);
            CFRelease(cvTexY);  // CVMetalTextureRef 只是一个"包装"，
                                // 取到 MTLTexture 后就可以释放包装了。
                                // 底层 IOSurface 不受影响。
        }
    }

    // ---- 映射平面 1: UV（色度，交错）----
    {
        CVMetalTextureRef cvTexUV = NULL;
        CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,
            _textureCache,
            pixelBuffer,
            NULL,
            MTLPixelFormatRG8Unorm,     // ★ UV 平面: 双通道 8 位（U=G, V=R? 待定）
            width / 2,                  // ★ 4:2:0 水平降采样，宽度减半
            height / 2,                 // ★ 4:2:0 垂直降采样，高度减半
            1,                          // ★ planeIndex = 1（UV 平面）
            &cvTexUV
        );
        if (ret == kCVReturnSuccess && cvTexUV) {
            _textureUV = CVMetalTextureGetTexture(cvTexUV);
            CFRelease(cvTexUV);
        }
    }

    // 注意: CVMetalTextureCache 内部会管理纹理的复用。
    // 同一个 IOSurface ID 不会被重复创建——第二次调用时返回缓存的纹理。
}

// ================================================================
// MARK: - MTKViewDelegate: 绘制回调
// ================================================================
// 由 MTKView 内部的 CADisplayLink 驱动，每 16.67ms (60fps) 调用一次。
// 如果没有新帧（_textureY 还是上一帧），画面会重绘同一帧——不影响。

- (void)drawInMTKView:(MTKView *)view {
    // ----- inflight 帧数控制 -----
    // 等 semaphore: 如果 GPU 里已经有 3 帧在排队，CPU 就在这里等。
    // 防止 CPU 提前提交第 4 帧 → 内存积压 + 延时增大。
    dispatch_semaphore_wait(_frameSemaphore, DISPATCH_TIME_FOREVER);

    if (!_textureY || !_textureUV) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;  // 还没有收到第一帧，什么都不画
    }

    // ① 创建 command buffer
    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];

    // ② 获取当前帧的 drawable（系统管理的可显示纹理）
    //    每次调用都会拿一个新的 drawable，内部是一个 CAMetalLayer 的 drawable 池
    id<CAMetalDrawable> drawable = [view currentDrawable];
    if (!drawable) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;  // drawable 可能被其他操作占用，放弃这一帧
    }

    // ③ 获取 MTKView 预配置的 render pass descriptor
    MTLRenderPassDescriptor *passDesc = [view currentRenderPassDescriptor];
    if (!passDesc) {
        dispatch_semaphore_signal(_frameSemaphore);
        return;
    }
    // passDesc 已经帮我们配好了 color attachment——目标 = drawable.texture
    // loadAction = Clear（先清屏）, storeAction = Store（写回 drawable）

    // ④ 创建 render command encoder（命令编码器）
    id<MTLRenderCommandEncoder> encoder =
        [cmdBuf renderCommandEncoderWithDescriptor:passDesc];

    // ⑤ 设置渲染状态
    [encoder setRenderPipelineState:_pipelineState];         // shader 管线
    [encoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0]; // 顶点数据
    [encoder setFragmentTexture:_textureY  atIndex:0];        // ★ Y 纹理
    [encoder setFragmentTexture:_textureUV atIndex:1];        // ★ UV 纹理
    [encoder setFragmentSamplerState:_sampler atIndex:0];     // 采样器

    // ⑥ ★ Draw Call: 画 6 个顶点 = 两个三角形 = 一个全屏矩形
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6];
    [encoder endEncoding];

    // ⑦ Present: 把这个 drawable 提交给系统，vsync 后显示到屏幕
    [cmdBuf presentDrawable:drawable];

    // ⑧ GPU 完成后释放 semaphore（让下一帧可以提交）
    __weak typeof(self) weakSelf = self;
    [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull buf) {
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            dispatch_semaphore_signal(strongSelf->_frameSemaphore);
        }
    }];

    // ⑨ Commit: 提交到 GPU 执行（异步，立刻返回）
    [cmdBuf commit];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    // drawable 尺寸变化时（旋转、分屏、调整窗口大小）被调用
    // 因为我们画的是全屏矩形（NDC 坐标），不需要在这里做额外处理
    // 如果你的 shader 需要知道 drawable 的实际像素尺寸，在这里更新
}

@end
```

### 3.3 使用方式

```objc
// 在 ViewController 或采集回调里使用:

// 1. 创建
MTKView *mtkView = [[MTKView alloc] initWithFrame:self.view.bounds
                                           device:MTLCreateSystemDefaultDevice()];
MetalRenderer *renderer = [[MetalRenderer alloc] initWithMetalView:mtkView];
[self.view addSubview:mtkView];

// 2. 每帧喂数据（比如在 AVCaptureVideoDataOutput 的回调里）
- (void)captureOutput:(AVCaptureOutput *)output
  didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection {

    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    [self.renderer renderPixelBuffer:pixelBuffer];  // 零拷贝！
    // MTKView 的 drawInMTKView: 会自动在下一次 vsync 时渲染
}

// 3. 如果帧率不是 60fps，控制 MTKView 的刷新:
//    mtkView.paused = YES;   // 暂停自动刷新
//    mtkView.paused = NO;    // 恢复
```

---

## 四、进阶：大厂面试追问深度

### 4.1 为什么 NV12 的 UV 纹理用 RG8Unorm，shader 里 .r 和 .g 哪个是 U 哪个是 V？

这是很多人搞混的细节。

```
NV12 的 UV 平面在内存中的交错排列: U₀ V₀ U₁ V₁ U₂ V₂ ...

CVMetalTextureCache 用 RG8Unorm 映射时:
  .r 通道读取第一个字节 → 也就是 U (Cb)
  .g 通道读取第二个字节 → 也就是 V (Cr)

所以 shader 里:
  float u = textureUV.sample(...).r;   // U (Cb)
  float v = textureUV.sample(...).g;   // V (Cr)
```

验证方法：如果 UV 映射反了（.r 和 .g 对调），画面呈现紫色调/绿色调异常——因为 Cb 和 Cr 在 YUV→RGB 矩阵里的系数完全不同。

### 4.2 BT.601 vs BT.709 怎么自动选择？

不要写死，从 CVPixelBuffer 的 attachment 里读：

```objc
CFStringRef matrixKey = CVBufferGetAttachment(
    pixelBuffer,
    kCVImageBufferYCbCrMatrixKey,  // ← 系统写入的色彩矩阵
    NULL
);

// 返回值的可能:
// kCVImageBufferYCbCrMatrix_ITU_R_601_4   → BT.601（SD）
// kCVImageBufferYCbCrMatrix_ITU_R_709_2   → BT.709（HD）
// kCVImageBufferYCbCrMatrix_ITU_R_2020    → BT.2020（HDR）

// 工程做法: 给 shader 传一个 uniform 参数选择矩阵，或者编译多个 variant。
```

```objc
// 从 pixelBuffer 判断后传到 shader:
typedef enum : int {
    ColorMatrixBT601 = 0,
    ColorMatrixBT709 = 1,
    ColorMatrixBT2020 = 2,
} ColorMatrixType;

// 作为 constant buffer 传进去:
[encoder setFragmentBytes:&colorMatrixType
                   length:sizeof(ColorMatrixType)
                  atIndex:0];
```

### 4.3 Metal 的多线程渲染

`MTLCommandQueue` 是线程安全的，可以从多个线程创建 `MTLCommandBuffer`。但 `CVMetalTextureCache` 不是线程安全的。

多线程渲染的典型架构：

```
采集线程:
  1. 拿到 CVPixelBuffer
  2. CVMetalTextureCacheCreateTextureFromImage → Y/UV textures
  3. 把 Y/UV textures 封装成任务，投递到渲染线程队列

渲染线程 (MTKView 的 draw 回调所在线程):
  1. 从队列取到 Y/UV textures
  2. 执行 Metal 渲染管线
  3. commit

关键: CVMetalTextureCache 的创建和读取都在同一个线程（采集线程），
      渲染线程只消费已创建好的 MTLTexture 引用。
```

### 4.4 如何确认零拷贝是否真的在生效

**Instruments 验证法**：

```
1. Xcode → Product → Profile → Metal System Trace
2. 开始录制，跑你的渲染场景
3. 看 "Texture" 事件:
   - 如果有 "texture upload" 或 "replaceRegion" → 有拷贝
   - 如果只有 "texture cache create" → 零拷贝
4. 对比耗时:
   零拷贝: ~0.05-0.1ms
   有拷贝: ~1-5ms（取决于分辨率）
```

**代码验证法**：

```objc
// 方法 1: 检测 IOSurface
void *surface = CVPixelBufferGetIOSurface(pixelBuffer);
NSLog(@"IOSurface: %s, ID: %d", surface ? "YES" : "NO",
      surface ? IOSurfaceGetID((IOSurfaceRef)surface) : -1);
// 输出 IOSurface ID — 同一条链路上的 buffer ID 应该相同（同一块内存）

// 方法 2: 负向测试
// 注释 CVMetalTextureCache → 改用 LockBaseAddress + newTexture + replaceRegion
// → Instruments 里对比前后耗时
```

---

## 五、与 Android 零拷贝对比（面试常考跨平台题）

| 维度 | iOS | Android |
|------|-----|---------|
| 底层机制 | IOSurface（内核对象，全局 32 位 ID） | gralloc / GraphicBuffer（HAL 层，厂商实现） |
| 跨进程 | IOSurfaceLookup(id) 天然支持 | AHardwareBuffer + binder 序列化 |
| GPU API | Metal only（CVMetalTextureCache） | OpenGL ES（EGLImage + OES_EGL_image_external）/ Vulkan |
| NV12 纹理 | 两张独立纹理（R8Unorm + RG8Unorm） | Vulkan 原生 YUV 多平面格式 |
| 采集到编码零拷贝 | 自动（同 IOSurface ID） | 需要 ImageReader + AHardwareBuffer 显式传递 |
| 坑 | 忘带 IOSurfacePropertiesKey 或 MetalCompatibilityKey | 厂商特定 gralloc 实现 bug、格式转换隐藏拷贝 |
| 调试 | Instruments Metal System Trace | systrace / GPU Inspector / RenderDoc |

---

## 六、常见坑速查

| 现象 | 根因 | 修复 |
|------|------|------|
| CVMetalTextureCacheCreateTextureFromImage 返回 -6660 | PixelFormat 不兼容或 planeIndex 越界 | 检查 format 和 planeIndex 的对应关系 |
| 画面颜色发绿/偏紫 | YUV→RGB 矩阵选错，或 UV 通道映射反 | 读 kCVImageBufferYCbCrMatrixKey 选择矩阵；确认 RG8Unorm 的 .r/.g 不是反的 |
| 画面闪烁 | inflight 帧数过多，同一块 IOSurface 被覆盖 | dispatch_semaphore 限制 kMaxInflightFrames=3 |
| MTKView 不显示 | delegate 没设 / paused=YES / clearColor 黑色 + 纹理全黑 | 打断点确认 drawInMTKView 被调 |
| LockBaseAddress 后画面异常 | 和其他 GPU 操作冲突（同步问题） | Lock/Unlock 配对；热路径不要 Lock |
| pixelBufferPool 创建的 buffer 无法 CVMetalTextureCache alias | 漏了 MetalCompatibilityKey 或 IOSurfacePropertiesKey | 加上两个 key 重新创建 pool |

---

## 七、一句话总结

> iOS 零拷贝的本质：**全链路通过 IOSurface ID 共享同一块 GPU 内存，CVMetalTextureCache 给它建纹理视图（alias），GPU 直接采样，CPU 永远不碰像素数据。** 打破零拷贝的三大杀手：LockBaseAddress、缺 IOSurfacePropertiesKey、格式转换。

---

## 八、自检清单

- CVPixelBuffer 底层的存储到底是什么？IOSurface 又是什么？
- CVMetalTextureCacheCreateTextureFromImage 有哪几个关键参数？每个参数的含义？
- NV12 两个平面分别用什么 MTLPixelFormat 映射？宽高分别是多少？
- 为什么 LockBaseAddress 在 UMA 架构下仍然有开销？
- 零拷贝 CVPixelBufferPool 创建必须带哪两个 key？
- 怎么在运行时判断一个 CVPixelBuffer 是不是 IOSurface 后端？
- BT.601 和 BT.709 怎么从 pixelBuffer 里自动读取并按需选择？
- Metal 渲染中 dispatch_semaphore 限制 inflight 帧数的目的是什么？
- CVMetalTextureCache 是线程安全的吗？
- iOS 和 Android 零拷贝的本质区别是什么？
