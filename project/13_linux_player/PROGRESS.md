# Player SDK — 开发进度追踪

> 本文档记录工程整体进度。每个模块完成后更新状态。
> Claude 每完成一个任务，必须更新本文档，并在继续前请求用户确认。

---

## 一、总体状态

| 阶段 | 状态 | 备注 |
|------|------|------|
| 架构设计 | ✅ 完成 | ARCHITECTURE.md |
| 工程脚手架 | ✅ 完成 | 120 个桩文件 + CMake + 脚本 |
| 规范对照修正 | ⬜ 待做 | 桩代码中有命名/返回值与 CLAUDE.md 冲突 |
| Demux 模块实现 | 🔜 下一个任务 | |
| Core 基础设施 | ⬜ 待做 | Queue / Clock / Event / Thread |
| Decode 模块 | ⬜ 待做 | |
| Process 模块 | ⬜ 待做 | |
| Render 模块 | ⬜ 待做 | |
| Control 模块 | ⬜ 待做 | |
| 集成测试 | ⬜ 待做 | |
| 示例程序 | ⬜ 待做 | |

---

## 二、已知待修正项（桩代码 vs CLAUDE.md 冲突）

> 以下是脚手架生成时未对齐 CLAUDE.md 的问题，需在对应模块实现时修正。

| # | 问题 | 涉及文件 | 优先级 |
|---|------|----------|--------|
| 1 | 成员变量命名 `xxx_` → 应为 `m_xxx` | 几乎所有 .h | P1 |
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
| FrameQueue | ⬜ | - | - | |
| Clock | ⬜ | - | - | |
| ClockManager | ⬜ | - | - | |
| EventBus | ⬜ | - | - | |
| ThreadPool | ⬜ | - | - | |
| MemoryPool | ⬜ | - | - | |
| PluginManager | ⬜ | - | - | |

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
| VideoDecoder | ⬜ | - | - | |
| HWAccel (VAAPI) | ⬜ | - | - | |
| AudioDecoder | ⬜ | - | - | |
| SubtitleDecoder | ⬜ | - | - | |

### 3.4 Process（处理层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| FilterGraph | ⬜ | - | - | |
| AudioResampler | ⬜ | - | - | |
| ColorConverter | ⬜ | - | - | |

### 3.5 Render（渲染层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| OpenGLRenderer | ⬜ | - | - | |
| SDL2AudioRenderer | ⬜ | - | - | |
| SubtitleRenderer | ⬜ | - | - | |

### 3.6 Control（控制层）

| 模块 | 状态 | 开始日期 | 完成日期 | 备注 |
|------|------|----------|----------|------|
| PlayerController | ⬜ | - | - | |
| StateMachine | ⬜ | - | - | |
| AVSyncEngine | ⬜ | - | - | |
| SeekHandler | ⬜ | - | - | |
| PlaylistManager | ⬜ | - | - | |

### 3.7 测试

| 模块 | 状态 | 备注 |
|------|------|------|
| Mock 对象 | ⬜ | |
| Unit Tests | ⬜ | 8 个桩已创建 |
| Integration Tests | ⬜ | 5 个桩已创建 |

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

---

## 五、下一步

> 🔜 **FileProtocol 实现**（PacketQueue → FileProtocol → ProtocolFactory → FFmpegDemuxer 的第二步）

| # | 模块 | 状态 |
|---|------|------|
| 1 | PacketQueue | ✅ 完成 |
| 2 | FileProtocol | ✅ 完成 |
| 3 | ProtocolFactory | ✅ 完成 |
| 4 | **FFmpegDemuxer** | 🔜 等待确认 |

---

*最后更新: 2026-07-28*
