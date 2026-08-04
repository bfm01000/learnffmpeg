# 004 摄像头不可用时的 Test Pattern Fallback

## 本次目标

解决浏览器页面点击 `Join` 后出现 `Requested device not found` 的问题，让 WebRTC MVP 不被当前浏览器摄像头环境卡住。

## 做了什么

在页面控制区新增 `Source` 下拉框，支持 `Camera` 和 `Test Pattern` 两种视频源。

修改 `public/app.js`：

1. 加入 `enumerateDevices()`，在请求摄像头前记录浏览器可见的视频输入设备数量。
2. 保留浏览器摄像头作为默认输入。
3. 当 `getUserMedia()` 失败时，记录错误并自动 fallback 到 Canvas 生成的动态测试画面。
4. 使用 `canvas.captureStream(frameRate)` 生成 MediaStream，继续走同一条 WebRTC 发送链路。
5. 离开房间时清理测试画面定时器，避免资源泄漏。

## 遇到的问题

页面日志显示 `Requested device not found`。这说明当前浏览器环境没有拿到可用摄像头设备，或者浏览器进程不支持访问该摄像头。这个问题和 WSL 中 `/dev/video0` 是否可用不是同一层：WSL 的 FFmpeg 能采集，不代表浏览器 getUserMedia 一定能看到设备。

## 怎么排查

先确认 WSL 采集链路已经通过：`/dev/video0` 存在，FFmpeg 能列格式，也能保存 JPG。然后把问题范围收窄到浏览器采集层，也就是 `navigator.mediaDevices.getUserMedia()`。

## 怎么解决

没有强行把浏览器问题和 WebRTC 传输问题绑在一起，而是增加 Test Pattern fallback。这样即使摄像头不可用，也能继续验证 signaling、offer / answer、ICE candidate、RTCPeerConnection、RTP 发送和 getStats。

## 技术取舍

Test Pattern 不是最终采集方案，但它是实时音视频工程里很有价值的隔离手段。它把采集设备变量拿掉，让我们先验证传输链路。如果 Test Pattern 能通而 Camera 不通，问题就在采集权限、设备枚举或浏览器环境；如果 Test Pattern 也不通，才继续排查信令、ICE 或 PeerConnection。

## 涉及的关键知识点

1. `getUserMedia()` 摄像头采集。
2. `enumerateDevices()` 设备枚举。
3. `canvas.captureStream()` 生成测试视频流。
4. WebRTC 采集层和传输层隔离。
5. MediaStream / MediaStreamTrack。

## 和我过往经验的连接

这和做 RTMP 或实时预览时使用测试源、假帧、色条源定位问题是一样的。先用可控输入验证后续链路，再回头排查真实设备采集。这样能快速区分是采集层、编码层、传输层还是渲染层问题。

## 面试讲法

我在 MVP 验证时遇到浏览器报 `Requested device not found`。我没有直接把它当成 WebRTC 建连失败，而是先区分采集层和传输层。WSL 侧 FFmpeg 已经能采集 `/dev/video0`，所以我在浏览器端加了 Canvas Test Pattern fallback，用 `canvas.captureStream()` 生成一个可控视频流继续走 RTCPeerConnection。这样如果测试画面能通，就说明信令、SDP、ICE 和 RTP 发送链路没问题，摄像头问题可以单独排查。

## 后续可扩展

1. 增加摄像头设备列表选择。
2. 在 UI 中显示 getUserMedia 错误类型和排查建议。
3. 增加固定色条、运动图案、时间戳叠加，用于端到端延迟测量。
4. 后续 native 阶段增加 FFmpeg / V4L2 测试源，和浏览器 Test Pattern 对齐。
