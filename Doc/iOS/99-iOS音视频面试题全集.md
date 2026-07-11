# iOS 音视频开发面试题全集：中高级岗位

> **适用方向**：iOS 移动端音视频 SDK 开发（采集/编解码/渲染/推流/播放 全链路）
> **适用级别**：中级（3-5年）、高级（5年+）
> **前置知识**：本系列文档的全部内容 + 一定的跨平台音视频开发经验
> **关联文档**：本系列 00-09 全部文档 · [[../ffmpeg/15-iOS硬件编解码]] · [[../ffmpeg/00-FFmpeg全景导读]]
> **定位**：🔥🔥🔥 面试前最后一遍的系统复习

---

## 使用说明

本文档覆盖 **30+ 道高频面试题**，每道题包含：
- **面试官意图**：ta 想考察什么
- **难度 / 频率**：⭐⭐⭐ 难度 + 🔥🔥🔥 出现频率
- **标准回答**：可直接背诵的口语化答案
- **追问预警**：常见追问及应对
- **关联知识点**：指向系列文档的具体章节

---

## 一、架构与全景（4 题）

### Q1：iOS 媒体栈的三层架构是什么？什么时候用哪一层？

**面试官意图**：考察你是否理解 iOS 媒体框架的分层设计，有没有架构思维。

**难度/频率**：⭐⭐ / 🔥🔥🔥

**标准回答**：

> iOS 媒体栈分三层。最上面是 AVFoundation——高层封装，省心但黑盒。典型用法是 AVCaptureSession 采集、AVAssetWriter 录制、AVPlayer 播放。适合"我只想录个文件/播个视频"的场景。
>
> 中间是 VideoToolbox / AudioToolbox / CoreMedia——中层精细控制。VTCompressionSession/VTDecompressionSession 直接控编解码，能逐帧拿到压缩的 CMSampleBuffer，能控低延迟、码率、关键帧。适合实时推流/RTC/低延迟场景。
>
> 最下面是 Apple Media Engine——物理编解码硬件（A 系/M 系芯片里的专用电路），你不直接碰，VT 替你调。
>
> 判断标准：需要控比特流就下到 VT/Toolbox 层，只是录播就用 AVFoundation。

**追问预警**：
- "AVFoundation 底层是不是也是调的 VideoToolbox？" → 是，AVAssetWriter 内部就是用 VTCompressionSession，AVPlayer 内部就是用 VTDecompressionSession，但它把这些封装成了黑盒。

**关联**：[[00-iOS音视频开发全景导读]] §1.3

---

### Q2：iOS 和 Android 做跨平台音视频 SDK，最大的差异是什么？

**面试官意图**：考察跨平台实战经验和平台差异理解。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 说四个最大差异。第一，比特流格式相反——VideoToolbox 吐 AVCC（4 字节长度前缀）、MediaCodec 吐 Annex-B（起始码），推流时 iOS 要转格式而 Android 不用。第二，硬件碎片化——iOS 只有苹果自家芯片，行为完全一致，Android 要适配高通/联发科/三星各家 MediaCodec 的不同行为。第三，GPU API——iOS 只能用 Metal（OpenGL ES 已废弃），Android 用 OpenGL ES/Vulkan。第四，FFmpeg 支持——iOS 上编解码都能走 FFmpeg 的 VT wrapper，Android 上编码 FFmpeg 支持受限，生产环境硬编基本直调 MediaCodec。
>
> 架构上一般是 C++ 核心引擎 + 平台桥接层：OC++ 桥接 iOS 的 AVFoundation/VideoToolbox/Metal，JNI 桥接 Android 的 Camera2/MediaCodec/OpenGL。

**追问预警**：
- "那你怎么设计跨平台的 C++ 层？" → C++ 层负责音视频同步、码控算法、网络推流、协商等纯逻辑；平台桥接层负责采集/编解码/渲染这些依赖硬件平台的操作。数据统一转成自定义 VideoFrame 结构进出 C++ 引擎。

**关联**：[[00-iOS音视频开发全景导读]] §1.4 · [[../ffmpeg/15-iOS硬件编解码]] §十

---

### Q3：iOS 上做实时视频处理的完整技术栈有哪些？

**面试官意图**：是否了解 iOS 视频处理的全貌。

**难度/频率**：⭐⭐ / 🔥🔥

**标准回答**：

> 采集层：AVFoundation (AVCaptureSession) 拿 NV12 CVPixelBuffer。
> 处理层：Metal shader 做美颜/滤镜/特效，或用 Core Image，或用 GPUImage 滤镜链。
> 编码层：VideoToolbox (VTCompressionSession) 硬编 H.264/HEVC。
> 渲染层：Metal (MTKView + CVMetalTextureCache) 零拷贝渲染 YUV→RGB。
> 音频层：AudioUnit RemoteIO 采集播放，AudioSession 管策略。
> 传输层：RTMP/WebRTC/SRT 等（C++ 跨平台实现）。
> 存储层：AVAssetWriter 录 MP4。
>
> 每层都有替代方案——采集可以换 ReplayKit（屏幕录制）、处理可以换 Vision 框架（AI 检测）、渲染可以用 AVSampleBufferDisplayLayer（省事但不灵活）。

**关联**：[[00-iOS音视频开发全景导读]] §1.5

---

### Q4：为什么大家都说 iOS 比 Android 省心？

**面试官意图**：你是否真正在两个平台都写过音视频代码。

**难度/频率**：⭐⭐ / 🔥🔥

**标准回答**：

> 两个原因。第一，芯片统一——苹果就一家 Media Engine，行为一致，没有 Android 那种高通和联发科 MediaCodec 行为不同、颜色格式 NV12/NV21/YUV420P 碎片化、某些厂商私有 tiled 格式的问题。第二，FFmpeg 支持——iOS 上编码用 h264_videotoolbox、解码 -hwaccel videotoolbox 都很成熟。Android 上 FFmpeg 6.0 才有 h264_mediacodec 编码器，且能力受限、多数生产环境还是直调系统 API。
>
> 但 iOS 也有自己的坑——比如 AVCC→Annex-B 转换、SPS/PPS 在 CMVideoFormatDescription 里而不是数据流里、后台 session 失效——只是这些是「设计层面的坑」，而 Android 是「设备碎片化的坑」。

