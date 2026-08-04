# Phase 3 Native 设计框架

## 1. Phase 3 目标

Phase 1 和 Phase 2 已经完成浏览器 WebRTC MVP、stats 观测、DataChannel RTT、编码参数控制和 Simple ABR。Phase 3 的目标是把项目从“浏览器 API 实验台”推进到“能体现 C++ / FFmpeg / Linux 音视频底层能力”的 native 阶段。

这一阶段不建议一上来直接编译和接入完整 libwebrtc。libwebrtc 工程庞大、构建复杂、反馈周期长，容易让项目卡在环境和构建上。更合理的路线是先把实时音视频底层链路拆开，一步一步建立可运行、可解释、可面试表达的小闭环。

核心目标：

1. 打通 Linux 摄像头采集入口。
2. 理解 V4L2 buffer、像素格式、帧时间戳和采集线程模型。
3. 用 FFmpeg 分析本地媒体文件的 PTS / DTS / time_base / GOP / keyframe。
4. 逐步把采集、编码、RTP/WebRTC 的边界讲清楚。
5. 最后再规划 native libwebrtc sender，而不是一开始就陷入 libwebrtc 工程复杂度。

## 2. 总体架构路线

Phase 3 按四层推进：

```text
Layer 1: Linux Capture
  V4L2 device
  format enumerate
  mmap buffer
  frame capture
  timestamp

Layer 2: Media Analysis
  FFmpeg demux
  stream info
  PTS / DTS / time_base
  GOP / keyframe
  H264 Annex-B / AVCC

Layer 3: Encode / Packetize Experiment
  raw frame / compressed frame
  encoder input model
  H264 NALU
  RTP packetization concept

Layer 4: Native WebRTC Roadmap
  libwebrtc native API
  VideoTrackSource
  encoded frame path
  RTP / RTCP stats
  browser receiver interop
```

先做 Layer 1 和 Layer 2，因为它们和作者已有经验最强相关，也最容易快速产出可运行结果。

## 3. 推荐目录结构

```text
native/
  CMakeLists.txt
  README.md
  v4l2_capture/
    CMakeLists.txt
    main.cpp
    v4l2_device.h
    v4l2_device.cpp
    image_writer.h
    image_writer.cpp
  ffmpeg_probe/
    CMakeLists.txt
    main.cpp
    media_probe.h
    media_probe.cpp

docs/
  phase3/
    phase3-native-design.md
    v4l2-capture-design.md
    ffmpeg-timestamp-design.md
  interview-notes/
    09-v4l2-capture.md
    10-ffmpeg-timestamp-gop.md
  dev-log/
    010-phase3-native-design.md
    011-v4l2-capture-demo.md
    012-ffmpeg-probe-demo.md
```

## 4. 模块一：V4L2 Capture Demo

### 4.1 目标

实现一个 Linux C++ 摄像头采集工具，直接访问 `/dev/video0`，完成格式枚举和单帧采集。

### 4.2 功能范围

第一版只做：

1. 打开 V4L2 设备。
2. 查询设备能力。
3. 枚举支持的 pixel format、resolution、fps。
4. 设置采集格式，例如 MJPEG / YUYV 640x480。
5. 使用 mmap buffer 采集一帧。
6. 如果是 MJPEG，直接保存 `.jpg`。
7. 如果是 YUYV，保存 `.ppm`，后续再扩展转 RGB / 编码。
8. 打印每帧 metadata：bytesused、timestamp、sequence、format。

暂时不做：

1. 连续采集线程。
2. 编码器接入。
3. RTP packetize。
4. libwebrtc 接入。
5. GUI 预览。

### 4.3 为什么先做这个

WebRTC 项目最终要处理实时视频。浏览器 `getUserMedia` 隐藏了大量底层细节，而 native 阶段需要重新面对设备、buffer、pixel format、timestamp、阻塞/非阻塞 IO、采集线程和内存生命周期。

这个 Demo 可以帮助面试时讲清楚：

1. 浏览器 API 之下，真实摄像头采集是怎么工作的。
2. 一帧视频从设备到用户态 buffer 的过程。
3. MJPEG / YUYV 这种输入格式和 H264 编码输入之间的关系。
4. 为什么实时链路里 timestamp 和 buffer 生命周期很重要。

### 4.4 验收标准

命令示例：

```bash
cd native
cmake -S . -B build
cmake --build build
./build/v4l2_capture/v4l2_capture --device /dev/video0 --list
./build/v4l2_capture/v4l2_capture --device /dev/video0 --format mjpeg --size 640x480 --output captures/native-camera.jpg
```

验收结果：

1. 能列出摄像头格式。
2. 能保存一张 native C++ 采集出来的图片。
3. 日志中能看到 V4L2 buffer 信息和 timestamp。
4. 文档中解释 mmap buffer 和采集流程。

## 5. 模块二：FFmpeg Probe Demo

### 5.1 目标

实现一个 C++/FFmpeg 媒体分析工具，读取 MP4 / H264 文件，打印音视频 stream 信息和关键时间戳数据。

### 5.2 功能范围

第一版只做：

1. 打开媒体文件。
2. 打印 format、duration、bitrate。
3. 打印 video stream codec、width、height、fps、time_base。
4. 遍历前 N 个 packet。
5. 打印 packet 的 pts、dts、duration、pos、flags、是否 keyframe。
6. 统计 GOP 间隔。
7. 解释 time_base 下 pts 到秒的转换。

