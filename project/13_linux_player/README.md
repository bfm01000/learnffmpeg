# Linux Player SDK

现代 C++17 高性能播放器框架。

## 快速开始

```bash
# 构建
./scripts/build.sh --release

# 运行
./build/examples/simple_player /path/to/video.mp4
./build/examples/simple_player http://example.com/stream.flv
./build/examples/simple_player rtsp://192.168.1.1/live

# 测试
./scripts/run_tests.sh
```

## 依赖

- **FFmpeg** ≥ 4.4 (libavformat, libavcodec, libavutil, libswscale, libswresample, libavfilter)
- **SDL2** ≥ 2.0
- **OpenGL** + **GLFW3**
- **CMake** ≥ 3.16
- **GCC** ≥ 9 或 **Clang** ≥ 10 (C++17)

### Ubuntu 安装

```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev \
  libswscale-dev libswresample-dev libavfilter-dev \
  libsdl2-dev libglfw3-dev libva-dev libdrm-dev
```

## 项目结构

```
player_sdk/
├── api/                    # 公开 API 头文件
├── core/                   # 基础设施（队列、时钟、事件、线程、内存、插件）
├── source/                 # 源层（协议处理器 + 解封装）
│   └── protocol/           # file / http / rtmp / rtsp / hls / srt / webrtc
├── decode/                 # 解码层（视频/音频/字幕 + 硬件加速）
├── process/                # 处理层（滤镜、重采样、色彩转换）
├── render/                 # 渲染层（OpenGL 视频 + SDL2 音频 + 字幕叠加）
├── control/                # 控制层（状态机、同步、Seek、播放列表）
├── utils/                  # 工具（日志、配置、性能探针）
├── examples/               # 示例程序
├── test/                   # 单元测试 + 集成测试
├── cmake/                  # CMake 模块
├── scripts/                # 构建/测试/格式化脚本
└── doc/                    # 文档
```

## 特性

- [x] MP4 / FLV / MKV 本地播放
- [x] HTTP / HTTPS 网络流
- [x] RTMP / RTSP 直播流
- [x] HLS (m3u8) 流媒体
- [x] 硬件加速 (VAAPI)
- [x] 音视频同步（Audio Master / System Master / External）
- [x] 零拷贝渲染管线 (DRM PRIME → EGL → GL Texture)
- [x] 插件架构（协议/渲染器/解码器均可动态加载）
- [ ] WebRTC (规划中)
- [ ] SRT (规划中)

## 许可

Internal — Player SDK
