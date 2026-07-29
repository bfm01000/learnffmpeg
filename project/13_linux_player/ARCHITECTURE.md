# Linux 高性能播放器 SDK — 架构设计文档

> **设计目标**：现代 C++17、高性能、可扩展、Linux 原生
> **当前支持**：MP4 / FLV / MKV / RTSP / RTMP / HTTP / HTTPS
> **后续扩展**：WebRTC / HLS / SRT

---

## 1. 整体架构（分层设计）

```
┌─────────────────────────────────────────────────────────────┐
│                      PUBLIC API LAYER                        │
│  Player::create(...)  |  Player::play()  |  Player::seek()  │
├─────────────────────────────────────────────────────────────┤
│                     CONTROL LAYER                            │
│  PlayerController  │  StateMachine  │  PlaylistManager      │
│  AVSyncEngine      │  ClockManager  │  EventDispatcher      │
├──────────┬──────────┬──────────────┬────────────────────────┤
│  SOURCE  │  DECODE  │   PROCESS    │       RENDER           │
│  LAYER   │  LAYER   │   LAYER      │       LAYER            │
│          │          │              │                        │
│ Demuxer  │ Video    │ FilterGraph  │ OpenGLVideoRenderer    │
│ Protocol │ Decoder  │ (postproc)   │ SDL2AudioRenderer      │
│ Handlers │ Audio    │ Resampler    │ SubtitleRenderer       │
│          │ Decoder  │ ColorConv    │                        │
├──────────┴──────────┴──────────────┴────────────────────────┤
│                     INFRASTRUCTURE LAYER                     │
│  PacketQueue  │  FrameQueue  │  ThreadPool  │  MemoryPool   │
│  Logger       │  ConfigMgr   │  PluginMgr   │  Timer        │
├─────────────────────────────────────────────────────────────┤
│                  EXTERNAL DEPENDENCIES                       │
│  FFmpeg (libavformat/codec/swscale/avutil)                  │
│  SDL2  (audio / window / event)                             │
│  OpenGL / GLFW  (render window)                             │
│  libcurl / openssl  (network for WebRTC/SRT future)         │
│  VAAPI / VDPAU  (HW decode)                                 │
└─────────────────────────────────────────────────────────────┘
```

**设计原则：**

| 原则 | 说明 |
|------|------|
| **分层解耦** | 每层只依赖下层接口，上层通过抽象接口调用下层 |
| **插件化** | Source、Renderer、Protocol 均可动态注册/替换 |
| **零拷贝路径** | Demux → Decode → Render 路径上仅传递指针/引用，GPU 纹理直通 |
| **背压传递** | 队列满时自动阻塞上游，防止内存爆炸 |
| **故障隔离** | 单模块崩溃不影响整体，可热重启解码器/渲染器 |

---

## 2. 模块划分

### 2.1 模块总览

```
player_sdk/
├── core/                    # 核心基础设施
│   ├── queue/               # 无锁/有锁队列
│   ├── clock/               # 时钟系统
│   ├── event/               # 事件总线
│   ├── thread/              # 线程池 & 线程工具
│   ├── memory/              # 内存池 & 智能指针工具
│   └── plugin/              # 插件管理器
├── source/                  # 数据源层
│   ├── protocol/            # 协议处理器（可插拔）
│   │   ├── file_protocol    # 本地文件
│   │   ├── http_protocol    # HTTP/HTTPS
│   │   ├── rtmp_protocol    # RTMP
│   │   ├── rtsp_protocol    # RTSP
│   │   └── (webrtc/srt/hls) # 后续扩展
│   └── demuxer/             # 解封装
│       └── ffmpeg_demuxer   # 基于 FFmpeg 的统一解封装
├── decode/                  # 解码层
│   ├── video_decoder        # 视频解码（软解 + VAAPI/VDPAU 硬解）
│   ├── audio_decoder        # 音频解码
│   └── subtitle_decoder     # 字幕解码
├── process/                 # 处理层
│   ├── filter_graph         # FFmpeg filter 集成
│   ├── resampler            # 音频重采样
│   └── color_converter      # 像素格式转换（swscale + shader）
├── render/                  # 渲染层
│   ├── video/
│   │   └── opengl_renderer  # OpenGL 视频渲染
│   ├── audio/
│   │   └── sdl2_renderer    # SDL2 音频输出
│   └── subtitle/
│       └── overlay_renderer # 字幕叠加
├── control/                 # 控制层
│   ├── player_controller    # 播放器主控制器
│   ├── state_machine        # 播放器状态机
│   ├── av_sync_engine       # 音视频同步引擎
│   ├── seek_handler         # Seek 逻辑
│   └── playlist_manager     # 播放列表管理
├── api/                     # 公开 API
│   ├── player.h             # 主接口
│   ├── player_config.h      # 配置结构
│   └── player_callback.h    # 回调接口
└── utils/                   # 工具
    ├── logger               # 日志
    ├── config               # 配置管理
    └── perf_probe           # 性能探针
```

### 2.2 各模块职责