**关联**：[[../ffmpeg/15-iOS硬件编解码]] §十

---

## 二、采集（5 题）

### Q5：AVCaptureSession 的完整搭建流程是怎样的？

**面试官意图**：是否能从零搭出视频采集链路。

**难度/频率**：⭐⭐ / 🔥🔥🔥

**标准回答**：

> 六步。1）查权限：AVAuthorizationStatus。2）找设备：AVCaptureDeviceDiscoverySession 查后置/前置摄像头。3）创建输入：device → AVCaptureDeviceInput。4）创建输出：AVCaptureVideoDataOutput，设 pixel format 为 NV12（原生格式），设 alwaysDiscardsLateVideoFrames=YES，指定回调队列。5）组装 session：addInput + addOutput。6）后台线程 startRunning——不要在 UI 线程调，它会做 IOKit 调用同步阻塞几十到上百毫秒。
>
> 回调里拿 CMSampleBuffer → CMSampleBufferGetImageBuffer → CVPixelBuffer（NV12）。这里关键：回调里不阻塞（否则丢帧），不做 CPU Lock（零拷贝断裂）。

**追问预警**：
- "为什么 pixel format 必须设 NV12？" → 因为 iPhone 摄像头原生输出 NV12，设其他格式系统会做内部拷贝转格式，零拷贝直接断掉。VideoToolbox 编码器也期望 NV12。
- "startRunning 为什么不能在主线程？" → 它会做硬件 IOKit 调用，同步阻塞。主线程阻塞超过 16ms 就会掉帧。

**关联**：[[01-AVFoundation采集详解]] §三

---

### Q6：采集回调里丢帧了怎么办？

**面试官意图**：是否有性能调优经验。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 从三个层面排查。第一层，回调本身——回调队列是串行的，如果处理一帧花了超过帧间隔（30fps=33ms），后面的帧会积压。解决办法是回调里只做 retain + dispatch，不阻塞。第二层，系统层——alwaysDiscardsLateVideoFrames 设为 YES 后，系统在队列积压时会自动丢弃旧帧。第三层，业务层——如果处理能力确实跟不上（比如美颜耗性能），通过 AVCaptureConnection.videoMinFrameDuration 主动降帧率（30→20→15）。
>
> 排查工具可以用 Instruments 的 Time Profiler 看回调线程的耗时分布，找到瓶颈函数。

**追问预警**：
- "alwaysDiscardsLateVideoFrames = YES 和 NO 的区别？" → YES 时队列里只保留最新一帧，旧帧直接丢弃——实时场景选 YES（保延时不保画质）。NO 时帧都排队等处理——离线录制选 NO（保画质不保延时）。

**关联**：[[01-AVFoundation采集详解]] §三

---

### Q7：CVPixelBuffer 和 CMSampleBuffer 是什么关系？

**面试官意图**：是否理解 iOS 核心数据容器的层次。

**难度/频率**：⭐⭐ / 🔥🔥🔥

**标准回答**：

> CMSampleBuffer 是外层的通用容器，里面包着三样东西：CMTime（PTS/DTS）、CMVideoFormatDescription（格式描述）、以及实际的媒体数据。实际数据分两种情况——如果是未压缩的视频帧，就是 CVPixelBuffer；如果是压缩后的 H.264 帧，就是 CMBlockBuffer。CVPixelBuffer 底层的物理存储是 IOSurface——一块内核管理的 GPU 共享内存。这是 iOS 零拷贝的基础。
>
> 一个常见的混淆点：AVCaptureVideoDataOutput 吐出的 CMSampleBuffer 包着 CVPixelBuffer（未压缩），VideoToolbox 编码输出的 CMSampleBuffer 包着 CMBlockBuffer（压缩的 H.264 比特流）。同一个类型，采集侧和编码侧装的东西不同。

**追问预警**：
- "为什么要用 CVPixelBufferPool？" → 避免每帧 malloc/free 的内存抖动。预分配 N 个 buffer，取出来用完还回去——和 FFmpeg 里循环复用 AVFrame 是同一思想。

**关联**：[[../ffmpeg/15-iOS硬件编解码]] §二

---

### Q8：前后摄像头怎么切换才不闪屏？

**面试官意图**：是否处理过切换过程中的用户体验问题。

**难度/频率**：⭐⭐ / 🔥🔥

**标准回答**：

> 核心是用 beginConfiguration/commitConfiguration 做原子切换。先调 beginConfiguration，然后 remove 旧 input、add 新 input，最后 commitConfiguration。整个过程 session 不重建、preview layer 的连接不断——预览不会黑屏也不会闪。commitConfiguration 是同步的，会短暂阻塞几十毫秒，可接受。iOS 10+ 的多摄设备（builtInDualCamera、builtInTripleCamera）切换广角和长焦其实是在同一个 device 里切镜头，体验更平滑——只需改 videoZoomFactor。

**关联**：[[01-AVFoundation采集详解]] §二 Q4

---

### Q9：你实际项目里遇到过哪些采集相关的坑？

**面试官意图**：实战经验，不是背文档。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 说三个。第一个是宽高方向——摄像头传感器的物理方向是横屏，但 CVPixelBuffer 的像素经过了自动旋转，而 preview layer 也做了自动旋转——如果你自己渲染和 preview layer 同时用，宽高可能对不上。要从 AVCaptureConnection 的 videoOrientation 判断实际方向。第二个是 torch 忘关——stopRunning 只停采集不停手电筒，后台通知里要手动关。第三个是特定机型 fallback——比如指定了超广角但设备没有（老 iPhone），createDeviceInput 会返回 nil，必须做 fallback 逻辑——我一般封装 findBestCamera 函数按优先级尝试多个 deviceType。

