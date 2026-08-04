# WebRTC 播放支持 — 设计方案

> 状态: 草案，待讨论
> 日期: 2026-08-03

---

## 1. 背景

WebRTC 与传统播放（文件/RTMP/RTSP）有本质区别：

| 特性 | 传统播放 | WebRTC |
|------|---------|--------|
| 传输模式 | Client-Server | Peer-to-Peer |
| 信令 | 无需 | SDP + ICE 协商 |
| 连接建立 | `open(url)` 直接连接 | 需要信令服务器交换 SDP |
| 加密 | 可选 | 强制 DTLS + SRTP |
| 媒体格式 | 任意 | 仅 VP8/VP9/H.264 + Opus |
| NAT 穿透 | 不需要 | ICE (STUN/TURN) |

---

## 2. 总体方案：GStreamer + webrtcbin 作为外部模块

选用 **GStreamer** 而非自建 WebRTC 协议栈的理由：
- `webrtcbin` 是成熟的开源 WebRTC 实现（Google 主导）
- 内置 SDP 解析/生成、ICE 协商、DTLS 握手
- 支持 WHIP/WHEP 标准简化信令（WebRTC-HTTP Ingestion Protocol）
- 可独立于播放器升级

架构：

```
┌─────────────────────────────────────────────────┐
│                  Player API                      │
│  open("whip://signaling.example.com/stream")     │
├─────────────────────────────────────────────────┤
│              IMediaSource (新增)                  │
│  ┌──────────────────────────────────────────┐   │
│  │         WebRTCSource                      │   │
│  │                                           │   │
│  │  ┌─────────┐  ┌──────────────────────┐   │   │
│  │  │ WHIP    │  │  GStreamer Pipeline    │   │   │
│  │  │ Client  │  │                        │   │   │
│  │  │ (HTTP)  │  │  webrtcbin → decodebin │   │   │
│  │  │         │  │     ↓                  │   │   │
│  │  │ SDP     │  │  appsink(video)        │   │   │
│  │  │ offer   │  │  appsink(audio)        │   │   │
│  │  └─────────┘  └──────────────────────┘   │   │
│  └──────────────────────────────────────────┘   │
│                      │                          │
│                      │ AVPacket (video/audio)    │
│                      ▼                          │
│              PacketQueue → Decoder → ...         │
└─────────────────────────────────────────────────┘
```

---

## 3. 实施计划（5 个阶段）

### Phase 1: 依赖评估与环境搭建

- 安装 GStreamer 开发包：`libgstreamer1.0-dev`, `libgstreamer-plugins-bad1.0-dev`（含 webrtcbin）
- 安装 libsoup（HTTP 信令）：`libsoup-3.0-dev`
- 验证：用 `gst-launch-1.0` 命令行测试 WHIP 连接
- 在 CMake 中通过 `pkg-config` 查找 `gstreamer-webrtc-1.0`

**预计耗时**: 0.5 天

### Phase 2: WHIP 信令客户端

WHIP（WebRTC-HTTP Ingestion Protocol）是 IETF 标准，简化信令流程：

```
Client                          Server (WHIP endpoint)
  │                                    │
  │── POST /whip/endpoint ────────────►│  (SDP offer)
  │◄─ 201 Created + SDP answer ────────│
  │                                    │
  │── PATCH /whip/endpoint ───────────►│  (ICE candidates)
  │                                    │
  │◄─────── Media (SRTP) ─────────────►│
```

实现一个轻量 HTTP 客户端类：
- `WhipClient::sendOffer(sdp)` → SDP answer
- `WhipClient::sendCandidates(candidates)`

**预计耗时**: 1 天

### Phase 3: GStreamer WebRTC Pipeline

构建 GStreamer pipeline：

```cpp
pipeline = gst_parse_launch(
    "webrtcbin name=webrtc "
    "webrtc. ! decodebin ! videoconvert ! appsink name=video_sink "
    "webrtc. ! decodebin ! audioconvert ! audioresample ! appsink name=audio_sink",
    nullptr);
```

关键回调：
- `on-negotiation-needed` → 创建 SDP offer → 发送到 WHIP endpoint
- `on-ice-candidate` → 收集 ICE candidates → 发送到 WHIP endpoint
- `pad-added` → 链接新的媒体 track 到 decodebin

封装为 `WebRTCSource` 类：

```cpp
class WebRTCSource {
public:
    int  open(const char* whipUrl);     // 启动 pipeline + WHIP 握手
    std::shared_ptr<AVPacket> readPacket(); // 从 appsink 拉取数据
    int  close();                        // 停止 pipeline
};
```

**预计耗时**: 2 天

### Phase 4: 集成到 Player SDK

将 `WebRTCSource` 实现 `IMediaSource` 接口，接入现有管线：

```cpp
// player_controller.cpp
if (isWebRTC(url)) {
    m_source = std::make_unique<WebRTCSource>();
} else {
    m_source = std::make_unique<FFmpegDemuxer>();
}
m_source->open(url);
```

新增 URL scheme：`whip://`（自动使用 WHIP 信令）

**预计耗时**: 1 天

### Phase 5: 测试与优化

- 使用公开 WHIP 测试服务验证（如 Cloudflare Stream、Janus Gateway）
- 网络抖动下的缓冲策略
- ICE 超时重连
- 音频/视频 track 动态增减

**预计耗时**: 2 天

**总预计**: 约 6.5 个工作日

---

## 4. 技术风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| GStreamer API 版本不兼容 | 高 | 锁定 webrtcbin 1.24+ API |
| WHIP 服务端差异 | 中 | 先对接公开测试服务验证 |
| ICE 穿透需要 TURN 服务器 | 中 | 默认使用 STUN，TURN 作为配置项 |
| appsink 到 AVPacket 转换开销 | 低 | GStreamer buffer 可直接映射到 FFmpeg |
| Pipeline 状态机调试 | 中 | 用 gst-launch 先验证再写代码 |

---

## 5. 不做的事情

- ❌ 从零实现 WebRTC 协议栈（SDP/ICE/DTLS/SRTP）——用 GStreamer
- ❌ 实现 WebRTC 推流（编码+发送）——仅播放端
- ❌ 支持 DataChannel
- ❌ 支持 Simulcast/SVC
- ❌ 自建信令服务器——用 WHIP 标准，对接已有服务

---

## 6. 讨论要点

1. **优先级**：WebRTC 是否当前最高优？还有不少 P2/P3 任务（ColorConverter、FilterGraph、字幕、OpenGL 接入编译）
2. **依赖范围**：引入 GStreamer 意味着额外的系统依赖。可接受吗？
3. **URL scheme**：用 `whip://` 还是直接 `webrtc://` + 配置信令方式？
4. **仅播放**：确认只需要播放端（接收），不需要推流（发送）？