| 模块 | 职责 | 关键约束 |
|------|------|----------|
| **core/queue** | 提供有界无锁 MPSC/SPSC 队列 | 单生产者单消费者场景零锁竞争 |
| **core/clock** | 三套时钟：System / Audio / External | Audio clock 为主时钟源，单调递增 |
| **core/event** | 观察者模式事件总线，支持异步回调 | 回调中不得执行耗时操作 |
| **source/protocol** | 统一 `IProtocolHandler` 接口适配不同协议 | 支持断线重连、超时控制 |
| **source/demuxer** | 解封装为 `AVPacket*` 流 | 每个流独立 packet queue |
| **decode** | 发送 packet 到解码器，产出 `AVFrame*` | 支持 flush、硬件加速、错误恢复 |
| **process** | 后处理链：色彩转换、缩放、音频重采样、Filter | 可在 shader 或 swscale 之间切换 |
| **render** | 消费 `VideoFrame` / `AudioFrame`，写入 SDL2/OpenGL | 渲染线程就是垂直同步的锚点 |
| **control** | 状态机、同步、Seek、播放列表 | 所有外部 API 的入口 |

---

## 3. 线程模型

```
┌─────────────────────────────────────────────────────────────────┐
│                        THREAD LAYOUT                            │
│                                                                 │
│  ┌──────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │  MAIN/API    │  │   EVENT          │  │   WATCHDOG       │  │
│  │  Thread      │  │   Thread         │  │   Thread         │  │
│  │              │  │                  │  │                  │  │
│  │ - API calls  │  │ - Callback       │  │ - Health check   │  │
│  │ - State      │  │   dispatch       │  │ - Timeout        │  │
│  │   machine    │  │ - User events    │  │ - Reconnect      │  │
│  └──────┬───────┘  └────────┬─────────┘  └────────┬─────────┘  │
│         │                   │                      │            │
│  ┌──────┴───────────────────┴──────────────────────┴──────────┐│
│  │               MESSAGE / COMMAND BUS                         ││
│  └──┬──────────┬──────────┬──────────┬──────────┬─────────────┘│
│     │          │          │          │          │               │
│  ┌──┴────┐ ┌──┴────┐ ┌──┴────┐ ┌──┴────┐ ┌──┴─────┐           │
│  │ DEMUX │ │VIDEO  │ │AUDIO  │ │VIDEO  │ │AUDIO   │           │
│  │Thread │ │DECODE │ │DECODE │ │RENDER │ │RENDER  │           │
│  │       │ │Thread │ │Thread │ │Thread │ │Thread  │           │
│  │       │ │       │ │       │ │       │ │(SDL2   │           │
│  │Read   │ │avcodec│ │avcodec│ │OpenGL │ │callback│           │
│  │Packet │ │decode │ │decode │ │render │ │thread) │           │
│  └──┬────┘ └──┬────┘ └──┬────┘ └──┬────┘ └──┬─────┘           │
│     │         │         │         │         │                   │
│     ▼         ▼         ▼         ▼         ▼                   │
│  ┌──────────────────────────────────────────────────┐          │
│  │              Frame Reference Pool                │          │
│  │  (shared_ptr<Frame> with custom deleter)         │          │
│  └──────────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────────┘
```

### 3.1 线程详细说明

```
                     ┌──────────────┐
                     │  Main Thread │  用户 API 调用
                     │  (API/UI)    │  状态机推进
                     └──────┬───────┘
                            │ Command (lock-free SPSC)
          ┌─────────────────┼─────────────────┐
          ▼                 ▼                  ▼
   ┌─────────────┐  ┌─────────────┐   ┌──────────────┐
   │ Demux       │  │ Controller  │   │ Event        │
   │ Thread      │  │ (embedded   │   │ Thread       │
   │             │  │  in Main)   │   │              │
   │ av_read_    │  │             │   │ 用户回调分发  │
   │ frame()     │  │ Seek/Pause  │   │ 进度/错误/   │
   │  ─────────► │  │ /Stop/      │   │ 状态通知     │
   │ PacketQueue │  │ Volume...   │   │              │
   │ [Video]     │  │             │   │ 推送给 App   │
   │ [Audio]     │  │             │   │              │
   │ [Subtitle]  │  │             │   │              │
   └──────┬──────┘  └─────────────┘   └──────────────┘
          │
    ┌─────┴──────────┐
    ▼                ▼
┌──────────┐  ┌──────────┐
│ Video    │  │ Audio    │
│ Decode   │  │ Decode   │
│ Thread   │  │ Thread   │
│          │  │          │
│ send ──► │  │ send ──► │
│ recv     │  │ recv     │
│  ──────► │  │  ──────► │
│FrameQueue│  │FrameQueue│
│ [Video]  │  │ [Audio]  │
└────┬─────┘  └────┬─────┘
     │             │
     ▼             ▼
┌──────────┐  ┌──────────┐
│ Video    │  │ Audio    │
│ Render   │  │ Render   │
│ Thread   │  │ Thread   │
│          │  │          │
│ VSync    │  │ SDL2     │
│ PTS wait │  │ Callback │
│ Upload   │  │ Resample │
│ Shader   │  │ Mix ────►│
│ Swap ───►│  │ Speaker  │
│ Display  │  │          │
│          │  │ ⚠ Audio  │
│          │  │ ⚠ Clock  │
│          │  │ ⚠ Master │
└──────────┘  └──────────┘
     ▲             │
     │   AV Sync   │
     └─────┬───────┘
           │
   clock->getMasterTime()
```

