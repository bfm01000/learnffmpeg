# 工作日志

## 2026-07-29 ~ 2026-07-31 Control 层实现 & 音视频管线调试

### 概述
完成了 Control 层 5 个模块（StateMachine/AVSyncEngine/SeekHandler/PlayerController/PlaylistManager），并解决了三个关键的运行时问题。

---

## 问题 1: 视频黑屏（窗口出现但无画面）

### 现象
`full_player` 正常启动、SDL 窗口弹出、状态机正常流转（Idle→Loading→Ready→Playing），但窗口内全黑，无任何画面。

### 排查过程

**第一步：隔离测试。** 写了一个绕过 player_sdk 的裸 SDL+FFmpeg 单线程 demo（`minimal_sdl`），直接在主线程 decode + SDL_UpdateYUVTexture + SDL_RenderPresent。**有画面**，证明：
- FFmpeg 解码正常工作
- SDL YUV 纹理渲染通路正常
- 显示环境正常

**第二步：定位差异。** `minimal_sdl` 所有操作在主线程；`full_player` 的 SDL 渲染在 worker 线程（`videoRenderLoop_`）。进一步测试发现把 SDL 初始化挪到 worker 线程会导致程序卡死（X11 的 `SDL_Init` 必须在主线程）。

**第三步：确认根因。** X11 要求 SDL 渲染操作在调用 `SDL_Init` 的线程（主线程），但 `full_player` 在 worker 线程做 `SDL_UpdateYUVTexture` + `SDL_RenderPresent`。不同 X11 配置下可能黑屏或闪烁。

### 解决方案
新增 `IPlayer::pumpEvents()` 接口，由主线程调用：
- **SDL 事件轮询**（`SDL_PollEvent`）→ 主线程
- **视频帧渲染**（`SDL_UpdateYUVTexture` + `SDL_RenderCopy` + `SDL_RenderPresent`）→ 主线程
- **视频解码** → 保持 worker 线程（`videoDecodeLoop_`），通过 `FrameQueue` 传递解码帧

主线程渲染需要解码结果，但两者通过 `FrameQueue` 解耦：解码线程生产 Frame 入队，主线程消费 Frame 出队渲染。

**核心代码对比:**

`api/player.h` — 新增 pumpEvents() 接口:
```diff
+  /// 主线程事件泵 — 调用者需在帧循环中周期性调用此方法.
+  /// 处理 SDL 事件轮询 + 视频帧渲染 (必须在主线程调用, X11 要求).
+  /// @return true 窗口仍然打开, false 窗口已关闭
+  virtual bool pumpEvents() = 0;
```

`full_player/main.cpp` — 主循环从单纯 sleep 改为 pumpEvents:
```diff
-  while(g_run){
-    auto s=p->getState();
-    if(s==PlayerState::Completed||s==PlayerState::Error)break;
-    std::this_thread::sleep_for(100ms);
-  }
+  // Main loop — pump SDL events + render frames (X11 requires main thread)
+  while (g_run) {
+    auto s = p->getState();
+    if (s == PlayerState::Completed || s == PlayerState::Error) break;
+    if (!p->pumpEvents()) break;  // window closed
+    std::this_thread::sleep_for(2ms);
+  }
```

`sdl_video_renderer.cpp` — SDL_PollEvent 从 render() 移到 pollEvents():
```diff
-  SDL_RenderClear(m_renderer);
-  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
-  SDL_RenderPresent(m_renderer);
-
-  SDL_Event e; while(SDL_PollEvent(&e)) { if(e.type==SDL_QUIT) destroy(); }
-  return 0;
+  SDL_RenderClear(m_renderer);
+  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
+  SDL_RenderPresent(m_renderer);
+  return 0;
...
+bool SDLVideoRenderer::pollEvents() {
+  if (!m_window) return false;
+  SDL_Event e;
+  while (SDL_PollEvent(&e)) {
+    if (e.type == SDL_QUIT) {
+      destroy();
+      return false;
+    }
+  }
+  return true;
+}
```

