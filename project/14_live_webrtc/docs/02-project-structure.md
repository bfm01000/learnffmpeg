# Step 2: 工程目录结构设计

> 状态: 待评审
> 日期: 2026-08-04

---

## 1. 设计原则

### 1.1 模块化

每个模块独立目录，包含自己的头文件、源文件、内部实现。模块间通过抽象接口耦合，具体实现可替换。

### 1.2 物理层映射逻辑层

```
架构逻辑层              目录物理层
────────────────────  ─────────────────
Application Layer  →  src/app/
Capture Layer      →  src/capture/
Frame Layer        →  src/frame/
Encoding Layer     →  src/encoder/
Transport Layer    →  src/webrtc/
                         src/signaling/
Monitoring Layer   →  src/monitoring/
Common             →  src/common/
```

### 1.3 可扩展性

每个模块目录预留扩展点：
- 编码器：`src/encoder/` 下新增实现文件，不改接口
- 采集器：`src/capture/` 下可新增 DeckLink/NDI 等采集
- 协议：`src/protocol/` 下可新增 RTMP/SRT 等推流协议

---

## 2. 完整目录结构

```
project/14_live_webrtc/
│
├── CMakeLists.txt                      # 根 CMake: 项目/版本/子目录/依赖
├── README.md                           # 项目说明 + 快速开始
├── CLAUDE.md                           # AI 开发助手指令
├── PROCESS.md                          # 开发流程记录
├── .clang-format                       # 代码格式化规则
├── .clang-tidy                         # 静态分析规则
├── .gitignore                          # Git 忽略规则
│
├── cmake/                              # ★ CMake 自定义模块
│   ├── FindFFmpeg.cmake                #   查找 FFmpeg 库
│   ├── FindLibWebRTC.cmake             #   查找 libwebrtc
│   ├── FindSpdlog.cmake                #   查找 spdlog
│   ├── CompilerWarnings.cmake          #   统一编译器警告配置
│   └── Sanitizers.cmake                #   ASAN/TSAN/UBSAN 选项
│
├── src/                                # ★ 主源码目录
│   │
│   ├── main.cpp                        #   入口: 信号注册, 启动 AppController
│   │
│   ├── app/                            # ── Application Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── app_controller.h            #     应用控制器: 状态机 + 模块编排
│   │   └── app_controller.cpp
│   │
│   ├── common/                         # ── Common Utilities ──
│   │   ├── CMakeLists.txt
│   │   ├── types.h                     #     基础类型定义 (Pts, Resolution, ...)
│   │   ├── result.h                    #     Result<T, E> 错误处理 (类似 Rust)
│   │   ├── noncopyable.h              #     NonCopyable / NonMovable 基类
│   │   ├── spsc_queue.h               #     Lock-free SPSC 队列模板
│   │   ├── mpsc_queue.h               #     Thread-safe MPSC 队列模板
│   │   ├── bounded_buffer.h           #     有界环形缓冲区
│   │   ├── rate_limiter.h             #     速率限制器
│   │   ├── timer.h                     #     高精度定时器
│   │   ├── logging.h                   #     spdlog 封装
│   │   ├── stats_counter.h            #     原子计数器 (丢包/帧率/码率)
│   │   └── platform.h                  #     平台宏 (Linux/glibc 版本检测)
│   │
│   ├── capture/                        # ── Capture Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── capture_config.h            #     采集配置: 分辨率/帧率/格式
│   │   ├── device_info.h               #     设备信息: 路径/名称/支持的能力
│   │   ├── device_enumerator.h         #     设备枚举器接口
│   │   ├── device_enumerator.cpp
│   │   ├── v4l2_capture.h             #     V4L2 采集器接口
│   │   ├── v4l2_capture.cpp            #     V4L2 采集器实现
│   │   ├── v4l2_buffer.h              #     V4L2 buffer 封装
│   │   ├── v4l2_controls.h            #     V4L2 控制项 (曝光/白平衡)
│   │   └── ivideo_capture.h           #     采集器抽象接口 (未来扩展)
│   │
│   ├── frame/                          # ── Frame Management Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── pixel_format.h             #     像素格式枚举 + 转换函数
│   │   ├── video_frame.h               #     视频帧: 数据 + 元数据
│   │   ├── video_frame.cpp
│   │   ├── frame_data.h               #     帧数据: 引用计数 + DMA-BUF fd
│   │   ├── frame_data.cpp
│   │   ├── frame_buffer_pool.h        #     帧缓冲池: 预分配 + 引用计数回收
│   │   └── frame_buffer_pool.cpp
│   │
│   ├── encoder/                        # ── Encoding Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── i_video_encoder.h           #     编码器抽象接口
│   │   ├── encoder_config.h            #     编码配置: 码率/GOP/preset/profile
│   │   ├── encoded_packet.h            #     编码后数据包
│   │   ├── encoder_stats.h            #     编码器统计: 帧率/PSNR/QP
│   │   ├── ffmpeg_utils.h             #     FFmpeg 工具函数 (av_log, dict)
│   │   ├── ffmpeg_h264_encoder.h      #     libx264 软件编码器
│   │   ├── ffmpeg_h264_encoder.cpp
│   │   └── vaapi_h264_encoder.h       #     VAAPI 硬件编码器 (Phase 2)
│   │   └── vaapi_h264_encoder.cpp      #
│   │
│   ├── webrtc/                         # ── WebRTC Transport Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── webrtc_config.h             #     WebRTC 配置: ICE servers, codec pref
│   │   ├── webrtc_manager.h           #     WebRTC 管理器: PC 生命周期
│   │   ├── webrtc_manager.cpp
│   │   ├── custom_video_source.h      #     自定义视频源 (VideoTrackSource)
│   │   ├── custom_video_source.cpp
│   │   ├── peer_connection_observer.h #     PC 事件回调实现
│   │   ├── peer_connection_observer.cpp
│   │   ├── set_session_description_observer.h  # SDP 操作回调
│   │   └── ice_candidate.h             #     ICE 候选封装
│   │
│   ├── signaling/                      # ── Signaling Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── signaling_config.h         #     信令配置: server URL, room ID
│   │   ├── signaling_message.h        #     信令消息定义 (SDP/ICE/Join)
│   │   ├── signaling_message.cpp      #     消息 JSON 序列化/反序列化
│   │   ├── signaling_client.h         #     WebSocket 信令客户端
│   │   └── signaling_client.cpp
│   │
│   ├── monitoring/                     # ── Monitoring Layer ──
│   │   ├── CMakeLists.txt
│   │   ├── stats_snapshot.h           #     统计快照数据结构
│   │   ├── stats_collector.h          #     统计收集器: 周期 poll getStats
│   │   ├── stats_collector.cpp
│   │   ├── bitrate_controller.h       #     码率控制器: 水位线 + AIMD
│   │   ├── bitrate_controller.cpp
│   │   ├── congestion_detector.h      #     拥塞检测器
│   │   └── network_quality.h          #     网络质量评估
│   │
│   └── stream/                         # ── Stream Coordinator Layer (Phase 7+) ──
│       ├── CMakeLists.txt
│       ├── live_stream.h              #     直播流协调器: 编排 Capture+Encode+WebRTC
│       └── live_stream.cpp
│
├── test/                               # ★ 测试目录
│   ├── CMakeLists.txt                  #   测试根 CMake
│   ├── test_common.h                   #   测试工具函数
│   ├── common/                         #   基础工具测试
│   │   ├── CMakeLists.txt
│   │   ├── test_result.cpp
│   │   ├── test_spsc_queue.cpp
│   │   └── test_bounded_buffer.cpp
│   ├── capture/                        #   采集层测试
│   │   ├── CMakeLists.txt
│   │   ├── test_v4l2_capture.cpp      #     需要 v4l2loopback
│   │   └── mock_v4l2_device.h         #     Mock V4L2 设备
│   ├── frame/                          #   帧管理层测试
│   │   ├── CMakeLists.txt
│   │   └── test_frame_buffer_pool.cpp
│   ├── encoder/                        #   编码层测试
│   │   ├── CMakeLists.txt
│   │   └── test_ffmpeg_encoder.cpp
│   ├── signaling/                      #   信令层测试
│   │   ├── CMakeLists.txt
│   │   └── test_signaling_message.cpp
│   └── monitoring/                     #   监控层测试
│       ├── CMakeLists.txt
│       └── test_bitrate_controller.cpp
│
├── signaling-server/                   # ★ Go 信令服务器 (独立 Go 项目)
│   ├── go.mod
│   ├── go.sum
│   ├── main.go                         #   入口
│   ├── server/                         #   HTTP + WebSocket server
│   │   ├── server.go
│   │   └── websocket.go
│   ├── room/                           #   房间管理
│   │   └── room_manager.go
│   ├── message/                        #   消息定义
│   │   └── signaling_message.go
│   └── Makefile
│
├── config/                             # ★ 配置文件
│   ├── default.json                    #   默认配置
│   ├── development.json                #   开发环境
│   └── production.json                 #   生产环境
│
├── scripts/                            # ★ 工具脚本
│   ├── build.sh                        #   编译脚本
│   ├── build_debug.sh                  #   Debug 编译
│   ├── build_release.sh               #   Release 编译
│   ├── run.sh                          #   运行脚本
│   ├── test.sh                         #   测试脚本
│   ├── format-check.sh                #   格式检查
│   ├── lint.sh                         #   静态分析
│   ├── setup_v4l2_loopback.sh         #   创建 v4l2loopback 虚拟摄像头
│   └── setup_chrome_test.sh           #   Chrome 测试页面准备
│
├── docs/                               # ★ 文档目录
│   ├── 01-architecture-design.md       #   Step 1: 架构设计
│   ├── 02-project-structure.md         #   Step 2: 目录结构 (本文)
│   ├── decisions/                      #   技术决策记录 (ADR)
│   │   └── .gitkeep
│   ├── modules/                        #   各模块设计文档
│   │   ├── capture.md
│   │   ├── frame.md
│   │   ├── encoder.md
│   │   ├── webrtc.md
│   │   ├── signaling.md
│   │   └── monitoring.md
│   ├── performance/                    #   性能测试数据和分析
│   │   └── .gitkeep
│   ├── problems/                       #   问题排查记录
│   │   └── .gitkeep
│   ├── interview/                      #   面试总结材料
│   │   └── .gitkeep
│   └── development_log.md             #   开发日志
│
├── third_party/                        # ★ 第三方依赖 (FetchContent 下载)
│   └── .gitkeep
│
└── web/                                # ★ 浏览器测试页面 (可选)
    ├── index.html                      #   简单的视频接收页面
    └── webrtc_receiver.js             #   WebRTC 接收逻辑
```

