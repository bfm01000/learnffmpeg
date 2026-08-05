# 直播与实时音视频学习索引

## 一、面试复习路径

1. [RTMP直播协议深入理解与面试指南](./RTMP直播协议深入理解与面试指南.md)：先建立 RTMP 推流链路、低延迟来源、弱网问题和 FFmpeg 推流流程。
2. [FLV与HTTP-FLV直播封装深入理解](./FLV与HTTP-FLV直播封装深入理解.md)：补齐 FLV Tag、H.264/AAC 封装、HTTP-FLV 低延迟播放和转封装边界。
3. [WebRTC从浅入深：实时音视频架构与面试指南](./WebRTC从浅入深：实时音视频架构与面试指南.md)：理解超低延迟 RTC、ICE/STUN/TURN、RTP/RTCP、弱网对抗和 SFU 架构。
4. 回到 [../ffmpeg/08-网络协议与流媒体.md](../ffmpeg/08-网络协议与流媒体.md) 和 [../ffmpeg/12-RTMP推流详解.md](../ffmpeg/12-RTMP推流详解.md)，把协议知识接到 FFmpeg API。

## 二、深入学习路径

| 阶段 | 文档 | 重点 |
|---|---|---|
| 直播入口 | [RTMP直播协议深入理解与面试指南](./RTMP直播协议深入理解与面试指南.md) | 推流 URL、握手、命令交互、Message/Chunk、弱网和延迟 |
| 封装细节 | [FLV与HTTP-FLV直播封装深入理解](./FLV与HTTP-FLV直播封装深入理解.md) | FLV Header/Tag、AVC/AAC sequence header、timestamp、HTTP-FLV |
| RTC 架构 | [WebRTC从浅入深：实时音视频架构与面试指南](./WebRTC从浅入深：实时音视频架构与面试指南.md) | SDP、ICE、DTLS-SRTP、RTP/RTCP、JitterBuffer、NACK/FEC、GCC、SFU |
| 协议底层 | [../网络协议/README.md](../网络协议/README.md) | TCP、RTMP Chunk 细节、网络排障 |
| 工程实现 | [../cpp/24-线程池与音视频流水线.md](../cpp/24-线程池与音视频流水线.md) | 队列背压、发送线程、慢客户端、seek/flush 状态机 |

## 三、职责边界

| 主题 | 主讲位置 | 本目录怎么用 |
|---|---|---|
| RTMP 业务链路和面试口述 | [RTMP直播协议深入理解与面试指南](./RTMP直播协议深入理解与面试指南.md) | 讲清推流、延迟、弱网、移动端编码器和 FFmpeg 推流 |
| RTMP 字段级协议细节 | [../网络协议/RTMP协议深度解析与面试指南.md](../网络协议/RTMP协议深度解析与面试指南.md) | 遇到 Chunk、AMF、服务端 parser、抓包再跳过去 |
| FLV/HTTP-FLV 封装 | [FLV与HTTP-FLV直播封装深入理解](./FLV与HTTP-FLV直播封装深入理解.md) | 负责直播封装语义和转封装 |
| FFmpeg API 操作 | [../ffmpeg/12-RTMP推流详解.md](../ffmpeg/12-RTMP推流详解.md) | 负责 `avformat_alloc_output_context2`、`av_interleaved_write_frame` 等 API 模板 |
| WebRTC 项目实战 | `../../project/14_WebRTC_Interview_Project/` | 本目录讲知识体系，项目目录讲可运行工程和开发记录 |

## 四、面试答题抓手

- 先按“编码格式 -> 封装格式 -> 传输协议 -> 网络传输层”分层，不要把 RTMP、FLV、H.264、TCP 混成一团。
- RTMP/HTTP-FLV/HLS/WebRTC 对比时，围绕延迟、可靠性、弱网、浏览器兼容、CDN 生态和实现复杂度回答。
- 中高级回答要带工程细节：队列堆积、慢客户端、GOP cache、首帧、SPS/PPS、timestamp、码率自适应和抓包排障。