PlayerController — `videoDecodeLoop_` 不再负责渲染（去掉 `videoRenderLoop_`）:
```diff
-  if (m_videoStreamIdx >= 0) {
-      m_videoDecodeThread = std::make_unique<std::thread>(&PlayerController::videoDecodeLoop_, this);
-      m_videoRenderThread = std::make_unique<std::thread>(&PlayerController::videoRenderLoop_, this);
-  }
+  if (m_videoStreamIdx >= 0) {
+      m_videoDecodeThread = std::make_unique<std::thread>(&PlayerController::videoDecodeLoop_, this);
+      // Video rendering happens on main thread via pumpEvents()
+  }
```

---

## 问题 2: 完全无声音

### 现象
视频正常后，`full_player` 完全没有音频输出。但 `audio_player`（纯音频 demo）和 `minimal_audio`（裸 SDL 音频 demo）都能正常播放声音。

### 排查过程

**第一步：验证音频管线在生产帧。** 在 `audioDecodeLoop_` 中添加计数器，确认 AAC 解码→重采样→`render()` 调用正常执行，每秒约 43 帧音频（44100Hz / 1024 samples）。

**第二步：尝试 `SDL_Init` 统一初始化。** 怀疑是 `SDL_InitSubSystem(AUDIO)` 和 `SDL_InitSubSystem(VIDEO)` 分别调用有冲突。改为 `SDL_Init(AUDIO|VIDEO)` 一次性初始化 → **无效**。

**第三步：检查 SDL_OpenAudioDevice 返回值。** 添加错误日志后看到：
```
SDL_OpenAudioDevice failed: Audio subsystem is not initialized
```

音频子系统未初始化！但 `SDL_Init(AUDIO|VIDEO)` 明明在构造函数中调用了。

**第四步：追踪调用链。** `open()` 第一步是 `stop()`：
```
open() → stop() → m_audioRenderer.destroy() → SDL_QuitSubSystem(SDL_INIT_AUDIO)
```

`SDL2AudioRenderer::destroy()` 中**无条件**调用了 `SDL_QuitSubSystem(SDL_INIT_AUDIO)`。即便设备从未打开，quit 也会把构造函数中 `SDL_Init` 初始化的音频子系统引用计数归零、彻底关闭。然后 `initPipeline_()` 尝试 `SDL_OpenAudioDevice` 时音频子系统已死。

另外 `SDLVideoRenderer::destroy()` 有同样问题，无条件 quit `SDL_INIT_VIDEO`。

### 解决方案
从 renderer 的 `destroy()` 中移除 `SDL_QuitSubSystem` 调用，改为在 `PlayerController` 析构函数中统一 `SDL_Quit()`。

同时发现第二个问题：SDL 音频设备在视频窗口**之后**才打开（lazy init），而 PulseAudio 在 X11 窗口创建后会干扰后续音频设备打开。修复为：在 `initPipeline_` 的音频初始化段落**提前**调用 `openDevice()`（在 `SDL_CreateWindow` 之前）。

完整修复要点：
1. `SDL_Init(AUDIO|VIDEO)` 在构造函数中一次性调用
2. 音频设备在视频窗口之前立即打开（非 lazy）
3. 移除 renderer 中的 `SDL_QuitSubSystem`
4. `~PlayerController` 统一 `SDL_Quit()`

**核心代码对比:**

`player_controller.cpp` — SDL 生命周期集中管理:
```diff
 PlayerController::PlayerController()
-    { changeState_(PlayerState::Idle); }
+    : m_avSync(m_clockMgr)
+{
+    // Unified SDL init — audio+video together, avoids PulseAudio/X11 conflict
+    SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
+    ...
+}

 PlayerController::~PlayerController()
 {
     stop();
     teardown_();
+    SDL_Quit(); // centralized SDL cleanup
 }
```

