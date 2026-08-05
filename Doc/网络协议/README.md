# 网络协议与音视频传输学习索引

## 一、面试复习路径

1. [音视频网络协议面试指南](./音视频网络协议面试指南.md)：先建立 TCP/UDP/HTTP/RTMP/RTP/WebRTC/HLS 的全局对比。
2. [TCP协议深度解析与面试指南](./TCP协议深度解析与面试指南.md)：补齐三次握手、可靠性、滑动窗口、拥塞控制、队头阻塞和排障工具。
3. [RTMP协议深度解析与面试指南](./RTMP协议深度解析与面试指南.md)：深入 Chunk、Message、AMF、控制消息、服务端解析和抓包。
4. 回链到 [../直播/README.md](../直播/README.md) 和 [../ffmpeg/08-网络协议与流媒体.md](../ffmpeg/08-网络协议与流媒体.md)，把协议细节落到直播链路和 FFmpeg API。

## 二、详细学习路径

| 阶段 | 文档 | 重点 |
|---|---|---|
| 全局对比 | [音视频网络协议面试指南](./音视频网络协议面试指南.md) | TCP/UDP/HTTP/RTMP/RTP/WebRTC/HLS 的选型与面试口径 |
| TCP 底座 | [TCP协议深度解析与面试指南](./TCP协议深度解析与面试指南.md) | 可靠传输、重传、拥塞控制、Nagle、Keepalive、音视频弱网矛盾 |
| RTMP 字段级深挖 | [RTMP协议深度解析与面试指南](./RTMP协议深度解析与面试指南.md) | 握手、Chunk Header、Message、AMF、服务端、故障排查 |
| 直播封装 | [../直播/FLV与HTTP-FLV直播封装深入理解.md](../直播/FLV与HTTP-FLV直播封装深入理解.md) | FLV Tag、AVC/AAC sequence header、timestamp |
| RTP/WebRTC | [../ffmpeg/21-RTP详解.md](../ffmpeg/21-RTP详解.md) + [../直播/WebRTC从浅入深：实时音视频架构与面试指南.md](../直播/WebRTC从浅入深：实时音视频架构与面试指南.md) | RTP 时间戳、RTCP、NACK/FEC、JitterBuffer、拥塞控制 |

## 三、职责边界

| 目录 | 负责什么 | 不重复什么 |
|---|---|---|
| `网络协议/` | TCP/RTMP/通用网络协议的底层机制、字段、抓包和排障 | 不展开移动端采集编码细节 |
| `直播/` | 直播链路、业务选型、RTMP/HTTP-FLV/WebRTC 直播口述 | 不重复 TCP 基础和 Chunk 字段级解析 |
| `ffmpeg/` | FFmpeg API 使用和跨平台音视频主线 | 不重复协议字段长篇讲解 |
| `cpp/` | 发送线程、队列背压、连接生命周期、资源封装 | 不重复协议规范 |

## 四、面试答题抓手

- TCP 题不要只背三次握手，要能落到音视频：队头阻塞、重传放大延迟、Nagle、发送缓冲区和弱网退化。
- RTMP 题分三层讲：连接命令层、Chunk/Message 传输层、FLV/H.264/AAC 负载层。
- WebRTC 题强调“为实时互动设计”：UDP/RTP、JitterBuffer、NACK/FEC、GCC、动态码率和 SFU。