---

## 3. CMake 构建体系设计

### 3.1 构建层次

```
CMakeLists.txt (根)
├── 项目元信息 (project, version, C++17)
├── 编译选项 (warnings, sanitizers)
├── 依赖发现 (FFmpeg, spdlog, libwebrtc, ...)
├── 子目录
│   ├── src/common/CMakeLists.txt     →  myapp_common (static lib)
│   ├── src/capture/CMakeLists.txt    →  myapp_capture (static lib)
│   ├── src/frame/CMakeLists.txt      →  myapp_frame (static lib)
│   ├── src/encoder/CMakeLists.txt    →  myapp_encoder (static lib)
│   ├── src/webrtc/CMakeLists.txt     →  myapp_webrtc (static lib)
│   ├── src/signaling/CMakeLists.txt  →  myapp_signaling (static lib)
│   ├── src/monitoring/CMakeLists.txt →  myapp_monitoring (static lib)
│   ├── src/app/CMakeLists.txt        →  myapp_app (static lib)
│   ├── src/stream/CMakeLists.txt     →  myapp_stream (static lib)
│   ├── test/CMakeLists.txt           →  myapp_tests (executable)
│   └── src/main.cpp                  →  live_webrtc (executable)
```

### 3.2 模块间依赖关系

```
                      live_webrtc (可执行文件)
                            │
                     myapp_app (AppController)
                            │
              ┌─────────────┼─────────────┬──────────────┐
              │             │             │              │
       myapp_stream    myapp_webrtc  myapp_monitoring  myapp_signaling
              │             │             │              │
              ├───── myapp_encoder ───────┤              │
              │             │             │              │
              ├───── myapp_frame ─────────┤              │
              │             │             │              │
              └───── myapp_capture ───────┘              │
                            │                            │
                      myapp_common ◄─────────────────────┘
```