### 3.2 线程数量

| 场景 | 线程数 | 说明 |
|------|--------|------|
| 纯音频播放 | 4 | Main + Demux + AudioDecode + AudioRender(SDL2) |
| 纯视频播放 | 4 | Main + Demux + VideoDecode + VideoRender(VSync) |
| 音视频播放 | 5 | Main + Demux + VideoDecode + AudioDecode + VideoRender + AudioRender |
| +字幕 | 6 | 增加 SubtitleDecode，SubtitleRender 复用 VideoRender |
| +Event | +1 | Event 线程始终存在 |
| +Watchdog | +1 | 可选，网络流推荐 |

### 3.3 线程间通信

```
Demux ──[PacketQueue(Video)]──► VideoDecoder ──[FrameQueue(Video)]──► VideoRenderer
   │                                                                      │
   └────[PacketQueue(Audio)]──► AudioDecoder ──[FrameQueue(Audio)]──► AudioRenderer
                                                                          │
                                                                   ┌──────┘
                                                                   ▼
                                                            ClockManager
                                                           (audio_clock)
                                                                   │
                                                                   ▼
                                                            VideoRenderer
                                                           (PTS vs clock)
```

**队列类型：**

| 队列 | 类型 | 容量 | 特点 |
|------|------|------|------|
| `PacketQueue` | Bounded MPSC | Video: 256 pkts, Audio: 512 pkts | 存 `AVPacket`，带 flush_token |
| `FrameQueue` | Bounded SPSC | Video: 3-5 frames, Audio: 8-16 frames | 存 `shared_ptr<VideoFrame>` |
| `CommandQueue` | Lock-free MPSC | 无界 | 控制命令：Seek/Pause/Stop/SetVolume |

---

## 4. 类图

```
┌─────────────────────────────────────────────────────────────────────┐
│                          PUBLIC INTERFACE                           │
│  «interface»                                                        │
│  ┌──────────────────────────┐                                       │
│  │       IPlayer             │                                       │
│  ├──────────────────────────┤                                       │
│  │ + open(url) → Result      │                                       │
│  │ + play()                  │                                       │
│  │ + pause()                 │                                       │
│  │ + seek(pos)               │                                       │
│  │ + stop()                  │                                       │
│  │ + setVolume(v)            │                                       │
│  │ + setLoop(b)              │                                       │
│  │ + getState() → State      │                                       │
│  │ + getPosition() → int64   │                                       │
│  │ + getDuration() → int64   │                                       │
│  │ + setCallback(IPlayerCB*) │                                       │
│  └──────────┬───────────────┘                                       │
│             │                                                       │
│             ▼                                                       │
│  ┌──────────────────────────┐                                       │
│  │        Player            │  «facade»                             │
│  ├──────────────────────────┤                                       │
│  │ - controller_            │                                       │
│  │ - config_                │                                       │
│  │ - event_bus_             │                                       │
│  │ - plugin_mgr_            │                                       │
│  └──┬──────────┬───────────┘                                       │
└─────┼──────────┼───────────────────────────────────────────────────┘
      │          │
      ▼          ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          CONTROL LAYER                               │
│  ┌────────────────────┐    ┌────────────────────┐                    │
│  │  PlayerController   │    │   StateMachine     │                    │
│  ├────────────────────┤    ├────────────────────┤                    │
│  │ - pipeline_        │    │ - state_: State    │                    │
│  │ - clock_mgr_       │    │ - transitions_     │                    │
│  │ - av_sync_         │    │ - guards_          │                    │
│  ├────────────────────┤    ├────────────────────┤                    │
│  │ + startPipeline()  │    │ + transit(Event)   │                    │
│  │ + stopPipeline()   │    │ + canTransit(E)→bool│                   │
│  │ + handleSeek()     │    │ + getState()→State │                    │
│  │ + handleCommand()  │    └────────────────────┘                    │
│  └──┬────────┬────────┘                                             │
│     │        │                                                       │
│     ▼        ▼                                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                │
│  │ AVSyncEngine │  │ ClockManager │  │ EventBus     │                │
│  ├──────────────┤  ├──────────────┤  ├──────────────┤                │
│  │ - master_:   │  │ - audio_clock│  │ - listeners_ │                │
│  │   AudioClock │  │ - system_clock│  │ - queue_    │                │
│  │ - diff_thr_  │  │ - ext_clock  │  ├──────────────┤                │
│  ├──────────────┤  ├──────────────┤  │ + subscribe()│                │
│  │ + syncVideo()│  │ + getMaster()│  │ + emit()     │                │
│  │ + syncAudio()│  │ + setMaster()│  │ + dispatch() │                │
│  │ + calcDelay()│  │ + update()   │  └──────────────┘                │
│  └──────────────┘  └──────────────┘                                  │
└─────────────────────────────────────────────────────────────────────┘
                                     │
          ┌──────────────────────────┼──────────────────────────┐
          ▼                          ▼                          ▼
┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐
│   SOURCE LAYER   │   │   DECODE LAYER   │   │   RENDER LAYER   │
├──────────────────┤   ├──────────────────┤   ├──────────────────┤
│                  │   │                  │   │                  │
│ «interface»      │   │ «interface»      │   │ «interface»      │
│ ┌──────────────┐ │   │ ┌──────────────┐ │   │ ┌──────────────┐ │
│ │IMediaSource  │ │   │ │  IDecoder    │ │   │ │ IRenderer    │ │
│ ├──────────────┤ │   │ ├──────────────┤ │   │ ├──────────────┤ │
│ │+open()       │ │   │ │+open()       │ │   │ │+init()       │ │
│ │+readPacket() │ │   │ │+sendPacket() │ │   │ │+render()     │ │
│ │+seekTo()     │ │   │ │+recvFrame()  │ │   │ │+resize()     │ │
│ │+getStreamInfo│ │   │ │+flush()      │ │   │ │+destroy()    │ │
│ │+close()      │ │   │ │+close()      │ │   │ └──────────────┘ │
│ └──────┬───────┘ │   │ └──────┬───────┘ │   │        △         │
│        △         │   │        △         │   │   ┌────┴────┐    │
│ ┌──────┴───────┐ │   │ ┌──────┴───────┐ │   │ ┌─┴───┐ ┌──┴──┐ │
│ │FFmpegSource  │ │   │ │VideoDecoder  │ │   │ │Open │ │SDL2 │ │
│ ├──────────────┤ │   │ ├──────────────┤ │   │ │GL   │ │Audio│ │
│ │- fmt_ctx_    │ │   │ │- codec_ctx_  │ │   │ │Ren- │ │Ren- │ │
│ │- protocol_   │ │   │ │- hw_ctx_     │ │   │ │derer│ │derer│ │
│ │- streams_[]  │ │   │ │- frame_q_*   │ │   │ └─────┘ └─────┘ │
│ └──────┬───────┘ │   │ └──────────────┘ │   │                  │
│        │         │   │                  │   └──────────────────┘
│   ┌────┴────┐    │   │ ┌──────────────┐ │
│   │Protocol │    │   │ │AudioDecoder  │ │
│   │Handlers │    │   │ ├──────────────┤ │
│   ├─────────┤    │   │ │- swr_ctx_    │ │
│   │File     │    │   │ │- frame_q_*   │ │
│   │HTTP     │    │   │ └──────────────┘ │
│   │RTMP     │    │   │                  │
│   │RTSP     │    │   └──────────────────┘
│   │WebRTC ● │    │
│   │SRT    ● │    │   ● = planned / future
│   │HLS    ● │    │
│   └─────────┘    │
└──────────────────┘
```

