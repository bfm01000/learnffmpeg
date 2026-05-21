# 杜孟林 · C++ 音视频开发工程师

|              |                                          |
| ------------ | ---------------------------------------- |
| **求职意向** | C++ 音视频开发 / 端侧多媒体 / 嵌入式音视频 SDK |
| **工作年限** | 近 5 年（2021.07 至今）                  |
| **教育背景** | 西安科技大学 · 本科 · 2021.07 毕业       |
| **联系方式** | 电话 / 邮箱（投递时按目标公司填写）      |
| **现居城市** | 深圳（可考虑：杭州 / 上海 / 北京 / 远程）|

---

## 一、个人优势

- **5 年扎根移动端 C++ 音视频底层**，主导过 4K 直播、低时延预览、自动剪辑三类典型音视频 Pipeline 的性能攻坚，从架构设计到底层踩坑都有完整的复盘沉淀。
- **真实玩过硬件零拷贝与异构调度**：在影石运动相机 SoC 形态下，打通 `GPU → AHardwareBuffer → VPU` 的物理级零拷贝链路，熟悉 Gralloc / DMA-BUF / Sync Fence / Surface 直通这一套硬件 Buffer 流转机制。
- **数据驱动的性能调优习惯**：所有优化先做全链路埋点（自研 `SFT_SCOPE` RAII 耗时统计 + 10s 平均聚合），用控制变量法定位根因，不靠猜测。
- **跨平台 SDK 工程化经验**：C++ 写核心 Pipeline，通过 JNI / Objective-C++ 桥接 Android 与 iOS 双端，熟悉 ABI 兼容、生命周期管理、多线程对象安全释放等 SDK 级踩坑。
- **会刨根问底**：自动剪辑 Smart Seek 时直接读 FFmpeg 的 `AVStream->index_entries`（即 MP4 `stss` Box 索引）做关键帧决策；定位 4K 直播瓶颈时翻 OpenGL 显存 Tiled / Linear 布局与 De-tiling 的原理。

---

## 二、技能清单

**编程语言**
- 精通 C++（C++11 / 14 / 17，STL、模板、智能指针、RAII、移动语义）、C
- 熟悉 Java（Android 端）、Objective-C / Objective-C++（iOS 端）

**音视频核心**
- FFmpeg：解封装 / 编解码 / AVFilter / 软硬解切换、`AVStream->index_entries` Seek 优化、`AVBufferRef` 引用计数与内存池
- 编解码：H.264 / H.265 NALU 结构、I/P/B 帧、GOP、SPS/PPS、Profile 差异（Main / High）、AAC / ADTS
- 封装格式：MP4（moov / mdat / stss / stsc / stco box）、FLV、TS
- 流媒体：RTMP 推拉流、HLS、基础 WebRTC 概念（NACK / FEC / Jitter Buffer）
- 时间戳与同步：PTS / DTS / Timebase 转换、跨时钟域映射（媒体时钟 ↔ 本地单调时钟）

**Android 多媒体栈**
- MediaCodec（Java / NDK 双层 API 取舍）、`MediaCodec.setParameters` 动态调码率
- AHardwareBuffer / Surface / Gralloc / DMA-BUF / Hardware Sync Fence
- OpenGL ES（外部纹理 `GL_TEXTURE_EXTERNAL_OES`、GPU Blit、`glReadPixels` 与 PBO 异步回读取舍）
- JNI（Local / Global Ref 管理、跨线程回调、C++ 对象到 Java 的生命周期绑定）

**iOS 多媒体栈**
- VideoToolbox 硬解、CVPixelBuffer / IOSurface 零拷贝
- Objective-C ARC、Runtime、桥接 C++ 核心 SDK

**性能与并发**
- 多线程：`std::thread` / `std::condition_variable` / `std::atomic`、生产者-消费者队列、无锁队列概念
- 内存优化：自定义 AVBufferPool、对象池、内存池
- 调度：异构硬件流水线（VPU + GPU + NPU 并行）、QoS / ABR 自适应码率
- 工具链：Systrace / Perfetto、Android Studio CPU Profiler、Instruments、ASan / TSan

