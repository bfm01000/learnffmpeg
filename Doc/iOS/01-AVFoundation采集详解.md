# AVFoundation 视频采集详解：面试速记与原理详解

## 0. 本篇定位

- 面试复习：先掌握 `AVCaptureSession`、input、output、preset、activeFormat、回调队列和前后摄像头切换。
- 深入学习：重点看 `CMSampleBuffer`、`CVPixelBuffer`、丢帧策略、曝光/对焦控制和采集线程约束。
- 工程落点：采集输出最好直接进入 `CVPixelBuffer`/Metal/VideoToolbox 链路，避免无意义的 CPU 格式转换。
> **适用方向**：iOS 移动端音视频 SDK 开发，特别是**视频拍摄/直播采集**方向
> **前置知识**：了解 iOS 媒体栈三层架构（见 [[00-iOS音视频开发全景导读]]），最好有 Android Camera2 经验
> **难度**：⭐⭐⭐（1-5 星）
> **预计阅读**：速记 10 分钟｜全文 35 分钟
> **关联文档**：[[00-iOS音视频开发全景导读]] · [[../ffmpeg/15-iOS硬件编解码]] · [[../OC/【重点】OC面试高频考点与标准回答大全]]
> **定位**：🟢 中级必会 —— 这是 iOS 拍摄端的入口，面试问「iOS 怎么做视频采集」必答

---

## 一、全景导读：AVFoundation 采集在 iOS 媒体栈的位置

### 1.1 从场景说起

你要做一个 iOS 端的美颜相机 App，或者一个直播 SDK——第一步就是打开 iPhone 摄像头，拿到每一帧 YUV 画面。在 Android 上，你用的是 Camera2 API，拿到 Image 或 Surface 里的数据。在 iOS 上，入口只有一个：**AVCaptureSession**。

和 Android Camera2 最大的不同是：AVCaptureSession 的抽象层次更高，你不用操心 session.configure()/capture()/repeating request 那一套——**你把输入（camera）+ 输出（video data output）连到 session 上，startRunning，数据就自动流过来了**。

但「省心」不等于「没坑」。输出格式选错了、回调队列阻塞了、前后摄切换时机不对了、session 的 preset 和实际分辨率不一致——这些都是真实的生产事故。

### 1.2 AVFoundation 采集是干什么的

**一句话定义**：AVCaptureSession 是 iOS 上摄像头、麦克风等采集设备的统一管理会话，负责把采集设备的原始数据路由到一个或多个输出端。

**核心解决什么问题**：
- 屏蔽不同 iPhone 型号的摄像头差异（双摄、三摄、LiDAR）
- 提供统一的像素格式输出（NV12，全平台一致）
- 管理采集生命周期（前后摄像头切换、中断恢复、权限管理）

**什么时候用 AVFoundation vs 其他**：

| 场景 | 用什么 |
|------|--------|
| 拿到摄像头每一帧 YUV 做自定义处理 | AVCaptureSession + AVCaptureVideoDataOutput |
| 只录个视频文件，不需要中间处理 | AVCaptureSession + AVCaptureMovieFileOutput |
| 拿到未压缩的静态照片做处理 | AVCaptureSession + AVCapturePhotoOutput |
| 屏幕录制（录 App 自己的画面） | ReplayKit |

### 1.3 一张图：AVCaptureSession 的「水管」模型

```
AVCaptureDevice         AVCaptureSession            AVCaptureOutput
(物理摄像头)              (水管枢纽)                  (数据出口)

┌─────────────┐         ┌───────────────┐         ┌──────────────────────┐
│ 后置摄像头    │──input──│               │──output──│ AVCaptureVideoData-  │
│ (广角/超广角/ │         │               │         │ Output (每帧 YUV 回调) │
│  长焦)       │         │               │         └──────────────────────┘
└─────────────┘         │ AVCapture     │         ┌──────────────────────┐
                        │ Session       │──output──│ AVCaptureMovieFile-  │
┌─────────────┐         │               │         │ Output (.mp4)        │
│ 麦克风       │──input──│               │         └──────────────────────┘
└─────────────┘         │               │         ┌──────────────────────┐
                        │               │──output──│ AVCapturePhotoOutput │
                        └───────────────┘         │ (静态照片)             │
                                                  └──────────────────────┘

每个 output 可以有自己的回调队列（串行）
一个 session 可以同时有多个 output（预览用 video output，录制用 movie output）
```

