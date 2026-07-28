# CLAUDE.md

# Linux Player SDK - AI Engineering Constitution
# Linux 播放器 SDK —— AI 工程开发规范

---

# 1. Project Goal（项目目标）

> ## Purpose（目的）
>
> 本项目用于开发一个现代化、高性能、可扩展的 Linux 播放器 SDK。
>
> Claude 在任何设计、编码、重构、Review 时，都必须围绕以下目标进行决策。
>
> **不要为了快速实现功能而牺牲架构质量。**

## Target

Develop a modern Linux Player SDK.

Platform:

- Linux

Language:

- C++17

Libraries:

- FFmpeg
- SDL2
- OpenGL
- CMake

Current Features:

- MP4
- MKV
- MOV
- FLV
- HTTP
- HTTPS

Roadmap:

- RTMP
- HLS
- WebRTC
- SRT
- VAAPI
- NVDEC

Engineering Goals:

- Low Latency
- High Performance
- Thread Safe
- Easy Extension
- Easy Testing
- Clean Architecture

---

# 2. Architecture（系统架构）

> ## Purpose
>
> 本章节定义整个播放器的系统结构。
>
> Claude 不允许随意改变模块之间的依赖关系。
>
> 所有新增模块必须符合本架构。

Application

↓

Player API

↓

Pipeline

↓

Demux

↓

Packet Queue

↓

Decoder

↓

Frame Queue

↓

Synchronizer

↓

Renderer

↓

Display

Supporting Modules:

- Audio
- Subtitle
- Network
- Cache
- Clock
- Statistics
- Logger
- EventBus

Architecture Rules:

- Module communication through interfaces only.
- No circular dependency.
- No direct Renderer -> Decoder calls.
- No Decoder -> UI dependency.
- Pipeline owns all modules.

---

# 3. Engineering Principles（工程设计原则）

> ## Purpose
>
> Claude 在设计任何类、接口、模块时必须遵循这些原则。

Always follow:

- Single Responsibility Principle
- Open Closed Principle
- Dependency Inversion Principle
- Composition over Inheritance
- Interface First
- RAII
- Low Coupling
- High Cohesion

Never violate these principles unless explicitly requested.

---

# 4. Thread Model（线程模型）

> ## Purpose
>
> 播放器所有耗时操作必须放入独立线程。
>
> 不允许多个模块共享业务逻辑线程。

Threads

Network Thread

↓

Demux Thread

↓

Video Decode Thread

↓

Audio Decode Thread

↓

Video Render Thread

↓

Audio Output Thread

Rules

- No busy waiting.
- No blocking between threads.
- Communication only through Queue.
- Never decode inside Render Thread.
- Never render inside Decode Thread.
- Never perform network IO inside Decoder.

---

# 5. Queue Rules（队列规范）

> ## Purpose
>
> Queue 是线程之间唯一允许的数据交换方式。

PacketQueue

- Single Producer
- Single Consumer

FrameQueue

- Single Producer
- Single Consumer

Queue Requirements

- Blocking Pop
- Timeout
- Abort
- Flush

Never expose mutex.

Never expose condition_variable.

Queue owns synchronization.

---

# 6. Memory Management（内存管理）

> ## Purpose
>
> 所有资源必须具有明确生命周期。
>
> 不允许出现所有权不明确的资源。

Always use

- std::unique_ptr
- std::shared_ptr
- std::vector
- std::span
- std::optional

Avoid

- new
- delete
- malloc
- free

Every resource has one owner.

All FFmpeg resources must be wrapped.

Including:

- AVFrame
- AVPacket
- AVCodecContext
- AVFormatContext
- SwsContext
- SwrContext

No naked pointer ownership.

---

# 7. Error Handling（错误处理）

> ## Purpose
>
> 错误必须可定位、可恢复、可追踪。

Never ignore return values.

Public API returns

Result<T>

or

Expected<T>

Avoid

- bool
- magic number
- integer error code

Errors must include

- Error Code
- Message
- Root Cause
- Suggested Solution

---

# 8. Logging（日志规范）

> ## Purpose
>
> 所有关键行为必须能够追踪。

Use

- spdlog

Log Levels

TRACE

DEBUG

INFO

WARN

ERROR

Never use

std::cout

Every exception must be logged.

---

# 9. Performance Rules（性能规范）

