工作职责
1、负责移动端（Android & iOS）视频拍摄及视频剪辑底层SDK能力建设，编写高质量的代码
2、为公司视频拍摄和视频剪辑提供技术支持和解决方案
任职资格
1、本科及以上学历，计算机，电子通信相关专业；具有扎实的编程功底，良好的代码风格及编程习惯
2、熟练掌握Android及iOS 平台的 C/C++开发，并熟练掌握C/C++与平台Native代码相互调用技术；具有一定的Android，iOS Native应用程序开发经验
3、熟悉移动平台多媒体相关功能特性的使用，如Camera/Camera2，MediaCodec，AudioUnit，VideoToolBox等，熟悉Android平台多媒体框架，iOS AVFoundation；熟练掌握Android 及iOS 平台OpenGL ES 开发，熟悉GPUImage优先
4、熟悉多媒体领域的技术标准，熟悉H.264、H.265、AAC等音视频编解码原理，并熟练掌握平台相关能力使用
5、熟练掌握开源多媒体处理框架FFMpeg的使用，精通源码者优先
6、有短视频拍摄及视频剪辑SDK项目开发经验者优先；了解音频处理算法，如均衡器，混响等算法者优先；有传统图像处理算法开发经验者优先

---

## 📊 岗位匹配度分析（杜孟林）

### 🟢 匹配项（你的优势）

| # | 要求 | 你的匹配情况 | 匹配度 |
|---|------|-------------|--------|
| 1 | 本科及以上，计算机/电子通信相关 | 西安科技大学，电子信息科学与技术 | ✅ 完全匹配 |
| 2 | 扎实编程功底，良好代码风格 | 4年+ C++ SDK 开发，Insta360 工程化经验 | ✅ 完全匹配 |
| 3 | Android/iOS C/C++ 开发 + Native 互调 | JNI + OC++ 跨平台封装是你的核心工作，KMP 从 0 到 1 搭建 | ✅ 强匹配 |
| 4 | MediaCodec、OpenGL ES | 4K RTMP 直播推流中深度使用 MediaCodec、OpenGL、AHardwareBuffer 零拷贝 | ✅ 强匹配 |
| 5 | H.264 编解码原理 | 直播推流、预览优化中实际应用，理解 GOP、Seek 策略等 | ✅ 匹配 |
| 6 | 性能优化能力 | 全链路埋点、AJB 动态抖动缓冲、零拷贝、CPU/GPU 协同——有完整方法论 | ✅ 强匹配 |
| 7 | 跨平台 SDK 架构 | iOS/Android 双端 SDK 设计、集成、发布，独立负责完整模块 | ✅ 强匹配 |

### 🟡 部分匹配（有基础但需加强）

| # | 要求 | 你的现状 | 匹配度 |
|---|------|---------|--------|
| 1 | **FFmpeg 熟练掌握，精通源码优先** | 简历和 self.md 中 FFmpeg 排在技能关键词里，但实际深度有限：自动剪辑中做了 Seek 优化，了解 stss box；目前整个 learnffmpeg 仓库还在系统学习中。面试中如被问到 `av_read_frame`/`avcodec_send_packet` 内部实现、filter graph 机制等，可能无法深入回答 | ⚠️ 约 50% |
| 2 | **Camera/Camera2 拍摄端** | 你一直做的是**接收/解码/渲染**端（相机传过来的流），对 Camera2 的采集管线（session/request/capture）、3A（AE/AF/AWB）、预览 Surface 管理不熟悉 | ⚠️ 约 30% |
| 3 | **iOS 端深度（VideoToolBox、AudioUnit、AVFoundation）** | 你的经验明显偏 Android——MediaCodec、AHardwareBuffer 掌握得很好，但 iOS 的硬编解码（VideoToolBox）、音频（AudioUnit）、采集框架（AVFoundation）不是强项 | ⚠️ 约 35% |
| 4 | **H.265/HEVC** | 简历未提及 H.265，self-talk 中也没有涉及。H.265 与 H.264 在编码工具、码流结构、平台硬解支持上有较大差异 | ⚠️ 约 20% |
| 5 | **传统图像处理算法** | 你做过滤镜接入和渲染链路优化，但都是工程集成侧；传统图像处理（边缘检测、特征提取、图像分割、色彩空间变换的数学原理）没有开发经验 | ⚠️ 约 25% |

### 🔴 明显差距（需要主动学习）

| # | 要求 | 你的现状 | 匹配度 |
|---|------|---------|--------|
| 1 | **视频拍摄 SDK 经验** | 你的工作是**播放/处理/推流**端，拍摄端（Camera 采集、预览、录制启停、对焦/曝光控制）整个链路没做过 | ❌ 约 10% |
| 2 | **视频剪辑 SDK 经验** | 你做过自动剪辑算法的**接入与性能优化**，但剪辑 SDK 的核心能力（时间线/Track 模型、转场、特效叠加、变速、音频混音、撤消重做、导出管线）不是你负责的 | ❌ 约 15% |
| 3 | **音频处理算法（均衡器、混响等）** | 完全空白。音频 DSP（滤波器设计、FFT、混响算法如 Schroeder/Freeverb、音频重采样、混音）没有任何经验 | ❌ 约 5% |
| 4 | **GPUImage** | 简历和项目文档均未提及。GPUImage 是移动端图像处理/滤镜链的知名框架，面试可能会问到其滤镜链设计思想 | ❌ 约 5% |
| 5 | **AAC 等音频编解码** | 你主要做视频侧，音频编解码（AAC-LC/HE-AAC、Opus、音频编码参数、AudioSpecificConfig 等）未涉及 | ❌ 约 10% |