暂时不做：

1. 解码。
2. 编码。
3. filter graph。
4. 音画同步播放器。

### 5.3 为什么做这个

作者已经学了很多编解码知识，也做过精准 Seek、frame index、int64 pts、Smart Seek。FFmpeg Probe Demo 可以把这些经验和 WebRTC 连接起来：WebRTC 的 RTP timestamp、jitter buffer、帧顺序、关键帧请求 PLI/FIR 都和时间戳、帧依赖、GOP 有关。

### 5.4 验收标准

命令示例：

```bash
./build/ffmpeg_probe/ffmpeg_probe --input sample.mp4 --packets 80
```

验收结果：

1. 能打印 stream 信息。
2. 能打印前 N 个 packet 的 pts/dts/keyframe。
3. 能识别 GOP 间隔。
4. 文档中解释 PTS / DTS / time_base / GOP 和 WebRTC 的关系。

## 6. 模块三：Encode / RTP / WebRTC 边界设计

这一模块先写设计，不急着实现。

目标是回答：从 native 采集到 WebRTC 发送，中间有哪些路线？

### 路线 A：Raw Frame -> libwebrtc Encoder

```text
V4L2 YUYV/NV12/RGB
  -> convert to I420/NV12
  -> custom VideoTrackSource
  -> libwebrtc 内部编码
  -> RTP / RTCP
```

优点：更符合 libwebrtc 原生模型，拥塞控制和编码适配更完整。

缺点：libwebrtc API 和线程模型复杂，构建成本高。

### 路线 B：Encoded H264 -> libwebrtc Encoded Path

```text
V4L2 / FFmpeg
  -> H264 encoder
  -> encoded frame
  -> WebRTC encoded transform / native encoded source
  -> RTP packetization
```

优点：更贴近作者已有 H264 / MediaCodec / FFmpeg 经验。

缺点：浏览器 / libwebrtc 对 encoded frame 注入路径限制更多，兼容性和 API 成本更高。

### 路线 C：先做 RTP Packetization 实验

```text
H264 Annex-B NALU
  -> FU-A packetization
  -> RTP sequence / timestamp
  -> receiver concept / dump analysis
```

优点：能深入理解 RTP 和 H264 over RTP。

缺点：还不是完整 WebRTC，需要后续接 SRTP / DTLS / ICE / RTCP。

## 7. 面试主线

Phase 3 面试时可以这样讲：

1. 浏览器 MVP 证明我理解 WebRTC 建连和 stats。
2. V4L2 Demo 证明我能处理 Linux native 摄像头采集，理解设备、buffer、格式和 timestamp。
3. FFmpeg Probe Demo 证明我理解封装、packet、PTS/DTS、time_base、GOP 和关键帧。
4. 编码参数控制和 Simple ABR 证明我能建立控制和反馈闭环。
5. native WebRTC 设计说明我知道从 raw frame / encoded frame 接入 WebRTC 的工程路线和取舍。

关键表达：

> 我不是只会调浏览器 API，而是从采集、时间戳、编码、网络传输、stats 和自适应策略多个层面拆解实时音视频链路。

## 8. Phase 3 执行顺序建议

### Step 1：V4L2 Capture Demo

优先级最高。它能快速运行，也能直接使用当前 WSL 摄像头环境验证。

交付物：

1. CMake 工程。
2. `v4l2_capture` 可执行文件。
3. 格式枚举。
4. 单帧抓图。
5. `docs/phase3/v4l2-capture-design.md`。
6. `docs/interview-notes/09-v4l2-capture.md`。
7. `docs/dev-log/011-v4l2-capture-demo.md`。

### Step 2：FFmpeg Probe Demo

交付物：

1. `ffmpeg_probe` 可执行文件。
2. packet 时间戳打印。
3. GOP 统计。
4. `docs/phase3/ffmpeg-timestamp-design.md`。
5. `docs/interview-notes/10-ffmpeg-timestamp-gop.md`。
6. `docs/dev-log/012-ffmpeg-probe-demo.md`。

### Step 3：Native WebRTC Sender 方案调研

交付物：

1. `docs/phase3/native-webrtc-sender-roadmap.md`。
2. raw frame path 与 encoded frame path 对比。
3. libwebrtc build 风险和最小验证计划。

## 9. 风险和边界

1. WSL 摄像头依赖 usbipd attach，重启后可能需要重新挂载。
2. V4L2 中不同摄像头支持格式不同，代码必须支持枚举和错误提示。
3. MJPEG 可以直接保存为 JPG，但 YUYV 需要转换才能保存成常见图片格式。
4. FFmpeg 开发依赖 dev package，如果缺少 `libavformat-dev` 等包，需要安装。
5. libwebrtc native 阶段构建成本高，应作为后续独立阶段，不和 V4L2/FFmpeg 基础模块混在一起。

## 10. 我建议先执行什么

建议先执行 **Step 1：V4L2 Capture Demo**。

原因：

1. 当前 WSL 已经有 `/dev/video0`，可立即验证。
2. 它最能体现 Linux C++ 音视频底层能力。
3. 它和后续 native WebRTC sender 的采集入口直接相关。
4. 它的面试表达很清晰：设备能力枚举、mmap buffer、帧格式、timestamp、单帧保存。

如果你确认这个设计没有问题，我下一步就开始实现 `native/v4l2_capture`。