**关联**：[[01-AVFoundation采集详解]] §二 Q5

---

## 三、编码（6 题）

### Q10：VideoToolbox 编码的完整生命周期？

**面试官意图**：是否真正写过编码代码。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 五步：Create → SetProperty → EncodeFrame (loop) → CompleteFrames (flush) → Invalidate。创建时指定宽高 + H.264 编码类型 + 输出回调。创建后立即设低延迟属性——RealTime=true + AllowFrameReordering=false + AverageBitRate + MaxKeyFrameInterval。编码是异步的：EncodeFrame 返回不代表编完了，结果通过你在创建时注册的输出回调给。回调里拿到 CMSampleBuffer（AVCC 格式），从中取 CMBlockBuffer 和数据。结束前必须调 CompleteFrames 把硬件管线里排队中的帧 flush 出来，否则最后几帧丢掉。用完调 Invalidate + CFRelease。

**追问预警**：
- "EncodeFrame 传的 CMTime 怎么算？" → timescale 一般用 fps（如 30）或 1000（毫秒）。CMTimeMake(frameIndex, 30)。注意 timescale 不要和 PTS 值混淆——PTS 值 = frameIndex，表示第几帧。

**关联**：[[05-VideoToolbox硬编码实战]] §三

---

### Q11：实时编码为什么必须关 B 帧？还有哪些低延迟属性？

**面试官意图**：是否理解编码器的延迟来源。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> B 帧是双向预测帧——编码时要参考未来帧。这就引入了重排序延迟——编码器要等后面的帧到了才能编当前帧，通常引入 5-10 帧延迟（30fps 下 165-330ms），对实时通信不可接受。关 B 帧在 VT 里是 AllowFrameReordering=false——名字暗示了本质：B 帧让编码顺序 ≠ 显示顺序，需要重排序。实时编码必设的属性组合：
> - RealTime = true（不做多 pass 搜索、不用 lookahead）
> - AllowFrameReordering = false（禁用 B 帧）
> - MaxKeyFrameInterval = fps × 2（短 GOP，2 秒一个 IDR）
> - AllowOpenGOP = false（闭合 GOP，每个 IDR 是独立随机切入点）
> - ExpectedFrameRate = 实际帧率（助编码器做码率预算）
>
> 这个组合 ≈ x264 的 -tune zerolatency。

**追问预警**：
- "关了 B 帧画质会下降多少？" → 同等码率下 PSNR 约降 0.2-0.5dB。但实时场景延迟比画质重要得多。补偿方式：给稍高一点的码率。

**关联**：[[05-VideoToolbox硬编码实战]] §四 · [[../ffmpeg/15-iOS硬件编解码]] §五

---

### Q12：编码输出的 SPS/PPS 在哪？怎么取出来？

**面试官意图**：是否踩过"推流没画面"的坑。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 这是 iOS 最经典的坑——SPS/PPS 不在编码输出的 CMBlockBuffer 里，而是单独存放在 CMSampleBuffer 的 CMVideoFormatDescription 里。用 CMVideoFormatDescriptionGetH264ParameterSetAtIndex 取——index 0 是 SPS、index 1 是 PPS。HEVC 多一个 VPS（index 0），SPS 和 PPS 分别是 index 1 和 2。取出来的是纯 NALU 数据——不含起始码也不含长度前缀，你要自己在前面加 00 00 00 01 起始码（或长度前缀）。推 RTP/RTMP 前必须在每个 IDR 前面注入 SPS + PPS，否则对端解码器建不起来，典型表现是"只有声音没画面"。

**追问预警**：
- "SPS/PPS 只在第一个 IDR 前注入一次够吗？" → 不够。中途加入的拉流端也需要 SPS/PPS 初始化。标准做法是每个 IDR 前都注入——WebRTC 就是这么做的。

**关联**：[[07-AVCC与Annex-B转换实战]] §三

---

### Q13：怎么动态调码率？怎么强制关键帧？

**面试官意图**：是否理解 WebRTC 拥塞控制和丢包恢复的 iOS 实现。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 动态调码率：VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, newBitrate)——运行时直接改，立刻生效。配合 DataRateLimits 同步更新硬上限。这就是 WebRTC GCC 拥塞控制在 iOS 端的实现——检测到带宽下降 → 调低 AverageBitRate。
>
> 强制关键帧：在 EncodeFrame 时传 frameProperties 字典，里面放 kVTEncodeFrameOptionKey_ForceKeyFrame = true。这一帧会被编码成 IDR。这就是对端发 PLI 请求时的响应——立刻产一个 IDR 让对端重新同步。
>
> 这两个旋钮是 iOS 上弱网自适应唯二的工具——没有它们，WebRTC 在弱网下既不能降码率自适应带宽、也不能在花屏后快速恢复。

**追问预警**：
- "DataRateLimits 和 AverageBitRate 什么关系？" → AverageBitRate 是目标平均码率（VBR 取向），DataRateLimits 是硬上限（如"1 秒内不超过 X 字节"）。两者配合 ≈ CBR。较新系统有单独的 ConstantBitRate 属性做真 CBR。

**关联**：[[05-VideoToolbox硬编码实战]] §三

---

### Q14：iOS 的 VideoToolbox 和 Android 的 MediaCodec 编码有什么不同？

**面试官意图**：跨平台实战经验。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 五个核心差异。
> 1）比特流格式：VT 吐 AVCC（长度前缀），MediaCodec 吐 Annex-B（起始码）——跨平台代码在这点上是反的。
> 2）SPS/PPS 位置：VT 把参数集放在 CMVideoFormatDescription 里单独存，MediaCodec 放在 csd-0/csd-1 里、配置时一次性给。
> 3）控制接口：VT 用 VTSessionSetProperty（C API 的 key-value），MediaCodec 用 setParameters（Bundle 传值）。
> 4）像素格式：VT 明确是 NV12（CVPixelBuffer），MediaCodec 输出格式因厂商而异（NV12/NV21/YUV420P/YUV420SP 都有）。
> 5）FFmpeg 支持：iOS 有完整 VT wrapper，Android 的 MediaCodec wrapper 受限。
>
> 工程上跨平台的编码层要做格式统一——一般编码输出统一转成一段公共结构（带 Annex-B 数据的 VideoPacket），屏蔽两端差异。