### 核心类详细设计

```
┌─────────────────────────────────────────┐
│              Frame                      │  引用计数帧对象
├─────────────────────────────────────────┤
│ - avframe: AVFrame*                     │
│ - pts: int64_t                          │
│ - duration: int64_t                     │
│ - type: MediaType (Video/Audio/Sub)     │
│ - ref_count: atomic<int>                │
├─────────────────────────────────────────┤
│ + retain(): void                        │
│ + release(): void                       │
│ + getData(): uint8_t**                  │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│          PacketQueue<T>                 │  Bounded MPSC
├─────────────────────────────────────────┤
│ - buffer_: ring_buffer<T>               │
│ - capacity_: size_t                     │
│ - write_pos_: atomic<size_t>            │
│ - read_pos_: atomic<size_t>             │
│ - not_empty_: condition_variable        │
│ - not_full_: condition_variable         │
├─────────────────────────────────────────┤
│ + push(T): bool (block/timeout)         │
│ + pop(): optional<T> (block/timeout)    │
│ + flush(): void                         │
│ + size(): size_t                        │
│ + clear(): void                         │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│            FrameQueue                   │  Bounded SPSC
├─────────────────────────────────────────┤
│ - frames_: array<shared_ptr<Frame>, N>  │
│ - write_idx_: atomic<size_t>            │
│ - read_idx_: size_t                     │
│ - rindex_shown_: int (for frame-step)   │
├─────────────────────────────────────────┤
│ + pushFrame(shared_ptr<Frame>): bool    │
│ + peekFrame(): shared_ptr<Frame>        │
│ + nextFrame(): shared_ptr<Frame>        │
│ + prevFrame(): shared_ptr<Frame>        │
│ + numRemaining(): int                   │
│ + flush(): void                         │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│              Clock                       │
├─────────────────────────────────────────┤
│ - pts_: atomic<int64_t>                 │
│ - pts_drift_: atomic<int64_t>           │
│ - last_updated_: atomic<int64_t>        │
│ - paused_: atomic<bool>                 │
│ - speed_: atomic<double>                │
│ - serial_: atomic<int>                  │
├─────────────────────────────────────────┤
│ + setClock(pts): void                   │
│ + getClock(): int64_t                   │
│ + setSpeed(speed): void                 │
│ + setPaused(bool): void                 │
│ + getSpeed(): double                    │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│            PluginManager                 │  插件注册/发现
├─────────────────────────────────────────┤
│ - protocol_factories_: map<string, ...> │
│ - renderer_factories_: map<string, ...> │
│ - decoder_factories_: map<string, ...>  │
├─────────────────────────────────────────┤
│ + registerProtocol(name, factory)       │
│ + createProtocol(name) → IProtocol*     │
│ + registerRenderer(name, factory)       │
│ + createRenderer(name) → IRenderer*     │
│ + loadPlugin(so_path): bool             │
│ + unloadPlugin(name): bool              │
└─────────────────────────────────────────┘
```