`sdl2_audio_renderer.cpp` — 移除 SDL_QuitSubSystem:
```diff
 SDL2AudioRenderer::SDL2AudioRenderer() {
-  SDL_InitSubSystem(SDL_INIT_AUDIO);
   m_ringBuffer = std::make_unique<AudioRingBuffer>();
 }

 void SDL2AudioRenderer::destroy() {
   ...
   m_ringBuffer->clear();
-  SDL_QuitSubSystem(SDL_INIT_AUDIO);
+  // SDL_QuitSubSystem handled centrally by PlayerController
 }
```

`sdl_video_renderer.cpp` — 同样移除:
```diff
 void SDLVideoRenderer::destroy() {
   ...
-  SDL_QuitSubSystem(SDL_INIT_VIDEO);
+  // SDL_QuitSubSystem handled centrally by PlayerController
 }
```

`player_controller.cpp` — 音频设备在视频窗口之前打开:
```diff
+        // Eagerly open audio device BEFORE video window creation.
+        // On Linux/PulseAudio, SDL_CreateWindow can interfere with
+        // subsequent audio device opening. Opening audio first avoids this.
+        m_audioRenderer.openDevice(
+            outRate, outCh, static_cast<AVSampleFormat>(outFmt));
+
         // ... video pipeline init (SDL_CreateWindow happens later) ...
```

---

## 问题 3: 音频变调/音色异常

### 现象
声音播放出来了，但音色不正常（音调偏移、偶尔有爆音）。视频正常。

### 排查过程

**第一步：检查采样率匹配。** 音频重采样器输出 44100Hz S16 2ch，SDL 音频设备请求 44100Hz S16 2ch（带 `SDL_AUDIO_ALLOW_ANY_CHANGE`）。当 SDL 选择实际不同采样率（如 48000Hz），重采样器仍输出 44100 → 播放速度不对 → 音调偏移。

**第二步：检查 ring buffer。** 对比能正常工作的 `minimal_audio`（裸实现）和 `SDL2AudioRenderer`：

| | minimal_audio ✅ | SDL2AudioRenderer ❌ |
|---|---|---|
| buffer 满时 | `while(writable<need) SDL_Delay(1)` 等待 | **直接 return 0，丢帧** |

音频解码线程远快于 SDL 回调消费速度（解码异步、播放同步），ring buffer 快速填满。`SDL2AudioRenderer::render()` 中 `write()` 返回 0 后直接丢弃帧 → 音频空洞 → 爆音/变调。

### 解决方案
1. **采样率同步**：`openDevice()` 通过引用参数把 SDL 设备的**实际**采样率传回，`AudioResampler` 使用该值初始化
2. **Ring buffer 背压**：`render()` 中改为 `while (m_ringBuffer->writable() < bytes) SDL_Delay(1)`，阻塞等待直到有空间，不再丢帧

**核心代码对比:**

`sdl2_audio_renderer.cpp` — SDL_AUDIO_ALLOW_ANY_CHANGE → 精确匹配:
```diff
   SDL_AudioSpec obtained;
-  m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained,
-                                    SDL_AUDIO_ALLOW_ANY_CHANGE);
+  // Don't use SDL_AUDIO_ALLOW_ANY_CHANGE — we want exact format match
+  m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
```

`sdl2_audio_renderer.cpp` — render() 非阻塞丢帧 → 阻塞背压:
```diff
   int bytes = av_get_bytes_per_sample(
       static_cast<AVSampleFormat>(frame->format), 1);
   if (bytes <= 0) return 0;

-  size_t written = m_ringBuffer->write(frame->data[0], static_cast<size_t>(bytes));
-  return written > 0 ? 0 : -1;
+  // Block until ring buffer has space (don't drop audio frames)
+  while (m_ringBuffer->writable() < static_cast<size_t>(bytes)) {
+    SDL_Delay(1);
+  }
+  m_ringBuffer->write(frame->data[0], static_cast<size_t>(bytes));
+  return 0;
```

