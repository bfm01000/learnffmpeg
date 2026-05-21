# 杜孟林

C++ 音视频开发工程师 · 5 年经验 · 西安科技大学 · 深圳

求职意向：C++ 音视频开发 / 端侧多媒体 / 嵌入式音视频 SDK

---

## 技能

- **语言**：C++ 11/14/17、C、Java、Objective-C / Objective-C++
- **音视频**：FFmpeg、H.264/H.265、AAC、MP4 / FLV / RTMP、PTS/DTS 同步、AVFilter
- **Android**：MediaCodec、AHardwareBuffer、Surface、Gralloc、Sync Fence、OpenGL ES、JNI
- **iOS**：VideoToolbox、CVPixelBuffer / IOSurface 零拷贝、ARC / Runtime
- **并发与性能**：生产者-消费者模型、内存池 / 对象池、异构 Pipeline（VPU + GPU + NPU）、Systrace / Perfetto
- **构建**：CMake、NDK、Xcode、Android & iOS 双端 FFmpeg 交叉编译

---

## 工作经历

### 影石创新（Insta360） · 移动端 C++ 高级开发 · 2021.07 – 至今

负责跨平台音视频 SDK，C++ 实现核心 Pipeline，经 JNI / OC 提供给 Android / iOS / 桌面端。

---

## 核心项目

### 项目一 · Android 4K 全景直播零拷贝与 ABR 重构

- 4K 推流帧率 **22 fps → 30 fps**，长时推流杜绝 5 fps 跌底，发热与卡顿问题解决。
- 渲染线程单帧耗时 **40 ms → 10 ms**（CPU 耗时近乎为 0），抹除 4K 30 fps 下约 600 MB/s 的冗余总线带宽。
- 基于 `AHardwareBuffer` + Surface 打通 GPU → VPU 物理级零拷贝，用 Sync Fence 实现渲染与编码的硬件级流水线并行。
- 设计异步发送队列解耦编码与网络 I/O，基于队列水位实现轻量级 ABR 自适应码率（高水位 20% 阶梯降级、低水位 20% 阶梯恢复）。
- 通过参数化动态路由 + JNI 透传保证旧业务 100% 向后兼容。

技术栈：C++ / FFmpeg / AHardwareBuffer / OpenGL ES / MediaCodec / JNI

### 项目二 · 车载相机预览 AJB 自适应抖动缓冲调度器

- 小鹏汽车客户场景下，X5 端到端预览延时 **1.5 s → 0.5 s，下降 60%+**，画面平滑度无损。
- 全链路埋点定位"首帧延时被永久继承"的历史缺陷，并以控制变量法验证根因。
- 自研 AJB 调度器：单调时钟映射 + 非对称 EMA 滤波（快升慢降）+ 背压追赶 + Resync 兜底 + Deadband 防抖。
- 在三种方案（立即渲染 / 动态追赶 / AJB）中完成工程权衡，方案沉淀为 SDK 通用预览调度模块。

技术栈：C++ / FFmpeg / MediaCodec / EMA 滤波 / 单调时钟

### 项目三 · 自动剪辑 3.0 异构 Pipeline 与 Smart Seek

- 抽帧频率提升 3× 的前提下，**出片速度仍提升数倍**，并修复 2.0 抽帧精度异常 Bug。
- Smart Seek：基于 FFmpeg `AVStream->index_entries`（MP4 `stss` Box 索引）做 GOP 内顺解优化，消除底层抽帧器重复解码。
- 引入生产者-消费者模型解耦抽帧 / 渲染 / 推理三阶段，VPU + GPU + NPU 异构硬件并行，总耗时收敛到算法推理耗时。
- 内存池接收 GPU 渲染产物 + 防抖按窗口动态加载，将 30 分钟素材防抖内存 270 MB 降为按需加载。
- 架构复用至全景自动剪辑项目，统一 iOS / Android 渲染层。

技术栈：C++ / FFmpeg / OpenGL ES / 生产者-消费者 / 对象池

---

## 教育背景

西安科技大学 · 本科 · 2021.07 毕业