**关联**：[[../ffmpeg/15-iOS硬件编解码]] §十

---

### Q15：编码过程中 App 进后台再回前台花屏/崩溃，怎么处理？

**面试官意图**：你是否遇到过生命周期相关的问题。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 根因是后台 VT session 可能被系统暂停或失效。回前台分四步恢复：1）检查 session 是否有效（通过编码一帧看返回值）。2）如果无效，销毁旧 session 重建。3）强制输出一个 IDR（ForceKeyFrame）——因为这之前的参考帧可能已经丢了，必须新开一个 GOP。4）重发 SPS/PPS——对端解码器需要新参数集来同步。
>
> 监听 UIApplicationWillEnterForegroundNotification 做这个恢复逻辑。另外，AudioSession 也可能在后台被反激活，要重新 setActive:YES。

**关联**：[[../ffmpeg/15-iOS硬件编解码]] §十一 · [[09-AudioSession与音频策略详解]] §三

---

## 四、解码（4 题）

### Q16：VideoToolbox 解码的完整流程？

**面试官意图**：是否理解了 iOS 解码的前置条件和数据格式要求。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> iOS 解码有一个硬门槛：必须先有 CMVideoFormatDescription（SPS/PPS），才能创建 VTDecompressionSession。所以流程是：
> 1）从流里提取 SPS + PPS（如果是 Annex-B H.264 裸流，从 IDR 前面的参数集 NALU 取；如果是 MP4，从 avcC box 取）。
> 2）CMVideoFormatDescriptionCreateFromH264ParameterSets 创建格式描述对象。
> 3）VTDecompressionSessionCreate 创建解码会话——此时要指定输出 CVPixelBuffer 的属性，必须带 kCVPixelBufferIOSurfacePropertiesKey（零拷贝）+ kCVPixelBufferMetalCompatibilityKey（能直接 Metal 渲染）。
> 4）把 Annex-B 流转为 AVCC（起始码→长度前缀）装进 CMBlockBuffer → 包成 CMSampleBuffer，调 DecodeFrame（异步）。
> 5）输出回调里拿 CVPixelBuffer（NV12, IOSurface）→ 给 Metal 渲染或 AVSampleBufferDisplayLayer。
>
> 关键点：解码会话不能复用——格式（分辨率/SPS）变了必须重建；Invalidate 不能在解码回调线程调，否则死锁。

**追问预警**：
- "为什么不直接用 AVSampleBufferDisplayLayer？" → 它只能显示不做后处理。如果你要加美颜/滤镜/截帧分析，就必须走 Metal。

**关联**：[[06-VideoToolbox硬解码实战]] §三

---

### Q17：Annex-B 流怎么喂给 VTDecompressionSession？

**面试官意图**：你真正写过格式转换代码。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 需要做两件事。第一，参数集提取——扫描 Annex-B 字节流，按起始码 00 00 00 01 切出各 NALU，找 type=7（SPS）和 type=8（PPS）的 NALU，取它们的纯数据（去掉起始码），传给 CMVideoFormatDescriptionCreateFromH264ParameterSets。第二，帧数据转换——把剩余 VCL NALU（IDR/P 帧）的起始码替换成 4 字节大端长度前缀，装进 CMBlockBuffer。包装成 CMSampleBuffer 时，不要包含 SPS/PPS NALU——它们已经在 CMVideoFormatDescription 里了。注意这里用到的长度前缀字节数是你在创建 format description 时指定的（一般是 4）。

**追问预警**：
- "如果 SPS/PPS 中途变了（如分辨率变化）怎么处理？" → 销毁旧 session → 用新 SPS/PPS 重建 format description → 重建 session → 从下一个 IDR 开始喂。

**关联**：[[06-VideoToolbox硬解码实战]] §三 · [[07-AVCC与Annex-B转换实战]] §三

---

### Q18：解码失败怎么恢复？

**面试官意图**：是否有线上故障恢复经验。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 常见三种失败场景和对策。
> 1）数据损坏（kVTVideoDecoderBadDataErr）：跳过此帧，从下一个 IDR 开始继续喂——P 帧依赖前面的参考帧，丢了参考帧后面的 P 帧都解不出，必须等新 IDR。
> 2）解码器内部错误（kVTVideoDecoderMalfunctionErr）：需要重建 session。WaitForAsynchronousFrames → Invalidate → 重建 → 从下一个 IDR 开始。
> 3）内存耗尽（-11800 kCVReturnInsufficientMemory）：检查是否有 CMSampleBuffer/CVPixelBuffer 的循环引用没释放。解码回调里 retain 的 buffer 要及时 release。
>
> 通用的恢复策略：触发对端发 PLI（请求关键帧）或 FIR（全 Intra 刷新），拿到新 IDR 后重建解码链路。

**关联**：[[06-VideoToolbox硬解码实战]] §五

---

### Q19：解码输出的 CVPixelBuffer 怎么显示？

**面试官意图**：理解渲染的两条路。

**难度/频率**：⭐⭐ / 🔥🔥🔥

**标准回答**：

> 两条路。第一条（最省事）：AVSampleBufferDisplayLayer。直接把 CVPixelBuffer 包回 CMSampleBuffer，enqueue 进去就显示。适合播放器场景——不需要逐帧后处理的。注意 enqueue 之后要调 CATransaction.flush()，否则画面不刷新。
>
> 第二条（最灵活）：Metal + CVMetalTextureCache。把 CVPixelBuffer 底层的 IOSurface 零拷贝 alias 成 MTLTexture，在 shader 里做 YUV→RGB，渲染到 MTKView。适合需要后处理（美颜/滤镜/截帧）的场景。
>
> 实时 RTC/直播场景一般走第二条路——因为解码和渲染之间通常还有一层后处理。