### 1.4 Android Camera2 ↔ iOS AVCaptureSession 对照

| 概念 | Android Camera2 | iOS AVFoundation |
|------|----------------|------------------|
| 摄像头管理 | CameraManager | AVCaptureDeviceDiscoverySession |
| 采集会话 | CameraCaptureSession | AVCaptureSession |
| 采集请求 | CaptureRequest (builder) | AVCaptureConnection（自动管理） |
| 输出 YUV | ImageReader | AVCaptureVideoDataOutput |
| 输出 Surface | Surface | AVCaptureVideoDataOutput（直接给 CVPixelBuffer） |
| 预览 | SurfaceView / TextureView | AVCaptureVideoPreviewLayer 或自渲染 |
| 格式协商 | stream configuration map | session preset + device format |
| 线程模型 | 自己管理（Handler） | 指定 serial queue |
| 3A | AE/AF/AWB mode | focusMode / exposureMode / whiteBalanceMode |
| 前后摄切换 | 重新 openCamera | 通过 session beginConfiguration 切换 |

### 1.5 学习优先级

| 层级 | 内容 | 重要度 |
|------|------|--------|
| 🟢 中级必会 | AVCaptureSession 搭基本采集链路 | 🔥🔥🔥 |
| 🟢 中级必会 | AVCaptureVideoDataOutput 拿 YUV | 🔥🔥🔥 |
| 🟢 中级必会 | 像素格式（NV12）和 CVPixelBuffer | 🔥🔥🔥 |
| 🟡 高级加分 | session preset vs device format 的精确控制 | 🔥🔥 |
| 🟡 高级加分 | 多摄切换与虚拟摄像头（builtInDualCamera） | 🔥🔥 |
| 🟡 高级加分 | 手动曝光/对焦/白平衡 | 🔥🔥 |
| 🔵 专家深水区 | depth data / portrait effect matte | 🔥 |

---

## 二、面试速记（考前 10 分钟扫一遍）

### 2.1 高频考点速查

| # | 考点 | 一句话答案 | 频率 | 难度 |
|---|------|-----------|------|------|
| 1 | AVCaptureSession 怎么搭 | 创建 session → 选 device → 创建 input → 创建 output → add 到 session → startRunning | 🔥🔥🔥 | 🟢 |
| 2 | 怎么拿到每一帧 YUV | AVCaptureVideoDataOutput + setSampleBufferDelegate | 🔥🔥🔥 | 🟢 |
| 3 | 输出格式是什么 | NV12（BiPlanar，Y 平面 + UV 交错平面），不是 YUV420P | 🔥🔥🔥 | 🟢 |
| 4 | 回调在哪个线程 | 你在 addOutput 时指定的串行 queue，**不是主线程** | 🔥🔥 | 🟢 |
| 5 | 怎么控制分辨率/帧率 | sessionPreset（粗粒度）或 device.activeFormat（细粒度） | 🔥🔥 | 🟢 |
| 6 | 前后摄怎么切换 | beginConfiguration → remove/change input → commitConfiguration | 🔥🔥 | 🟢 |
| 7 | 权限怎么处理 | Info.plist 加 NSCameraUsageDescription + AVAuthorizationStatus | 🔥🔥 | 🟢 |
| 8 | 回调丢了是什么原因 | 队列被阻塞、或者 processing 耗时超过帧间隔导致 drop | 🔥🔥 | 🟡 |

### 2.2 面试标准回答

#### Q1：给我写一段 iOS 打开摄像头并拿到每一帧视频数据的代码。