**构建与跨平台**
- CMake / NDK / Xcode / Gradle、Android & iOS 双端交叉编译 FFmpeg

---

## 三、工作经历

### 影石创新（Insta360） · 移动端 C++ 高级开发工程师 · 2021.07 – 至今

影石是全球领先的全景相机厂商（运动相机 / 360° 全景 / 车载相机），所在团队负责跨平台音视频 SDK，C++ 写核心 Pipeline，通过 JNI / OC 提供给 Android、iOS 与桌面端使用。

主要职责：
- 负责 Android / iOS 端 4K / 全景直播、相机预览、自动剪辑等核心音视频链路的开发与性能优化。
- 主导多个高优客诉的底层架构重构，输出工业级直播推流、低时延预览、智能剪辑能力。
- 负责 SDK 跨平台架构演进与代码评审，沉淀团队级技术文档。

---

## 四、核心项目

### 项目一：Android 4K 全景直播全链路性能优化与重构

**项目背景**
高优客诉：4K RTMP 直播初始帧率仅 22 fps，连续推流 10 分钟后断崖式跌至 5 fps，画面卡顿、手机严重发热（Pixel 7 Pro / 小米 10 等机型）。

**我的工作**

1. **全链路埋点定位瓶颈**：使用自研 `SFT_SCOPE` RAII 宏对采集 → 渲染 → 编码 → 推流四段做耗时统计，定位到两大瓶颈：渲染到编码交接处的 `glReadPixels` + `libyuv::ABGRToI420` 同步阻塞约 30 ms；以及网络 I/O 同步阻塞编码器导致的雪崩式掉帧。

2. **打通 GPU → VPU 物理级零拷贝**：基于 `AHardwareBuffer` + Surface 直通方案重构渲染链路。
   - 严格协商 `USAGE_GPU_SAMPLING | USAGE_GPU_FRAMEBUFFER | USAGE_VIDEO_ENCODE`，**刻意不加** `CPU_READ` 避免 Gralloc 退化为 Linear 布局导致 AFBC / UBWC 无损压缩失效。
   - 将阻塞的 `glFinish` 改为 `glFlush`，通过硬件 Sync Fence 让 VPU 在编码前等待 GPU 渲染完成，将"CPU 死等"转化为"GPU / VPU 流水线并行"。
   - 工程落地：将 `AHardwareBuffer` 包入 `AVFrame->data[3]`，自定义 `AV_PIX_FMT_AHARDWAREBUFFER` 像素格式，编码器侧通过 `SurfaceWriter` 用 OpenGL 外部纹理做一次 GPU Blit，无侵入对接现有 FFmpeg 封装架构。

3. **异步发送队列 + 队列水位 ABR**：基于团队 `DispatchQueue2` 构建生产者-消费者模型解耦编码与网络发送（队列容量 90，约 3 秒缓冲）。
   - 通过 Lambda 值捕获 `std::shared_ptr<Muxer>` 解决跨线程异步对象生命周期问题。
   - 基于队列积压水位实现轻量级 ABR：积压 > 60 帧（约 2 s）按目标码率 20% 阶梯降级，积压 < 15 帧按 20% 阶梯恢复，每 2 s 评估一次防止振荡。
   - 通过 JNI 调用 Java 层 `MediaCodec.setParameters` 动态调码率（兼容 API 19+，比 NDK `AMediaCodec_setParameters` 的 API 26 要求覆盖更广机型）。

4. **参数化动态路由保证向后兼容**：在 Java 层 `RecordParam` / `CameraLiveOptions` 新增 `enableLiveZeroCopy` / `enableAsyncSendAndABR` 两个独立开关，JNI 透传至底层动态分发，旧业务零侵入。

