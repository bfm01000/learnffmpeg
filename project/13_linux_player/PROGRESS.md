# Player SDK — 开发进度追踪

> 本文档记录工程整体进度。每个模块完成后更新状态。
> Claude 每完成一个任务，必须更新本文档。

[2026-07-31] 在 `api/player_config.h` 文件开头添加了详细的配置树设计说明注释，涵盖: 结构概览、设计理由（嵌套 struct vs AVDictionary vs JSON）、预设模式、以及主流大厂做法对比（VLC/mpv/GStreamer/ExoPlayer/Chromium/国内商业 SDK）。

---

## 一、总体状态

| 阶段 | 状态 | 备注 |
|------|------|------|
| 架构设计 | ✅ 完成 | ARCHITECTURE.md |
| 工程脚手架 | ✅ 完成 | 120 个桩文件 + CMake + 脚本 |
| 规范对照修正 | ✅ 完成 | Frame.h MediaType 冲突 ✅; 桩文件 xxx_→m_xxx ✅; Result<T> 返回值 ✅; Logger 单例⚠(P2) |
| Demux 模块实现 | ✅ 完成 | |
| Core 基础设施 | ✅ 完成 | Queue / Clock / Event / Thread / Frame / MemoryPool / PluginManager |
| Decode 模块 | ✅ 完成 | Video + Audio |
| Process 模块 | 🔧 部分完成 | AudioResampler ✅; FilterGraph/ColorConverter ⬜ |
| Render 模块 | 🔧 部分完成 | SDL2Audio ✅; SDLVideo ✅; OpenGL ⬜(已实现未接入编译) |
| Control 模块 | ✅ 完成 | PlayerController + StateMachine + AVSyncEngine + SeekHandler + PlaylistManager |
| 集成测试 | 🔧 部分完成 | StateMachine 15/15; AVSync 9/9 |
| 示例程序 | ✅ 完成 | simple_player / audio_player / full_player 可编译运行 |

---

## 二、已知待修正项（桩代码 vs CLAUDE.md 冲突）

| # | 问题 | 涉及文件 | 优先级 |
|---|------|----------|--------|
| 1 | 成员变量命名 `xxx_` → 应为 `m_xxx` | ColorConverter, FilterGraph, SubtitleRenderer, HTTP/RTMP/RTSP protocol stubs | P1 |
| 2 | API 返回值 `int` → 应为 `Result<T>` | api/player.h | P1 |
| 3 | 自建 Logger 单例 → 应封装 spdlog，禁用单例 | utils/logger/ | P2 |
| 4 | `PlayerConfig` 暴露 `AV_TIME_BASE_Q` | api/player_config.h | P2 |
| 5 | API 层头文件不应 include FFmpeg 类型 | api/player_types.h 等 | P3 |

---

## 三、模块实现进度

### 3.1 Core 基础设施

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| PacketQueue | ✅ | 2026-07-28 | 2026-07-28 | 19 tests passed |
| FrameQueue | ✅ | 2026-07-29 | 2026-07-29 | header-only 模板完整实现 |
| Clock | ✅ | 2026-07-28 | 2026-07-28 | 10 tests |
| ClockManager | ✅ | 2026-07-28 | 2026-07-28 | 7 tests |
| EventBus | ✅ | 2026-07-28 | 2026-07-28 | 10 tests |
| ThreadPool | ✅ | 2026-07-28 | 2026-07-28 | 实现完成 |
| MemoryPool | ✅ | 2026-07-29 | 2026-07-29 | header-only 模板完整实现 |
| PluginManager | ✅ | 2026-07-29 | 2026-07-29 | dlopen/dlsym 完整实现 |

### 3.2 Source（数据源层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| IProtocolHandler | ✅ | - | - | 接口已定义（纯虚类） |
| ProtocolFactory | ✅ | 2026-07-28 | 2026-07-28 | 13 tests passed |
| IMediaSource | ⬜ | - | - | 接口已定义 |
| FFmpegDemuxer | ✅ | 2026-07-28 | 2026-07-28 | 14 tests passed |
| FileProtocol | ✅ | 2026-07-28 | 2026-07-28 | 17 tests passed |
| HTTPProtocol | ⬜ | - | - | |
| RTMPProtocol | ⬜ | - | - | |
| RTSPProtocol | ⬜ | - | - | |

### 3.3 Decode（解码层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| VideoDecoder | ✅ | 2026-07-29 | 2026-07-29 | 8 tests |
| HWAccel (VAAPI) | ⬜ | - | - | |
| AudioDecoder | ✅ | 2026-07-29 | 2026-07-29 | 6 tests |
| SubtitleDecoder | ⬜ | - | - | |

### 3.4 Process（处理层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| FilterGraph | ⬜ | - | - | |
| AudioResampler | ✅ | 2026-07-29 | 2026-07-29 | 4 tests |
| ColorConverter | ⬜ | - | - | |