**面试官想听什么：** 你能不能说清完整的 setup 流程，而不是只记得一两个 API 名字。

**🗣️ 标准回答（可背诵）：**

> "整个过程分五步。第一步查权限：用 AVAuthorizationStatus 检查摄像头权限，没授权的先 requestAccess。第二步创建设备：用 AVCaptureDeviceDiscoverySession 查找后置摄像头，通常传 builtInWideAngleCamera 作为 deviceType。第三步创建输入输出：用 device 创建 AVCaptureDeviceInput，创建 AVCaptureVideoDataOutput 设置输出格式为 kCVPixelFormatType_420YpCbCr8BiPlanarFullRange——也就是 NV12。第四步组装 session：把 input 和 output 加到 AVCaptureSession，output 指定一个串行 queue 作为回调队列。第五步启动：session startRunning。之后每一帧都会在 setSampleBufferDelegate 回调里拿到 CMSampleBuffer，从中用 CMSampleBufferGetImageBuffer 取出 CVPixelBuffer——这就是 NV12 格式的 YUV 数据。这里有个细节：回调里不要做耗时操作，否则会丢帧，通常的做法是把 CVPixelBuffer retain 一下后立即返回，在另一条线程异步处理。"

**👨‍💻 追问预警：**
> Q: "kCVPixelFormatType_420YpCbCr8BiPlanarFullRange 这个格式和 kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange 有什么区别？"
> A: FullRange 的 Y 范围是 0-255，VideoRange 的 Y 是 16-235（广播电视标准范围）。iPhone 默认采集是 FullRange，但 VideoToolbox 编码器默认期望 VideoRange。如果不一致可能导致画面偏亮或偏暗——一般用 libyuv 或者 Metal shader 做 range 转换。

---

#### Q2：采集的回调里要注意什么？丢帧了怎么办？

**面试官想听什么：** 你有没有实际的性能调优经验。

**🗣️ 标准回答（可背诵）：**

> "AVCaptureVideoDataOutput 的回调要注意三个事。第一，回调队列不能阻塞——它是个串行队列，摄像头给你每秒 30 帧，如果你处理一帧花了 100ms，后面的帧全被排在后面积压，表现出来的现象就是延时越来越大最后丢帧。解决办法很简单：回调里拿到 CVPixelBuffer 后立即 retain 然后 dispatch_async 到处理线程，回调函数本身快速返回。第二，如果处理能力确实跟不上采集速度（比如开了美颜耗性能），可以通过 AVCaptureConnection 的 videoMinFrameDuration 做主动降帧。第三，alwaysDiscardsLateVideoFrames 这个属性默认是 YES——意思是一帧还没处理完下一帧就来了的话，直接丢弃而不是排队——实时场景下保持 YES，离线录制可以设 NO。"

**👨‍💻 追问预警：**
> Q: "你说 retain CVPixelBuffer，为什么不是 copy？"
> A: 因为 CVPixelBuffer 底层是 IOSurface——内核管理的共享内存。retain 只是引用计数加一，数据还是同一份，不用拷贝。如果 copy，不仅慢，而且破坏了零拷贝链路。但 retain 之后要确保在使用完之前不要 release——通常用 dispatch_async 的 block 自动管理生命周期。

---

#### Q3：AVCaptureSession 的 preset 和 device 的 activeFormat 有什么区别？

**面试官想听什么：** 你理解 iOS 的分辨率控制不是单一维度。

**🗣️ 标准回答（可背诵）：**

> "这是 iOS 采集里最容易搞混的地方。sessionPreset 是一个高层的预设值，比如 AVCaptureSessionPreset1920x1080 告诉 session 我想要 1080p——但最终能不能达到、实际取什么格式，session 会自动选择。device.activeFormat 是底层、精确的控制——你指定具体的 AVCaptureDeviceFormat，包括分辨率、帧率范围、支持的像素格式。一般开发用 preset 就够了，但如果你要精确控制采集参数（比如做慢动作 240fps 或者用特定曝光时间），就必须切到 activeFormat。切换的方法是：先 lockForConfiguration，设 activeFormat 和 activeVideoMin/MaxFrameDuration，再 unlock。这里有个坑——切换 format 必须在 session 停止或 beginConfiguration 期间做，否则会崩溃。"