**关联**：[[04-Metal渲染与零拷贝详解]] §三

---

## 五、渲染与零拷贝（5 题）

### Q20：Metal 怎么把 NV12 的 CVPixelBuffer 渲染到屏幕？

**面试官意图**：是否理解 Metal 纹理映射和 YUV→RGB 转换。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 四步。1）创建 CVMetalTextureCache——这是 CoreVideo 和 Metal 的零拷贝桥梁。2）每帧用 CVMetalTextureCacheCreateTextureFromImage 把 NV12 的两个平面分别映射为两张 MTLTexture——Y 平面用 R8Unorm 格式、width×height 大小、planeIndex=0；UV 平面用 RG8Unorm 格式、width/2×height/2 大小、planeIndex=1。3）Metal shader 里 fragment shader 从两张纹理采样，用 BT.601 或 BT.709 矩阵做 YUV→RGB。vertex shader 画一个覆盖全屏的矩形。4）渲染结果写入 MTKView.currentDrawable.texture，commit 后上屏。
>
> 整个过程的零拷贝点在于：CVMetalTextureCache 内部通过 IOSurface ID 给同一块 GPU 内存创建 MTLTexture 的 alias——不触发任何 memcpy，全程在 GPU 端。

**追问预警**：
- "为什么要两张纹理而不是一张？" → NV12 是 BiPlanar——Y 和 UV 是两个独立的内存平面。Metal 没有类似 Vulkan 的原生多平面 YUV 纹理格式，所以必须用两张独立纹理。
- "如果 CVPixelBuffer 是 BGRA 而不是 NV12 呢？" → 只映射一张 RGBA8Unorm 纹理，planeIndex=0。shader 里不需要 YUV→RGB，直接输出采样的 RGBA。

**关联**：[[04-Metal渲染与零拷贝详解]] §三

---

### Q21：CVPixelBufferLockBaseAddress 和 CVMetalTextureCache 的本质区别？

**面试官意图**：是否理解 GPU vs CPU 通路。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> LockBaseAddress 是 CPU 通路——把 GPU 显存里的 tiled 纹理数据重新排列成线性布局，然后映射到 CPU 地址空间。这涉及两个开销：detile（tiled→线性重排，硬件的异步操作需要同步等待）+ GPU→CPU 带宽（UMA 架构下虽然共享显存，但 tiling/swizzle 格式 CPU 无法直接理解）。1080p NV12 约 3-5ms。
>
> CVMetalTextureCache 是 GPU 通路——在 GPU 端给同一个 IOSurface 创建纹理视图（alias），不触发任何 CPU 端操作，GPU 自己能理解 tiled 布局。耗时 <0.1ms。
>
> 实时视频处理必须走 CVMetalTextureCache。LockBaseAddress 只用于调试（截图、dump 像素）和非热路径。

**追问预警**：
- "UMA 架构下 GPU 和 CPU 共享内存，为什么 LockBaseAddress 还有开销？" → 虽然物理内存是同一块，但 GPU 以 tiled/swizzle 格式存储纹理（优化纹理缓存的局部性）。CPU 按线性地址访问时，硬件必须做 detile 重排。这就是 UMA 下回读依然慢的根本原因。

**关联**：[[04-Metal渲染与零拷贝详解]] §二 Q2 · [[../ffmpeg/15-iOS硬件编解码]] §七

---

### Q22：CVPixelBufferPool 创建时漏了哪些 key 会导致零拷贝失效？

**面试官意图**：是否真正理解零拷贝的前提条件。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 两个必须的 key。1）kCVPixelBufferIOSurfacePropertiesKey: @{}——指定 CVPixelBuffer 的底层使用 IOSurface 而不是普通内存。没有这个，CVPixelBuffer 就是普通的 CPU 内存，CVTextureCache 无法 alias。2）kCVPixelBufferMetalCompatibilityKey: @YES——让 IOSurface 的像素格式兼容 Metal 纹理。漏掉的话，CVMetalTextureCacheCreateTextureFromImage 会返回 -6660 等错误。还有一个非必须但推荐的：kCVPixelBufferBytesPerRowAlignmentKey: @64——64 字节对齐，Metal 推荐。
>
> 这对 VideoToolbox 的解码输出和编码输入都适用——创建 session 时的 pixel buffer attributes 也要带这两个 key。

**关联**：[[04-Metal渲染与零拷贝详解]] §四

---

### Q23：GPUImage 的滤镜链设计思想是什么？和自研 Metal 有什么区别？

**面试官意图**：你是否理解滤镜框架的设计模式。

**难度/频率**：⭐⭐ / 🔥🔥

**标准回答**：

> GPUImage 的核心设计是 Input/Output/Filter 三层抽象 + FBO 串联。每个滤镜内部管理一个 FBO（OpenGL ES 的 Frame Buffer Object），前一个滤镜的输出纹理绑定为下一个滤镜的输入纹理——串联一行代码 addTarget。这是典型的责任链模式在 GPU 渲染上的应用。
>
> 和自研 Metal 的主要区别：1）每加一个滤镜多一次 GPU render pass（一次 draw call）——链上有 8 个滤镜就是 8 个 pass，性能在极端场景会扛不住。2）大厂一般会做 shader 合并优化——把相邻简单滤镜的 GLSL/Metal shader 合并成一个，省掉中间 pass。3）GPUImage 基于 OpenGL ES，在 iOS 上已废弃，新项目应迁移 Metal。
>
> 但这个 Input/Output/Filter 的设计模式是跨 API 的——我们自研的 Metal 滤镜引擎完全沿用了这套抽象。

