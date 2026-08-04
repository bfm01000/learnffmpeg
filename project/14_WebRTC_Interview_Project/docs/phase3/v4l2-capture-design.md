# V4L2 Capture 设计说明

## 目标

`native/v4l2_capture` 是 Phase 3 的第一个 native 小闭环：用 C++ 直接访问 Linux V4L2 摄像头设备，枚举采集能力，并抓取一帧保存到文件。

## 采集流程

```text
open /dev/video0
  -> VIDIOC_QUERYCAP
  -> VIDIOC_ENUM_FMT / FRAMESIZES / FRAMEINTERVALS
  -> VIDIOC_S_FMT
  -> VIDIOC_REQBUFS
  -> VIDIOC_QUERYBUF
  -> mmap
  -> VIDIOC_QBUF
  -> VIDIOC_STREAMON
  -> select
  -> VIDIOC_DQBUF
  -> copy frame data
  -> write jpg / ppm
  -> VIDIOC_STREAMOFF
  -> munmap
```

## 当前支持

1. `--list` 枚举摄像头格式、分辨率和 fps。
2. `--format mjpeg` 抓取 MJPEG 并直接保存为 JPG。
3. `--format yuyv` 抓取 YUYV422，转换成 RGB 后保存为 PPM。
4. 打印 frame metadata：`sequence`、`bytes_used`、`timestamp_us`、`pixel_format`。`r`n`r`n当前环境验证结果：MJPEG 抓帧已通过；YUYV 可以协商格式，但在 WSL USB/IP 摄像头环境下采集超时，暂不作为已验证路径。

## 为什么使用 mmap

V4L2 支持 read、mmap、userptr 等 I/O 模型。当前选择 mmap，因为它是 Linux 摄像头实时采集中常见的方式：驱动分配 buffer，用户态 mmap 到进程地址空间，采集时通过 QBUF / DQBUF 在驱动和应用之间流转 buffer。

这比 read 更接近真实实时链路，也更容易扩展到连续采集线程和后续编码输入队列。

## 和 WebRTC 的关系

浏览器 `getUserMedia` 隐藏了摄像头采集细节。native WebRTC sender 最终也需要一个帧源，这个帧源可能来自：

1. V4L2 摄像头。
2. FFmpeg 文件解码。
3. 硬件采集卡。
4. 测试图案生成器。

V4L2 Capture Demo 对应 WebRTC native sender 的采集入口。后续要接入 WebRTC，需要继续解决像素格式转换、时间戳映射、线程模型、编码路径和 RTP 发送。

## 面试表达重点

可以重点讲：

1. 我没有只停留在浏览器 API，而是向下打通 Linux 摄像头采集。
2. V4L2 的核心是设备能力查询、格式协商、buffer 队列和流开关。
3. 一帧视频不是凭空出现的，它有 pixel format、bytesused、sequence 和 timestamp。
4. timestamp 和 buffer 生命周期会影响后续编码、同步、延迟和抖动分析。

## 后续扩展

1. 连续采集线程。
2. YUYV / MJPEG 到 I420 / NV12 转换。
3. 接入 FFmpeg 编码器。
4. 抽象 `FrameSource`，对齐后续 native WebRTC sender。