**👨‍💻 追问预警：**
> Q: "preset 设了 1920x1080，实际输出分辨率一定是 1920x1080 吗？"
> A: 不一定。取决于设备支持、方向、以及输出配置。实际分辨率可以通过 `CMVideoFormatDescriptionGetDimensions` 从 CMSampleBuffer 里拿——永远以实际值为准。

---

#### Q4：前后摄像头怎么切换才不闪屏？

**面试官想听什么：** 你有没有处理过切换过程的用户体验问题。

**🗣️ 标准回答（可背诵）：**

> "切换前先调 beginConfiguration，表示你要做一组原子修改。然后判断新摄像头是否存在、能不能加 input；能的话 remove 旧 input、add 新 input，然后 commitConfiguration——session 会自动重新协商各个 output 的连接参数。整个过程不会重建 session，所以预览不会黑屏或闪。但注意：commitConfiguration 是同步的，会短暂阻塞当前线程（通常几十毫秒），所以不要在音频回调这种高优先级线程里做。如果是因为用户点了个切换按钮，这几十毫秒完全可接受。另外，iOS 10 之后支持 builtInDualCamera、builtInTripleCamera 这些虚拟设备，切换广角和长焦实际上是在同一个 device 里切换镜头——这时候用户体验更平滑，只需要改 device.videoZoomFactor。"

**👨‍💻 追问预警：**
> Q: "切换过程中怎么确保预览不中断？"
> A: 在 begin/commit 之间只改 input，不改 output 和 preview layer 的连接——preview layer 是连在 session 上的，input 切换不影响它的数据流。同时展示层可以配合一个短暂的前一帧 freeze，视觉上完全无感。

---

#### Q5：你实际做项目时遇到过哪些采集相关的坑？

**面试官想听什么：** 实战经验，不是背文档。

**🗣️ 标准回答（可背诵）：**

> "说三个我印象比较深的。第一个是 orientation 问题——摄像头采集的物理方向永远是横屏，但实际拿到的 CVPixelBuffer 里像素已经是根据设备方向旋转过了。如果你同时用 AVCaptureVideoPreviewLayer 和自己渲染，要注意 preview layer 自动处理了旋转而你自己渲染时 CVPixelBuffer 的宽高可能和你预期不一样——需要从 AVCaptureConnection 的 videoOrientation 来判断。第二个是闪光灯和 torch——锁屏或进后台时 session 会自动停，但 torch 不会自动关——如果你没在 UIApplicationDidEnterBackground 通知里手动关手电筒，用户切回来发现手电筒还亮着，会被投诉。第三个是特定机型限制——比如 iPhone 的多摄设备，如果你指定了 builtInUltraWideCamera（超广角）、但用户手机根本没有，创建 device input 会返回 nil——必须做 fallback。我一般封装一个 findBestCamera 函数，按优先级尝试多个 deviceType。"

**⚠️ 常见误区（接在上面的回答中自然带出）：** 很多人以为拿到 CVPixelBuffer 后 `CVPixelBufferLockBaseAddress` 就能读，殊不知摄像头输出的 CVPixelBuffer 在 GPU 上，如果你 Lock 并尝试 CPU 读，底层会做一次 GPU→CPU 拷贝（非常慢），零拷贝链断裂。正确做法是通过 Metal/CoreVideo 纹理缓存直接访问。

---

## 三、核心代码：一个完整的视频采集 Demo

> 以下代码演示了开摄像头 → 拿 YUV → 异步处理的完整流程。可直接在 iOS 项目中使用。

```objc
// VideoCaptureManager.h
#import <AVFoundation/AVFoundation.h>

@interface VideoCaptureManager : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) dispatch_queue_t videoQueue;
@property (nonatomic, assign) BOOL isRunning;

- (void)startCapture;
- (void)stopCapture;
- (void)switchCamera;

@end
```