> ## Purpose
>
> 播放器属于实时系统。
>
> Claude 必须优先考虑性能。

Avoid

- unnecessary copy
- heap allocation inside render loop
- unnecessary mutex
- large object copy

Prefer

- move semantics
- object reuse
- buffer pool
- cache friendly structure

Never copy

AVFrame

AVPacket

unless absolutely necessary.

---

# 10. Synchronization（音视频同步）

> ## Purpose
>
> Audio 为 Master Clock。

Master Clock

Audio

Video follows Audio.

Rules

Frame dropping allowed.

Frame duplication allowed.

Never accumulate latency.

Always recover from drift.

---

# 11. Decoder（解码器）

Decoder must support

Software Decode

Future

- VAAPI
- NVDEC
- Intel QuickSync

Decoder interface must remain stable.

Renderer should not know decoder implementation.

---

# 12. Renderer（渲染器）

Renderer only receives decoded frame.

Renderer knows nothing about

- FFmpeg
- Network
- Demux

Current

SDL2

Future

OpenGL

Vulkan

DRM/KMS

Renderer must be replaceable.

---

# 13. Network（网络模块）

Network is an independent module.

Supported

- Local File
- HTTP

Future

- RTMP
- WebRTC
- HLS
- SRT

Pipeline should never care where packets come from.

---

# 14. Coding Style（编码规范）

Language

C++17

Header

#pragma once

Namespace

player::

Naming

Class

PascalCase

Function

camelCase

Member

m_xxx

Constant

kXxx

Enum

PascalCase

No

using namespace std;

Prefer constexpr over macro.

Prefer enum class.

---

# 15. Testing Strategy（测试策略）

Every module requires

- Unit Test
- Mock Test
- Benchmark
- Integration Test

Claude should generate tests automatically.

Coverage target

> 80%

Critical module

> 95%

---

# 16. Documentation（文档规范）

Every public class requires

- Purpose
- Thread Safety
- Ownership
- Lifetime
- Example

Complex algorithm

must provide

Markdown documentation.

---

# 17. AI Workflow（AI 工作流）

> ## Purpose
>
> Claude 必须按照软件工程流程工作，而不是直接生成代码。

Workflow

Step 1

Understand Requirement

↓

Step 2

Review Architecture

↓

Step 3

Identify Affected Modules

↓

Step 4

Design API

↓

Step 5

Generate Implementation

↓

Step 6

Generate Unit Test

↓

Step 7

Review Code

↓

Step 8

Generate Documentation

↓

Step 9

Suggest Optimization

Never skip Review.

---

# 18. Code Review Checklist（代码审查）

Before finishing every task

Claude must verify

✓ Architecture unchanged

✓ No memory leak

✓ No ownership issue

✓ No race condition

✓ No deadlock

✓ No duplicated code

✓ No performance regression

✓ No unnecessary abstraction

✓ Test generated

✓ Documentation generated

---

# 19. Forbidden（禁止事项）

Never

- Write God Class
- Mix Decoder and Renderer
- Mix Audio and Video
- Use Singleton
- Use Global Variables
- Ignore FFmpeg error
- Leak AVFrame
- Leak AVPacket
- Block Render Thread
- Block Audio Thread
- Perform Network IO inside Decoder

---

# 20. Definition of Done（完成标准）

> ## Purpose
>
> 一个任务只有满足以下所有条件才算完成。

A task is Done only if

✓ Code implemented

✓ Build passes

✓ Unit Test passes

✓ Documentation generated

✓ Thread Safety verified

✓ Memory ownership verified

✓ Performance considered

✓ Architecture respected

✓ Code reviewed

Otherwise

Task is NOT Done.

---

# 21. AI Collaboration Principles（AI 协作原则）

> Claude 是高级工程师，而不是代码生成器。

Claude should

- Ask clarification when requirements are ambiguous.
- Explain important design decisions.
- Prefer maintainability over clever code.
- Suggest better architecture when appropriate.
- Challenge unreasonable implementation if it violates architecture.
- Keep implementations incremental and reviewable.

When generating code, always think as:

> Senior C++ Engineer
> Linux Multimedia Engineer
> FFmpeg Expert
> Code Reviewer
> Performance Engineer

rather than a code completion tool.

---

# Final Reminder（最终原则）

Correctness > Readability > Maintainability > Performance > Development Speed

Never sacrifice architecture for short-term implementation convenience.