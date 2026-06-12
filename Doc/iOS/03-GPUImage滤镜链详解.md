# GPUImage 滤镜链详解：面试速记与原理详解

> **适用方向**：iOS 移动端图像/视频特效处理、美颜 SDK、滤镜引擎
> **前置知识**：OpenGL ES 基础（纹理、FBO、shader），了解 CVPixelBuffer 基本概念
> **难度**：⭐⭐（1-5 星）
> **预计阅读**：速记 8 分钟｜全文 20 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[01-AVFoundation采集详解]] —— 拿到 CVPixelBuffer 后怎么加滤镜
> **定位**：🟡 高级加分 —— JD 里写了「熟悉 GPUImage 优先」，说明面试官期望候选人对这套滤镜链框架有基本认知

---

## 一、全景导读：GPUImage 是什么、为什么需要它

### 1.1 从场景说起

你从 AVCaptureSession 拿到了 NV12 的 CVPixelBuffer，现在要做美颜——磨皮、大眼、美白、加上一个复古滤镜。方案 A：用 CPU 逐像素处理；方案 B：用 GPU shader 并行处理。1080p 下方案 A 一帧磨皮可能 50-100ms，方案 B 只需要 2-5ms。

但手写 OpenGL ES / Metal 的 shader、管理 FBO 切换、纹理绑定、滤镜串联——代码量很大且容易出错。**GPUImage 就是用一套设计模式把这些 GPU 滤镜的串联、纹理管理、FBO 切换全部封装好了**。

### 1.2 GPUImage 的前世今生

- **作者**：Brad Larson，2012 年开源
- **产生背景**：iOS 上做图像处理要么用 Core Image（黑盒、不可定制），要么手写 OpenGL ES（门槛高、代码多）。GPUImage 给了第三条路——**开源、可定制、滤镜链可组合**。
- **GPUImage 2（Swift + Metal）**：2016 年发布，迁移到 Metal（因为 OpenGL ES 在 iOS 上被废弃）
- **GPUImage 3（Swift + Metal，重写）**：2018 年发布，API 更简洁

> **面试注意**：说「GPUImage」如果没有特别说明，一般指 GPUImage 1（OC 版，OpenGL ES）。现在新项目应该用 Metal 版本或直接写 Metal shader，但面试问「熟悉 GPUImage 吗」其实是在问：「你理解滤镜链的设计思想吗？」

### 1.3 核心思想：滤镜链（Filter Chain）

GPUImage 把图像处理抽象成一条链：

```
输入源               滤镜 1              滤镜 2              输出/显示
┌──────────┐       ┌──────────┐       ┌──────────┐       ┌──────────┐
│ Camera / │──→──│ 美颜滤镜  │──→──│ 色彩滤镜  │──→──│ GPUImage │
│ Image /  │       │ (Beauty) │       │ (Lookup) │       │ View     │
│ Video    │       └──────────┘       └──────────┘       └──────────┘
└──────────┘
    ↑                ↑                   ↑                   ↑
    ↓                ↓                   ↓                   ↓
 texture           FBO                FBO              screen/output
 (输入纹理)    (中间渲染目标)      (中间渲染目标)      (最终渲染目标)

每个节点的输出纹理 = 下一个节点的输入纹理
FBO（Frame Buffer Object）= GPU 上的"画布"，shader 在上面画
```

**核心规则**：每个滤镜有自己的 FBO——前一个滤镜的输出纹理绑定为后一个滤镜的输入纹理，后一个滤镜在自己的 FBO 上执行 shader，产出新的纹理传给下一个。**一次 GPU 渲染 pass 就是一个滤镜**。

### 1.4 GPUImage vs 自研 Metal shader

| 维度 | GPUImage | 自研 Metal Shader |
|------|----------|-------------------|
| 开发效率 | 高，滤镜串联一行代码 | 低，要自己管理 FBO/纹理/渲染管线 |
| 性能 | 每加一个滤镜多一次 render pass（额外 draw call） | 可以把多个滤镜合并到一个 shader，省 pass |
| 灵活性 | 只能用它提供的滤镜和自定义 shader | 完全自由 |
| 维护风险 | 开源项目，更新慢 | 自己可控 |
| 适合场景 | 快速原型、滤镜多但不极致压性能 | 性能敏感、需要极致优化 |