```objc
// VideoCaptureManager.m
#import "VideoCaptureManager.h"

@implementation VideoCaptureManager {
    AVCaptureDevice *_currentCamera;
    AVCaptureDeviceInput *_currentInput;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _videoQueue = dispatch_queue_create("com.video.capture", DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

// ========== 第一步：权限 ==========
- (void)requestPermission:(void(^)(BOOL granted))completion {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (status == AVAuthorizationStatusAuthorized) {
        completion(YES);
    } else if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:completion];
    } else {
        completion(NO);
    }
}

// ========== 第二步：找摄像头 ==========
- (AVCaptureDevice *)findBestCamera {
    // 按优先级尝试：广角 → 超广角 → 长焦
    NSArray *types = @[
        AVCaptureDeviceTypeBuiltInWideAngleCamera,
        AVCaptureDeviceTypeBuiltInUltraWideCamera,
        AVCaptureDeviceTypeBuiltInTelephotoCamera
    ];
    AVCaptureDeviceDiscoverySession *discovery =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionBack];
    // 优先取广角，没有就取第一个可用设备
    return discovery.devices.firstObject;
}

// ========== 第三步：搭建 session ==========
- (void)setupSession {
    self.session = [[AVCaptureSession alloc] init];
    self.session.sessionPreset = AVCaptureSessionPreset1920x1080;

    // 摄像头输入
    _currentCamera = [self findBestCamera];
    if (!_currentCamera) {
        NSLog(@"❌ 没有可用摄像头");
        return;
    }
    NSError *error = nil;
    _currentInput = [AVCaptureDeviceInput deviceInputWithDevice:_currentCamera error:&error];
    if (error) {
        NSLog(@"❌ 创建摄像头输入失败: %@", error);
        return;
    }
    if ([self.session canAddInput:_currentInput]) {
        [self.session addInput:_currentInput];
    }

    // 视频数据输出
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    // ⚠️ 关键：格式必须是摄像头的原生格式 NV12，改了就会触发内部拷贝
    NSDictionary *settings = @{
        (id)kCVPixelBufferPixelFormatTypeKey:
            @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
    };
    videoOutput.videoSettings = settings;
    videoOutput.alwaysDiscardsLateVideoFrames = YES; // 实时场景保持 YES
    [videoOutput setSampleBufferDelegate:self queue:self.videoQueue];
    if ([self.session canAddOutput:videoOutput]) {
        [self.session addOutput:videoOutput];
    }
}

// ========== 第四步：启动/停止 ==========
- (void)startCapture {
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        // startRunning 是同步阻塞的，不要在 UI 线程调
        [self.session startRunning];
        self.isRunning = YES;
    });
}

- (void)stopCapture {
    dispatch_async(self.videoQueue, ^{
        [self.session stopRunning];
        self.isRunning = NO;
    });
}

// ========== 第五步：切换摄像头（不闪屏）==========
- (void)switchCamera {
    AVCaptureDevicePosition targetPosition =
        (_currentCamera.position == AVCaptureDevicePositionBack)
            ? AVCaptureDevicePositionFront
            : AVCaptureDevicePositionBack;

    NSArray *types = @[AVCaptureDeviceTypeBuiltInWideAngleCamera];
    AVCaptureDeviceDiscoverySession *discovery =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                               mediaType:AVMediaTypeVideo
                                                                position:targetPosition];
    AVCaptureDevice *newCamera = discovery.devices.firstObject;
    if (!newCamera) return;

    AVCaptureDeviceInput *newInput =
        [AVCaptureDeviceInput deviceInputWithDevice:newCamera error:nil];
    if (!newInput) return;

    // 原子切换：begin/commit 之间不会闪
    [self.session beginConfiguration];
    [self.session removeInput:_currentInput];
    if ([self.session canAddInput:newInput]) {
        [self.session addInput:newInput];
        _currentInput = newInput;
        _currentCamera = newCamera;
    } else {
        // fallback：加回旧的 input
        [self.session addInput:_currentInput];
    }
    [self.session commitConfiguration];
}

// ========== 第六步：核心回调（每帧 YUV）==========
- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {

    // ⚠️ 这个回调在 videoQueue 上，不能阻塞

    // 取 PTS（来自摄像头硬件时钟）
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);

    // 取像素数据
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    // ⚠️ 不要在这里 Lock 读 CPU 数据！数据在 GPU 显存
    // ⚠️ 不要在这里做耗时操作（编码、渲染等），丢帧的源头就是这里阻塞
    // ✅ 正确的做法：retain 后异步处理

    CVPixelBufferRetain(pixelBuffer); // 引用计数 +1，保证异步处理时不被回收

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
        // 在这里做耗时操作：编码、渲染、美颜...
        // 比如：VideoToolbox 编码
        // VTCompressionSessionEncodeFrame(compressionSession, pixelBuffer, pts, ...);

        // 或：Metal 渲染
        // [metalRenderer renderPixelBuffer:pixelBuffer];

        CVPixelBufferRelease(pixelBuffer); // 处理完释放
    });
}

@end
```

