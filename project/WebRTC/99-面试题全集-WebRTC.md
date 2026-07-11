# WebRTC 面试题全集

> 从 `project/WebRTC/` 下所有文档中提取的 WebRTC 相关面试 Q&A，按主题模块组织。
> 共 **49 道有标准答案的题 + 33 道待补充答案的面试视角问题 + 7 条面试金句 + 3 道白板题**。
>
> 每个答案标注来源文档，方便回顾上下文。

> **另见**：`99-面试题全集-音视频基础.md`（音视频基础 40 题）

---

## 目录

- [一、RTP 传输模块（8 题）](#一rtp-传输模块8-题)
- [二、JitterBuffer 模块（7 题）](#二jitterbuffer-模块7-题)
- [三、SDP 协议（5 题 + 自检题）](#三sdp-协议5-题--自检题)
- [四、NTP 与 RTCP SR（5 题）](#四ntp-与-rtcp-sr5-题)
- [五、QoS 四驾马车（12 题）](#五qos-四驾马车12-题)
- [六、项目面试讲述（10 题）](#六项目面试讲述10-题)
- [七、白板题（3 题）](#七白板题3-题)
- [八、无答案的面试视角问题（33 题）](#八无答案的面试视角问题33-题)
- [九、面试金句汇总](#九面试金句汇总)

---

## 一、RTP 传输模块（8 题）

> 来源：`06-M4-RTP传输模块.md`

### Q1. 一个 1080p 视频帧从编码到 RTP 发送经过哪些步骤？

**口语化标准答案**：编码器把一帧 YUV 变成 H264 NALU 流（含 SPS、PPS、IDR/P 帧主体），然后交给 RtpPacketizer 决定打包模式——小 NALU 像 SPS/PPS 通常用 STAP-A 聚合在一个包，大 NALU 像 IDR 主体超过 MTU（典型 1200 字节）就走 FU-A 分片成几十个包。每个 RTP 包打上头部：版本 2、payload type 从 SDP 协商出来、SSRC 唯一标识这路流、SeqNum 单调递增、Timestamp 是同一帧共享一个值（90kHz 基）、Marker 位只在帧的最后一个 RTP 包设 1。Packetizer 用迭代器模式逐个吐包给 Pacer，Pacer 按拥塞控制反馈的码率节流交给 SRTP 加密、走 ICE 选好的 UDP 通道发出去。

### Q2. RTP 头 12 字节里你能记住的字段有哪些？

**口语化标准答案**：12 字节固定头：第 1 字节是版本号 + 几个标志位（V=2, P padding, X 扩展, CC csrc 数）；第 2 字节是 marker + payload type；第 3-4 字节是 sequence number；第 5-8 字节是 timestamp；第 9-12 字节是 SSRC。后面如果 CC > 0 还跟 CSRC 列表，X = 1 还跟扩展头。最常考的是 marker（视频里标记帧的最后一个包）、SeqNum（接收端排序和丢包检测）、Timestamp（同一帧所有包共享、跨流靠 RTCP SR 对齐）、SSRC（唯一标识一路流）。

### Q3. FU-A 分片为什么不能用简单的 "总字节数除以最大包大小" 切？

**口语化标准答案**：简单除法切的话，最后一片可能极小——比如 100KB NALU 除以 1200 字节得 84 片，前 83 片各 1200 字节、最后 1 片只剩 400 字节，带宽利用率低。WebRTC 的做法是先算总片数（向上取整除法），然后用"平均字节数 = 总字节 / 总片数 + 向上取整"分配，让每片尽量满，最后一片不会浪费。这样总片数还是一样，但每个包都接近 MTU 上限，发送效率最高。

### Q4. 接收端怎么判定一帧的边界？

**口语化标准答案**：靠 RTP 头的 Marker 位 + Timestamp。同一帧的所有 RTP 包共享同一个 Timestamp，**Marker = 1 的包是这一帧的最后一个 RTP 包**。接收端两种判定方式：① 看 Marker 位（最直接）；② 看 Timestamp 变化（下一个包的 Timestamp 和当前不一样，说明换帧了）。Jitter Buffer 用这两条规则组合：等到 Marker = 1 的包到达、且这一帧所有 SeqNum 连续没断层时，判定帧完整可解码。

### Q5. 为什么 RTP 用 UDP 不用 TCP？

**口语化标准答案**：核心是实时性 vs 可靠性的取舍。① TCP 有队头阻塞——丢一个包后续包必须等重传到达才能往上层投递，实时通信里这会让所有后续帧都跟着卡；UDP 没这个约束，应用层自己决定丢包恢复策略。② TCP 拥塞控制偏保守——检测到丢包就降速 50%，不适合视频流"宁可糊一会儿也要保住带宽"的需求；WebRTC 用 GCC/BBR 自己做拥塞控制，能更激进。③ 重传 vs 跳过的选择——实时通信里晚到的包等于没用，TCP 强行重传只让后续更晚；UDP 直接丢、用 FEC/NACK 选择性恢复关键帧。RTP/UDP 的可靠性是应用层补的：NACK 选择性重传、FEC 前向纠错、PLC 丢包隐藏。

### Q6. SSRC 冲突了怎么办？

**口语化标准答案**：SSRC 是 32 位随机数，同一会话内不同流必须不同。冲突场景：① 客户端启动时随机初始值碰撞（概率 1/2^32，极低）；② SFU 中转场景多路流汇合时被动碰撞。检测机制：每个端记录见过的 SSRC，收到陌生 RTP 包就更新 SSRC 表；如果发现自己发出的 SSRC 被别人复用了，就**自动切换到新的随机 SSRC** 并发一个 RTCP BYE 包通告旧 SSRC 终止。接收端看到 BYE 后清理对应的 Jitter Buffer 状态。

### Q7. 你的 B 层 RTP 实现和 libwebrtc 的差异在哪？

**口语化标准答案**：主要差异在简化范围。① 我只支持 H264，不支持 VP8/VP9/AV1，因为后者打包规则不同（VP8 用 picture ID + tl0picidx，VP9/AV1 用 scalability 结构）；② 不实现 RTX 重传通道，所以 NACK 重传场景下回退到普通 RTP 流；③ 扩展头只解析基本字段（abs-send-time、transport-cc seq），不支持完整的 RFC 8285 mixed one/two-byte 模式；④ SRTP 加密不做，让 libwebrtc 上层处理；⑤ Pacer 不做。这些简化让总代码量从 libwebrtc 的 1500+ 行降到 500 行，但**核心打包/解包路径（Single/STAP-A/FU-A）的字节输出和 libwebrtc 是字节对齐的**——我用 libwebrtc 的 RtpPacketizerH264 跑相同输入，对比两者输出的 RTP 包字节，验证正确性。

### Q8. 如果发送侧 Packetizer 和接收侧 Depacketizer 都正常工作，但解码端画面有花屏，可能是什么原因？

**口语化标准答案**：花屏意味着解码器收到了部分损坏的 NALU，常见 5 个原因：① **FU-A 重组逻辑错**——中间分片丢失但接收端没检测到 SeqNum 断层，把残缺数据拼接交给解码器；② **emulation prevention 字节没处理**——发送端漏写 0x03 转义、或接收端解析 NALU 时没去转义，导致 RBSP 数据偏移错位；③ **STAP-A 长度字段读错字节序**——把小端读成大端，NALU 边界全乱；④ **关键帧的 SPS/PPS 丢了**——解码器收到 IDR 但参数集缺失，瞎解一通；⑤ **时间戳不一致**——同一帧的多个 RTP 包 timestamp 不同，Jitter Buffer 误判为多帧。排查顺序：先 dump 发送端打的字节、再 dump 接收端解出的 NALU 字节、和原始 NALU 对比，差异在哪一步就定位到哪一层。

### Q9. 你的 Depacketizer 为什么只支持 Single NALU 和 FU-A，不做 STAP-A？

**口语化标准答案**：STAP-A 是把 SPS+PPS 等几个字节的小 NALU 用 2 字节长度前缀串在一起塞进一个 RTP 包——纯体力活，没有状态管理、没有乱序处理、没有断层检测，就是机械地按长度字段切分。**FU-A 才是面试官真正关心的难点**：一个大 NALU 拆成几十个包，接收端要维护重组状态（首片记录 NALU 类型、中间片拼 buffer、尾片标记完成），要处理乱序到达（SeqNum 不连续 → kSequenceGap）、跨 Timestamp 的包混入（时间戳不一致 → kInvalidPacket）、首片丢了但后续片还在到达（未启动重组收到中间片 → kSequenceGap）。这些才是体现工程能力的地方。

Depacketizer 的 `DepacketizeResult` 枚举只有 `kCompleteNalu / kFuaInProgress / kFuaCompleted / kInvalidPacket / kSequenceGap` 五种返回值，STAP-A 包到达直接返回 `kInvalidPacket`。如果后续要加 STAP-A，关键是**返回值语义要变**——一个 RTP 包能解出多个 NALU，`PopCompletedNalu` 的队列模型要从「一包一 NALU」改成「一包多 NALU」。B 层暂时跳过，生产环境补上即可。

---

## 二、JitterBuffer 模块（7 题）

> 来源：`07-M6-JitterBuffer模块.md`

### Q1. 什么是 Jitter Buffer？为什么 WebRTC 需要它？

**口语化标准答案**：Jitter Buffer 是接收侧的"网络抖动吸收器"——发送端按固定节奏发，但经过网络后到达间隔变得忽快忽慢，如果直接喂解码器会卡顿。JitterBuffer 在接收端主动延迟 N 毫秒（典型 50-200ms），把"乱序、突发"的到达整理成"有序、节奏稳定"的输出。**核心矛盾**是缓冲深度 vs 端到端延迟：深度越大抗抖越强但延迟越高，所以要动态估计当前抖动来调整深度。视频和音频都需要，但实现差异很大——视频可以丢帧、可以快放；音频不能，所以音频用更复杂的 NetEQ。

### Q2. 怎么判定一帧"完整可解码"？

**口语化标准答案**：三个条件同时满足：① 该帧所有 RTP 包都已到达（SeqNum 连续不断层）；② 该帧的 Marker 位包已到（标识帧的最后一个 RTP 包）；③ 该帧的所有包共享同一个 RTP Timestamp。但单纯靠 Marker 在丢包场景会卡死——Marker 包本身丢了就永远等不到。所以 libwebrtc 还有一个**间接判定**：如果下一帧的首包已到达（说明本帧网络层已发完），即使没收到 Marker 也能推断本帧范围。完整后帧塞给 FrameBuffer 做参考帧链分析，确认参考帧都齐才标记"可解"。

### Q3. 目标延迟（Target Delay）怎么算？为什么是 3 倍抖动？

**口语化标准答案**：TargetDelay 由三部分组成：抖动延迟 + 解码延迟 + 渲染延迟。抖动延迟用 3 倍当前抖动估计——假设抖动服从正态分布，3σ 覆盖 99.7% 的样本，只有 0.3% 的极端抖动会"漏过"，这部分通过 NACK 重传兜底。**典型数值**：局域网抖动 5ms → TargetDelay ≈ 35ms；4G 抖动 30ms → TargetDelay ≈ 110ms；弱网抖动 80ms → 260ms。解码延迟取历史 95 分位（避免被偶发的解码慢拖累），渲染延迟典型 10ms（显示器刷新一帧）。每收到一帧重算一次 TargetDelay 用于下一帧调度。

### Q4. EWMA 抗抖估计和 Kalman 滤波的区别？你的实现选哪个？

**口语化标准答案**：EWMA 是指数加权移动平均，每帧到达更新一次估计——`new = 0.95 × old + 0.05 × sample`，实现 10 行。核心是理解 `sample` 怎么来的：**实际到达间隔减去期望到达间隔的绝对值**。实际间隔用本机 `steady_clock` 量（帧首包到达时刻 − 上一帧首包到达时刻），期望间隔用 RTP timestamp 算（两个帧的 RTP ts 差除以时钟频率）。二者之差就是网络抖动对这帧造成的额外延迟。α = 0.05 时时间常数约 20 帧——一次突变需要 ~1 秒才在估计值里反映出来，这就是 EWMA 的"慢"。Kalman 把抖动建模成"每字节延迟 + 队列延迟"两个状态变量的线性系统，用卡尔曼增益动态调整新观测的权重——网络稳定时信任历史、突发时快速跟踪。**两者差异**：稳态下偏差 < 5%；突发场景 Kalman 收敛快 5-10 倍。我的 B 层选 EWMA：代码量 1/10、面试讲得清楚、稳态准确度接近，**主动暴露突发慢一拍作为已知简化**——这是有意识的工程取舍而不是能力不足。

### Q5. 检测到丢包后是立刻发 NACK 还是等一会儿？

**口语化标准答案**：等一会儿——典型 50-200ms（一个 RTT）。原因：① 大多数 SeqNum 缺失其实是**乱序到达**（路由器多路径 + 排队），等一下可能自然到了，无脑发 NACK 会产生大量无效重传请求；② 等一会儿能**批量发 NACK**节省 RTCP 包头开销。代价是弱网场景"等一个 RTT"会增加恢复延迟。libwebrtc 用 deadline 队列让每个缺失 SeqNum 独立计时，到 deadline 才进入 NACK 批次。重传 10 次还不到就放弃，**触发关键帧请求**重置参考链。

### Q6. 什么时候请求关键帧（IDR Request）？

**口语化标准答案**：三个触发源——① **PacketBuffer 检测到连续 N 帧不完整**（典型 N=10），认为丢包率太高 NACK 难以恢复；② **解码器报错"参考帧丢失"**（CodecCallback ErrorCode），这是被动兜底；③ **应用层主动请求**（首次连接、网络恢复后重启播放）。请求方式是发 RTCP **PLI**（Picture Loss Indication，轻量）或 **FIR**（Full Intra Request，强制）给发送端。**触发条件要严苛 + 防抖**——一个关键帧体积是 P 帧的 5-10 倍，滥发会浪费带宽，所以典型加 2 秒冷却窗口（同一个间隔内多次触发只发一个请求）。

### Q7. 你的 B 层 JitterBuffer 实现和 libwebrtc 的差异在哪？

**口语化标准答案**：主要在简化范围。① **抗抖估计用 EWMA 而非 Kalman**——突发场景收敛慢 5-10 倍，稳态接近；② **不做参考帧链依赖分析**——只判定 RTP 层完整性，假设所有完整帧都可解（实际项目里 P 帧依赖 I 帧的链不能断，但简化版交给解码器自己处理 reference frame missing 错误回调）；③ **不区分关键帧重传优先级**——libwebrtc 在 NACK 列表里给 keyframe 范围内的 SeqNum 更高优先级，简化版按 FIFO 一视同仁；④ **不支持音频**（音频 NetEQ 是另一套机制，几万行代码）；⑤ **没有线程模型**——libwebrtc 的 JitterBuffer 涉及 network thread / decoder thread 跨线程同步，简化版假设单线程调用。这些简化让总代码量从 libwebrtc 的 2000+ 行降到 600 行，但**核心算法（环形 buffer + 完整性判定 + EWMA + 渲染调度）是和 libwebrtc 等价的**。

---

## 三、SDP 协议（5 题 + 自检题）

> 来源：`08-SDP真实样本解析.md`

### Q1. 为什么需要 `a=rtcp-mux`？

**答**：把 RTP 和 RTCP 复用一个 UDP 端口（不开两个端口）。优点：① 减少 ICE 打洞开销（只打一个洞）；② 简化 NAT 穿透（一个映射搞定）；③ 现代 WebRTC 标准强制开启。早期 RTP 协议规定 RTCP 用 RTP 端口+1，但 WebRTC 把它们 mux 到同一端口。

### Q2. `a=group:BUNDLE 0` 是干什么的？

**答**：BUNDLE 让多个媒体流（音频 + 视频 + DataChannel）**复用同一个 ICE/DTLS 传输通道**。优点：① 只打一次 NAT 洞；② 只做一次 DTLS 握手；③ 节省端口和带宽。本样本只有一个 `m=video`，BUNDLE 只列了 mid 0；如果有音频会变成 `BUNDLE 0 1`。**没有 BUNDLE 的话每路媒体要走独立 ICE/DTLS**，开销爆炸。

### Q3. `setup:actpass` 和 `setup:active` 的差异？谁先发 DTLS ClientHello？

**答**：DTLS 是 client-server 模型，必须明确角色。Offer 端通常给 `actpass` 表示"我都行"；Answer 端必须给 `active` 或 `passive` 明确角色——**`active` 的一端主动发 ClientHello**。WebRTC 实践中几乎都是 `offer:actpass / answer:active`，所以**真正发起 DTLS 握手的是 answer 端**。

### Q4. 一个 NALU 被分成多个 RTP 包传输（FU-A），接收端怎么从 SDP 知道发送端支持 FU-A？

**答**：看 H264 的 `a=fmtp:N` 里的 **`packetization-mode`** 字段：

- `packetization-mode=0` → 只支持 Single NALU 模式（小 NALU）
- `packetization-mode=1` → 支持 Single NALU / STAP-A / FU-A 三种（**你 M4 代码里写的 `kNonInterleaved`**）

样本里 H264 各 profile 同时列出了 mode=0 和 mode=1 两个 payload，让对端按需选择。

### Q5. 接收端怎么从 SDP 知道哪个 SSRC 是重传流？

**答**：通过 **`a=ssrc-group:FID <主SSRC> <重传SSRC>`** 显式声明。两个 SSRC 同 cname 但角色不同，主流的 payload 是 96/103/...，重传流的 payload 是 97/104/...，重传 payload 通过 `a=fmtp:N apt=M` 指向对应主 payload。**接收端解 NACK 重传包时**：① 看到 SSRC 是 RTX SSRC；② 从 SDP 知道它对应哪条主流；③ 还原后塞回主流的 Jitter Buffer。

### 自检题（无答案）

1. SDP 分哪两层结构？分界在哪？
2. 怎么从 SDP 判断浏览器首选的编码？为什么不能只看 `fmtp` 行下结论？
3. `m=video` 行 `UDP/TLS/RTP/SAVPF` 拆开是哪 4 层？SAVPF 里 S 和 F 各代表什么？
4. `rtcp-fb` 行里你能认出几种反馈机制？哪几个是你 M4/M6 代码里实现/触发过的？
5. `fingerprint` 是什么？为什么必须在 SDP 里提前给（不能 DTLS 握手时再给）？
6. `setup:actpass` 和 `setup:active` 的关系是什么？真实场景谁主动发起 DTLS 握手？
7. `a=ssrc-group:FID` 把两个 SSRC 配成什么对？为什么需要？

---

## 四、NTP 与 RTCP SR（5 题）

> 来源：`10-NTP与RTCP-SR详解.md`

### Q1. RTP timestamp 是一个相对值，接收端怎么知道什么时候播放？

> RTP timestamp 本身不告诉接收端"什么时候播"——它只告诉"这一帧和同一 SSRC 的前一帧隔了多久"。把 RTP ts 映射到真实时间，靠的是 RTCP SR。SR 里有一对 `(NTP timestamp, RTP timestamp)`——NTP 是发送端的墙上时钟，RTP ts 是同一个时刻该 SSRC 的媒体时间戳。接收端拿到 SR 后，就能用公式 `NTP_time = T_ntp + (rtp_ts - S_rtp) / clock_rate` 把任意 RTP ts 换算成 NTP 时间。换成同一 NTP 基准后，视频帧和音频帧就可以比较早晚，决定谁追谁。

### Q2. NTP 是什么？和 Unix 时间有什么区别？

> NTP 时间是网络上同步时钟的标准格式。起点是 1900 年 1 月 1 日（Unix 是 1970 年），用 64 位表示——高 32 位是整数秒，低 32 位是秒的小数部分。Unix 时间转 NTP 只要加 70 年的秒数（2208988800）。RTCP SR 里用 NTP 而不是 Unix 时间，纯粹是历史原因——RTP 标准制定时（1996）NTP 已经是网络领域通用的时钟格式。

### Q3. SR 和 RR 的区别？什么时候用哪个？

> SR 是发送媒体的一方发的，携带 NTP↔RTP 时间映射关系；RR 是只接收不发送的一方发的，只有接收统计（丢包率、抖动），没有时间映射。如果一方既在发又在收（WebRTC 通话场景的每一端），那就发 SR（既带自己的时间映射，也通过 Report Block 报告对方的接收统计）。核心：**有 RTP 流出去 → 发 SR；只在接收 → 发 RR**。

### Q4. 音频 48kHz 基、视频 90kHz 基，怎么把它们的 RTP ts 对齐？

> 各自用各自的 SR 算出 NTP 时间即可——两个 SSRC 各自独立发 SR，接收端存两套映射 `(video: T0 ↔ V0, audio: T0 ↔ A0)`。然后把当前要渲染的视频帧和音频帧各换算到同一个 NTP 时间线：`video_time = T0 + (V_current - V0) / 90000`，`audio_time = T0 + (A_current - A0) / 48000`。两个值在同一 NTP 时间线上，直接比较就能决定谁快谁慢。

### Q5. 接收端多久能拿到第一次 RTCP SR？如果 SR 丢了怎么办？

> 通常 0.5-5 秒发一次 SR，首帧可能没有 SR 可用。接收端的方法：先缓存前几个 RTP 包，等到第一个 SR 到，立即建立映射。如果 SR 中途丢了，下一个 SR（通常几秒内）到就可以重建映射——SR 的 NTP 和 RTP ts 都是新的，映射照样有效（只要 RTP 时间线没有 wraparound）。WebRTC 还会用 RTCP 的 `last SR timestamp (LSR)` 和 `delay since last SR (DLSR)` 字段来检测和计算 RTT。

---

## 五、QoS 四驾马车（12 题）

> 来源：`11-WebRTC-QoS四驾马车-GCC-FEC-NACK-JitterBuffer.md`

### Q1. WebRTC 怎么做拥塞控制？GCC 的全称和工作原理是什么？

**标准答案**：GCC = Google Congestion Control，分两个独立控制器。Delay-Based Controller 用 Trendline 趋势线——对每个 5ms 时间窗口内 `(到达间隔 - 发送间隔)` 做线性回归，斜率 > 阈值判 overuse 降码率、< 阈值判 underuse 升码率。Loss-Based Controller 用 AIMD——丢包率 < 2% 每次加 8%、2-10% 保持、> 10% 乘 0.5。两个控制器取 min 作为最终目标码率。码率通过三个路径生效：调编码器 QP、调 Pacer 节奏、调 FEC 冗余率。

---

### Q2. 为什么 GCC 有两个控制器？光看丢包不行吗？

**标准答案**：延迟是拥塞的**早期信号**。路由器队列开始积压时，包的到达间隔会越来越长——这个信号比丢包早出现 200-500ms。如果只看丢包，等于等到队列已经满了、开始丢了才反应，相当于用"火灾报警器"代替"温度计"。理想情况是 Trendline 先发现 overuse → 提前降码率 → 丢包根本不发生。Loss-Based Controller 是 Trendline 漏掉时的**兜底保险**。

---

### Q3. Trendline 的斜率怎么算？为什么用线性回归而不是简单平均？

**标准答案**：维护最近 20 个时间窗口（每个 5ms）的 (累积发送时间, 累积到达延迟) 数据点，**最小二乘线性回归**算斜率。用线性回归而不是简单平均是因为它过滤了单点噪声——无线网络偶尔有一个包的到达延迟突然飙高（原因不是拥塞，是 WiFi 重传），简单平均把这个假信号当真，导致码率剧烈震荡。线性回归对单点离群值**天然鲁棒**。

---

### Q4. FEC 和 NACK 的根本区别是什么？怎么选？

**标准答案**：**FEC 是用带宽换时间**——提前发冗余数据，丢了包在本地解方程恢复，零延迟但吃额外带宽。**NACK 是用时间换带宽**——丢了包再请求重传，精准补缺省带宽但等 1 RTT。选择策略：
- RTT < 50ms：优先 NACK（重传足够快，FEC 浪费）
- RTT 50-200ms：FEC 保护关键帧（IDR）+ NACK 补 P 帧
- RTT > 200ms：FEC 做主力、NACK 兜底
- 关键帧（SPS/PPS/IDR）：**必须 FEC 保护**——丢一个 SPS 等于后面的帧全废，等 NACK 来不及

**追问**："FEC 保护率怎么动态调？" → GCC 输出码率后，FEC Controller 检查当前码率 vs 需求——码率充裕就多加点冗余，码率紧张就减冗余甚至关 FEC（给媒体数据多留带宽）。

---

### Q5. XOR FEC 和 Reed-Solomon FEC 的区别？

**标准答案**：XOR FEC 做最简单的奇偶校验——A ⊕ B = F，丢了 A 就用 B ⊕ F 恢复，但只能恢复**恰好 1 个**丢包（丢 ≥2 个就恢复不了）。Reed-Solomon 是通用纠删码——在 GF(2^8) 上用范德蒙德矩阵，RS(n,k) 可以在 n 个包里丢任意 ≤ n-k 个都能恢复。**代价**：RS 计算量比 XOR 高一个量级。WebRTC 的 UlpFEC 默认用 XOR（因为 CPU 省、够用），FlexFEC 可以升级到 RS。

---

### Q6. NACK 为什么不是"丢一个发一个"？

**标准答案**：三个原因：① **乱序不是丢包**——路由器多路径导致顺序错乱占多数，真正的丢包是少数，等 1 RTT 给乱序自然到达的机会（5-10ms 就来了，不等就是白请求）；② **批量省带宽**——一个 RTCP NACK 消息携带 17 个连续缺失 seq（PID + BLP 位图），可以多个 PID+BLP 对放在一个包里；③ **避免 NACK 风暴**——弱网下如果发现缺一个就发一个，NACK 数量级瞬间爆炸，反而进一步拥堵上行。

---

### Q7. RTX 重传为什么必须用独立 SSRC？

**标准答案**：三个原因：① **SeqNum 区分**——原始流 ssr=1234 的 SeqNum 是连续递增的，如果重传包混进去，seq=99 的重传会被接收端当成"重复包"或"旧包"丢弃；② **丢包统计不乱**——RTX 的 SSRC 在 RTCP RR 里独立统计，不会扭曲原始流的丢包率计算；③ **SDP 声明独立**——`ssrc-group:FID` 把原始 SSRC 和 RTX SSRC 绑在一起，接收端知道这两个流是"同一路媒体"，可以关联处理。

---

### Q8. NACK 重传 10 次还不到怎么办？

**标准答案**：放弃这个 seq，触发关键帧请求。等不到不一定是网络问题——经常是发送端的 RtpPacketHistory 已经过期了（默认保存 1000ms）。RTT 300ms 下重传 10 次需要 3 秒，1 秒后发送端就删了这个包。此外，连续多个 seq 重传失败通常意味着**参考帧链断了**——P 帧依赖的 I 帧部分丢失，重传个别包也没用，必须请求 IDR 重启参考链。

---

### Q9. JitterBuffer 的 TargetDelay 是怎么定的？为什么是 3 倍抖动？

**标准答案**：`TargetDelayMs = 3 × EstimatedJitterMs + DecodeDelayMs + RenderDelayMs`。3σ 源于正态分布：假设抖动服从正态分布，3σ 覆盖 99.7% 的样本——只有 0.3% 的极端抖动会"漏过"（这部分通过 NACK 兜底）。典型数值：局域网抖动 5ms → 35ms delay；4G 抖动 30ms → 110ms；弱网 80ms → 260ms。

**追问**："抖动估计怎么更新？" → EWMA（指数加权移动平均）：每收到一帧，`new_jitter = 0.95 × old + 0.05 × |actual_interval - expected_interval|`。比简单平均更平滑，自动过滤单次尖峰。库函数版（libwebrtc）用 Kalman 滤波，把抖动建模成"每字节传输延迟 + 队列延迟"两个状态变量，突发响应更快但实现 500+ 行。

---

### Q10. 四驾马车中，哪个是"不管丢不丢包都在工作的"？

**标准答案**：**JitterBuffer**。FEC 只在有冗余包时才产出（而且是按帧批处理），NACK 只在检测到丢包时触发，GCC 以 ~1Hz 的频率调整——但 JitterBuffer **每收到一个 RTP 包都在判断**"这个包属于哪一帧、这帧完整了吗、什么时候该吐"。它是四驾马车中唯一**持续运行**的机制。

---

### Q11. 如果把四驾马车比作一个乐队，它们各自是什么角色？

**标准答案**：
- **GCC = 指挥**：根据观众的反馈（RTCP TWCC）调整乐队的整体节奏（码率），确保不越奏越快导致现场混乱（拥塞）
- **FEC = 备用乐器**：提前在台下放好备用小提琴，台上有人弦断了（丢包），立刻换备用琴（XOR 本地恢复），观众根本听不出
- **NACK = 谱务**：发现某页谱子缺了（丢了某个 seq），立刻从后台档案室（RtpPacketHistory）找出来补上（重传）
- **JitterBuffer = 混音台/监听**：把各种乐器到达的音轨（原始/FEC修/NACK补）按谱子重新对齐（排序），统一延迟几毫秒（缓冲），确保最终输出的节奏稳定

这个比喻在面试时能有效展示你**理解全局架构**而非只会背定义。

---

### Q12. 你在自己的 B 层 JitterBuffer 里，怎么跟 FEC/NACK 交互？

**标准答案**：我的 B 层没有实现 FEC——这是有意识的简化。和 NACK 的交互在 `IJitterBufferObserver` 接口：当 JitterBuffer 检测到 SeqNum 断层（缺失的包在 1 RTT 内没收到），通过 `OnPacketLossDetected(missingSequences)` 通知 NACK 模块发重传请求。JitterBuffer 的 TargetDelay 会给 NACK 留出 `RTT × NACK_RETRIES` 的时间窗口——如果重传预估到达时间 > renderTimeMs，JitterBuffer 不再等待、直接标记帧不完整。

B 层的具体实现中，`PacketBuffer::InsertPacket` 在收到每个包时比较 `newPacket.seq` 和 `expectedNextSeq`，断层时 push 缺失 seq 列表到 observer，然后依赖外部 NACK 模块处理和等待。详细代码见 `07-M6-JitterBuffer模块/code/`。

---

## 六、项目面试讲述（10 题）

> 来源：`09-面试讲述.md`

### Q1. RTP 头字段你能默写吗？

**口语化标准答案**：12 字节固定头。字节 0：V(2 bit)+P(1)+X(1)+CC(4)；字节 1：M(1)+PT(7)；字节 2-3：Sequence Number 大端；字节 4-7：Timestamp 大端；字节 8-11：SSRC 大端。本项目不支持 CSRC 列表和扩展头，所以 V/P/X/CC 字节固定 0x80。

### Q2. FU-A 的均衡分配算法你能白板写吗？ ⚠️

```cpp
size_t totalPieces = (naluSize - 1 + maxDataPerPiece - 1) / maxDataPerPiece;
size_t avgBytesPerPiece = (naluSize - 1 + totalPieces - 1) / totalPieces;
```

**关键点**：① `naluSize - 1` 因为 NALU 头被 FU Header 替代；② 两次向上取整除法用 `(a + b - 1) / b` 模式避免浮点。

### Q3. 接收端怎么从 FU-A 分片重建原 NALU 头？ ⚠️

```cpp
restoredNaluHeader = (fuIndicator & 0xE0) | (fuHeader & 0x1F);
```

高 3 位 F+NRI 来自 FU Indicator，低 5 位 type 来自 FU Header。

### Q4. Marker 包丢了怎么判定帧完整？

**口语化标准答案**：用"下一帧首包已到达"作为副信号——如果下一帧的 isFrameFirstPacket=true 的包已经在 buffer 里，说明本帧网络层已发完，丢的就是 Marker 包本身，等不到的。这时直接基于"首包到下一帧首包前一个 SeqNum"判定本帧范围。我的实现在 `PacketBuffer::ExtractCompletedFrames` 的 fallback 逻辑里，找不到当前 nextSequenceToCheck_ 的首包时，自动前进到下一个 isFrameFirstPacket=true 的位置。

### Q5. SeqNum wraparound 怎么判断新旧？ ⚠️

```cpp
bool IsNewerSequenceNumber(uint16_t a, uint16_t b) {
    int16_t signedDelta = (int16_t)(uint16_t)(a - b);
    return signedDelta > 0;
}
```

**举例**：a=0, b=65535 → `(uint16_t)(0 - 65535) = 1` → `(int16_t)1 = 1 > 0` → 0 比 65535 新（正确，0 是 wraparound 后的新值）。

### Q6. EWMA 系数怎么选的？为什么是 0.05？

**口语化标准答案**：α=0.05 表示历史权重 95%、新样本权重 5%，对应的"等效平均窗口"约 1/α = 20 帧（30fps 下约 0.67 秒）。**比 libwebrtc 的 Kalman 慢**——Kalman 的卡尔曼增益是动态的，突发抖动时增益高（快速跟踪），稳态时增益低（信任历史）。我用固定 α 是简化选择，**稳态准确度接近、突发响应慢 5-10 倍**。生产环境如果有突发场景需求应该上 Kalman。

### Q7. 为什么目标延迟是 3 倍抖动？

**口语化标准答案**：3σ 是统计学经验值——假设抖动服从正态分布，3σ 覆盖 99.7% 样本，剩下 0.3% 极端值用 NACK 重传兜底。**不是绝对要 3**：实际项目可以根据业务定，要求低延迟可以 2σ（87%覆盖），要求稳定可以 4σ（99.99%）。libwebrtc 用 3σ + RTT 倍数做 NACK 余地，本项目简化只用 3σ。

### Q8. 检测到丢包后立刻发 NACK 还是等？

**口语化标准答案**：**等一个 RTT 再发**（典型 50-200ms）。原因：① 大部分 SeqNum 缺失其实是**乱序到达**，等一下可能自然到；② 批量发节省 RTCP 头开销。本项目的 PacketBuffer 只**检测**丢包并通过 observer 回调上报，**真正等多久发 NACK 由独立的 NACK 模块决定**。

### Q9. 什么时候请求关键帧（IDR）？

**口语化标准答案**：三个触发源——① **连续丢包事件超阈值**（本项目设 10 次，源码在 `JitterBufferImpl::InsertPacket`）；② 解码器报"参考帧丢失"；③ 应用层主动请求（首次连接、网络恢复）。**关键帧到达后重置计数器**——视为网络恢复信号。**触发要严苛**：关键帧体积是 P 帧的 5-10 倍，滥发浪费带宽。

### Q10. 你的项目相比 libwebrtc 简化了什么？为什么这些可以简化？

**口语化标准答案**：见"Part 4"的简化清单。**关键观点**：所有简化都不是因为做不到，是为了**控制学习项目的范围 + 让面试讲述清晰**——花 1500 行 C++ 把核心算法讲清楚比复刻 libwebrtc 更适合面试。每一项简化的代价我都明确知道：EWMA → 突发慢、不做参考帧链 → 解码器兜底、不支持 VP8 → 只覆盖一种编码。如果让我加回去，工期 +2-3 周即可。

---

## 七、白板题（3 题）

> 来源：`09-面试讲述.md`

### 白板题 1：FU-A 分片函数 ⚠️

```cpp
// 给定 NALU 字节数和 MTU，返回每片的字节区间 [start, end)
struct FuAPiece {
    size_t startOffset;
    size_t endOffset;
    bool isFirst;
    bool isLast;
};

std::vector<FuAPiece> ComputeFuAPieces(size_t naluSize, size_t maxPayloadBytes) {
    constexpr size_t kFuOverhead = 2;
    size_t maxDataPerPiece = maxPayloadBytes - kFuOverhead;
    size_t fragmentableBytes = naluSize - 1;
    size_t totalPieces =
        (fragmentableBytes + maxDataPerPiece - 1) / maxDataPerPiece;
    size_t avgBytesPerPiece =
        (fragmentableBytes + totalPieces - 1) / totalPieces;

    std::vector<FuAPiece> pieces;
    size_t cursor = 1;  // 跳过原 NALU 头
    for (size_t pieceIndex = 0; pieceIndex < totalPieces; ++pieceIndex) {
        FuAPiece piece;
        piece.startOffset = cursor;
        piece.endOffset = std::min(cursor + avgBytesPerPiece, naluSize);
        piece.isFirst = (pieceIndex == 0);
        piece.isLast = (pieceIndex == totalPieces - 1);
        pieces.push_back(piece);
        cursor = piece.endOffset;
    }
    return pieces;
}
```

**画图辅助**：在白板上画一个 100KB 的 NALU 矩形，MTU 线分割，标 S=1 在首片、E=1 在尾片。

### 白板题 2：RTP 头序列化

```cpp
size_t WriteRtpHeader(uint8_t* buffer, uint16_t seq, uint32_t ts, uint32_t ssrc,
                     bool marker, uint8_t pt) {
    buffer[0] = (2 << 6);                              // V=2, P=0, X=0, CC=0
    buffer[1] = (marker ? 0x80 : 0x00) | (pt & 0x7F);  // M, PT
    buffer[2] = (seq >> 8) & 0xFF;                     // Seq 高字节
    buffer[3] = seq & 0xFF;                            // Seq 低字节
    buffer[4] = (ts >> 24) & 0xFF;                     // Ts 大端
    buffer[5] = (ts >> 16) & 0xFF;
    buffer[6] = (ts >> 8) & 0xFF;
    buffer[7] = ts & 0xFF;
    buffer[8] = (ssrc >> 24) & 0xFF;                   // SSRC 大端
    buffer[9] = (ssrc >> 16) & 0xFF;
    buffer[10] = (ssrc >> 8) & 0xFF;
    buffer[11] = ssrc & 0xFF;
    return 12;
}
```

**坑点**：忘记 `& 0xFF` 截断（在 ARM 等平台可能没事，但写规范代码必须加）。

### 白板题 3：画环形 buffer 数据结构

```
        SeqNum mod 2048 = slot index
        ┌──────────────────────────────┐
slot:   │ 0 │ 1 │ 2 │ ... │ 2046 │ 2047│
        └─┬─┴─┬─┴─┬─┴─────┴──┬───┴──┬──┘
          ▼   ▼   ▼          ▼      ▼
        occu? Packet (seq/ts/payload)
        
SeqNum=100 → slot 100
SeqNum=2148 → slot 100 (覆盖)
        
wraparound 判断: int16_t((uint16_t)(a-b)) > 0
```

**画完后说**：① O(1) 插入；② 自然有界容量；③ wraparound 用 16 位符号距离判断。

---

## 八、无答案的面试视角问题（33 题）

> 这些问题分布在各文档中，是面试官可能的追问方向。只有问题没有答案——建议你自己思考一遍，能答上来说明真懂了。

### 9.1 架构层（来源：`02-架构总览.md`）

1. WebRTC 为什么要把 Signaling / Worker / Network 三条线程分开？合并行不行？
2. `PeerConnectionFactory` 为什么是进程级单例？它握住了什么资源？
3. 媒体引擎和网络传输为什么是平级而不是上下层？

### 9.2 协商层（来源：`02-架构总览.md`）

4. SDP 协商为什么要分 Offer/Answer？为什么不一次性互换能力？
5. ICE 的 host / srflx / prflx / relay 四类候选优先级怎么排？为什么 relay 是兜底？
6. DTLS 既然能加密，为什么还需要 SRTP？两者各自防什么攻击？

### 9.3 媒体层（来源：`02-架构总览.md`）

7. JitterBuffer 是按时间排还是按序号排？乱序和延迟它怎么权衡？
8. NetEQ 和 JitterBuffer 的核心区别是什么？为什么音频不能复用视频的 JitterBuffer？
9. Pacer 存在的意义是什么？没有它会怎样？

### 9.4 传输层（来源：`02-架构总览.md`）

10. GCC 的"延迟梯度"和"丢包率"两路信号为什么要联合而不是单用一路？
11. NACK 和 FEC 各自的代价是什么？为什么要做"NACK + FEC"混合策略？
12. Simulcast 和 SVC 在 SFU 场景里各自的优劣？为什么大多数生产环境选 Simulcast？

### 9.5 工程层（来源：`02-架构总览.md`）

13. WebRTC 的端到端延迟一般拆成哪几段？每段你能怎么压？
14. 跨线程对象生命周期 WebRTC 是怎么管理的？`scoped_refptr` 和 `WeakPtr` 各自解决什么？
15. 如果让你设计一个 SFU，你会复用 WebRTC 哪些模块？哪些必须自己写？

### 9.6 设计方案层（来源：`04-项目设计方案.md`）

16. **为什么你的项目要拆成 8 个模块？拆得太细或太粗有什么坏处？**
17. **PeerConnectionFactory 为什么是工厂模式？直接 new PeerConnection 会有什么问题？**
18. **Observer 跨线程怎么投递？回调里调用 PeerConnection 的 API 会死锁吗？**
19. **你的 RTP 打包器和 libwebrtc 的相比，简化了什么？为什么这些可以简化？**
20. **Jitter Buffer 的 "目标延迟" 怎么算？太大太小分别有什么后果？**
21. **如果让你加 NACK 重传，应该加在哪个模块？为什么？**
22. **数据面和控制面分离的好处是什么？反例（不分离）会怎样？**
23. **你的项目能不能改成 SFU 架构？要改哪些模块？**

### 9.7 设计方案内嵌问答（来源：`04-项目设计方案.md`）

24. Q: 你的 Jitter Buffer 用了 EWMA，那 libwebrtc 用 Kalman 的优势在哪？
    A: Kalman 能同时考虑系统状态和观测噪声，对突发抖动（比如 WiFi 切换）适应更快；EWMA 对稳态网络效果接近，但突发场景会有 100-200ms 的滞后。
25. Q: 为什么选 libwebrtc 不用 mediasoup？
    A: 目标是吃透 WebRTC 内部实现，libwebrtc 是源码最完整、面试参考价值最高的选择；mediasoup 抽象层厚，看不到底层。

### 9.8 SDP 自检题（来源：`08-SDP真实样本解析.md`）

26. SDP 分哪两层结构？分界在哪？
27. 怎么从 SDP 判断浏览器首选的编码？为什么不能只看 `fmtp` 行下结论？
28. `m=video` 行 `UDP/TLS/RTP/SAVPF` 拆开是哪 4 层？SAVPF 里 S 和 F 各代表什么？
29. `rtcp-fb` 行里你能认出几种反馈机制？哪几个是你 M4/M6 代码里实现/触发过的？
30. `fingerprint` 是什么？为什么必须在 SDP 里提前给（不能 DTLS 握手时再给）？
31. `setup:actpass` 和 `setup:active` 的关系是什么？真实场景谁主动发起 DTLS 握手？
32. `a=ssrc-group:FID` 把两个 SSRC 配成什么对？为什么需要？
33. PLI 和 FIR 的区别？

---

## 九、面试金句汇总

> 来自各文档中的"一句话总结"，适合面试时作为段落收尾或概念定调。

### SDP

> "SDP 分**会话级**和**媒体级**两层。`m=` 行的传输是 `UDP/TLS/RTP/SAVPF`——UDP 上跑 DTLS 加密的 RTP，SAVPF 表示支持 RTCP 反馈。**payload 列表第一个是首选编码**（不能只看局部 fmtp 行下结论）。
>
> 每个编码下的 **`rtcp-fb`** 声明支持的反馈机制：`nack` 重传 / `nack pli` 和 `ccm fir` 关键帧请求 / `transport-cc` 拥塞控制 / `goog-remb` 带宽估计。
>
> **`a=fingerprint`** 是 DTLS 证书指纹（防中间人替换证书），**`a=ice-ufrag/pwd`** 是 ICE 连通性检查的鉴权凭证。**`a=ssrc-group:FID`** 把主流和 RTX 重传流配对——印证了 NACK 重传走独立 SSRC 的设计。
>
> Offer 通常 `sendrecv`，Answer 视实际方向可能是 `recvonly`。ICE candidate 有 `host / srflx / relay` 三种，按 priority 和 network-cost 选最优路径。"

### 音画同步

> "90% 播放器用视频追音频——音频走硬件时钟天然稳定、人耳敏感不能动；无音轨或视频优先用音频追视频——音频变速不变调适配；RTC/多路混流用外部墙钟——多时钟源没法互追，只能各自和 NTP 对齐。"

### 视频 vs 音频丢帧

> "视频丢帧到关键帧重启，音频用变速算法吸收抖动。"

### 3A

> "AEC 消回声，AGC 调音量，ANS 去噪声——三者串联在采集后、编码前，WebRTC 里都在 APM（AudioProcessingModule）里。"

### NetEQ

> "NetEQ = 抖动缓冲 + 丢包隐藏 + 变速不变调 + 舒适噪声，源码在 `modules/audio_coding/neteq/`。"

### Opus vs AAC

> "Opus = 实时通信的事实标准，AAC = 直播/点播音乐场景的事实标准。"

### 编码选型

> "实时通信我会选 H264——硬件覆盖好、延迟低、CPU 友好；离线场景可以上 H265/AV1 换码率收益。"

### RTX

> "独立通道重传 · 独立 SSRC · 前 2 字节存原始 Seq · 按需触发省带宽 · 低 RTT 下的省流量利器"

---

## 附录：答案类型说明

| 答案类型 | 出现位置 | 风格 |
| :--- | :--- | :--- |
| **标准答案** | 03-音视频基础 (40题)、11-QoS四驾马车 (12题) | 正式解释式，100-200字 |
| **口语化标准答案** | 06-RTP (8题)、07-JitterBuffer (7题)、09-面试讲述 (10题) | 对话式，面试友好 |
| **答** | 08-SDP (5题) | 简洁，1-2句 |
| **引用块** | 10-NTP与RTCP-SR (5题) | 一问一答格式 |

---

> 生成时间：2026-07-09
> 来源：`project/WebRTC/` 目录下所有 .md 文档