### 3.3 根 CMakeLists.txt 骨架

```cmake
cmake_minimum_required(VERSION 3.20)
project(live_webrtc VERSION 0.1.0 LANGUAGES CXX)

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

# 依赖
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil)
find_package(spdlog REQUIRED)
# find_package(LibWebRTC REQUIRED)  # Phase 5

# 子目录
add_subdirectory(src/common)
add_subdirectory(src/frame)
add_subdirectory(src/capture)
add_subdirectory(src/encoder)
add_subdirectory(src/signaling)
add_subdirectory(src/webrtc)
add_subdirectory(src/monitoring)
add_subdirectory(src/stream)
add_subdirectory(src/app)

# 主可执行文件
add_executable(live_webrtc src/main.cpp)
target_link_libraries(live_webrtc PRIVATE myapp_app)

# 测试
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

---

## 4. 各模块详细说明

### 4.1 src/common/ — 基础设施

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `types.h` | Pts, Resolution, Rate 等基础类型 | `using` 别名, `struct` 封装, 避免裸 `int64_t` |
| `result.h` | `Result<T, E>` 模板 | 类似 Rust Result, 强制错误检查, `try!` 宏 |
| `noncopyable.h` | RAII 基类 | 禁止拷贝/移动, 用于管理设备句柄等 |
| `spsc_queue.h` | Lock-free SPSC 队列 | 基于 `std::atomic`, cache line padding, 适用于采集→编码 |
| `mpsc_queue.h` | 线程安全 MPSC 队列 | 基于 mutex+condition_variable, 用于多生产者场景 |
| `bounded_buffer.h` | 有界环形缓冲 | 固定容量, 覆盖写入或阻塞策略 |
| `rate_limiter.h` | 令牌桶限速器 | 控制采集帧率/编码输出速率 |
| `timer.h` | 高精度定时器 | `clock_gettime(CLOCK_MONOTONIC)`, 延迟测量 |
| `logging.h` | spdlog 封装 | 项目统一日志宏, category 标签, 级别控制 |
| `stats_counter.h` | 原子统计计数器 | `std::atomic` 实现, 线程安全累加 |
| `platform.h` | 平台兼容宏 | `likely/unlikely`, `LIVE_LOG_LOCATION` |

### 4.2 src/capture/ — 采集层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `capture_config.h` | 采集参数配置 | Resolution, FrameRate, PixelFormat |
| `device_info.h` | 设备信息 | 描述 V4L2 设备能力 |
| `device_enumerator.h/cpp` | 枚举 /dev/video* | VIDIOC_QUERYCAP 循环, 获取设备名+能力 |
| `ivideo_capture.h` | 抽象接口 | `start()/stop()/dequeue()/close()`, 纯虚函数 |
| `v4l2_capture.h/cpp` | V4L2 实现 | ioctl, mmap, epoll, 采集循环 |
| `v4l2_buffer.h` | V4L2 buffer 封装 | index, bytesused, timestamp, fd |
| `v4l2_controls.h` | V4L2 控制 | set/get exposure, white_balance, etc. |

**线程模型**: 采集线程由 `V4L2Capture` 内部管理，start() 启动线程，stop() 停止。

### 4.3 src/frame/ — 帧管理层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `pixel_format.h` | 像素格式定义+转换 | enum class PixelFormat, `getBpp()`, `getPlanes()` |
| `video_frame.h/cpp` | 视频帧 | 持有 shared_ptr<FrameData> + 元数据 |
| `frame_data.h/cpp` | 帧数据存储 | 引用计数, planes, strides, 可选的 dma_buf_fd |
| `frame_buffer_pool.h/cpp` | 缓冲池 | 预分配 N 个 slot, acquire/release, 池满策略 |

**核心设计**: `shared_ptr<FrameData>` + 自定义 deleter 自动回收 slot。

### 4.4 src/encoder/ — 编码层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `i_video_encoder.h` | 编码器抽象接口 | `encode()`, `setBitrate()`, `requestKeyFrame()`, `flush()` |
| `encoder_config.h` | 编码参数 | bitrate, gop_size, preset, profile, tune |
| `encoded_packet.h` | 编码输出 | data, size, pts, dts, is_keyframe |
| `encoder_stats.h` | 编码统计 | fps, bitrate, psnr, qp, encode_latency_ms |
| `ffmpeg_utils.h` | FFmpeg 工具 | av_log callback, opts dict 构建, 错误码转换 |
| `ffmpeg_h264_encoder.h/cpp` | FFmpeg 编码实现 | avcodec_send_frame/receive_packet 循环 |
| `vaapi_h264_encoder.h/cpp` | VAAPI 编码 (Phase 2) | libva + libva-drm, 硬件加速 |

**策略模式**: `IVideoEncoder` 纯虚接口 → `FFmpegH264Encoder` / `VAAPIH264Encoder`。

### 4.5 src/webrtc/ — WebRTC 传输层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `webrtc_config.h` | WebRTC 配置 | ICE servers, codec preference, bundle policy |
| `webrtc_manager.h/cpp` | PeerConnection 生命周期 | CreateFactory, CreatePC, AddTrack, Offer/Answer |
| `custom_video_source.h/cpp` | 自定义 VideoTrackSource | 继承 rtc::VideoTrackSourceInterface, OnFrame() |
| `peer_connection_observer.h/cpp` | PC 事件回调 | OnIceCandidate, OnConnectionChange, OnTrack |
| `set_session_description_observer.h/cpp` | SDP 操作回调 | OnSuccess, OnFailure |
| `ice_candidate.h` | ICE 候选 | 类型 (host/srflx/relay), priority, address |

### 4.6 src/signaling/ — 信令层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `signaling_config.h` | 信令配置 | server URL, room ID, client type |
| `signaling_message.h/cpp` | 消息定义+JSON | Join/SDP/ICE/Leave 消息, nlohmann/json |
| `signaling_client.h/cpp` | WebSocket 客户端 | IXWebSocket, 重连, 心跳 |

### 4.7 src/monitoring/ — 监控层

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `stats_snapshot.h` | 统计快照 | RTT, loss_rate, jitter, bitrate, fps, queue_depth |
| `stats_collector.h/cpp` | 统计收集 | 周期 poll pc->GetStats(), 计算统计量 |
| `bitrate_controller.h/cpp` | 码率控制 | 水位线 + AIMD, 输出 target_bitrate |
| `congestion_detector.h` | 拥塞检测 | overuse/normal/underuse 状态机 |
| `network_quality.h` | 网络质量 | excellent/good/poor/bad 评级 |

### 4.8 src/stream/ — 流协调器 (Phase 7+)

| 文件 | 职责 | 关键设计点 |
|------|------|-----------|
| `live_stream.h/cpp` | 编排各模块 | 创建+连接 Capture/Encoder/WebRTC pipeline |

---

## 5. 扩展路径

### 5.1 硬编码扩展 (Phase 6)

```
src/encoder/
├── i_video_encoder.h          # 不变
├── ffmpeg_h264_encoder.h/cpp  # 不变
├── vaapi_h264_encoder.h/cpp   # ★ 新增
└── nvenc_h264_encoder.h/cpp   # ★ 新增 (可选)
```

外部代码仅依赖于 `IVideoEncoder` 接口，新增实现无需修改调用方。

### 5.2 DMA-BUF 零拷贝扩展 (Phase 7)

```
src/frame/
├── frame_data.h/cpp           # 已有 dma_buf_fd 字段
├── frame_buffer_pool.h/cpp    # ★ 新增 exportDmaBuf() 方法
└── dma_buf_allocator.h/cpp    # ★ 新增: 从 V4L2 导出 dma_buf fd
```

### 5.3 音频链路扩展 (Phase 8)

```
src/
├── audio/                     # ★ 新增目录
│   ├── i_audio_capture.h
│   ├── alsa_capture.h/cpp
│   ├── i_audio_encoder.h
│   ├── opus_encoder.h/cpp
│   └── audio_frame.h
├── av_sync/                   # ★ 新增目录
│   └── av_sync_controller.h/cpp
└── webrtc/                    # 修改: 添加 AudioTrack
    └── webrtc_manager.cpp     #   AddTrack(audio_track)