### 关键设计决策解读（对应上面的代码）

**1. 为什么 `videoSettings` 要设成 NV12 而不是让系统自选？**
明确设为 `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange` 是为了保证输出格式可预测——VideoToolbox 编码器接受 NV12、Metal 零拷贝也基于 NV12。如果让系统自选，它可能在某些机型上给了 YUV420P，你在后续链路里要额外做格式转换。

**2. 为什么 `alwaysDiscardsLateVideoFrames = YES`？**
实时直播/预览场景下，你宁愿丢帧也不希望延时累积。设成 YES 后，队列里最多 pending 一帧——新的来了旧的被扔掉。如果是离线高质量录制，设成 NO。

**3. 为什么 `startRunning` 不在 UI 线程调？**
`startRunning` 会做 IOKit 调用（打开摄像头硬件），同步阻塞几十到上百毫秒。主线程阻塞超过 16ms 就会掉帧。所以必须扔到后台线程。

**4. 为什么不 `CVPixelBufferLockBaseAddress` 读数据？**
摄像头输出的 CVPixelBuffer 在 GPU 显存（IOSurface backed），`LockBaseAddress` 会触发 GPU→CPU 的回读通路，一块 1080p NV12 数据大约 3MB，拷贝到 CPU 内存大约 2-5ms。如果你每帧都 Lock，就是每 33ms 浪费 2-5ms 在无意义的内存拷贝上。正确做法是通过 Metal 的 `CVMetalTextureCache` 或 `CVOpenGLESTextureCache` 直接在 GPU 上访问。

---

## 四、常见踩坑实录

### 坑 1：session 配置在 startRunning 后不生效

**现象**：你已经 `startRunning` 了，然后又去改 preset 或加 output，发现不生效。

**根因**：大部分 session 配置（preset、input、output）必须在 session 未运行时或 `beginConfiguration`/`commitConfiguration` 之间修改。

**正确做法**：
```objc
// ❌ 错误
[session startRunning];
session.sessionPreset = AVCaptureSessionPreset1280x720; // 不生效或报错

// ✅ 正确
[session beginConfiguration];
session.sessionPreset = AVCaptureSessionPreset1280x720;
[session commitConfiguration];
```

### 坑 2：CVPixelBuffer 的宽高和摄像头分辨率不一致

**现象**：preset 设了 1920x1080，但拿到的 CVPixelBuffer 宽高是 1080x1920。

**根因**：摄像头物理方向永远是横屏（landscape），CVPixelBuffer 的宽高反映的是**传感器方向**。如果你设备竖着拿，`CVPixelBufferGetWidth` 返回的是传感器的宽（1920），`CVPixelBufferGetHeight` 返回的是传感器的高（1080），和你的屏幕坐标系是反的。