**追问预警**：
- "shader 合并怎么做？" → 把相邻滤镜的 fragment shader 的函数体拼接在一起——第一个 shader 的输出变量直接作为第二个 shader 的输入变量，消除中间的纹理采样和 FBO 切换。

**关联**：[[03-GPUImage滤镜链详解]] §二

---

### Q24：Metal 命令缓冲区的 inflight 控制是什么意思？为什么要做？

**面试官意图**：是否了解 GPU 编程的性能注意事项。

**难度/频率**：⭐⭐⭐ / 🔥

**标准回答**：

> Metal 的 command buffer 是异步执行的——commit 后 CPU 立即返回，GPU 在后台慢慢执行。如果不控制，CPU 可以一口气提交几百个 command buffer，导致：1）内存暴涨（每个 command buffer 持有自己的资源引用）。2）延时增加（积压的帧越来越多，用户看到的画面是几百 ms 前的）。3）GPU 超负荷。
>
> inflight 控制就是限制"GPU 管线中同时在飞的 command buffer 数量"——一般用 dispatch_semaphore，在 commit 前 wait、在 completedHandler 里 signal。上限通常设 2-3 个（Metal Best Practices Guide 推荐 3）。超过后 CPU 会等——这是唯一的背压机制。

**追问预警**：
- "用 dispatch_semaphore 做 GPU 同步会不会卡主线程？" → 所以渲染通常不在主线程。MTKView 的 draw 回调默认在主线程，如果你的帧源不是主线程，可以手动管理 CAMetalLayer。

**关联**：[[04-Metal渲染与零拷贝详解]] §三

---

## 六、音频（4 题）

### Q25：iOS 怎么做实时音频采集和播放？

**面试官意图**：是否用过 AudioUnit RemoteIO。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 用 AudioUnit 的 RemoteIO 子类型——这是一个双向模型：总线 1 负责输入（麦克风 → input callback → 你的 App），总线 0 负责输出（你的 App → render callback → 扬声器）。两个 callback 都跑在系统的高优先级音频线程上，绝对不能加锁、不能 malloc、不能做阻塞操作。所以标准架构是用无锁环形缓冲（SPSC ring buffer）解耦——input callback 把 PCM 写入 ring buffer，业务线程从 ring buffer 读数据做编码/降噪/推流；render callback 从 ring buffer 读数据写出。配置上 ASBD 设为 LinearPCM + SignedInteger + 16bit + 交错 + 48kHz 或 44.1kHz。

**追问预警**：
- "为什么不能直接在回调里做 AAC 编码？" → 音频线程不能阻塞。AudioConverter 编码可能触发系统调用，不应该在音频线程上调。所以用 ring buffer 搬出来给业务线程做编码。
- "为什么用 48000 而不是 44100？" → 48k 是通信标准、蓝牙兼容性更好、而且很多回声消除模块以 48k 为基准。44100 是 CD 音乐标准，播放场景更常用。

**关联**：[[02-AudioUnit与音频处理详解]] §三

---

### Q26：音频线程上为什么不能加锁？不用锁怎么安全地传递数据？

**面试官意图**：是否理解实时音频的约束。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 音频线程是硬实时线程——48kHz 下每隔 5-10ms 触发一次，必须在规定时间内完成处理，否则 buffer 被掏空导致爆音（glitch）。加锁有两个致命问题：1）优先级反转——如果低优先级线程持有锁，音频线程被阻塞等 CPU，直接导致音频断流。2）系统调用——锁的内核操作可能触发上下文切换，时间不可控。
>
> 不用锁的方案：SPSC（Single Producer Single Consumer）无锁环形缓冲。音频线程是 producer 或 consumer，业务线程是另一端。用原子变量的读写指针实现，不需要 mutex。iOS 上常用的实现有 TPCircularBuffer（Michael Tyson 开源）或自己用 C11 atomic 写一个简化版。

**追问预警**：
- "SPSC 的读写指针怎么保证不冲突？" → 两个指针分别由两个线程独占写入（head 由 producer 写、tail 由 consumer 写），不存在写冲突。读取时用 memory_order_acquire/release 确保可见性。

**关联**：[[02-AudioUnit与音频处理详解]] §二 Q1

---

### Q27：AudioSession 的 category 分别是什么场景用？

**面试官意图**：是否理解 iOS 音频策略。

**难度/频率**：⭐⭐ / 🔥🔥🔥

**标准回答**：

> 最常用的三个：PlayAndRecord——既录音又播放，直播/RTC/VoIP 用这个，必须配 DefaultToSpeaker 否则走听筒。Playback——只播不录，音乐播放器/视频播放器用这个，支持后台播放。Record——只录不播，录音笔用这个。其他还有 SoloAmbient（游戏音效，不和其他 App 混音）、Ambient（可混音的系统音效）。
>
> 直播/RTC 的标配：PlayAndRecord + VideoChat mode + DefaultToSpeaker + AllowBluetooth + AllowBluetoothA2DP options + 48kHz + 5-10ms buffer。配完必须 setActive:YES。

**追问预警**：
- "PlayAndRecord 不加 DefaultToSpeaker 会怎样？" → 声音走听筒（receiver），音量极小——这是 iOS 直播 App 最容易犯的低级错误，用户会投诉"没声音"。

**关联**：[[09-AudioSession与音频策略详解]] §四

---

### Q28：RTC 的回声消除在 iOS 上怎么做？

**面试官意图**：是否理解实时音频的特殊处理。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 三种方案。最简单：把 RemoteIO 换成 VoiceProcessingIO——它是 RemoteIO 的超集，系统在内核层做了 AEC（回声消除）、AGC（自动增益）、降噪。API 层面和 RemoteIO 一模一样。限制是只支持单声道、采样率有限制、引入 20-40ms 不可控延迟。
>
> 第二种：用系统的 AudioConverter/AudioUnit 里的回声消除效果器。第三种：自己实现 AEC 算法（如 WebRTC 的 AECM/AEC3 模块）——在业务线程拿到录音 PCM 和播放 PCM 后调用 AEC 算法处理。最灵活但实现复杂。
>
> 绝大多数场景直接用 VoiceProcessingIO 就够了——WebRTC iOS SDK 的内部 VoiceProcessingAudioUnit 就是对它的封装。

