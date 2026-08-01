# Player SDK 架构重构文档

> 重构目标：将 `PlayerController`（God Class, 668 行）拆解为独立的 pipeline 模块，引入命令队列实现线程安全，修复 EOS/Seek 关键 Bug，达到可测试、可扩展的状态。

---

## 1. 当前架构 vs 目标架构

### 当前 (as-is)

```
                    PlayerController (668 lines, 单一类)
┌──────────────────────────────────────────────────────────┐
│  IPlayer 实现                                             │
│  + StateMachine  + ClockManager  + AVSyncEngine          │
│  + SeekHandler                                            │
│  + FFmpegDemuxer + AudioDecoder + VideoDecoder           │
│  + AudioResampler + SDL2AudioRenderer + SDLVideoRenderer │
│  + 3 个 Queue (shared_ptr)                                │
│  + 3 个 Thread (unique_ptr<thread>)                       │
│  + demuxLoop_ / audioDecodeLoop_ / videoDecodeLoop_      │
│  + pumpEvents (渲染在主线程)                               │
│  ─────────────────────────────────────                    │
│  问题:                                                    │
│  - API 非线程安全 (直接操作内部状态)                       │
│  - Seek 数据竞争 (FFmpeg context 并发调用)                 │
│  - EOS 从不触发 (Completed 状态不可达)                     │
│  - 状态机锁内回调 (死锁风险)                               │
│  - 硬编码依赖 (无法 mock 测试)                             │
│  - 非 owning shared_ptr 反模式                            │
│  - static 日志计数器共享                                   │
└──────────────────────────────────────────────────────────┘
```

### 目标 (to-be)

```
┌──────────────────────────────────────────────────────────────┐
│                     API Layer (player.h)                      │
│  IPlayer — 对外 Facade，接口不变                              │
├──────────────────────────────────────────────────────────────┤
│                     PlayerController                          │
│  (~200 lines, thin orchestrator)                              │
│  ├─ CommandQueue (MPSC, mutex+condvar)                        │
│  ├─ StateMachine                                              │
│  └─ PipelineManager&                                          │
│  所有 API → enqueue Command → Controller thread 串行处理       │
├──────────────────────────────────────────────────────────────┤
│                     Pipeline Modules                           │
│                                                               │
│  PipelineManager (聚合所有模块, 管理生命周期)                  │
│  ├── DemuxModule        demux thread  → PktQueue(audio+video) │
│  ├── DecodeModule       audio+video decode threads → FrmQueue │
│  ├── AudioOutputModule  aout thread + SDL callback → 音频时钟 │
│  ├── VideoOutputModule  FrmQueue consumer + syncVideo + render│
│  └── SyncModule         ClockManager + AVSyncEngine wiring    │
│                                                               │
├──────────────────────────────────────────────────────────────┤
│                     Infrastructure                             │
│  PacketQueue (flush-token/serial)  │  FrameQueue (peek/next)  │
│  Clock (全原子, 外推模型)           │  AudioRingBuffer (无锁)  │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. 与大厂架构对比

| 决策 | 本方案 | mpv | VLC | ExoPlayer |
|------|--------|-----|-----|-----------|
| **命令序列化** | Command Queue + Controller 线程 | `mp_dispatch_queue` + playback thread | `input_ControlPush` + input thread | Message-based (Handler+Looper) |
| **模块分解** | Demux/Decode/AudioOut/VideoOut 独立 module | `demuxer.c`/`decoder.c`/`vo.c`/`ao.c` | `input`/`vout_thread`/`aout_thread` | `MediaSource`/`Renderer`/`AudioSink` |
| **模块通信** | Queue (PacketQueue/FrameQueue) | `mp_pin`/ring buffer | Queue + picture pool | Listener callbacks |
| **时钟模型** | Audio callback → setClock() + getClock() 外推 | 完全相同 | 完全相同 | `MediaClock` |
| **Seek 安全** | Command 串行 + worker 原子标记协作 | `mp_dispatch_lock` 暂停 worker | `input_Control` 序列化 | player thread 串行 |
| **EOS** | 逐级 sentinel 传播 (flush token → nullptr → Completed) | demux EOF → decoder drain → VO/AO → END_FILE | `INPUT_EVENT_DEAD` → `END_S` | renderer END_OF_STREAM → STATE_ENDED |
| **依赖注入** | 接口 + Factory | `mpv_global` 注册表 | `module_t` 插件系统 | `RenderersFactory` |

---

## 3. 各模块设计方案

### 3.1 CommandQueue — 命令队列

```
外部 API 线程:   play() → Command{Play} → CommandQueue.push() → return future
                       seek(ms) → Command{Seek, ms} → push → return future

