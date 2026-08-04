# Dev Log 016 - Native libwebrtc Sender 设计

## 本次目标

进入 native libwebrtc sender 前，先输出完整设计图和实施边界，避免直接陷入 libwebrtc 构建复杂度。

## 做了什么

1. 调研 WebRTC Native APIs 的基本调用模型。
2. 确认 native sender 第一版优先走 Raw Frame -> libwebrtc 的路线。
3. 明确前一步 H.264 RTP packetizer 的定位：学习和解释模块，不作为第一版发送主路径。
4. 设计 native sender 的模块拆分：signaling、PeerConnection app、custom video source、frame producer、frame clock。
5. 设计三阶段媒体输入：synthetic I420、FFmpeg decode FRXXZ.mp4、V4L2 camera。
6. 写入 Phase 3 设计文档和面试讲解文档。

## 调研依据

参考了 WebRTC 官方 Native APIs 文档、development 文档、`PeerConnectionInterface` 头文件说明和官方 peerconnection 示例目录。

## 关键取舍

### 为什么第一版不用 encoded H.264 path

encoded path 更贴近 H.264 经验，但 native libwebrtc 的 encoded frame 注入路径和浏览器互通限制更复杂。第一版项目目标是跑通 native 到 browser 的最小闭环，因此优先用 raw I420 frame，让 libwebrtc 自己完成编码和 RTP/RTCP。

### 为什么第一版不用 V4L2 摄像头

WSL USB/IP 摄像头已经验证 MJPEG 抓图可用，但连续采集可能不稳定。为了减少变量，第一版先用 synthetic frame；一旦 sender 成功，再替换为 FFmpeg 解码素材和 V4L2 摄像头。

### 为什么保留 RTP packetizer

RTP packetizer 是解释工具。它可以证明我理解 H.264 over RTP 的 single NALU、FU-A、timestamp、sequence number 和 marker bit，但完整 WebRTC 发送仍应交给 libwebrtc 的传输栈。

## 遇到的问题

这一步没有进入编译实现，因此没有新的构建问题。主要风险是 libwebrtc checkout 和 build 成本较高，需要下一步单独评估本机环境、磁盘和网络条件。

## 面试可讲点

我把 native libwebrtc sender 设计成可替换输入源的结构。先 synthetic frame 验证 PeerConnection 和浏览器接收，再替换成 FFmpeg 解码素材，最后接 V4L2 摄像头。这样能体现我做复杂音视频系统时，会先收敛变量、建立最小闭环，再逐步替换真实模块。

## 后续

下一步建议先检查本机是否已经有 libwebrtc checkout、depot_tools、ninja、gn 等环境。如果没有，再决定是否拉取官方源码，或先用官方 peerconnection_client 验证构建链路。
