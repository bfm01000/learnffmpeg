# 001 摄像头预览验证

## 本次目标

确认 WSL 中的摄像头不仅能被系统识别，还能被 FFmpeg 工具链实际采集和预览，为后续 WebRTC 摄像头采集、编码参数实验和端到端链路验证打基础。

## 做了什么

新增 `scripts/preview_camera.sh`，用于通过 `ffplay` 打开 `/dev/video0` 的实时预览画面。脚本默认使用 `640x480`、`mjpeg`，也支持通过参数指定设备、分辨率和输入格式。

## 遇到的问题

前一步检查时，WSL 起初没有直接暴露 `/dev/video*`。后续确认 Windows 侧摄像头已经通过 `usbipd-win` attach 到 WSL，Linux 内核也加载了 `uvcvideo`、`videodev` 等模块。

## 怎么排查

通过以下信息确认链路：

1. `usbipd list` 看到 `Integrated Camera` 处于 attached 状态。
2. WSL 中出现 `/dev/video0` 和 `/dev/video1`。
3. `lsmod` 看到 `uvcvideo`、`videodev`、`videobuf2`。
4. `ffmpeg -f v4l2 -list_formats all -i /dev/video0` 能列出 MJPEG / YUYV 格式和多个分辨率。
5. FFmpeg 成功从 `/dev/video0` 抓取一帧 JPEG 图片。

## 怎么解决

将 WSL 用户 `bfm01000` 加入 `video` 组，解决普通用户访问 `/dev/video0` 的权限问题。然后使用 `ffplay` 直接读取 V4L2 摄像头设备进行画面预览。

## 技术取舍

当前阶段优先使用 `ffplay` 做最小验证，因为它能直接走 V4L2 采集链路，不需要先写 C++ 或 WebRTC 代码。这样可以先把硬件、驱动、权限和采集格式问题排干净，再进入浏览器 WebRTC 或 native libwebrtc 阶段。

## 涉及的关键知识点

1. WSL2 USB/IP 设备透传。
2. UVC 摄像头驱动。
3. V4L2 设备节点。
4. FFmpeg / ffplay 摄像头采集。
5. MJPEG 与 YUYV 输入格式。
6. Linux `video` 用户组权限。

## 和我过往经验的连接

这一步对应实时音视频链路中的“采集入口”。它和之前做相机实时预览、RTMP 推流、硬编硬解链路类似：第一步不是急着调上层 API，而是先确认设备、格式、权限和基础采集是否稳定。后续 WebRTC 项目的 getUserMedia、native camera capture 或 FFmpeg 输入源都可以沿用这种排查思路。

## 面试讲法

我在做 WebRTC 项目前，先把 WSL 摄像头采集链路打通。因为实时音视频问题经常不是单点 API 问题，而是设备、驱动、权限、采集格式和后续编码传输共同影响。我先确认 Windows 摄像头通过 USB/IP attach 到 WSL，再检查 UVC / V4L2 设备节点和用户权限，最后用 FFmpeg/ffplay 验证真实采集。这样后面如果 WebRTC 画面异常，我能快速区分是采集层问题、编码层问题，还是传输和渲染层问题。

## 后续可扩展

1. 增加摄像头格式枚举脚本。
2. 增加抓帧脚本，用于保存采集样本。
3. 接入 WebRTC 浏览器端 getUserMedia，对比浏览器采集和 WSL V4L2 采集差异。
4. 后续 native 阶段可以用 FFmpeg 或 V4L2 API 采集摄像头帧，再研究如何送入编码器或 WebRTC 发送链路。

## 本次补充：保存 JPG 文件

在预览脚本基础上，新增 `scripts/capture_camera_jpg.sh`，用于从 `/dev/video0` 抓取单帧并保存为 JPG 文件。当前已验证生成 `captures/camera-test.jpg` 成功，图片格式为 JPEG，分辨率为 `640x480`。

这一步的价值是把摄像头链路从“能枚举、能预览”推进到“能稳定拿到一帧图像数据”。后续如果接入 WebRTC 或 FFmpeg/native 采集链路，可以先用这张 JPG 和抓帧脚本判断问题是否发生在采集层。