---

## 5. 状态机

### 5.1 状态定义

```
                    ┌─────────────┐
                    │    IDLE     │  初始状态 / 已停止
                    └──────┬──────┘
                           │ open(url)
                           ▼
                    ┌─────────────┐
              ┌────►│  LOADING    │  解析媒体 / 建立连接
              │     └──────┬──────┘
              │            │ load success
              │            ▼
              │     ┌─────────────┐
              │     │   READY     │  首帧已解码 / 暂停等待播放
              │     └──┬──────┬───┘
              │        │      │
              │    play│      │ seek
              │        ▼      ▼
              │     ┌─────────────┐
              │     │  PLAYING    │◄──────────┐
              │     └──┬──┬──┬───┘           │
              │        │  │  │               │
              │   pause│  │  │ buffer full    │
              │        ▼  │  │ (underrun     │
              │  ┌────────┐ │  │  resolved)   │
              │  │ PAUSED │ │  │              │
              │  └───┬────┘ │  ▼              │
              │      │      │ ┌──────────┐    │
              │      │      │ │BUFFERING │────┘
              │      │      │ └────┬─────┘
              │      │      │      │ timeout
              │      │      │      ▼
              │      │      │ ┌──────────┐
              │      │      │ │  ERROR   │──┐
              │      │      │ └────┬─────┘  │
              │      │      │      │        │
              │      │      │      │ EOS    │
              │      │      │      ▼        │
              │      │      │ ┌────────────┐│
              │      │      │ │ COMPLETED  ││
              │      │      │ └─────┬──────┘│
              │      │      │       │       │
              │      │      │ stop  │       │
              │      │      ▼       ▼       │
              │      │   ┌─────────────┐    │
              │      │   │  STOPPING   │◄───┘
              │      │   └──────┬──────┘
              │      │          │ stopped
              │      │          ▼
              │      │   ┌─────────────┐
              └──────┴───│    IDLE     │
                    seek │   STOPPED   │
                    done └─────────────┘
              ←  seek 完成回到当前播放状态  →
```

### 5.2 状态转移表

| 当前状态 | 事件 | 下一状态 | 动作 |
|----------|------|----------|------|
| IDLE | `OPEN` | LOADING | 创建 Demuxer、Decoder、Renderer；启动网络连接 |
| LOADING | `LOADED` | READY | 获取流信息；解码首帧；通知 App `OnPrepared` |
| LOADING | `LOAD_FAILED` | ERROR | 清理资源；通知 `OnError`；可选自动回 IDLE |
| READY | `PLAY` | PLAYING | 启动 Decode/Render 线程；启动 Clock |
| READY | `SEEK` | 对自身 | 执行 Seek→保持 READY，SEEK_COMPLETE 后通知 |
| PLAYING | `PAUSE` | PAUSED | 暂停 Clock；暂停 Audio 输出；保留 Video 最后一帧 |
| PLAYING | `BUFFER_UNDERRUN` | BUFFERING | 暂停 Clock；保留最后一帧；等待队列恢复 |
| PLAYING | `EOS` | COMPLETED | 停止管线；通知 `OnCompletion` |
| PLAYING | `SEEK` | PLAYING | Flush 队列；Demuxer seek；Decoder flush；等首帧 |
| PLAYING | `ERROR_FATAL` | ERROR | 停止管线；通知 `OnError` |
| PAUSED | `PLAY` | PLAYING | 恢复 Clock；恢复 Audio；通知 `OnResume` |
| PAUSED | `SEEK` | PAUSED | Flush 队列；Seek；解码目标帧；保留 PAUSED |
| PAUSED | `STOP` | STOPPING | 停止线程；释放资源 |
| BUFFERING | `BUFFER_FULL` | PLAYING | 恢复 Clock/播放 |
| BUFFERING | `BUFFER_TIMEOUT` | ERROR | 停止管线；通知 `OnError` |
| BUFFERING | `STOP` | STOPPING | 停止线程；释放资源 |
| SEEKING ● | `SEEK_COMPLETE` | PLAYING/PAUSED | 恢复到 Seek 前的播放状态 |
| SEEKING ● | `SEEK_FAILED` | 前一状态 | 通知 `OnSeekFailed`；恢复前一状态 |
| COMPLETED | `PLAY` | PLAYING | （Loop 模式）重新播放 / （单次）从 READY 播放 |
| COMPLETED | `STOP` | STOPPING | 停止线程；释放资源 |
| ERROR | `RETRY` | LOADING | 用户触发重试（仅特定错误类型允许） |
| ERROR | `STOP` | STOPPING | 停止并清理 |
| STOPPING | `STOPPED` | IDLE | 所有线程已退出；资源已释放；通知 `OnStopped` |
| 任意状态 | `ERROR_ASYNC` | ERROR | 异步错误（如网络断开、OOM）强制转 ERROR |

> ● `SEEKING` 不是独立顶层状态——它是 PLAYING/PAUSED 的子状态，用 `is_seeking_` 标记管理。

---

## 6. 模块依赖

### 6.1 依赖图