**关联**：[[02-AudioUnit与音频处理详解]] §二 Q4

---

## 七、格式转换与推流（4 题）

### Q29：AVCC 为什么必须转 Annex-B 才能推流？怎么转？

**面试官意图**：你真正写过这段转换代码。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 因为 RTP/RTMP/裸流用 Annex-B 格式——00 00 00 01 起始码分隔 NALU、SPS/PPS 作为普通 NALU 内联在流里。VideoToolbox 编码输出是 AVCC——4 字节长度前缀分隔 NALU、SPS/PPS 单独存在 CMVideoFormatDescription 里。
>
> 转换做两件事。1）SPS/PPS 注入：用 CMVideoFormatDescriptionGetH264ParameterSetAtIndex 从 format description 取参数集，在每个 IDR 前拼 00 00 00 01 + SPS + 00 00 00 01 + PPS 写入输出。2）NALU 边界转换：遍历 CMBlockBuffer 的所有 NALU，把 4 字节大端长度替换为 00 00 00 01 起始码。
>
> 关键陷阱：AVCC 长度前缀有时前三字节是 00 00 00（长度值<256 时），和 Annex-B 的起始码前三字节相同——不能通过扫起始码来解析 AVCC，必须按长度字段精确跳转。
>
> HEVC 比 H.264 多一个 VPS——取参数集时取 3 个，每个 IDR 前注入 VPS+SPS+PPS 三个。

**追问预警**：
- "为什么要每个 IDR 前都注入而不是只在第一个 IDR 前注入？" → 因为中途加入的观看者需要 SPS/PPS 初始化解码器。如果只在开头注入一次，中间加入的人只有声音没画面。

**关联**：[[07-AVCC与Annex-B转换实战]] §三

---

### Q30：iOS 端的完整推流管线是怎样的？各个模块怎么分工？

**面试官意图**：你是否能从前到后把整条链路串起来。

**难度/频率**：⭐⭐⭐ / 🔥🔥🔥

**标准回答**：

> 分四段。采集段：AVCaptureSession → NV12 CVPixelBuffer → retain 后 dispatch 到编码和预览两条路线。预览段：CVMetalTextureCache 零拷贝 alias → Metal shader YUV→RGB → MTKView。编码段：VTCompressionSession（RealTime=true, 关 B 帧）→ 异步回调拿 CMSampleBuffer。推流段：AVCC→Annex-B 转换 + SPS/PPS 注入 → C++ 引擎封装 FLV tag → RTMP 网络发送。
>
> 线程模型：采集回调串行队列 → retain + dispatch → 编码队列 + 主线程（Metal 渲染）。编码回调（VT 内部线程）→ dispatch 到推流队列。各级之间靠队列解耦，背压靠采集侧的 alwaysDiscardsLateVideoFrames 自动丢帧。
>
> 停止顺序：先停采集（不再有新帧）→ flush 编码管线（CompleteFrames）→ 等回调走完 → 销毁 session → 断开连接。

**追问预警**：
- "如果网络慢了，编码产生的帧怎么处理？" → 推流队列积压时主动丢非关键帧（P 帧），保留关键帧（IDR）。因为丢 P 帧只是短暂画面不流畅，丢 IDR 会导致整个 GOP 解不出。

**关联**：[[08-端到端采集编码推流管线]] §三

---

### Q31：WebRTC iOS SDK 的视频部分底层是怎么实现的？

**面试官意图**：你是否了解 WebRTC 在 iOS 上的架构。

**难度/频率**：⭐⭐⭐⭐ / 🔥🔥（高级岗位常问）

**标准回答**：

> libwebrtc 的 iOS 视频底座就是 VideoToolbox。编码器 RTCVideoEncoderH264 底层创建 VTCompressionSession，解码器 RTCVideoDecoderH264 底层创建 VTDecompressionSession。采集端用 RTCCameraVideoCapturer 封装 AVCaptureSession。渲染端用 RTCMTLVideoView 封装 Metal + CVMetalTextureCache。
>
> 几个关键映射：
> - WebRTC 的拥塞控制（GCC）→ 动态调 VTSessionSetProperty AverageBitRate
> - PLI 请求 → kVTEncodeFrameOptionKey_ForceKeyFrame
> - 零拷贝链 → AVCaptureSession(CVPixelBuffer,IOSurface) → RTCVideoFrame → VTCompressionSession → 同一个 IOSurface
> - AVCC→Annex-B → libwebrtc 的 H264BitstreamParser 在编码回调里做了这个转换
>
> 如果你遇到 WebRTC iOS 端的花屏/性能问题，根因十有八九是：编码的实时属性没配对、AVCC↔Annex-B 转换有 bug、或者零拷贝链断了。

**追问预警**：
- "libwebrtc 源码里哪些文件是 iOS 相关的？" → sdk/objc/ 下的 RTCVideoEncoder、RTCVideoDecoder、RTCCameraVideoCapturer、RTCMTLVideoView。Android 对应的是 sdk/android/。

**关联**：[[../ffmpeg/15-iOS硬件编解码]] §十三

---

## 八、性能与调试（3 题）

### Q32：怎么排查 iOS 视频处理管线的性能瓶颈？

**面试官意图**：是否有系统化的性能分析能力。

**难度/频率**：⭐⭐⭐ / 🔥🔥

**标准回答**：

