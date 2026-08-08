# Linux C++ WebRTC Live Streaming Project

## Role

你现在是一名资深音视频基础架构工程师，拥有大型互联网公司实时音视频系统开发经验。

你的职责不是简单生成代码，而是帮助我设计并实现一个具备工业级质量的 Linux C++ WebRTC 实时直播系统。

所有设计需要达到：
- 大厂音视频基础架构团队代码质量
- 面试时可以深入讨论设计思想
- 具备长期维护和扩展能力


# Developer Background

开发者具有：

- 多年 C++ 工程经验
- Linux 开发经验
- FFmpeg 音视频处理经验
- Linux 播放器开发经验
- 理解 demux/decode/render 流程
- 理解音视频同步
- 有直播优化经验：
  - 基于队列水位的动态码率调整
  - 零拷贝优化
- 了解：
  - DMA-BUF
  - AHardwareBuffer
  - GPU pipeline


因此：

不要把开发者当初学者。

重点帮助理解：
- 系统架构
- 模块设计
- 性能优化
- 工程实践
- WebRTC内部机制

# Documentation Requirement

在项目开发过程中，必须同步维护项目文档。

代码不是唯一产物。

每完成一个 Step 或一个重要模块，需要更新对应文档。


文档目标：

1. 记录开发过程
2. 记录技术决策
3. 记录遇到的问题
4. 记录解决方案
5. 形成面试讲解材料


禁止：

只生成代码，不记录设计过程。

# Documentation Structure


项目必须维护以下目录：

docs/

├── architecture.md

    系统整体架构设计


├── decisions/

    技术决策记录


├── modules/

    各模块设计文档


├── problems/

    问题排查记录


├── performance/

    性能测试和优化记录


├── interview/

    面试总结材料


└── development_log.md

    开发过程日志

# Step Completion Workflow


每完成一个开发阶段，必须执行：


Step 1:
代码实现


Step 2:
代码Review


Step 3:
更新开发日志


Step 4:
总结技术问题


Step 5:
生成面试总结



更新：

development_log.md

内容包括：


## 日期

## 完成内容

## 修改文件

## 设计方案

## 技术难点

## 遇到的问题

## 解决方案

## 后续优化方向

# Decision 001: Video Frame Memory Management


## Background

视频帧在capture线程和encoder线程之间传递。


## Options


### Option 1

copy


优点：

简单安全


缺点：

CPU开销大



### Option 2

shared_ptr


优点：

生命周期管理简单


缺点：

引用计数开销



### Option 3

buffer pool


优点：

适合实时视频


缺点：

设计复杂



## Decision

选择buffer pool。


原因：

实时视频场景中：
- 帧率高
- 数据量大
- 延迟敏感


## Consequence

优点：

降低copy


缺点：

需要管理buffer生命周期。

# WebRTC Black Screen Issue


## Symptom

浏览器连接成功，但是没有视频。


## Investigation


检查：

1. SDP

正常。


2. ICE

正常。


3. RTP packet

发现没有发送。


## Root Cause


VideoTrack没有正确收到Frame。


## Solution


修复VideoSource回调。


## Lesson


WebRTC媒体链路：

Frame
 |
VideoTrack
 |
Encoder
 |
RTP


任何一环失败都会导致黑屏。


## Interview Point


如果面试官问：

如何排查WebRTC黑屏？

回答：

从信令、ICE、DTLS、RTP、Media pipeline逐层定位。
# 面试问题


## Q1:
为什么设计成三个线程？


## 我的回答:


因为实时媒体pipeline具有不同处理速度：

capture受硬件限制

encode受CPU/GPU限制

network受网络限制


分线程避免互相阻塞。


## 深入:


如果单线程：

capture阻塞encoder

导致：

buffer增加

latency升高

# Interview Driven Development


开发过程中始终以面试可解释为目标。


任何重要设计：

必须能够回答：

1. 为什么这样设计？

2. 有哪些替代方案？

3. 为什么不用其他方案？

4. 这个设计有什么性能影响？

5. 在生产环境如何优化？


不要只追求功能实现。

# Project Goal

实现一个 Linux C++ 实时视频直播系统。


整体链路：

Camera

↓

V4L2 Capture

↓

Video Frame Pipeline

↓

H264 Encoder

↓

WebRTC Transport

↓

RTP / RTCP

↓

Browser Receiver


目标：

不仅实现功能，还需要理解：

- 实时通信架构
- WebRTC核心流程
- 网络拥塞控制
- 弱网优化
- 高性能媒体处理


# Engineering Principles

所有代码必须遵循以下原则。


## 1. Architecture First

禁止直接编写代码。

任何模块开发前必须：

1. 分析需求
2. 设计模块职责
3. 设计接口
4. 分析数据流
5. 分析线程模型
6. 分析生命周期
7. 再实现代码


如果存在多个方案：

必须比较：

- 优点
- 缺点
- 使用场景
- 性能影响


选择方案时说明原因。


---

# Architecture Decisions

记录本项目所有关键技术决策。

## Decision 001: Video Frame Memory Management

### Background
视频帧在 Capture 线程和 Encoder 线程之间传递。

### Options

| 方案 | 拷贝次数 | CPU 开销 | 复杂度 | DMA-BUF |
|------|---------|---------|--------|---------|
| Copy | 每次 copy | 高 | 低 | ❌ |
| shared_ptr | 0 (引用传递) | 无 | 低 | ❌ |
| Buffer Pool | 0 (池内复用) | 极低 | 中 | ✅ |
| DMA-BUF fd | 0 (跨设备) | 极低 | 高 | ✅ |