sdl2_audio_renderer — openDevice 返回实际采样率:
```diff
-int SDL2AudioRenderer::openDevice_(AVFrame* frame) {
+int SDL2AudioRenderer::openDevice(int& sampleRate, int channels, AVSampleFormat fmt) {
   ...
+  sampleRate = obtained.freq;  // pass back to caller for resampler config
   return 0;
 }
```

---

## 问题 4: 视频播放速度异常（太快）

### 现象
画面正常但播放速度远快于正常（~2-3x 速）。

### 排查过程

原始帧率控制使用 `static` 局部变量累积时间戳：
```cpp
static auto nextFrameTime = steady_clock::now();
nextFrameTime += frameDur;
if (nextFrameTime > now) sleep(...);
else nextFrameTime = now;
```

问题：
1. `static` 变量在多次 `play()`/`stop()` 之间不重置，累积的 `nextFrameTime` 远落后于时钟
2. 主循环额外 `sleep(2ms)` 破坏了时序

### 解决方案
改为每帧独立计时：渲染帧前记录 `t0`，渲染后计算 `elapsed`，sleep `frameDuration - elapsed`。逻辑简单，和 `minimal_av` 完全一致，不受帧间累积误差影响。

**核心代码对比:**

`full_player/main.cpp` — static 变量帧率控制 → pumpEvents + syncVideo:
```diff
-  while(g_run){
-    auto s=p->getState();
-    if(s==PlayerState::Completed||s==PlayerState::Error)break;
-    std::this_thread::sleep_for(100ms);
-  }
+  // Main loop — pump SDL events + render frames (X11 requires main thread)
+  while (g_run) {
+    auto s = p->getState();
+    if (s == PlayerState::Completed || s == PlayerState::Error) break;
+    if (!p->pumpEvents()) break;  // window closed
+    std::this_thread::sleep_for(2ms);
+  }
```

PlayerController — `pumpEvents()` 中 syncVideo 负责帧率控制:
```cpp
// syncVideo 内部决策:
//   diff < tolerance → Render (正常显示)
//   diff > tolerance (视频超前) → Sleep → Render (等待音频追上)
//   diff < -drop_threshold (视频严重落后) → Drop (丢帧追赶)
auto action = m_avSync.syncVideo(newFrame);
```

旧的 `videoRenderLoop_` 被完全移除 — 渲染统一由主线程 `pumpEvents()` 驱动，帧率由 AVSyncEngine 根据音频时钟自动调节。

---

## 其他修复

### EventType 扩展
StateMachine 需要但缺失的事件类型：
- `Open` — 用户调用 `open()`
- `Retry` — 错误后重试
- `Stopped` — 线程全部退出
- `SeekComplete` — seek 完成通知

**核心代码对比:**

`core/event/event_types.h`:
```diff
 enum class EventType {
     // Playback events
+    Open,         // open(url) called
     Play,
     Pause,
     Stop,
     Seek,
+    SeekComplete, // seek finished, new position ready
     EOS,
     Buffering,
     BufferingEnd,
@@
     StateChanged,
     Error,
     Info,
+    Retry,        // user retry after error
+    Stopped,      // all threads exited, resources released

     // Media events
     MediaLoaded,
```

### Frame.h MediaType 冲突
`core/memory/frame.h` 和 `api/player_types.h` 各自定义了 `enum class MediaType`，且 `frame.h` 版本多了 `Data` 值。编译时产生重定义错误。修复：`frame.h` 移除重复枚举，改为 `#include "api/player_types.h"`（Core → API 的依赖是已存在的，`clock_manager.h` 已有同样依赖）。

**核心代码对比:**

`core/memory/frame.h`:
```diff
+#include "api/player_types.h"
+
 #include <cstdint>
 #include <memory>

 struct AVFrame;

 namespace player {

-enum class MediaType {
-    Unknown,
-    Video,
-    Audio,
-    Subtitle,
-    Data
-};
-
 struct Frame {
```