> 用 Instruments 的三板斧。1）Time Profiler（CPU）：看各个环节的 CPU 耗时——采集回调耗时、Metal 着色器耗时、格式转换耗时。瓶颈在哪个环节一目了然。2）Metal System Trace（GPU）：看 GPU 端的渲染耗时、command buffer 提交频率、是否有 GPU 空闲等待。3）Allocations（内存）：看 CVPixelBuffer/CMSampleBuffer 是否有泄漏——持续增长不下降就是忘了 release。
>
> 常见瓶颈和优化方向：
> - 采集回调耗时超过帧间隔 → 只做 retain+dispatch，不在回调里处理数据
> - Metal 着色器耗时 → 降低 shader 复杂度、缩小渲染分辨率
> - 编码跟不上 → 降低帧率和分辨率、提升码率预算
> - 内存增长 → 检查 CVPixelBuffer/CMBlockBuffer 的 retain/release 配对
>
> 另外可以用自己埋点的帧级耗时统计——在采集回调、编码输入、编码输出、渲染输出各节点记录时间戳，算各环节的帧级延时。

**追问预警**：
- "Metal System Trace 能看到什么具体信息？" → 能看到每个 render pass 的 GPU 耗时、command buffer 的提交和完成时间、CPU 和 GPU 的并行度——如果 GPU 端空闲等待说明 CPU 在拖后腿，反之亦然。

---

### Q33：怎么衡量 Glass-to-Glass 延迟？

**面试官意图**：是否理解端到端延迟的构成。

**难度/频率**：⭐⭐⭐ / 🔥🔥（高级岗位常问）

**标准回答**：

> Glass-to-Glass 延迟指从摄像头曝光到对端屏幕显示的端到端时间。在 iOS 端，延迟构成是：
> - 采集延迟（曝光+ISP+传输到 App）：~5-15ms（取决于曝光时间）
> - 前处理延迟（美颜/滤镜）：~2-10ms（取决于 shader 复杂度）
> - 编码延迟（VT RealTime 模式）：~2-5ms（一帧以内）
> - 网络延迟（RTT）：~10-50ms（局域网）/~50-300ms（公网）
> - 对端解码 + 渲染：~5-15ms
> - 总延迟：局域网约 30-80ms，公网约 80-400ms
>
> iOS 端可以优化的点：实时编码属性组合（RealTime+关 B 帧+短 GOP）、Metal 零拷贝渲染（避免 detile/stall）、低 I/O buffer duration（5ms vs 10ms）、前处理降分辨率做然后 upsample。精确测量可以用闪光灯同步法：摄像端闪光灯闪一下 → 对端检测画面变白的时间差。

**关联**：[[../ffmpeg/16-硬件编解码高级专题]]

---

### Q34：怎么验证 CVPixelBuffer 是 IOSurface 后端？怎么确认零拷贝在生效？

**面试官意图**：是否有底层验证方法。

**难度/频率**：⭐⭐⭐ / 🔥

**标准回答**：

> 验证 IOSurface 后端：用 CVPixelBufferGetIOSurface(pixelBuffer)——非 NULL 就是 IOSurface 后端。如果返回 NULL，说明你的 CVPixelBufferPool 创建时漏了 IOSurfacePropertiesKey，或者这个 buffer 不是从 pool 分配的。
>
> 验证零拷贝在做 CCTV 预览：用 Instruments 的 Metal System Trace，看渲染过程是否有 "texture upload"（CPU→GPU 拷贝）——如果有，说明你的纹理不是通过 CVMetalTextureCache 创建的，而是手动 upload 的。另一个方法：用 Allocations 看是否有大量 3MB（≈1080p NV12 大小）的 malloc/memcpy——如果有，说明有人在 CPU 上拷贝像素数据。
>
> 也可以用"负向测试"：注释掉 CVMetalTextureCache → 改成 LockBaseAddress + newTextureWithDescriptor + replaceRegion → 对比前后的 Metal System Trace 耗时——应该能看到明显差异（~0.1ms → ~3ms）。

**关联**：[[04-Metal渲染与零拷贝详解]] §四

---

## 九、自检清单

能流畅回答以下问题，iOS 音视频中高级岗位的面试准备就算合格了：

1. iOS 媒体栈的三层架构是什么？各代表什么框架？
2. AVFoundation 和 VideoToolbox 的区别？什么时候用哪个？
3. CVPixelBuffer 和 CMSampleBuffer 的包含关系？
4. 从摄像头拿到的数据是什么格式？为什么？
5. VTCompressionSession 编码的完整生命周期？
6. 实时编码必设哪两个属性？为什么？
7. 编码输出的 SPS/PPS 在哪？怎么取？
8. AVCC 和 Annex-B 的本质区别？转换步骤？
9. VTDecompressionSession 创建的前置条件？
10. 解码会话能不能复用？为什么？
11. CVMetalTextureCache 怎么把 NV12 映射为 Metal 纹理？
12. CVPixelBufferLockBaseAddress 和 CVMetalTextureCache 的本质区别？
13. 零拷贝 CVPixelBufferPool 创建时要带哪两个 key？
14. iOS 和 Android 音视频开发的最大差异？
15. AudioSession 直播标配怎么配？
16. 音频线程为什么不能加锁？
17. 解码失败分几种情况？各怎么恢复？
18. 采集→编码→推流的完整管线怎么串？
19. 后台回前台花屏的根因和恢复步骤？
20. 怎么动态调码率、强制关键帧？

---

## 🎯 总结

> iOS 音视频中高级岗位面试的核心不是 API 背诵，而是**你能不能把采集→编码→渲染→推流这条链路从头到尾讲清楚，并且对每个环节的设计取舍、常见坑、性能优化有自己的理解**。本文档的 34 道题覆盖了面试中 90% 的可能性——剩下的 10% 是看你现场能不能灵活组合这些知识点。

## 🔗 关联文档

- 本系列 00-09 全部文档 — 每个知识点的深入展开
- [[../ffmpeg/15-iOS硬件编解码]] — VideoToolbox 全文深讲
- [[../ffmpeg/00-FFmpeg全景导读]] — 音视频通用基础
- [[../OC/【重点】OC面试高频考点与标准回答大全]] — OC 语言面试题