> 大厂的做法：自己的渲染引擎 + 借鉴 GPUImage 的滤镜链设计思想，但前端一般会做 shader 合并优化（把相邻的简单滤镜合并成一个 shader 减少 render pass）。

---

## 二、面试速记

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | GPUImage 是什么 | iOS 开源 GPU 图像/视频处理框架，核心是滤镜链设计 | 🔥🔥🔥 | 🟢 |
| 2 | 滤镜链怎么工作 | 每个滤镜有自己的 FBO，输出纹理 = 下一级输入纹理 | 🔥🔥🔥 | 🟢 |
| 3 | 怎么加自定义滤镜 | 继承 GPUImageFilter，写 GLSL shader 字符串 | 🔥🔥 | 🟢 |
| 4 | GPUImage 和 Core Image 区别 | CI 是系统框架、黑盒、CPU/GPU 自动选择；GPUImage 开源、全程 GPU、可控 | 🔥🔥 | 🟢 |
| 5 | 多个滤镜能不能合并减少 pass | 理论上可以，GPUImage 本身不支持，需自研 | 🔥🔥 | 🟡 |
| 6 | GPUImage2/3 为什么迁移到 Metal | OpenGL ES 在 iOS 12+ deprecated，Metal 是唯一方向 | 🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：你知道 GPUImage 吗？它的滤镜链是怎么工作的？

**面试官想听什么：** 你是不是真的用它做过东西，还是只听说过名字。能说清 FBO 切换和纹理传递机制。

**🗣️ 标准回答（可背诵）：**

> "GPUImage 的核心设计是滤镜链。每个滤镜——不管是美颜、色彩调整还是混合——内部都有一套标准的 shader 渲染流程：把输入纹理绑定到 FBO 的输入，在自己的 FBO 上跑 vertex shader + fragment shader，产出一张输出纹理。滤镜串联时，前一个滤镜的输出纹理就是后一个滤镜的输入纹理——每多一个滤镜就是多一次 GPU render pass。相机采集的场景下链路是：GPUImageVideoCamera（封装了 AVCaptureSession，输出 OpenGL 纹理）→ GPUImageBeautifyFilter → GPUImageLookupFilter → GPUImageView（渲染到屏幕）。我实际用的时候最深的感受是——滤镜链很方便，但每加一个滤镜就是一个 draw call，极端情况下（比如十个滤镜串联）性能就扛不住了。所以大厂一般会做 shader 合并优化——把相邻的几个简单滤镜的 GLSL 合并成一个 shader，省掉中间的 render pass。"

**👨‍💻 追问预警：**
> Q: "如果让你自己实现一个滤镜链框架，你会怎么设计？"
> A: 核心三个抽象类：Input（接收纹理）、Output（产出纹理）、Filter（Input + Output 双向适配器，内部管理 FBO）。Input 协议定义 setInputTexture/setInputSize，Output 协议定义 addTarget/removeTarget，Filter 实现两者。这和 GPUImage 的设计完全对应——GPUImageOutput（产出）+ GPUImageInput（接收）+ GPUImageFilter（中间的滤镜）。

---

#### Q2：GPUImage 和 Core Image 有什么区别？什么时候选哪个？

**面试官想听什么：** 你有技术选型的判断力，不是只会用一个框架。

**🗣️ 标准回答（可背诵）：**