Controller 线程:  while (alive) { cmd = queue.pop(); handle(cmd); future.set_value(result); }
```

- **类型**: MPSC, mutex+condvar (非 lock-free — API 调用不频繁, 不需要无锁)
- **重入保护**: 如果 `current_thread == controller_thread`，直接执行（避免自我死锁）
- **类比**: mpv 的 `mp_dispatch_run`, VLC 的 `input_Control`

### 3.2 DemuxModule — 解封装模块

```
输入:   url (string)
输出:   audioPktQueue + videoPktQueue (shared_ptr)
拥有:   FFmpegDemuxer + demux thread
接口:   open(url) → start() → stop() → getQueues()
```

职责: `av_read_frame` 循环, 按 stream_index 路由到对应 PacketQueue, 关键帧感知丢帧恢复, EOF 时 push nullptr sentinel

### 3.3 DecodeModule — 解码模块

```
输入:   audioPktQueue + videoPktQueue (shared_ptr)
输出:   videoFrmQueue (shared_ptr), audio → ring buffer
拥有:   AudioDecoder + VideoDecoder + decode threads
接口:   attach(DemuxModule&) → start() → stop() → getVideoFrmQueue()
```

职责: pkt → send/recv → 包装 Frame → push FrameQueue (改 condvar 阻塞 push); audio decode → resample → ring buffer; 超前 2s 包过滤; EOS drain; 解码器 flush 只在自身线程

### 3.4 AudioOutputModule — 音频输出模块

```
输入:   AudioRingBuffer (DecodeModule 写入)
输出:   音频时钟 → ClockManager (驱动全系统同步)
拥有:   AudioResampler + SDL2 设备 + SDL callback + 音频 FrameQueue 消费线程
```

职责: 独立的 aout 线程消费音频帧 → resample → ring buffer; SDL callback 读取 → 更新时钟; seek 时 clear() ring buffer 防 stale 音频

### 3.5 VideoOutputModule — 视频输出模块

```
输入:   videoFrmQueue (shared_ptr)
输出:   渲染到 SDL 窗口
拥有:   SDLVideoRenderer
接口:   renderNext() (pumpEvents 调用) → bool
```

职责: peekFrame → nextFrame(提前释放槽位) → syncVideo 决策(Render/Sleep/Drop) → render; EOS 检测(frame queue 空 + decoder finished); 主线程执行 (X11 限制)

### 3.6 SyncModule — 同步模块 (纯 wiring, 无线程)

```
拥有:   ClockManager + AVSyncEngine
接口:   masterTime()  audioClock()  syncVideo(frame)
```

职责: 只是把 `m_clockMgr` + `m_avSync` 打包; Clock::getClock() = pts + (now - lastUpdated) * speed 外推模型完全保留

---

## 4. 重构步骤

| Step | 内容 | 风险 | 预期行数变化 |
|------|------|------|------------|
| **1** ✅ | 修复小问题 (锁内回调 / static / shared_ptr 反模式 / 死代码) | 零 | ~50 行改 |
| **2** | 引入 CommandQueue + Controller 线程 | 中 | +100 新建, ~30 改 |
| **3** | 修复 Seek 线程安全 (demux 线程执行 seek, flush token+serial) | 中高 | +80 新建, ~60 改 |
| **4** | 实现 EOS 传播链路 (demux→decode→render→Completed→onCompletion) | 中高 | +60 新建, ~40 改 |
| **5** | 拆出 DemuxModule + DecodeModule + PipelineManager | 高 | +250 新建, ~200 删 |
| **6** | 拆出 AudioOutputModule + VideoOutputModule + SyncModule | 高 | +300 新建, ~200 删 |
| **7** | DI + Factory + 死代码大清理 | 中 | +80 新建, ~500 删 |

### Step 1 已完成内容 (2026-08-01)

- **StateMachine**: `transit()` 先拷贝 listeners、释放锁后再通知（修复死锁风险）
- **SeekHandler::Dependencies**: 移除 `audioFrmQueue`(从未使用) + `eventBus`(从未订阅); `shared_ptr<ClockManager>` → `ClockManager*` 裸指针
- **PlayerController**: 移除构造函数中无意义的 `transit(Open)+reset()`; 移除 `initPipeline_` 中重复的 SeekHandler 构造; 5 个 `static` 日志计数器 → 实例成员
- **测试修复**: 3 个测试文件的 include 路径修正

---

## 5. 不变的部分（保留现有实现）

这些模块设计正确，整个重构中不需修改：

| 模块 | 原因 |
|------|------|
| `Clock` | 全原子操作，`getClock() = pts + (now - lastUpdated) * speed` 外推模型与 ffplay 一致 |
| `AudioRingBuffer` | 无锁 SPSC，设计正确 |
| `PacketQueue` | mutex+condvar，flush-token/serial 设计与 ffplay 一致 |
| `FrameQueue` | peek/next/prev 语义正确，只需加 condvar 阻塞 push |
| `AVSyncEngine` | 三层决策（Render/Sleep/Drop）逻辑正确 |
| `StateMachine` | 28 条转移表完整 |
| `AudioDecoder`/`VideoDecoder` | 正确的 FFmpeg 封装，零拷贝 ref 传递 |
| `AudioResampler` | swresample 封装正确 |
| `PlayerConfig` | 嵌套 struct 设计合理 |
| `IPlayer` | 对外接口不变 — 示例程序和测试完全兼容 |

---

## 6. 关键设计决策

### 为什么 VideoOutputModule 没有独立线程

X11 要求 SDL 渲染操作必须在调用 `SDL_Init` 的线程（主线程）。现有代码已经在 `pumpEvents()` 中做视频渲染，这是正确的。VideoOutputModule 封装渲染逻辑但不拥有线程。如果需要未来支持离屏渲染或 EGL，新后端可以拥有自己的线程而不影响调用者。

### 为什么 CommandQueue 用 mutex+condvar 而不是 lock-free

lock-free SPSC 要求单生产者单消费者。API（play/pause/seek）可从任意线程调用 → 多生产者。MPSC mutex+condvar 是正确选择，和 mpv 的 `mp_dispatch_queue` 一致。Command queue 不在热路径上，mutex 开销可忽略。

### 为什么 Seek 由 demux 线程执行

ffplay 的做法：seek request → `av_seek_frame` 在 `read_thread` 中执行 → `packet_queue_flush` → 插入 flush token。我们在 demux 线程中做同样的事，彻底消除 `av_seek_frame` 和 `av_read_frame` 之间的并发。

### EOS 为什么需要逐级传播

不能简单地在 demux EOF 时立刻转 Completed — 解码器和渲染器的缓冲区里还有残留帧需要处理完。正确做法是：
1. demux EOF → push nullptr sentinel
2. decoder 收到 sentinel → flush+drain（出清残留帧）→ 标记 finished
3. renderer 消费完 FrameQueue → 标记 drained
4. demux EOF + 所有流 finished + 所有队列 drained → Completed

这保证最后一帧不会被丢弃。