**正确做法**：通过 `AVCaptureConnection.videoOrientation` 判断当前方向，自行做宽高判断：
```objc
size_t width = CVPixelBufferGetWidth(pixelBuffer);
size_t height = CVPixelBufferGetHeight(pixelBuffer);
// width/height 是传感器的宽高，结合 videoOrientation 判断实际方向
```

### 坑 3：设备锁（lockForConfiguration）忘了解锁

**现象**：切换了 activeFormat 之后，摄像头完全不输出帧了、或者崩溃。

**根因**：`lockForConfiguration` 拿到的是硬件层的排他锁——在 unlock 之前，设备的采集流水线是暂停的。如果没调 unlock 或者异常路径没解锁，设备永久锁死。

**正确做法**：
```objc
NSError *error = nil;
if ([device lockForConfiguration:&error]) {
    device.activeVideoMinFrameDuration = CMTimeMake(1, 30);
    device.activeVideoMaxFrameDuration = CMTimeMake(1, 30);
    [device unlockForConfiguration];
} else {
    NSLog(@"lock failed: %@", error);
}
// ⚠️ 不要用 try-finally（OC 没有），确保每个 lock 都有对应的 unlock
```

### 坑 4：后台停止采集后，torch 忘了关

**现象**：用户切到后台，摄像头采集停了，但手电筒（torch）还亮着。

**根因**：`stopRunning` 自动关摄像头但不自动关 torch——torch 是 device 属性，独立于 session 生命周期。

**正确做法**：在 `UIApplicationDidEnterBackgroundNotification` 里手动关：
```objc
if (_currentCamera.hasTorch && _currentCamera.torchActive) {
    [_currentCamera lockForConfiguration:nil];
    _currentCamera.torchMode = AVCaptureTorchModeOff;
    [_currentCamera unlockForConfiguration];
}
```

---

## 五、进阶话题：手动控制曝光和对焦

对于拍摄/美颜 SDK，很多时候需要手动控制 3A：

```objc
// 手动设置焦点
- (void)focusAtPoint:(CGPoint)point {
    if (!_currentCamera.isFocusPointOfInterestSupported) return;
    [_currentCamera lockForConfiguration:nil];
    _currentCamera.focusPointOfInterest = point;   // (0,0)~(1,1)，屏幕归一化坐标
    _currentCamera.focusMode = AVCaptureFocusModeAutoFocus;
    [_currentCamera unlockForConfiguration];
}

// 手动曝光
- (void)setExposureBias:(float)bias {
    if (!_currentCamera.isExposurePointOfInterestSupported) return;
    [_currentCamera lockForConfiguration:nil];
    // bias 范围 [-8, 8]，EV 单位，查询 max/minExposureTargetBias
    _currentCamera.setExposureTargetBias = bias;
    [_currentCamera unlockForConfiguration];
}

// 锁白平衡（防止室外光线变化导致的色温跳动）
- (void)lockWhiteBalance {
    [_currentCamera lockForConfiguration:nil];
    _currentCamera.whiteBalanceMode = AVCaptureWhiteBalanceModeLocked;
    [_currentCamera unlockForConfiguration];
}
```

---

## 一句话总结
> iOS 视频采集就是把 AVCaptureDevice（摄像头）和 AVCaptureVideoDataOutput（数据出口）挂到 AVCaptureSession 上，startRunning 后你就在回调里收 NV12 的 CVPixelBuffer——核心原则是回调不阻塞、数据不在 CPU 上读、切换用 beginConfiguration 不闪屏。

## 关联文档
- [[00-iOS音视频开发全景导读]] — 整个 iOS 媒体栈全景图
- [[../ffmpeg/15-iOS硬件编解码]] — VideoToolbox：拿到 CVPixelBuffer 后怎么编码
- [[02-AudioUnit与音频处理详解]] — iOS 音频采集/播放
- [[03-GPUImage滤镜链详解]] — GPUImage：拿到 YUV 后怎么加滤镜