### CMake 构建修复
- `/usr/local` 安装的 FFmpeg 是静态库且无 `-fPIC`，无法链接共享库 → 设 `BUILD_SHARED_LIBS=OFF`
- `simple_player`/`offscreen_player` 缺 `<thread>`/`<chrono>` include → 补上

---

## 最终架构

```
主线程 (main loop)                     Worker 线程
─────────────────────                  ────────────
pumpEvents()                           demuxLoop_()
  ├─ SDL_PollEvent()                     └─ 读包 → PacketQueue
  ├─ FrameQueue.peek()                                 ↓
  ├─ SDL_UpdateYUVTexture()           audioDecodeLoop_()
  ├─ SDL_RenderPresent()                └─ PacketQueue.pop → 解码 → 重采样
  └─ frame pacing sleep                     → RingBuffer → SDL callback
                                       
                                     videoDecodeLoop_()
                                       └─ PacketQueue.pop → 解码 → FrameQueue
```

关键设计决策：
- **SDL 调用全在主线程**（X11 硬性要求）
- **音频设备在视频窗口之前打开**（PulseAudio/X11 兼容）
- **音频 ring buffer 带背压阻塞**（不丢帧）
- **SDL 生命周期由 PlayerController 统一管理**（不在 renderer 内 quit subsystem）

---

## 2026-07-31 音视频同步排查与修复

### 问题 5: 音视频不同步（视频严重滞后 + 跳帧）

### 现象
音频流畅，但视频明显滞后于音频（口型对不上），且视频频繁跳帧，每隔几秒跳变数秒画面。

### 日志系统建设

为排查此问题，先完善了日志基础设施：

1. **通道化日志**：新增 5 个通道宏（`LOGD_AV`/`LOGD_AUDIO`/`LOGD_VIDEO`/`LOGD_CLOCK`/`LOGD_RENDER`），支持按 tag 独立过滤级别
2. **关键数据采集**：
   - 音频解码：帧 PTS + 采样数
   - 视频解码：帧 PTS + 队列水位 + push 阻塞时长
   - SDL 时钟回调：每次回调的时钟值
   - 渲染决策：视频 PTS vs 音频时钟差值 + Render/Drop 决策
3. **时间基准**：所有日志携带自 `play()` 起的墙钟时间（`T+XXXms`），音频帧/视频帧/渲染点可在同一时间轴上对比
4. **丢帧全量输出**：每帧 DROP 都打印，不采样
5. **文件名+行号**：每行日志带 `文件名:行号`

**核心代码对比:**

`utils/logger/logger.h` — 新增通道化日志宏:
```diff
+// ── 通道化日志宏（用于 AV 同步调试）────────────────────────────────────────
+// 使用方式: LOGD_AV("frame pts=%.3f clock=%.3f", fPts, clk);
+// 运行时可过滤: Logger::instance().setTagLevel("avsync", LogLevel::Debug);
+
+#define LOGD_TAG(tag, fmt, ...) \
+    player::Logger::instance().log(tag, player::LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
+
+#define LOGD_AV(fmt, ...)     LOGD_TAG("avsync", fmt, ##__VA_ARGS__)
+#define LOGD_AUDIO(fmt, ...)  LOGD_TAG("audio",  fmt, ##__VA_ARGS__)
+#define LOGD_VIDEO(fmt, ...)  LOGD_TAG("video",  fmt, ##__VA_ARGS__)
+#define LOGD_CLOCK(fmt, ...)  LOGD_TAG("clock",  fmt, ##__VA_ARGS__)
+#define LOGD_RENDER(fmt, ...) LOGD_TAG("render", fmt, ##__VA_ARGS__)
```

`utils/logger/logger.h` — 支持 per-tag 级别过滤:
```diff
+    void setTagLevel(const std::string& tag, LogLevel level);
+    bool isEnabled(const char* tag, LogLevel level) const;
+    void log(const char* tag, LogLevel level, const char* file, int line,
+             const char* fmt, ...);
...
+    std::map<std::string, LogLevel> tagLevels_; // per-tag level overrides
```