> "第一个区别是开放性：Core Image 是系统提供、封闭的——你只能用 Apple 内置的滤镜（CIFilter），虽然 iOS 8 后支持了 CIKernel 自定义 shader 但调试极痛苦且限制多；GPUImage 开源、你可以改任何代码。第二个区别是执行平台：Core Image 会自动决定在 CPU 还是 GPU 上执行，你控制不了；GPUImage 确保全程在 GPU——这在实时视频处理里是明确的性能优势。第三个区别是灵活度：GPUImage 可以自定义滤镜链里的任何一环，GPUImageLookupFilter 就是加载一张 LUT 图做色彩映射——这在 Core Image 里也能做但接口笨重。选型上：简单滤镜需求、不想引入第三方——用 Core Image；要做美颜、需要复杂的自定义滤镜链——用 GPUImage 或者自研引擎。"

**👨‍💻 追问预警：**
> Q: "iOS 上 OpenGL ES 已经 deprecated 了，你现在会用 GPUImage 吗？"
> A: GPUImage 1 是基于 OpenGL ES 的，不会在新项目里直接用。但它的滤镜链设计思想是跨平台的——Android 端的同类框架也是这个思路。新项目要么用 GPUImage 3（Metal 版），要么借鉴 GPUImage 的链式设计自己写 Metal 引擎。

---

#### Q3：你是怎么在项目里用 GPUImage 做美颜的？

**面试官想听什么：** 你有没有真正集成过，理解从 CVPixelBuffer 到 GPUImage 的数据通路。

**🗣️ 标准回答（可背诵）：**

> "在之前的跨平台 SDK 项目里，我们接入了字节的美颜 SDK，但有些简单的滤镜效果——比如色彩增强、LUT 滤镜——是用 GPUImage 自己实现的。链路是从 AVCaptureVideoDataOutput 的回调里拿到 CVPixelBuffer，通过 GPUImageVideoCamera 封装好的接口传入——它内部用 CVOpenGLESTextureCache 把 NV12 的两个平面映射成两张 OpenGL 纹理。然后依次接上磨皮滤镜、美白滤镜、LUT 色彩映射滤镜，最后渲染到 GPUImageView。这里一个关键的优化是我们做了 GPUImage 的 texture cache 复用——每一帧处理完不新建纹理，而是复用上一帧的 FBO 纹理——减少了 GPU 端的 alloc/free，整体从原来的 12ms 优化到 7ms 左右。另外我们还把 GPUImage 的部分逻辑重构了——原来的 GPUImageVideoCamera 是自带 AVCaptureSession 的，我们是直接拿到 CVPixelBuffer 后喂给它，这样采集和滤镜完全解耦。"

**👨‍💻 追问预警：**
> Q: "为什么不用 Core Image + Metal 全家桶？"
> A: 当时项目是 2022 年开始的，团队里大家更熟 GPUImage 的滤镜链模式，快速出活。现在回头看新项目应该直接上 Metal。不过 GPUImage 的滤镜链设计思想——Input/Output/Filter 三层抽象——在我们后来的自研 Metal 引擎里完全沿用了。

---

## 三、核心代码：GPUImage 滤镜链

```objc
// ---------- 1. 基本滤镜链 ----------
#import "GPUImage.h"

// 采集源
GPUImageVideoCamera *camera = [[GPUImageVideoCamera alloc]
    initWithSessionPreset:AVCaptureSessionPreset1920x1080
           cameraPosition:AVCaptureDevicePositionBack];
camera.outputImageOrientation = UIInterfaceOrientationPortrait;

// 滤镜
GPUImageBeautifyFilter *beautyFilter = [[GPUImageBeautifyFilter alloc] init];
GPUImageLookupFilter *lookupFilter = [[GPUImageLookupFilter alloc] init];
lookupFilter.intensity = 0.8;

// 显示
GPUImageView *previewView = [[GPUImageView alloc] initWithFrame:self.view.bounds];

// 链：camera → beauty → lookup → view
[camera addTarget:beautyFilter];
[beautyFilter addTarget:lookupFilter];
[lookupFilter addTarget:previewView];

// 启动
[camera startCameraCapture];

// ---------- 2. 自定义滤镜 ----------
// 一个简单的灰度滤镜 GLSL
@interface GrayscaleFilter : GPUImageFilter
@end

@implementation GrayscaleFilter

- (instancetype)init {
    // vertex shader 用默认的（全屏矩形）
    // fragment shader 只改颜色转换逻辑
    NSString *fragmentShader =
    @"varying highp vec2 textureCoordinate;                              \n"
    @"uniform sampler2D inputImageTexture;                               \n"
    @"void main() {                                                      \n"
    @"    lowp vec4 color = texture2D(inputImageTexture, textureCoordinate);\n"
    @"    lowp float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));  \n"
    @"    gl_FragColor = vec4(vec3(gray), color.a);                      \n"
    @"}                                                                  \n";

    self = [super initWithFragmentShaderFromString:fragmentShader];
    return self;
}

@end

// ---------- 3. 从 CVPixelBuffer 直接喂给 GPUImage ----------
- (void)processPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    // GPUImage 内部用 CVOpenGLESTextureCache 做零拷贝映射
    GPUImageTextureInput *textureInput = [[GPUImageTextureInput alloc]
        initWithTextureSize:CGSizeMake(CVPixelBufferGetWidth(pixelBuffer),
                                        CVPixelBufferGetHeight(pixelBuffer))];

    // 绑定当前 EGL context
    [textureInput processCVPixelBuffer:pixelBuffer];
    [textureInput addTarget:_nextFilter];
}
```