```
                        ┌─────────────┐
                        │    API      │  (Player.h / PlayerConfig.h)
                        └──────┬──────┘
                               │ 依赖
                               ▼
                        ┌─────────────┐
                        │   Control   │  PlayerController, StateMachine
                        │   Layer     │  AVSyncEngine, ClockManager
                        └──┬───┬───┬──┘
                           │   │   │
              ┌────────────┼───┼───┼────────────────┐
              │            │   │   │                │
              ▼            ▼   │   ▼                ▼
        ┌──────────┐ ┌────────┐│┌─────────┐   ┌──────────┐
        │  Source  │ │Decode  │││ Process │   │  Render  │
        │  Layer   │ │Layer   │││ Layer   │   │  Layer   │
        └─────┬────┘ └───┬────┘│└────┬─────┘   └────┬─────┘
              │          │     │     │               │
              └──────────┼─────┼─────┼───────────────┘
                         │     │     │
                         ▼     ▼     ▼
                    ┌─────────────────────┐
                    │  Infrastructure     │
                    │  Queue / Event /    │
                    │  Thread / Memory    │
                    └──────────┬──────────┘
                               │ 依赖
                               ▼
                    ┌─────────────────────┐
                    │  External Libs      │
                    │  FFmpeg / SDL2 /    │
                    │  OpenGL / GLFW      │
                    └─────────────────────┘
```

### 6.2 依赖规则

```
规则 1: 上层可依赖下层，下层绝不可依赖上层
规则 2: 同层模块通过接口（抽象类）交互，不直接依赖具体实现
规则 3: Infrastructure 层无外部依赖（除 C++17 标准库）
规则 4: Source / Decode / Render 之间通过 Queue 解耦，互不感知
规则 5: Control 层通过接口持有 Source/Decode/Render 引用——依赖倒置
```

### 6.3 编译依赖矩阵

| | API | Control | Source | Decode | Process | Render | Infra | FFmpeg | SDL2 | OpenGL |
|--|-----|---------|--------|--------|---------|--------|-------|--------|------|--------|
| **API** | - | ✓ | - | - | - | - | - | - | - | - |
| **Control** | - | - | ✓(I) | ✓(I) | - | ✓(I) | ✓ | - | - | - |
| **Source** | - | - | - | - | - | - | ✓ | ✓ | - | - |
| **Decode** | - | - | - | - | - | - | ✓ | ✓ | - | - |
| **Process** | - | - | - | - | - | - | ✓ | ✓ | - | - |
| **Render** | - | - | - | - | - | - | ✓ | - | ✓ | ✓ |
| **Infra** | - | - | - | - | - | - | - | - | - | - |

> `(I)` 表示仅依赖接口/抽象类

---

## 7. 数据流

### 7.1 播放数据流（主路径）

```
                            SOURCE                          DECODE
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                                                                             │
 │  ┌──────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────────┐       │
 │  │ Network  │───▶│ Protocol │───▶│  AVIOContext  │───▶│ AVFormatCtx  │       │
 │  │ Socket   │    │ Handler  │    │  (buffered)   │    │ (demuxer)    │       │
 │  └──────────┘    └──────────┘    └──────────────┘    └──────┬───────┘       │
 │                                                            │               │
 │                                                            │ av_read_frame │
 │                                                            ▼               │
 │                                        ┌──────────────────────────────────┐ │
 │                                        │         AVPacket                 │ │
 │                                        │ stream_index → route to:         │ │
 │                                        │  [0] → VideoPacketQueue          │ │
 │                                        │  [1] → AudioPacketQueue          │ │
 │                                        │  [2] → SubtitlePacketQueue       │ │
 │                                        └────────┬──────────┬──────────────┘ │
 │                                                 │          │                │
 └─────────────────────────────────────────────────┼──────────┼────────────────┘
                                                   │          │
                        ┌──────────────────────────┘          └──────────────────┐
                        ▼                                                         ▼
            ┌──────────────────────┐                              ┌──────────────────────┐
            │  PacketQueue(Video)  │                              │  PacketQueue(Audio)  │
            │  (Bounded MPSC)      │                              │  (Bounded MPSC)      │
            └──────────┬───────────┘                              └──────────┬───────────┘
                       │                                                     │
                       ▼                                                     ▼
            ┌──────────────────────┐                              ┌──────────────────────┐
            │   VideoDecoder       │                              │   AudioDecoder       │
            │   avcodec_send_pkt() │                              │   avcodec_send_pkt() │
            │   avcodec_recv_frm() │                              │   avcodec_recv_frm() │
            │   ─────────────────► │                              │   ─────────────────► │
            │   HW Accel:          │                              │   PCM / AAC / OPUS   │
            │   VAAPI/VDPAU/CUDA   │                              │   float planar       │
            └──────────┬───────────┘                              └──────────┬───────────┘
                       │                                                     │
                       ▼                                                     ▼
            ┌──────────────────────┐                              ┌──────────────────────┐
            │  FrameQueue(Video)   │   PROCESS (optional)         │  FrameQueue(Audio)   │
            │  (Bounded SPSC, 3-5) │                              │  (Bounded SPSC, 8-16)│
            │                      │                              │                      │
            │  YUV420P / NV12 / P010│                             │  FLTP / S16P         │
            └──────────┬───────────┘                              └──────────┬───────────┘
                       │                                                     │
                       │                         ┌───────────────────────────┘
                       ▼                         ▼
            ┌──────────────────────┐  ┌──────────────────────┐
            │  Process Pipeline    │  │  AudioResampler      │
            │  (if needed)         │  │  libswresample       │
            │  swscale / shader    │  │  fmt → S16 interleave│
            │  YUV→RGB / resize    │  └──────────┬───────────┘
            └──────────┬───────────┘             │
                       │                         ▼
                       ▼              ┌──────────────────────┐
            ┌──────────────────────┐ │  AudioRingBuffer      │
            │ VideoRenderThread    │ │  (circular, lock-free)│
            │                      │ └──────────┬───────────┘
            │ 1. Peek frame        │            │
            │ 2. Calc delay        │            ▼
            │    (PTS - clock)     │ ┌──────────────────────┐
            │ 3. Wait until due    │ │  SDL2 Audio Callback │
            │ 4. Upload texture    │ │  (SDL thread)        │
            │ 5. Shader render     │ │                      │
            │ 6. Swap buffers      │ │ 1. Read ring buffer  │
            │ 7. Release frame     │ │ 2. Mix to SDL buf    │
            └──────────┬───────────┘ │ 3. Update AudioClock │
                       │             │ 4. Notify AVDiff     │
                       │             └──────────────────────┘
                       ▼
                  ┌─────────┐
                  │ Display │
                  └─────────┘
```