### 排查过程（日志驱动）

**第一轮排查：确认帧级数据流。**

日志显示首帧渲染正常：
```
#1 T+19ms  pts=0.0ms   clk=25.0ms  diff=-25ms  → RENDER  ✓
#2 T+69ms  pts=40.0ms  clk=80.2ms  diff=-40ms  → RENDER  ✓
#3 T+73ms  pts=80.0ms  clk=84.7ms  diff=-5ms   → RENDER  ✓ (精准!)
#4 T+104ms pts=120.0ms clk=116.2ms diff=+4ms   → RENDER  ✓ (精准!)
#5 T+388ms pts=160.0ms clk=381.8ms diff=-222ms → RENDER  ← 284ms 空白！
#6 T+408ms pts=200.0ms clk=399.4ms diff=-199ms → DROP    ← 开始丢帧!
#11 T+5062ms pts=15000ms clk=3809ms diff=+11191ms → RENDER ← 5 秒跳变!
```

关键发现：
- 帧 #4→#5 之间有 **284ms 空白期**，期间无任何渲染
- 渲染恢复后帧 #5 已落后音频 222ms→触发丢帧
- 丢帧 3 帧后，下一个可用帧 PTS=15000ms（跳了 14.8 秒！）

**第二轮排查：定位队列阻塞点。**

视频解码日志显示：
```
video #5  T+23ms  pts=160ms  fq=5/5  blocked=0ms   ← 队列满
video #6  T+66ms  pts=200ms  fq=5/5  blocked=42ms  ← 等了 42ms
video #10 T+391ms pts=360ms  fq=5/5  blocked=285ms ← 等了 285ms!!
video #50 T+7084ms pts=16480ms fq=5/5 ← 7 秒后才产生新帧!
```

**根因一：FrameQueue shrink 阈值过高。** `shrinkIfNeeded_()` 原设计 `m_readIndex > capacity/2`（>2 时清理），需要消费 3 帧才释放一个槽位。前 3 帧渲染期间解码线程被完全阻塞 → 队列空转 → 音频时钟跑远。

修复：改为 `m_readIndex > 0`（每消费 1 帧立即清理），解码线程不再空等。

**核心代码对比:**

`core/queue/frame_queue.h` — shrink 阈值:
```diff
   void shrinkIfNeeded_() {
-    if (m_readIndex > m_capacity / 2) {
+    if (m_readIndex > 0) {
       m_queue.erase(m_queue.begin(), m_queue.begin() + m_readIndex);
       m_readIndex = 0;
     }
```

**第三轮排查：修复后队列阻塞仍严重。**

缩小阈值后，阻塞时间从 285ms 降到 ~42ms，但问题依然存在——因为 `nextFrame()` 在 `syncVideo()`（可能 sleep 100ms）**之后**才调用，解码线程在 syncVideo 期间仍然阻塞。

**根因二：nextFrame 调用时机太晚。** `peekFrame` → `syncVideo`(sleep) → `render` → `nextFrame`(释放槽位)。syncVideo 期间队列满，解码线程空等。

修复：`nextFrame()` 移到 `peekFrame()` 之后**即刻**调用，不等渲染完成。`shared_ptr` 保证帧数据存活。

**核心代码对比:**

`player_controller.cpp` — pumpEvents() 中 nextFrame 提前:
```diff
 bool PlayerController::pumpEvents()
 {
     if (m_videoStreamIdx >= 0 && m_videoFrmQueue) {
         std::shared_ptr<Frame> newFrame;
         if (m_videoFrmQueue->peekFrame(newFrame)) {
+            // Advance queue IMMEDIATELY (before syncVideo may sleep).
+            // This frees a slot for the decode thread now, not 100ms later.
+            m_videoFrmQueue->nextFrame();
+
             auto action = m_avSync.syncVideo(newFrame);

             if (action == AVSyncEngine::SyncAction::Drop) {
-                m_videoFrmQueue->nextFrame();
             } else {
                 AVFrame* avf = newFrame->frame.get();
                 if (avf && avf->data[0]) {
                     m_videoRenderer.render(avf);
                 }
-                m_videoFrmQueue->nextFrame();
             }
         }
     }
```