### 3.5 Render（渲染层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| OpenGLRenderer | ✅* | - | - | 已实现但未接入编译（缺 GLFW） |
| SDL2AudioRenderer | ✅ | 2026-07-29 | 2026-07-29 | 延迟打开, ring buffer；已接线 ClockManager |
| SDLVideoRenderer | ✅ | 2026-07-29 | 2026-07-29 | SDL2 YUV 直接渲染，集成crash已修复 |
| SubtitleRenderer | ⬜ | - | - | |

### 3.6 Control（控制层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| PlayerController | ✅ | 2026-07-29 | 2026-07-29 | v3: StateMachine + FrameQueue + AVSyncEngine + SeekHandler |
| StateMachine | ✅ | 2026-07-29 | 2026-07-29 | 完整转换表 (ARCH §5.2), 15 tests |
| AVSyncEngine | ✅ | 2026-07-29 | 2026-07-29 | syncVideo(Render/Sleep/Drop) + syncAudio, 9 tests |
| SeekHandler | ✅ | 2026-07-29 | 2026-07-29 | 暂停时钟→清空队列→刷新解码器→seek→恢复 全流程 |
| PlaylistManager | ✅ | - | - | 完整实现 |

### 3.7 测试

| 模块 | 状态 | 备注 |
|------|------|------|
| StateMachine Tests | ✅ 15/15 | 全状态转移路径覆盖 |
| AVSyncEngine Tests | ✅ 9/9 | null/drop/sleep/render/syncAudio/params |

---

## 四、任务历史

| 日期 | 任务 | 结果 |
|------|------|------|
| 2026-07-28 | 架构设计 | ✅ ARCHITECTURE.md 完成 |
| 2026-07-28 | 工程脚手架 | ✅ 120 源文件 + CMake + 脚本创建 |
| 2026-07-28 | PROGRESS.md 创建 | ✅ 本文档 |
| 2026-07-28 | PacketQueue 实现 | ✅ 19 tests passed |
| 2026-07-28 | FileProtocol 实现 | ✅ 17 tests passed |
| 2026-07-28 | FrameQueue 文档完善 | ✅ 设计理由写到头文件 |
| 2026-07-28 | ProtocolFactory 实现 | ✅ 13 tests passed |
| 2026-07-28 | FFmpegDemuxer 实现 | ✅ 14 tests passed |
| 2026-07-28 | Core 层补齐 (Clock/EventBus/ThreadPool) | ✅ 27 tests, 合计 90 |
| 2026-07-29 | Decode 层 (VideoDecoder + AudioDecoder) | ✅ 14 tests, 合计 104 |
| 2026-07-29 | PlayerController 端到端音频 | ✅ 音频链路通 |
| 2026-07-29 | FFmpeg 双版本冲突修复 | ✅ PKG_CONFIG_PATH |
| 2026-07-29 | Control 层完整实现 | ✅ StateMachine + AVSyncEngine + SeekHandler + PlayerController v3 |
| 2026-07-29 | Frame.h MediaType 冲突修复 | ✅ 移除重复 enum, 统一用 api/player_types.h |
| 2026-07-29 | EventType 扩展 | ✅ 新增 Open/Retry/Stopped/SeekComplete 事件 |
| 2026-07-29 | SDL2AudioRenderer Clock 接线 | ✅ setClockTarget() → ClockManager 实时同步 |
| 2026-07-29 | 测试: StateMachine 15 + AVSync 9 | ✅ 24 tests passed |
| 2026-07-29 | 示例程序编译修复 | ✅ 补 <thread>/<chrono> includes; opengl_player 跳过(无GLFW) |
| 2026-07-29 | 静态库编译修复 | ✅ BUILD_SHARED_LIBS=OFF 解决 /usr/local FFmpeg PIC 问题 |

---

## 五、v3 Control 层架构变更摘要

1. **StateMachine**: 填充了完整的 28 条状态转移规则（ARCHITECTURE.md §5.2），PlayerController 通过 `transit()` 驱动状态流转
2. **AVSyncEngine**: `syncVideo()` 实现三层决策：接近→Render / 超前→Sleep→Render / 严重落后→Drop；`syncAudio()` 更新音频时钟
3. **SeekHandler**: 全流程：暂停时钟 → 清空队列 → 刷新解码器 → demux seek → 更新时钟 → 恢复
4. **FrameQueue 管线**: 视频 Decoder → FrameQueue → Renderer 解耦，非阻塞 push/peek
5. **音频时钟接线**: SDL callback → `setClockTarget()` → ClockManager.audioClock() 实时驱动
6. **setSpeed/setVolume/setLoop**: 全部实现

---

*最后更新: 2026-07-29 23:59*
