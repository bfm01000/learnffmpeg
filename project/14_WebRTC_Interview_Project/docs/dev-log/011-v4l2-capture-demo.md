# 011 V4L2 Capture Demo

## 本次目标

进入 Phase 3 的 native 阶段，实现一个 Linux C++ V4L2 摄像头采集 Demo，直接访问 `/dev/video0`，完成格式枚举和单帧抓图。

## 做了什么

新增 native CMake 工程：

1. `native/CMakeLists.txt`
2. `native/README.md`
3. `native/v4l2_capture/CMakeLists.txt`
4. `native/v4l2_capture/main.cpp`
5. `native/v4l2_capture/v4l2_device.h`
6. `native/v4l2_capture/v4l2_device.cpp`
7. `native/v4l2_capture/image_writer.h`
8. `native/v4l2_capture/image_writer.cpp`

新增文档：

1. `docs/phase3/v4l2-capture-design.md`
2. `docs/interview-notes/09-v4l2-capture.md`

## 遇到的问题

Phase 3 如果直接进入 native libwebrtc，构建成本和 API 复杂度会很高，不利于快速验证。因此先从 V4L2 采集小闭环开始。

## 怎么排查

先确认 WSL 环境中存在 `/dev/video0` 和 C++ 工具链。然后根据 Phase 3 设计文档确定第一步只做设备能力枚举和单帧采集，不引入编码器、RTP 或 libwebrtc。

## 怎么解决

使用标准 V4L2 mmap 流程实现采集。MJPEG 数据直接写成 JPG；YUYV 数据转换成 RGB 后写成 PPM，避免第一版依赖图像编码库。

## 技术取舍

第一版没有做连续采集线程、编码和 WebRTC 接入。这样可以把采集入口、buffer 队列、pixel format 和 timestamp 先讲清楚。后续再在这个基础上抽象 `FrameSource`，接入 FFmpeg encoder 或 native WebRTC。

## 涉及的关键知识点

1. V4L2 device capability。
2. pixel format / frame size / frame interval。
3. mmap buffer。
4. QBUF / DQBUF。
5. STREAMON / STREAMOFF。
6. MJPEG / YUYV。
7. frame timestamp 和 sequence。

## 和我过往经验的连接

这一步对应实时音视频链路的采集入口。它和之前做低延迟预览、RTMP 推流、硬编硬解链路一样，首先要确认帧从设备稳定进入用户态，然后才能谈编码、发送、ABR 和 WebRTC。

## 面试讲法

我在浏览器 WebRTC MVP 之后，继续向下实现了 Linux C++ V4L2 摄像头采集。这个 Demo 不是简单调用 OpenCV，而是直接走 V4L2：查询能力、协商格式、申请 mmap buffer、QBUF 入队、STREAMON 开流、DQBUF 取帧，并打印 sequence、bytesused 和 timestamp。这样我可以讲清楚 WebRTC getUserMedia 背后更底层的采集模型，也能把后续 native WebRTC sender 的帧源问题提前拆出来。

## 后续可扩展

1. 连续采集线程。
2. YUYV / MJPEG 转 I420 / NV12。
3. FFmpeg 编码器接入。
4. 抽象 FrameSource。
5. native libwebrtc sender 设计。

## 验证结果

已完成编译：

```bash
cd native
cmake -S . -B build
cmake --build build -j2
```

已验证格式枚举：

```bash
./build/v4l2_capture/v4l2_capture --device /dev/video0 --list
```

当前 WSL 摄像头识别为 `uvcvideo / Integrated Camera`，支持 `MJPG` 和 `YUYV`，其中 `MJPG 640x480 @ 30fps` 可用。

已验证 MJPEG 单帧采集：

```bash
./build/v4l2_capture/v4l2_capture --device /dev/video0 --format mjpeg --size 640x480 --output ../captures/native-camera.jpg
```

输出结果：

```text
active format: 640x480 MJPG
bytes_used   : 32386
pixel_format : MJPG
saved        : ../captures/native-camera.jpg
```

`captures/native-camera.jpg` 已成功生成，格式为 JPEG，分辨率为 `640x480`。

补充验证：YUYV 格式可以协商成功，但在当前 WSL USB/IP 摄像头环境下 DQBUF 超时。这个现象说明“格式枚举 / S_FMT 成功”不等于该格式在当前设备传输环境中一定能稳定出帧。当前 Phase 3 小闭环以 MJPEG 路径作为已验证采集路径。