**关键成果**
- 渲染线程单帧耗时从 **40 ms 压至约 10 ms**（且 10 ms 为 GPU 异步耗时，CPU 几乎为零）。
- 长时推流帧率从 5 fps 跌底**稳定回升至 29 – 30 fps**，发热问题彻底解决。
- 模拟 2 Mbps 极端弱网下不再变成幻灯片，通过 ABR 自动降码率维持流畅推流。
- 沉淀的零拷贝 + 异步发送 + ABR 三件套架构成为团队后续 8K 推流的基础。

**技术栈**：C++ 11 / FFmpeg / AHardwareBuffer / OpenGL ES / MediaCodec / JNI / Sync Fence / DispatchQueue2

---

### 项目二：车载相机预览低时延优化（小鹏汽车客户项目）

**项目背景**
小鹏汽车客户反馈：同样网络环境下，X3 相机预览端到端延时约 500 ms，X5 相机却高达 1.5 s，严重影响车载预览体验。

**我的工作**

1. **假设证伪 + 全链路埋点**：先抓 H.264 码流对比，发现 X5 用 High Profile、X3 用 Main Profile，但实测两端均能稳跑 30 fps，**排除解码算力瓶颈**。在网络接收 → 解码完成 → 入渲染队列 → 出队渲染各节点打点，发现真正的延时来自渲染前的等待逻辑。

2. **定位根因（首帧延时固化）**：历史代码使用"首帧 PTS + 当前本地时间"作为绝对锚点，后续所有帧都向这个基准对齐。一旦首帧因建连 / 网络抖动迟到 1 秒，**这 1 秒延时就被永久继承到后续所有帧**。注释 wait 逻辑做控制变量验证，延时瞬间降至 0.5 s，确认根因。

3. **三方案演进，选定 AJB**：评估"立即渲染 / 动态追赶 / 自适应抖动缓冲（AJB）"三种方案，最终落地 AJB：
   - **时钟解耦**：用单调时钟 `steady_clock` 建立媒体时间到本地时钟的相对映射（`basePts` / `baseMono`），避免推流端 PTS 与车机系统时间不同时钟源、晶振漂移的污染。
   - **快升慢降的非对称 EMA 滤波**：网络恶化时（observedDelay > target）按 α = 0.8 高权重快速升高目标缓冲防卡顿；网络恢复时按 α = 0.1（稳态）或 0.2（warmup）缓慢回收，避免画面忽快忽慢的呼吸效应。
   - **背压追赶 + 安全丢帧**：队列积压时强制将单帧等待压缩到 ≤ 2 ms 快速消化堆积；过期帧（迟到超阈值）直接丢弃，换取可恢复的实时性。
   - **异常重同步（Resync）**：当观测延时超过 250 ms 时强制重建时间轴基线，应对推流端 PTS 回退 / 跳变 / 本地线程异常抢占。
   - **Deadband 防抖**：误差小于 2 ms 不更新缓冲，过滤微小噪点。

4. **架构权衡的取舍说明**：选择把 AJB 放在解码后（YUV 队列），而非业界常见的解码前（H.264 队列）。原因：历史瓶颈在渲染前等待逻辑，在此处改造收益最直接；车载硬解 VPU 黑盒控制粒度有限；解码后丢帧不破坏 H.264 帧间依赖，追赶安全。同时通过严格限制队列最大长度 + 透传 `network_receive_time` 时间戳来抵消"解码后做 AJB 会引入解码抖动污染"的副作用。

**关键成果**
- X5 端到端预览延时从 **1.5 s 压至约 0.5 s**，下降 **60%+**。
- 完全不牺牲画面平滑度，AJB 在稳态下逼近"立即渲染"的极低延时，在抖动时自动伸缩缓冲。
- 项目作为车载场景方案输出给小鹏，沉淀为 SDK 的预览调度通用模块。

**技术栈**：C++ 11 / FFmpeg / MediaCodec / AJB / EMA 滤波 / 单调时钟 / Producer-Consumer 队列

---