### 关键设计解读

**1. 为什么 GPUImage 用 OpenGL ES 而不是 Metal？**
GPUImage 1.x 是 2012 年发布的，当时 Metal 还不存在（Metal 2014 年随 iOS 8 发布）。后来有了 GPUImage 2/3 迁移 Metal，但很多老项目和老工程师的习惯还在 OC + GL 版本。

**2. `addTarget` 内部做了什么？**
- A → addTarget:B：把 B 注册为 A 的 target，当 A 完成当前帧的渲染后，会自动通知 B 来取纹理
- 内部维护了一个 target 数组，支持一对多（一个输出可以给多个 input，比如同时预览和编码）
- 每一帧处理完成后，A 调 `[target setInputTexture:texture]` 把自己的输出纹理传给 B

**3. CVOpenGLESTextureCache 的作用？**
和 Metal 版的 CVMetalTextureCache 完全对应——把 CVPixelBuffer 底层的 IOSurface 零拷贝映射为 OpenGL ES 纹理。GPUImageVideoCamera 内部就在做这件事。

---

## 四、性能考量与常见坑

### 滤镜数量 vs 性能

一个滤镜 = 一次 draw call + 一次 FBO bind + 一次 shader 执行。在 1080p 下，每个额外的滤镜大约增加 1-3ms（取决于 shader 复杂度）。如果链上有 8 个滤镜，一帧就要 8-24ms——30fps 的时间预算是 33ms，直接就超了。

**对策**：
- 把简单滤镜合并到一个 shader（GPUImage 本身不支持，需自研）
- 对静态效果做缓存——如果滤镜参数没变，前一帧的输出可以复用
- 降分辨率处理——美颜/特效在 720p 甚至 480p 上做，最后再 upsample 到 1080p

### OpenGL ES deprecated 问题

GPUImage 1.x 基于 OpenGL ES，在 iOS 12+ 上被标记为 deprecated（但还能跑）。面试谈到时主动提：「我们知道 GL 已废弃，新项目应该用 Metal 版本或自研 Metal 引擎，但 GPUImage 的滤镜链设计模式是跨 API 的。」

---

## 🎯 一句话总结

> GPUImage 的核心价值不是某个具体的滤镜实现，而是 FBO 串联纹理传递的滤镜链设计模式——理解了这个，你在任何 GPU API（GL/Metal/Vulkan）上都能搭出一套滤镜管线。

## 🔗 关联文档

- [[00-iOS音视频开发全景导读]] — iOS 媒体栈全景
- [[01-AVFoundation采集详解]] — 拿到 CVPixelBuffer 之后怎么喂给滤镜
- [[../ffmpeg/00-FFmpeg全景导读]] §3.6 — 加工问题（libavfilter 滤镜图的通用思想，和 GPUImage 滤镜链的架构思路一致）
