# 09 V4L2 摄像头采集

## 面试官可能怎么问

1. Linux 下摄像头采集怎么做？
2. V4L2 的 mmap buffer 流程是什么？
3. MJPEG 和 YUYV 有什么区别？
4. 摄像头采集和 WebRTC getUserMedia 有什么关系？

## 一句话回答

Linux 摄像头采集通常通过 V4L2 完成，核心流程是查询设备能力、协商格式、申请 mmap buffer、QBUF 入队、STREAMON 开流、DQBUF 取帧，再根据 pixel format 送入保存、转换、编码或 WebRTC 发送链路。

## 项目里怎么体现

`native/v4l2_capture` 实现了：

1. `/dev/video0` 设备打开。
2. `VIDIOC_QUERYCAP` 查询能力。
3. `VIDIOC_ENUM_FMT` 枚举 MJPEG / YUYV 等格式。
4. `VIDIOC_S_FMT` 设置采集格式和分辨率。
5. mmap buffer 申请和映射。
6. `VIDIOC_STREAMON` / `VIDIOC_DQBUF` 抓取一帧。
7. MJPEG 直接保存 JPG，YUYV 转 RGB 保存 PPM。

## 底层原理

V4L2 是 Linux 视频设备接口。摄像头驱动提供一组 buffer，应用通过 mmap 把 buffer 映射到用户态。采集时应用先把 buffer 交给驱动，驱动写入帧数据后，应用再把完成的 buffer 取出来。

每个 `v4l2_buffer` 都包含重要 metadata：

1. `bytesused`：实际帧数据大小。
2. `sequence`：帧序号。
3. `timestamp`：采集时间戳。
4. `index`：buffer 队列位置。

## 和我过往经验的连接

这一步和之前做实时预览、硬编硬解、RTMP 推流的采集入口是一类问题。后续无论是 MediaCodec、VideoToolbox、FFmpeg encoder 还是 libwebrtc，都需要稳定的帧源、清晰的时间戳和可控的 buffer 生命周期。

## 常见坑

1. 设备不存在：WSL 摄像头没有 attach，或 `/dev/video0` 权限不足。
2. 格式不支持：摄像头不支持请求的 pixel format / resolution。
3. DQBUF 超时：没有 STREAMON，设备被占用，或 buffer 没有正确入队。
4. YUYV 不能直接保存成 JPG：需要先做颜色空间转换或编码。

## 可继续深入

1. 连续采集线程和环形队列。
2. timestamp 映射到 WebRTC RTP timestamp。
3. YUYV / MJPEG 转 I420 / NV12。
4. 接入编码器或 native WebRTC VideoTrackSource。