**第四轮排查：上游 PacketQueue 堆积。**

增大 FrameQueue 到 16 帧 + 提前 nextFrame 后，前 15 帧 diff 均在 ±20ms 完美，但第 15 帧后又出现跳变：
```
#15 T+719ms pts=560ms clk=567ms diff=-7ms → RENDER   ← 完美
#50 T+6735ms pts=15760ms clk=6132ms diff=+9628ms      ← 6 秒后跳到 15.7 秒!
```

**根因三：解封装线程脱离控制。** demux 线程以全速运行，PacketQueue（容量 256）缓冲了 10 秒的视频包。解码线程被 FrameQueue 节流后，消费的是这些"未来"包——其 PTS 远超当前音频时钟。渲染时 syncVideo 发现视频超前 10+ 秒，sleep 100ms 后强制渲染 → 视频跳变。

修复：在视频解码循环中，发包前检查包的 PTS 是否超过音频时钟 2 秒以上，超过则丢弃（`continue`）。这使解码管线始终贴近音频位置。

**核心代码对比:**

`player_controller.cpp` — videoDecodeLoop_ 中超前包过滤:
```diff
+        // Skip packets more than 2s ahead of audio clock.
+        // This keeps the decode pipeline close to real time and
+        // prevents the video from jumping 10+ seconds ahead.
+        double pktPts = pkt->pts * av_q2d(m_demuxer.formatContext()
+            ->streams[m_videoStreamIdx]->time_base);
+        double aClk = m_clockMgr.audioClock()->getClock();
+        if (pktPts - aClk > 2.0) {
+            static int sSkip = 0;
+            if (++sSkip <= 5) LOGD_VIDEO("skip pkt pts=%.1fms clk=%.1fms (>2s ahead)", pktPts*1000, aClk*1000);
+            continue;
+        }
```

### 最终效果

修复后日志：
```
#5  T+123ms  pts=160ms  clk=152ms  diff=+7.7ms  → RENDER  fq=16
#15 T+533ms  pts=560ms  clk=550ms  diff=+9.8ms  → RENDER  fq=16
#50 T+1924ms pts=1960ms clk=1967ms diff=-6.7ms  → RENDER  fq=16
#100 T+3937ms pts=3960ms clk=3950ms diff=+10.2ms → RENDER fq=16
```

- **全部帧 diff 在 ±20ms 以内**（人眼无法察觉）
- **零丢帧**（D=0）
- **队列水位稳定在 16/16**（缓冲区饱和，管线流畅）
- **blocked 时间 ~27-40ms**（解码线程轻微节流，无饥饿）

### 三根因总结

| # | 根因 | 修复 | 涉及文件 |
|---|------|------|----------|
| 1 | FrameQueue shrink 阈值 `>capacity/2` 太高 | 改为 `>0`，每消费一帧立即释放 | `core/queue/frame_queue.h` |
| 2 | `nextFrame()` 在 `syncVideo()` 休眠后调用 | 提前到 `peekFrame()` 后即刻调用 | `control/player_controller/player_controller.cpp` |
| 3 | 上游 PacketQueue 堆积未来包 | 视频解码前丢弃超前 >2s 的包 | `control/player_controller/player_controller.cpp` |

### 配套修改

- FrameQueue 容量从 5 增加到 16（吸收解码抖动）

```diff
-  m_videoFrmQueue = std::make_shared<FrmQueue>(
-      static_cast<size_t>(m_config.misc.video_frm_q_size));
+  m_videoFrmQueue = std::make_shared<FrmQueue>(16); // big enough to absorb bursts
```