---

### 📈 综合评估

| 维度 | 得分 |
|------|------|
| 整体匹配度 | **约 50-55%** |
| 核心竞争力匹配 | **高**（C++ 跨平台 SDK + 性能优化） |
| 领域匹配 | **中偏低**（播放/推流 → 拍摄/剪辑有跨度） |
| 面试竞争力 | **中等**（强在工程能力，弱在领域经验） |

**一句话总结：** 你的 C++ 工程能力、跨平台 SDK 架构和性能优化是硬通货，但这些都是在**播放/推流**链路上积累的；小红书的岗位偏**拍摄+剪辑**，领域上差了一个方向——面试官大概率会抓着「你没做过拍摄端」和「FFmpeg 深度」来问。

---

### 🎯 如果要面这个岗位，建议重点补强

#### 第一优先级（面试前必须补）

1. **FFmpeg 源码深度** — 这是 JD 里的「熟练掌握」项，也是最容易被深问的：
   - 精读 `avformat` 层：`av_read_frame` 内部实现、IO 缓存机制、`av_seek_frame` 的 seek 策略（by byte / by timestamp / by any）
   - 精读 `avcodec` 层：`avcodec_send_packet`/`avcodec_receive_frame` 的内部状态机、解码器缓冲区管理、h264_mp4toannexb 等 bitstream filter
   - Filter graph：`avfilter_graph_create_filter`、`avfilter_link`、buffersrc/buffersink 的工作机制
   - 能回答：「FFmpeg 解码一个 MP4 文件的全流程，从 `avformat_open_input` 到拿到第一帧 YUV，涉及哪些结构体和函数调用？」
   - 参考：你的 learnffmpeg 仓库继续推进，建议看 `Doc/ffmpeg/` 下的资料

2. **Android Camera2 采集链路** — 岗位明确要求 Camera/Camera2：
   - Camera2 核心类：`CameraManager` → `CameraDevice` → `CameraCaptureSession` → `CaptureRequest`
   - 预览流程：`Surface`（SurfaceView/TextureView）如何传给 Camera2，重复请求 vs 单次请求
   - 理解 3A（Auto Exposure / Auto Focus / Auto White Balance）的基本概念
   - 至少能写一个「打开相机 → 创建预览 → 开始采集」的 Demo

3. **H.265/HEVC 基础知识** — 至少要能和面试官聊：
   - H.265 vs H.264 的核心差异（编码单元 CTU vs 宏块、更多帧内预测方向、SAO 滤波器等）
   - H.265 在移动端的硬编解码支持情况
   - 同等画质下 H.265 比 H.264 节省约 50% 码率的原因

#### 第二优先级（中长期补强）

4. **iOS 端技术栈补齐** — 你 Android 强但 iOS 偏弱：
   - VideoToolBox：硬编/硬解的基本流程（`VTCompressionSession`/`VTDecompressionSession`）
   - AVFoundation 采集：`AVCaptureSession` 的基本用法
   - 至少跑通一个 iOS 端的视频采集+编码 Demo

5. **音频基础** — 不求深入，但要能聊：
   - AAC 编码基本概念（AAC-LC、profile、ADTS vs ADIF）
   - PCM 音频基础（采样率、位深、声道）
   - 了解一个音频处理场景（如：如何实现一个简单的 3-band EQ）

6. **传统图像处理算法** — 了解常用算法：
   - 图像滤波（高斯模糊、中值滤波、双边滤波的原理）
   - 边缘检测（Sobel、Canny 的基本思路）
   - 颜色空间转换（RGB ↔ YUV ↔ HSV 的数学公式）
   - 推荐看《数字图像处理》（冈萨雷斯）前 5 章

#### 面试话术建议

- **「没做过拍摄端」→ 转化势为优势：** 「我虽然主要做播放/推流端，但我对 MediaCodec 硬编解码管线的底层机制很熟悉，包括零拷贝、异步队列这些。拍摄 SDK 本质上是反向的链路，我理解的核心——Camera 采集 → 硬编码 → 封装——可以通过我对解码/编码的理解快速切入。」
- **「FFmpeg 深度不够」→ 如实但积极：** 「我在自动剪辑项目中用过 FFmpeg 做 Seek 和抽帧优化，也读过一部分源码。最近在系统性地深入学习 FFmpeg 的 avformat/avcodec/filter graph 三层架构（可以展示 learnffmpeg 仓库）。」
- **「剪辑 SDK 没做过」→ 强调可迁移能力：** 「我虽然没有直接做过剪辑 SDK，但我在跨平台 SDK 架构（JNI/OC++/KMP）、渲染管线优化、滤镜集成方面有丰富的工程经验。剪辑 SDK 的 Track 模型、渲染合成管线这些，我相信以我的 SDK 架构基础可以快速上手。」