### 7.2 Seek 数据流

```
 User calls seek(position_ms)
         │
         ▼
 ┌───────────────────┐
 │ PlayerController  │
 │ handleSeek(pos)   │
 └────────┬──────────┘
          │
          ▼
 ┌───────────────────┐
 │ StateMachine      │
 │ transit(SEEK)     │
 │ → is_seeking=true │
 └────────┬──────────┘
          │
          ▼
 ┌──────────────────────┐     ┌──────────────────────────┐
 │ 1. Pause Clock       │────►│ 2. Flush ALL Queues      │
 │    (freeze display)  │     │    PacketQueue.flush()    │
 │                      │     │    FrameQueue.flush()     │
 └──────────────────────┘     └────────────┬─────────────┘
                                           │
                                           ▼
                               ┌──────────────────────────┐
                               │ 3. Flush Decoders        │
                               │    avcodec_flush_buffers │
                               │    → discard internal    │
                               └────────────┬─────────────┘
                                            │
                                            ▼
                               ┌──────────────────────────┐
                               │ 4. Demuxer Seek          │
                               │    av_seek_frame(        │
                               │      pos,                │
                               │      AVSEEK_FLAG_BACKWARD│
                               │    )                     │
                               └────────────┬─────────────┘
                                            │
                                            ▼
                               ┌──────────────────────────┐
                               │ 5. Decode to Target      │
                               │    Decode frames until   │
                               │    PTS >= target_pos     │
                               │    (discard early frames)│
                               └────────────┬─────────────┘
                                            │
                                            ▼
                               ┌──────────────────────────┐
                               │ 6. Set Clock to new PTS  │
                               │    clock.set(new_pts)    │
                               │    Reset serial          │
                               └────────────┬─────────────┘
                                            │
                                            ▼
                               ┌──────────────────────────┐
                               │ 7. Resume Clock          │
                               │    is_seeking = false    │
                               │    → restore play/pause  │
                               └──────────────────────────┘
```

### 7.3 音视频同步数据流

```
 AudioRender Thread                    VideoRender Thread
 ┌──────────────────┐                 ┌──────────────────────┐
 │ SDL2 Callback    │                 │ VSync Loop           │
 │                  │                 │                      │
 │ Read samples     │                 │ frame = peekFrame()  │
 │ ↓                │                 │ ↓                    │
 │ Output to HW     │                 │ delay = frame.pts    │
 │ ↓                │                 │      - clock.master()│
 │ clock.update(    │                 │ ↓                    │
 │   audio_pts +    │─── audio_clock ─│→ if delay > threshold│
 │   buffer_offset) │    (master)     │ │  short sleep       │
 │                  │                 │ │  (fine-grained)    │
 │                  │                 │ │ else if delay < 0  │
 │                  │                 │ │  drop frame        │
 │                  │                 │ │ else               │
 │                  │                 │ │  render now        │
 │                  │                 │ ↓                    │
 │                  │                 │ Upload + Draw        │
 │                  │                 │ ↓                    │
 │                  │                 │ nextFrame()          │
 └──────────────────┘                 └──────────────────────┘

  Synchronization Strategy:

  ┌──────────────┬─────────────────────────────────────────────┐
  │ 场景         │ 策略                                        │
  ├──────────────┼─────────────────────────────────────────────┤
  │ 有音频流     │ Audio clock = MASTER                        │
  │              │ Video 追赶/等待 Audio clock                 │
  │              │ 丢帧阈值: delay < -100ms → drop             │
  │              │ 等帧阈值: delay > 10ms  → nanosleep         │
  ├──────────────┼─────────────────────────────────────────────┤
  │ 纯视频流     │ System clock = MASTER                       │
  │              │ Video 以目标帧率推送                        │
  ├──────────────┼─────────────────────────────────────────────┤
  │ 外部时钟     │ External clock = MASTER                     │
  │ (直播场景)   │ 用于 RTMP/RTSP/SRT 直播，Wall-clock driven  │
  └──────────────┴─────────────────────────────────────────────┘
```