### 项目三：自动剪辑 3.0 性能与精度优化

**项目背景**
自动剪辑业务：用户选择视频 → SDK 抽帧 → 算法分析高光片段 → 拼接转场 / 滤镜 / BGM 输出成片。3.0 算法把抽帧频率从 2.0 的 600 ms / 帧提升到 200 ms / 帧（3 倍），整体耗时严重劣化无法上线。

**我的工作**

1. **修复精度问题（2.0 → 3.0 架构重构）**：原架构上层用时间戳（浮点）抽帧，底层算法用帧 ID（整型），双向转换导致 999 ms 与 1001 ms 抽到不同帧。3.0 将帧 ID 作为贯穿始终的绝对索引，上层单向换算成时间戳，彻底消除转换误差。

2. **Smart Seek 优化抽帧重复解码**：
   - 定位问题：底层抽帧器 Seek 状态不复用——Seek 到 200 ms 时从 0 ms 关键帧解到 200 ms，再 Seek 到 400 ms 又从 0 ms 重新解到 400 ms。
   - 解决方案：如果下一个目标时间在当前目标之后且同一 GOP 内，**不调底层 Seek**，让解码器顺着解、丢掉中间不要的帧。
   - 进阶决策：直接读 FFmpeg 的 `AVStream->index_entries`（即 MP4 `stss` Box 解析后的关键帧索引数组），O(1) 内存操作精准判断每次 Seek 走新流程还是 Smart Seek。

3. **异构硬件并行流水线（核心收益）**：分析硬件分工——抽帧用 VPU、渲染用 GPU、推理用 NPU / CPU，三者物理硬件互不冲突。引入生产者-消费者队列解耦三个阶段：NPU 推理第 1 帧时 GPU 已在渲染第 2 帧、VPU 已在解第 3 帧。优化后总耗时 ≈ 算法推理耗时，抽帧与渲染时间被完美隐藏。

4. **内存池治理频繁分配**：GPU 渲染出的图片送给算法前用对象池接收，避免高频 alloc / free 导致内存碎片；防抖处理改为按窗口动态加载（之前一次性加载 30 分钟素材约 270 MB，优化后仅加载当前窗口）。

**关键成果**
- 出片速度提升 **数倍**（具体倍数随关键帧间隔变化，关键帧越稀疏增益越大）。
- 修复了 2.0 抽帧不准导致的算法异常 Bug。
- 沉淀的并行 Pipeline 架构复用至全景自动剪辑项目，并统一了 iOS / Android 双端渲染层。

**技术栈**：C++ 11 / FFmpeg / 硬件解码器 / OpenGL ES / 生产者-消费者模型 / MP4 stss Box / 对象池

---

## 五、其他技术沉淀

- 系统性整理过 FFmpeg 核心 API、内存管理、PTS / DTS / Timebase、零拷贝、JNI 回调管理、C++ 多线程内存模型、设计模式、KMP / 排序算法、CPU 缓存命中率、无锁队列、ByteBuffer 等专题文档（个人技术沉淀 100+ 篇）。
- 持续学习方向：WebRTC 源码、拥塞控制（GCC / BBR）、端侧 AI 推理（CoreML / MNN / NCNN）与音视频 Pipeline 的结合。

---

## 六、自我评价

- **底层硬骨头偏好**：遇到性能问题不停在"能跑就行"，习惯深挖到协议 / 源码层（FFmpeg Seek 逻辑、MP4 stss Box、OpenGL 显存布局、Gralloc usage 协商）找到最优解。
- **数据驱动决策**：所有优化"先测量、再优化"，靠 Trace 与埋点数据划清边界，不靠直觉拍脑袋。
- **架构兼容意识**：底层重构必须 100% 向后兼容，习惯用参数化动态路由 + 默认关闭新特性的方式平稳上线。
- **暴露风险意识**：定下个人规矩"卡 2 小时没头绪就停下来对齐"，把及时同步进度作为工作习惯。