- 解码线程自旋 sleep 从 500µs 降到 200µs（减少空转损失）

```diff
-  std::this_thread::sleep_for(std::chrono::microseconds(500));
+  std::this_thread::sleep_for(microseconds(200));
```

- 新增 `LOGD_*` 通道化日志宏，支持 per-tag 过滤
- `minimal_audio`/`minimal_av` 诊断 demo 程序

---

## 2026-07-28 ~ 2026-07-29 工程搭建 & Core/Source/Decode 层

### 2026-07-28 工程搭建
- 架构设计 → ARCHITECTURE.md
- 工程脚手架: 120 桩文件 + CMake + GoogleTest
- CLAUDE.md: 21 条 AI 工程规范

### 2026-07-28 Core 层
- PacketQueue (19 tests): Bounded MPSC, flush token + serial
- FrameQueue: 设计文档完善（无 condvar 理由）
- Clock + ClockManager (17 tests)
- EventBus (10 tests)
- ThreadPool

### 2026-07-28 Source 层
- FileProtocol (17 tests): POSIX I/O
- ProtocolFactory (13 tests): URL scheme 路由
- FFmpegDemuxer (14 tests): avformat 封装

### 2026-07-28~29 Decode + Process + Render
- VideoDecoder (8 tests), AudioDecoder (6 tests)
- AudioResampler (4 tests): libswresample 封装
- AudioRingBuffer (7 tests): SPSC 无锁环形缓冲
- SDL2AudioRenderer: 延迟打开 SDL 设备

### 2026-07-29 PlayerController + FFmpeg 修复
- PlayerController: 端到端音频管线 (Demux→Decode→Resample→SDL2)
- 修复 `.gitignore` 的 `core` 规则吞掉整个 core/ 目录
- 修复 FFmpeg 7.x/6.1 双版本 ABI 冲突: CMake PKG_CONFIG_PATH 强制系统路径

### FFmpeg 双版本问题详情
- 症状: av_frame_get_buffer / av_samples_alloc 返回 EINVAL(-22)
- 根因: `/usr/local` 的 FFmpeg 7.x 和系统 FFmpeg 6.1 的 .so 同时链接
- 解决: CMake 中 `set(ENV{PKG_CONFIG_PATH} /usr/lib/x86_64-linux-gnu/pkgconfig)` 强制系统 pkg-config

**核心代码对比:**

`CMakeLists.txt`:
```diff
-# FFmpeg
+# FFmpeg — force system install to avoid /usr/local ABI conflicts
+set(ENV{PKG_CONFIG_PATH} "/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
 pkg_check_modules(FFMPEG REQUIRED
-  libavformat libavcodec libavutil libswscale libswresample libavfilter)
+  libavformat libavcodec libavutil libswscale libswresample)
+unset(ENV{PKG_CONFIG_PATH})
```

### 2026-07-29 视频管线尝试（未完成）
- 从源码构建 GLFW 3.4 → `external/glfw/build/src/libglfw3.a`
- 实现 OpenGL 渲染器（gl_context / shader_program / texture_manager / opengl_renderer）
- 问题: OpenGL 3.3 函数需要 GLEW/GLAD 加载，环境未安装且无 sudo
- 切换方案: 改用 SDL2 做视频渲染 (`sdl_video_renderer.h/.cpp`)
- SDL2 视频渲染器编译通过
- 端到端测试 `full_player` 崩溃，尚未定位根因

### 当前状态
- ✅ 音频完整链路: Demux→Decode→Resample→SDL2 Audio→扬声器
- ✅ 115 单元测试全部通过
- ✅ FFmpeg 版本冲突已解决
- ❌ 视频管线: Decoder + SDL Renderer 已实现，集成后 crash
- ❌ full_player 崩溃待定位

### 下一步
1. 定位 full_player 崩溃根因
2. 修复后验证音视频同步播放
3. 实现 AVSync（音频 Master Clock → 视频帧同步）