---

## 8. 可扩展性设计（插件架构）

```
┌────────────────────────────────────────────────────────────┐
│                    Plugin Interface Layer                    │
│                                                             │
│  «interface»              «interface»         «interface»   │
│  IProtocolPlugin          IRendererPlugin     IFilterPlugin │
│  ┌─────────────────┐     ┌──────────────┐    ┌───────────┐ │
│  │ canHandle(url)  │     │ create()     │    │ create()  │ │
│  │ create(url)     │     │ type()       │    │ type()    │ │
│  │ schemes()       │     │ priority()   │    │           │ │
│  └────────┬────────┘     └──────┬───────┘    └───────────┘ │
│           │                     │                           │
└───────────┼─────────────────────┼───────────────────────────┘
            │                     │
    ┌───────┴──────────┐   ┌──────┴──────────┐
    │ Built-in Plugins │   │ Dynamic Plugins │
    ├──────────────────┤   │ (.so runtime)   │
    │ FileProtocol     │   ├─────────────────┤
    │ HTTPProtocol     │   │ WebRTC  (future)│
    │ RTMPProtocol     │   │ SRT     (future)│
    │ RTSPProtocol     │   │ HLS     (future)│
    │ OpenGLRenderer   │   │ Vulkan  (future)│
    │ SDL2AudioRender  │   │ PulseAudio      │
    └──────────────────┘   │ ALSA            │
                           └─────────────────┘

Plugin Discovery:
  1. 编译时注册: REGISTER_PLUGIN(FileProtocol)
  2. 运行时加载: PluginManager::loadPlugin("libwebrtc_protocol.so")
  3. 自动探测:   扫描 plugin_path/ 目录下 *.so → dlopen → dlsym("create_plugin")
```

---

## 9. 配置结构

```
PlayerConfig
├── source
│   ├── timeout_ms: 10000          # 连接超时
│   ├── reconnect: true            # 自动重连
│   ├── max_reconnect: 3           # 最大重连次数
│   ├── buffer_size: 4*1024*1024   # 网络缓冲
│   └── probesize: 5*1024*1024     # 分析大小
├── decode
│   ├── hw_accel: "vaapi"          # none / vaapi / vdpau / cuda
│   ├── video_threads: 0           # 0=auto
│   ├── skip_loop_filter: 0        # H.264 skip loop filter for perf
│   └── max_decode_errors: 100     # 连续错误上限
├── render
│   ├── video
│   │   ├── driver: "opengl"
│   │   ├── vsync: true
│   │   ├── color_space: BT.709
│   │   ├── deinterlace: false
│   │   └── shader_path: "..."
│   └── audio
│       ├── driver: "sdl2"
│       ├── sample_rate: 48000
│       ├── channels: 2            # stereo
│       ├── format: S16
│       └── latency_ms: 50
├── sync
│   ├── master_clock: "audio"      # audio / system / external
│   ├── max_frame_delay: 100ms     # 最大等待
│   ├── drop_threshold: -100ms     # 丢帧阈值
│   └── sync_tolerance: 10ms       # 同步容差
└── log
    ├── level: "info"              # trace/debug/info/warn/error
    └── output: "file"             # stdout / file / syslog
```

---

## 10. 关键技术要点

### 10.1 零拷贝路径

```
Demux 产出  →  Decode 消费  →  Render 消费
   AVPacket    AVFrame*         GPU Texture

零拷贝点:
  1. AVFrame data[] 直接传递，不 memcpy
  2. VAAPI decode → vaDeriveImage → DRM PRIME fd → EGL import → 零拷贝到 GL texture
  3. Audio: 环形缓冲区按块指针传递，不复制 PCM 数据
  4. Frame 使用 shared_ptr 管理生命周期，引用计数为零时归还 AVFrame 到内存池
```

### 10.2 硬件解码路径

```
AVPacket
   │
   ▼
avcodec_send_packet(hw_codec_ctx)
   │
   ▼
VAAPI/VDPAU 解码 (GPU)
   │
   ▼
avcodec_receive_frame → AVFrame (hw_pix_fmt: VAAPI / DRM_PRIME)
   │
   ├── [路径A: 零拷贝] DRM PRIME fd → EGLImage → GL Texture → Shader
   │
   └── [路径B: 回读]   av_hwframe_transfer → CPU AVFrame → glTexImage2D
```

### 10.3 错误恢复分级

| 级别 | 错误类型 | 恢复策略 |
|------|----------|----------|
| L1 | 单帧解码失败 | 丢弃，继续下一帧 |
| L2 | 连续帧解码失败 | Flush decoder，从下一个 I 帧恢复 |
| L3 | 网络断流 | 自动重连（含 backoff），从断点继续 |
| L4 | Demuxer 异常 | 重新 open，seek 到上次位置 |
| L5 | 硬件解码失败 | 降级到软解，通知 App |
| L6 | 致命错误(OOM等) | 转 ERROR 状态，通知 App 决定 |