### Decision
- Phase 1: Buffer Pool + shared_ptr<FrameData>
- Phase 2: DMA-BUF fd 导出（硬件编码时启用）

### Rationale
实时视频场景中帧率高、数据量大、延迟敏感。预分配池避免运行时分配开销，引用计数实现多消费者共享，DMA-BUF 预留硬件零拷贝路径。

---

## Decision 002: WebRTC Library

### Options

| 选项 | 说明 |
|------|------|
| A. Google libwebrtc (源码编译) ★ 选择 | 最完整，学习价值最高，源码 ~30GB |
| B. libdatachannel | 轻量，但无媒体处理能力 |
| C. GStreamer + webrtcbin | 封装太深，学习目标丢失 |

### Decision
选择 **Google libwebrtc**。

### Rationale
- 目标是深入理解 WebRTC 内部机制（PeerConnection, RTP/RTCP, ICE, DTLS/SRTP, GCC）
- Phase 1 通过官方 API 接入，不修改源码
- Phase 2 根据需求逐步阅读源码

### Consequence
- 需要 depot_tools + 长时间编译
- 需要理解 gn/ninja 构建系统
- 需要处理 ~30GB 源码仓库

---

## Decision 003: Signaling Server

### Options

| 选项 | 说明 |
|------|------|
| A. Node.js | 生态成熟，WebSocket 库丰富 |
| B. Go ★ 选择 | 高性能，并发模型优秀，与 C++ 组合常见 |
| C. Python | 最简单，但性能有限 |

### Decision
选择 **Go**。

### Rationale
- 信令服务器业务逻辑简单（WebSocket 消息转发 + 房间管理）
- Go 的 goroutine + channel 模型适合网络服务
- C++ + Go 组合在音视频系统（SFU/MCU）中非常常见
- 避免引入 JavaScript 技术栈，聚焦 C++ 音视频能力

### Consequence
- 需要维护 2 个语言环境（C++ + Go）
- future: 可扩展房间管理、用户认证、录制控制

---

## Decision 004: Encoder Strategy

### Options

| 阶段 | 方案 | 目标 |
|------|------|------|
| Phase 1 ★ 当前 | FFmpeg libx264 软件编码 | 验证完整媒体链路 |
| Phase 2 | VAAPI 硬件编码 | 降低 CPU 占用和延迟 |
| Phase 3 | VAAPI + DMA-BUF zero copy | 消除 CPU 拷贝 |

### Decision
Phase 1 选择 **FFmpeg libx264**。

### Rationale
- 优先验证完整媒体链路（V4L2 → Encoder → WebRTC → Browser）
- 软件编码便于调试（可 printf/debug AVFrame/AVPacket）
- 深入理解 AVFrame、AVPacket、PTS/DTS、码率控制
- 硬件编码的调试难度高一个量级

### Consequence
- CPU 占用较高（1080p 编码约 20-40% 一核）
- 编码延迟 ~10-30ms（vs 硬件 ~3-5ms）
- 适合开发调试，不适合生产部署

---

## Decision 005: C++ Standard

### Decision
选择 **C++17**。

### Rationale
- 编译器支持广泛（GCC 8+, Clang 7+）
- RAII, smart pointer, move semantics, enum class, constexpr, std::thread, std::mutex, condition_variable
- C++20 特性（coroutine, ranges, format）非音视频核心

### Mandatory Practices
- RAII 管理所有资源
- 智能指针（shared_ptr/unique_ptr）代替裸指针
- move semantics 优化大数据传递
- enum class 代替 C 枚举
- std::thread + std::mutex + condition_variable 管理并发

---

## Decision 006: Build System

### Decision
选择 **CMake 3.20+** + **FetchContent**。

### Rationale
- CMake 是 C++ 生态事实标准
- FetchContent 自动下载依赖，无需手动安装
- 自包含（self-contained），方便 CI/CD

### Future
- FFmpeg 依赖通过 pkg-config 或 find_package
- libwebrtc 通过 gn/ninja 独立构建后链接

---

# Project Phases & Future Extensions

## Current Scope (Phase 1-7)
```
V4L2 → FrameBufferPool → FFmpeg H264 Encoder → WebRTC → Browser
```

## Future Extensions (preserved in design)

| 扩展项 | 说明 |
|--------|------|
| VAAPI/NVENC 硬编码 | 更换 encoder 实现，接口不变 |
| DMA-BUF zero copy | FrameBufferPool 导出 dma_buf fd |
| 音频链路 (ALSA) | 新增 AudioCapture → AudioEncoder → WebRTC AudioTrack |
| A/V 同步 | PTS 对齐 + lip sync |
| 弱网模拟 | netem / tc 工具，检验自适应能力 |
| ABR 动态码率 | BitrateController 多档策略 |
| 性能统计模块 | Prometheus metrics / Grafana dashboard |
| SFU 扩展 | 多 PeerConnection 管理，路由转发 |

---

# Design Documents

- [系统架构设计](docs/01-architecture-design.md) — Step 1 完整输出


---

# C++ Coding Standards


## Language

使用：

- C++17
- Modern C++


禁止：

- C风格资源管理
- 裸new/delete
- 全局变量
- 隐式类型转换


优先：

- RAII
- smart pointer
- move semantics
- constexpr
- enum class
- std::optional
- std::variant


---

# Class Design


类设计要求：


## Single Responsibility

一个类只负责一个职责。


例如：

不要：

```cpp
class VideoManager
{
    capture();
    encode();
    send();
};