```

### 5.4 多协议推流扩展 (Phase 9)

```
src/
├── protocol/                  # ★ 新增目录
│   ├── i_streaming_protocol.h
│   ├── rtmp_publisher.h/cpp
│   └── srt_publisher.h/cpp
```

---

## 6. 依赖库清单

| 库 | 用途 | 集成方式 | Phase |
|----|------|---------|-------|
| **spdlog** | 日志 | FetchContent / find_package | 0 |
| **FFmpeg** (libavcodec, libavutil) | 编码 | find_package / pkg-config | 3 |
| **libwebrtc** | WebRTC 协议栈 | gn/ninja 独立编译 + find_package | 5 |
| **IXWebSocket** | WebSocket 客户端 | FetchContent | 4 |
| **nlohmann/json** | JSON 解析 | FetchContent | 4 |
| **Google Test** | 单元测试 | FetchContent | 0 |
| **moodycamel::ConcurrentQueue** | Lock-free 队列 (备选) | FetchContent | 2 |

---

## 7. .gitignore 概要

```gitignore
build/
cmake-build-*/
*.o
*.a
*.so
.deps/
*.swp
*.swo
*~
.clangd/
.cache/
compile_commands.json
third_party/
*.log
.env
```

---

## 下一步

确认目录结构后，进入 Step 3：实现 Phase 0 基础框架。
