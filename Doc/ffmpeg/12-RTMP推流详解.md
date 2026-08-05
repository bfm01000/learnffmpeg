# 12 - RTMP 推流详解

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | RTMP 推流专题：握手、控制命令、chunk、FLV Tag 和推流排查。 |
| 先背什么 | connect/createStream/publish、RTMP 与 FLV 的关系、黑屏花屏卡顿排查要熟。 |
| 深入怎么学 | 结合 FFmpeg C API 推流、时间戳、SPS/PPS、音视频交织理解。 |
| 关联阅读 | 05、08、19 |

---

> 对应导读第 3.7 节"运输问题"。这是 [08-网络协议与流媒体.md](./08-网络协议与流媒体.md) §六协议全景里 **RTMP 那一格的深挖**——专讲"主播端怎么把音视频推到服务器"。
> 前置：编码看 [11-H264与H265详解.md](./11-H264与H265详解.md) / [06-编码参数与码控.md](./06-编码参数与码控.md)，FLV 封装看 [05-H264-MP4-NALU.md](./05-H264-MP4-NALU.md) §5.5，协议大局看 [08](./08-网络协议与流媒体.md) §六。

---

## 一、面试通关（全部 Q&A 集中到这里）

### 1.1 基础问答（先背这个）

**Q1：讲一下 RTMP 推流的完整流程？**

> 嗯，RTMP 是跑在 TCP 上的，所以**第一步是建 TCP 连接**。连上之后它有一套**自己的应用层握手**，叫 C0/C1/C2，双方互发随机数再回显一下，确认通道通了、时钟基准对齐了。
>
> 握手完之后进入**命令交互**：先发 `connect`，告诉服务器"我要连哪个 app"；再发 `createStream`，申请一条消息流，服务器回一个 **message stream id** 给我；然后发 `publish`，带上推流码（streamKey），意思是"我要往这条流里推东西了"；服务器回个 `onStatus` 说准备好了。
>
> 接下来**真正发数据之前**，要先发三样：`onMetaData`（宽高、码率、编码这些元信息），然后是**视频的 sequence header**（也就是 avcC，里面是 SPS/PPS），还有**音频的 sequence header**（AudioSpecificConfig）。这一步特别关键，先发参数集，**不然拉流端解不出来会黑屏**。
>
> 最后才是源源不断地**按时间戳交织发音视频数据**。整个发送过程，每条消息都会被切成 Chunk 一块块发。
>
> 一句话收口：**握手 → connect 连应用 → createStream 拿流 → publish 声明推流 → 先发元信息和音视频参数集 → 再按时间戳交织发数据。**

**Q2：RTMP 为什么要分 Chunk？**

> 因为一条 TCP 连接上，音频、视频、控制命令是**混在一起跑**的。如果不分块，发一帧几十 KB 的关键帧时，就会把后面急着发的音频、控制消息全堵死——这就是队头阻塞。
>
> 所以 RTMP 把每条消息切成小块（默认 128 字节一块），这样大消息发一块、小消息插一块，能**交织着发**，谁也不饿死谁。
>
> 还有个附带好处：Chunk 的头有四种格式（fmt 0/1/2/3），同一路流连续的块，时间戳、长度、类型大多不变，后面的块就用更短的头，最省的 fmt 3 几乎没头——**等于做了头部压缩，省带宽**。

**Q3：RTMP 和 FLV 是什么关系？**

> 你可以这么理解：**RTMP 的音视频消息体，几乎就是 FLV Tag 扒掉那层 tag header 之后的 body**。FLV Tag 头里那些"类型、时间戳"信息，在 RTMP 里被挪到了 Chunk/Message 的头里，**真正的数据体两边是一模一样的**。
>
> 这就解释了一个工程现象：**RTMP 转 HTTP-FLV 几乎零成本**——服务器收到 RTMP，把 message header 那点信息拼回 FLV tag header，肉（body）原封不动，所以叫"换壳不换肉"，CPU 几乎不耗。转 HLS 才要重新切片，转 WebRTC 才要换成 RTP。

**Q4：RTMP 里的视频是 Annex-B 还是 AVCC？**

> 是 **AVCC**，也就是**长度前缀**格式——每个 NALU 前面是 4 字节的长度，不是起始码。
>
> 这点特别容易搞错：裸流、RTP 那种是 **Annex-B**，用 `00 00 00 01` 起始码分隔；而 MP4、FLV、RTMP 都是 **AVCC** 长度前缀。而且 SPS/PPS 不在数据帧里，是单独放在第一帧那个 **sequence header（avcC）** 里发的。
>
> 所以如果你的数据源是 Annex-B（比如编码器直接吐的裸流），推 RTMP 之前得先转 AVCC：去掉起始码、换成长度前缀，再把 SPS/PPS 抽出来塞进 sequence header。

**Q5：为什么推流必须先发 sequence header？**

> 因为 SPS/PPS 是解码器的"说明书"——分辨率、profile、level、参考帧配置全在里面。拉流端**没拿到这个就根本没法初始化解码器**，画面自然出不来。
>
> 表现就是：**有声音但画面黑屏**，或者首帧花屏。所以 publish 之后、发第一个数据帧之前，必须先把视频 avcC 和音频 AudioSpecificConfig 发过去。中途如果有人新进来拉流，服务器也会先把缓存的 sequence header 补给他。

**Q6：RTMP 为什么延迟低，为什么浏览器又播不了，为什么现在还在用？**

> 延迟低是因为它**长连接、来一帧发一帧、没有切片缓冲**——不像 HLS 要先攒几个 `.ts` 小文件才起播，那一攒就是好几秒。
>
> 浏览器播不了是因为 RTMP 当年是给 **Flash** 用的，Flash 2020 年被浏览器彻底干掉了，所以**播放端这一侧 RTMP 死了**。观众端现在用 HTTP-FLV（flv.js 解析喂给 `<video>`）或者 HLS。
>
> 但**推流端它活得好好的**——推流是主播到服务器，不碰浏览器；而且整个生态太成熟了，所有 CDN、所有推流软件都支持，延迟也够低，换掉它成本高收益小。所以格局就是：**推流用 RTMP，分发再转 HTTP-FLV / HLS / WebRTC。**

**Q7：RTMP、SRT、WebRTC 推流怎么选？**

> **RTMP**：跑 TCP，生态最成熟，延迟 1-3 秒，是推流的事实标准，绝大多数直播都用它。缺点是弱网下有队头阻塞，延迟会飙。
>
> **SRT**：基于可靠 UDP，自己做重传和拥塞控制，抗丢包强，延迟几百毫秒到 1 秒，**适合弱网、跨国回传**这种链路差的场景。
>
> **WebRTC**：UDP + RTP，延迟几百毫秒，是**互动级**的（连麦、超低延迟直播），但要 ICE 打洞、要 SFU 转发，整套架构重，运维复杂。
>
> 一句话：**稳妥选 RTMP，弱网回传选 SRT，要互动选 WebRTC。**

---

### 1.2 进阶问答

**Q8：RTMP 的握手（C0/C1/C2）做了什么？和 TCP 三次握手有什么区别？**

> RTMP 握手是**应用层握手**，发生在 TCP 三次握手已经完成之后。TCP 三次握手在内核完成，确认双方收发能力正常；RTMP 握手在应用层完成，确认应用层通道可用、对齐时钟基准。
>
> 具体过程：**C0** 就 1 字节 `03`，声明 RTMP 版本号。**C1** 是 1536 字节——前 4 字节时间戳、再 4 字节填零、剩下 1528 字节随机数。服务端收完后回 **S0+S1+S2**：S0 也是版本号，S1 是服务端自己的 1536 字节同构数据，**S2 是 C1 的原样回显**。客户端发 **C2，把 S1 原样回显**。
>
> 握手完成那一刻双方都验证了：对方确实收到了自己的随机数（双向通道通畅）；可以算出大致 RTT（用于后续超时判断）。
>
> 一句话收口：**C0/C1 → S0/S1/S2 → C2，双方换 1536 字节随机数并回显，验证双向通道 + 对齐时钟。**

**Q9：RTMP 里的 Message 和 Chunk 是什么关系？csid 和 message stream id 又是什么关系？**

> **Message 是业务语义单元**——一条音频消息、一条视频消息、一条 connect 命令，它告诉你"这条数据的类型、时间戳、大小"。**Chunk 是传输分片单元**——一条大 Message 被切成多个小块在 TCP 上发，小块收到后再拼回 Message。
>
> 打个比方：Message 是一封信，Chunk 是信的每一页纸。一页纸写不下整封信就分多页，收信人按页号拼回完整的信。
>
> **csid** 是 Chunk 层的分组编号——同一条"逻辑通道"上的所有 Chunk 用同一个 csid，接收端按 csid 把零散的 Chunk 归组、拼回 Message。**message stream id** 是 Message 层的业务流编号——区分多路推流/拉流。csid 每个 Chunk 都有，message stream id 只在 fmt=0 的 Message Header 里出现。
>
> 一句话收口：**Message 有类型+时间戳（业务语义），Chunk 是碎片（传输单元）；csid 分拣碎片归组，message stream id 区分业务流。**

**Q10：单条 TCP 连接上，RTMP 怎么同时传音频、视频、控制命令而不乱？**

> 靠两级区分机制。第一级在 **Message 层：message type id**——8=音频、9=视频、20=AMF 命令、1/3/5/6=协议控制。服务器拼出完整 Message 后看一眼 type id 就知道该丢给谁。
>
> 第二级在 **Chunk 层：CSID 做应用层多路复用**。音频、视频、控制、命令各用不同的 CSID。即使它们交替到达——视频 Chunk（csid=6）、音频 Chunk（csid=4）、控制 Chunk（csid=3）……接收端按 csid 归组，各拼各的，一块都不会乱。
>
> 这就是 RTMP 最精妙的设计：**TCP 只有一条物理连接，但 RTMP 通过 CSID 在应用层虚拟出了多条"逻辑通道"**。
>
> 一句话收口：**message type id 区分"这是什么"，CSID 做应用层多路复用——一条 TCP、多条逻辑通道。**

**Q11：RTMP 为什么设计在 TCP 之上，而不是 UDP？这带来了什么问题？**

> 推流场景的核心需求是**数据完整不丢**——音视频码流有强依赖关系，丢一帧关键帧会连锁导致后续所有 P 帧解不出来，花屏好几秒。TCP 天然提供可靠传输、有序到达、自动重传，完美覆盖推流的可靠性需求。如果用 UDP，这些都得自己实现，而 RTMP 设计的 2000 年代初还没有 SRT 这种成熟的可靠 UDP 方案。
>
> 但代价就是 **TCP 的队头阻塞**：TCP 是严格有序的字节流——丢包后，即使后面包已在接收端内核 buffer 里，也得等重传到达、按序交付。对推流来说就是延迟从 1 秒飙到 5-10 秒。
>
> 更根本的矛盾是：**TCP 对所有数据一视同仁**——关键帧和普通 P 帧在 TCP 眼里是一样的，丢谁都得等重传。但音视频场景里，P 帧丢了可以跳过（轻微花），关键帧丢了才真需要等。TCP 做不到这种"分级可靠"。
>
> 这也是为什么后来 SRT 和 WebRTC 在低延迟场景逐步取代 RTMP：**用 UDP 的好处不是"不可靠"，而是"你可以自己定义什么叫可靠"**——选择性重传、分级 QoS。
>
> 一句话收口：**推流要完整不丢→TCP；TCP 严格有序→队头阻塞→弱网延迟高；低延迟转 SRT/WebRTC 用可靠 UDP 做选择性重传。**

**Q12：AMF 是什么？为什么 RTMP 不用 JSON 或 Protobuf？**

> **AMF（Action Message Format）** 是 Adobe 为 Flash 生态设计的二进制序列化协议，类似 JSON 但更紧凑。RTMP 的所有控制命令（`connect`、`createStream`、`publish`）和元数据（`onMetaData`）都用 AMF 编码。
>
> 为什么不用 JSON？三点：① 时间背景——2000 年代初 JSON 还没普及，AMF 随 Flash 一起诞生、生态成熟；② **省带宽**——二进制编码比 JSON 省 30-50%；③ 解析快——二进制直接映射到内存结构。
>
> AMF 分 AMF0（Number/String/Boolean/Object/Null 等，RTMP 默认用）和 AMF3（加 ByteArray/Dictionary 等）。命令消息的 AMF 字节招牌特征：`00 00 09` 收尾。
>
> 一句话收口：**AMF 是 Adobe 的二进制序列化，类似 JSON 但省 30-50% 带宽、解析更快。**

**Q13：用 FFmpeg 推流时，connect、createStream、publish 这些是 FFmpeg 内部自动做的吗？**

> 是的，**全部自动完成，你一行协议代码都不用写**。整个建连过程被封装在 `avformat_write_header()` 这一个函数调用里。
>
> FFmpeg 从 URL 里解析出 `<app=live>`、`<streamKey=streamKey>` 等，内部自动走完：TCP connect(1935) → RTMP 握手 → connect("live") → createStream → publish("streamKey") → 收 onStatus。`avformat_write_header` 返回成功时，连接已处于可推数据状态。
>
> **FFmpeg 没帮你什么？** ① 自定义鉴权；② 断线重连；③ 动态切换分辨率/码率/旋转。这些生产必需的功能要在 FFmpeg 外面自己写。
>
> 一句话收口：**`avformat_write_header` 自动完成握手+connect+createStream+publish，你只管塞 AVPacket；鉴权、重连、动态参数切换必须自己在外面写。**

---

### 1.3 精通追问（说自己"精通 RTMP"时面试官的连环追问）

**追问 1：RTMP 的 ACK 和 Window Acknowledgement Size 流控是怎么工作的？**

> RTMP 的流控和 TCP 的滑动窗口是**两层独立的流控**，但配合工作。
>
> **Window Acknowledgement Size（WAS）**：接收端告诉发送端"我的缓冲区有多大"。发送端每发出多少字节后，必须停下来等一个 ACK。这就是 RTMP 自己实现的"发送窗口"。
>
> **ACK 机制**：接收端收到 WAS 规定的字节数后，回一个 ACK（type 3），里面带的是"从连接建立开始共收了多少字节"——累计值，不是增量值。
>
> FFmpeg 默认 Window Ack Size 是 **2500000 字节（约 2.5MB）**。4K 推流一帧关键帧可能 500KB+，外网 RTT 100ms+ 时等 ACK 会卡顿。高码率推流建议调到 10MB+。

**追问 2：RTMP 时间戳的 24 位回绕问题？**

> timestamp 字段默认 3 字节（24 位），单位毫秒。`2^24 ms ≈ 4 小时 39 分 37 秒`。超过后归零。
>
> **但 RTMP 有 Extended Timestamp**：当 3 字节 timestamp 为 `0xFFFFFF` 时，后面跟 4 字节的 Extended Timestamp（32 位），实际能表示约 49 天。
>
> 真正的坑：fmt=1/2 的时间戳 delta 也是 3 字节，回绕时算出来的绝对值可能错。解法：用 `fmt=0` 定期刷新绝对时间戳兜底。

**追问 3：Enhanced RTMP（H.265/AV1 推流）改了什么？**

> 标准 RTMP 的 CodecID 字段只有 4 位（最大 15），原生不支持 H.265。各家非标 (CodecID=12) 硬塞，兼容靠运气。
>
> Enhanced RTMP 用 **4 字节 FourCC**（`hvc1`=HEVC、`av01`=AV1）取代 4 位 CodecID，VideoTag 头从 1 字节扩展到 5 字节。推 H.265 要么用 Enhanced RTMP（确认服务器支持），要么换协议（SRT/WebRTC 原生支持 H.265）。

**追问 4：断线重连的策略？**

> 五个维度：
> 1. **退避策略分层**：短断（<3s）指数退避重试保留 seq header；中断（3~30s）慢启动码率+重发 seq header；长断（>30s）重新初始化。
> 2. **GOP 对齐**：重连后第一帧必须是 IDR。
> 3. **地址兜底**：主域不可达时自动切备域，预埋多组地址。
> 4. **流量平滑**：重连瞬间积压帧不要一股脑推，丢 GOP 级+request IDR。
> 5. **状态机**：DISCONNECTED → CONNECTING → HANDSHAKING → PUBLISHING → STREAMING，每个状态有超时和降级路径。

**追问 5：手写 RTMP 抓包命令？**

```bash
# 抓包
tcpdump -i any port 1935 -w rtmp.pcap -c 1000

# 过滤 RTMP 消息
tshark -r rtmp.pcap -Y 'rtmp' -T fields \
  -e frame.number -e rtmp.timestamp -e rtmp.type_id -e rtmp.body_size

# 拉流看第一个视频 tag
ffmpeg -i rtmp://... -c copy -t 1 -f flv - 2>/dev/null | xxd | head -20
# 必须是 17 00

# Wireshark 过滤
# rtmp.message.type_id == 9   → 所有视频消息
# rtmp.video.FrameType == 1   → 只看关键帧
```

**追问 6：4K 高码率推流卡顿，怎么分层排查？**

> **第一层隔离**：同时推流+本地录制，本地也卡→编码问题，本地正常→网络/发送侧。
> **第二层编码**：preset 太慢？换 `veryfast`；硬编降频？查 frame_number vs 时间戳。
> **第三层发送侧**：TCP Send-Q 满？Chunk Size 太小（128B）→调到 4096+；`av_interleaved_write_frame` 卡住→TCP buffer 满。
> **第四层服务器**：换低码率推一路看是否也卡；SRS http_hooks 模块是否有阻塞性 I/O。

**追问 7：DTS 不单调的具体表现和起因？**

> 三个症状：① 推流端 `av_interleaved_write_frame` 可能直接返回错误；② 服务器 DVR 录制的 FLV 播到乱序位置跳跃卡死；③ 拉流端 A/V sync 崩——音画漂移。
>
> 三个常见起因：① B 帧没用对——手动组 AVPacket 乱序写；② 重连后时间戳从 0 开始；③ time_base 换算精度截断导致相邻帧 DTS 差为 0。

**追问 8：onMetaData 里写了什么？少写会怎样？**

| 字段 | 含义 | 少了会怎样 |
|------|------|-----------|
| `width`/`height` | 分辨率 | 播放器首帧前不知道画布大小 |
| `videodatarate` | 视频码率 | ABR 策略无法判断 |
| `framerate` | 帧率 | 渲染节奏不准 |
| `videocodecid` | 编码类型 | 可能不知道该用什么解码器 |

**实际表现**：CDN/播放器大多不强依赖 onMetaData（SPS/PPS 已能算分辨率），少字段大概率不出 bug。**真正致命的是少发 sequence header，不是少发 onMetaData。**

**追问 9：SRS 收到 publish 后内部做了什么？**

> 1. **鉴权**：`on_publish` HTTP hook，业务后端判断是否允许
> 2. **创建 Source**：维护 GOP 缓存（默认最后一个 GOP）、sequence header 缓存、元数据缓存
> 3. **GOP 缓存意义**：新观众进来先发缓存 GOP + seq header → 不用等下一个 IDR 就能起播
> 4. **转协议分发**：根据拉流端请求的协议转 HTTP-FLV/HLS/WebRTC
> 5. **推流断开**：`on_unpublish` → 清理 Source → 通知拉流端

**追问 10：为什么大厂要在 FFmpeg 外面加异步发送队列？**

> `av_interleaved_write_frame` 最终走 `write()` 写 TCP socket——默认阻塞。如果对端接收窗口满，`write()` 会卡住调用线程。这个线程如果是采集/编码线程，整个链路被拖慢。
>
> 加异步队列：
> ```
> 编码线程 → 生产 AVPacket → 丢入队列 → 立即返回（不阻塞）
>                                    ↓
>                          异步发送线程 → 取队列 → write() → TCP
>                                    ↓
>                         队列满了？→ 按 GOP 丢帧 + request IDR
> ```
>
> 三个好处：① 解耦——编码和发送不在同一线程，不被网络反压；② 可观测——队列水位是实时网络质量指标；③ 可调控——队列满时不随机丢帧，丢整 GOP + IDR。

---

### 1.4 关键词速记（临场背要点）

- **Q：RTMP 推流完整流程。**
  TCP → C0/C1/C2 握手 → connect 连 app → createStream 拿 message stream id → publish 声明推流 → 先发 onMetaData + 音视频 sequence header → 按时间戳交织发音视频数据。
- **Q：为什么要分 Chunk？**
  一条 TCP 多路复用音视频/控制；大消息切小避免堵死小消息；Chunk header 4 种格式做头部压缩省带宽。
- **Q：RTMP 和 FLV 什么关系？**
  RTMP 音视频消息体≈FLV Tag Body（类型/时间戳挪到 RTMP message header）。RTMP 转 HTTP-FLV 几乎零成本——换壳不换肉。
- **Q：视频 Annex-B 还是 AVCC？**
  **AVCC（长度前缀）**，SPS/PPS 在第一帧 sequence header(avcC) 里——和裸流/RTP 的 Annex-B 起始码不同。
- **Q：为什么 RTMP 延迟比 HLS 低？**
  RTMP 长连接、来帧即发、无切片缓冲；HLS 要把流切成 `.ts` 小文件、还要攒几个切片才起播。
- **Q：为什么浏览器不能直接播 RTMP？**
  RTMP 依赖已停用的 Flash。观众端改用 HTTP-FLV（flv.js 解析喂 `<video>`）或 HLS。
- **Q：推流黑屏/花屏/卡顿怎么排查？**
  先三分法：黑屏→sequence header；花屏→AVCC/参考帧/IDR；卡顿→发送反压/掉帧。再六步：本地录制隔离→查 avcC→查 AVCC→查 DTS→查 GOP/IDR。
- **Q：RTMP vs SRT vs WebRTC？**
  RTMP：TCP、生态最成熟、1-3s、推流事实标准；SRT：可靠 UDP、抗丢包、几百 ms-1s、弱网回传；WebRTC：UDP+RTP、几百 ms、互动级但要 ICE/SFU。
- **Q：Chunk header 哪个字段是小端？**
  只有 **message stream id（4 字节）是小端**，其余多字节字段都是大端。
- **Q：RTMP 握手（C0/C1/C2）做了什么？**
  C0=1 字节版本号；C1=1536 字节（时间戳+0+随机数）；S2 回显 C1、C2 回显 S1——验证双向通道+对齐时钟。别跟 TCP 三次握手搞混。
- **Q：Message 和 Chunk 什么关系？csid 和 message stream id 呢？**
  Message 是业务语义单元，Chunk 是传输分片；csid 搞 Chunk 归组拼 Message，message stream id 区分业务流。
- **Q：单条 TCP 上怎么分音视频、控制命令？**
  message type id 区分类型（8/9/20），CSID 做应用层多路复用——等于一条 TCP 上虚拟出多条逻辑通道。
- **Q：为什么选 TCP 不选 UDP？代价？**
  推流要完整不丢→TCP 天然可靠；代价是队头阻塞（丢包等重传、后续全排队）。低延迟转 SRT/WebRTC——UDP 让你自己定义"什么叫可靠"。
- **Q：AMF 是什么？为什么不用 JSON？**
  Adobe 的二进制序列化，比 JSON 省 30-50% 带宽、解析快。AMF0 够 RTMP 用；命令消息标志：`00 00 09` 收尾。
- **Q：FFmpeg 推流要不要自己写 connect/publish？**
  不需要。`avformat_write_header` 内部自动走完握手→connect→createStream→publish。但鉴权、重连、动态切码率/分辨率 FFmpeg 不管。

### 1.5 自检清单

- 点"开始直播"后，从采集到服务器收到，完整链路有哪几环？RTMP 负责哪一环？
- RTMP 握手 C0/C1/C2 各是什么？各多少字节？
- 为什么 RTMP 要把消息切成 Chunk？分块带来哪三个好处？
- Chunk 的 Basic Header 怎么拆出 fmt 和 csid？Message Header 四种 fmt 各多少字节、差在哪？
- Message Header 里哪个字段是小端字节序？
- 推流建连的命令顺序是什么？publish 之后第一件事该发什么？
- 视频 sequence header 的头两个字节是什么、代表什么？avcC 里靠哪个字节决定 NALU 长度前缀？
- RTMP 的音视频消息体和 FLV Tag 是什么关系？这解释了什么工程现象？
- RTMP 里的 H.264 是 AVCC 还是 Annex-B？字节上怎么一眼区分？
- RTMP/FLV 里的 AAC 带不带 ADTS 头？
- 用 FFmpeg C API 推流，输出格式应该填什么？时间戳要注意什么？
- 拉流端黑屏有声音，最可能是哪一步出了问题？
- 花屏、黑屏、卡顿三者怎么区分？各自优先查什么？
- 弱网后突然花屏，最可能是什么丢帧/关键帧问题？
- 怎么用「本地录制 vs RTMP 推流」隔离编码问题和网络问题？
- 为什么 RTMP 延迟比 HLS 低、却又被认为"该被淘汰"？推流端为什么还在用？
- RTMP / SRT / WebRTC 推流各自的延迟量级和适用场景？

### 追问速查表

| 追问方向 | 考官在测什么 | 关键答点 |
|---------|------------|---------|
| ACK / Window Ack Size | 协议层流控理解 | TCP 流控≠RTMP 流控；WAS 调大对高码率有收益 |
| 时间戳回绕 | 对协议字段宽度的理解 | 3B→4.6h；Extended Timestamp；fmt=0 兜底 |
| Enhanced RTMP | 行业前沿认知 | 4位CodecID→32位FourCC；H.265非标扩展的坑 |
| 断线重连 | 工程化能力 | 五维度：退避策略/GOP对齐/地址兜底/流量平滑/状态机 |
| 抓包排查 | 动手能力 | tshark/Wireshark 过滤表达式；字节级识别 |
| 卡顿排查 | 系统性思维 | 四层隔离：编码/网络/发送队列/服务器 |
| DTS 不单调 | 对时间戳体系的认知 | 推流端报错/文件坏/拉流A/V不同步 |
| onMetaData 字段 | 协议细节广度 | 字段和缺失影响；真正致命的是 sequence header |
| SRS 内部流程 | 服务端理解 | GOP 缓存、鉴权回调、转协议分发 |
| 异步发送队列 | 生产环境经验 | 解耦编码发送/水位可观测/GOP级丢帧 |

---

## 二、先有个大局观

### 2.1 场景引入：点"开始直播"后，背后发生了什么

你在 OBS 里填好 `rtmp://live.example.com/app/streamKey`，点"开始直播"。这一瞬间：

1. OBS 抓屏/抓摄像头麦克风（采集）；
2. 用 x264 把画面编成 H.264、用 AAC 编声音（编码）；
3. 把每一帧打包成一小块"消息"；
4. 和服务器握手、登录、声明"我要推这一路流"；
5. 然后源源不断把音视频小块发过去。

几个直觉问题，这篇要回答：**RTMP 和直接发文件有什么区别？服务器怎么知道哪块是视频哪块是音频？为什么 Flash 都死了 RTMP 还在用？推流花屏/黑屏一般卡在哪一步？**

### 2.2 RTMP 在直播体系里的位置

```
 [主播端]                      [流媒体服务器]                    [观众端]
 OBS/手机                      SRS / nginx-rtmp / CDN
   │                                │
   │  ── RTMP 推流 ──►              │  ── HLS(.m3u8+.ts) ──►       海量观众(CDN 分发)
   │   (第一公里,本篇)              │  ── HTTP-FLV ──►             低延迟观众(国内主力)
   │                                │  ── WebRTC ──►               互动/超低延迟
   │                                │  (服务器转封装/转协议)
```

记住这条产业链：**RTMP 只负责"推上去"那一段**；服务器收到后，因为 RTMP 内部就是 FLV Tag，转成 HTTP-FLV 几乎零成本（换壳），转 HLS 要重新切片，转 WebRTC 要换成 RTP。这一格在 [08](./08-网络协议与流媒体.md) §6.0 的分层全景里属于"应用层 - 推流"。

### 2.3 RTMP 为什么存在，又为什么"半死不活"还在用

- **出身**：2000 年代 Adobe 为 Flash Player 设计，配合 Flash 做实时音视频。当年浏览器看视频几乎都靠 Flash + RTMP。
- **Flash 之死**：HTML5 `<video>` 普及 + 安全问题，浏览器 2020 年彻底干掉 Flash。**所以 RTMP 在"观众端/浏览器播放"这一侧死了**。
- **为什么推流端活下来**：推流是"主播 → 服务器"，不涉及浏览器；RTMP 生态极其成熟——所有 CDN、所有推流软件（OBS、FFmpeg、手机 SDK）都支持它，延迟也低。换掉它成本高、收益小。所以形成了今天的格局：**推流用 RTMP，分发再转成 HLS / HTTP-FLV / WebRTC**。

一句话：**RTMP 是"打不死的推流协议"——播放端早死了，推流端还是事实标准。**

### 2.4 一句话理解 RTMP

> RTMP（Real-Time Messaging Protocol）是 Adobe 在 **TCP** 上定义的流媒体协议，**推流端的事实标准**。它在不可靠的 IP 层之上，通过确认重传、滑动窗口、拥塞控制三大机制向上提供可靠字节流。一条 RTMP 连接 = 握手建连 + Chunk 分块传输 + FLV Tag 作为音视频载体。

---

## 三、RTMP 怎么建立连接

### 3.1 URL 结构

```
rtmp://live.example.com:1935/live/streamKey
└─┬─┘  └──────┬──────┘ └─┬┘ └┬┘ └───┬───┘
 协议        主机       端口  app  streamKey
```

- **app**：应用名（URL 路径第一段）。服务器用这个区分不同的应用实例（如 `live`、`vod`、`vod/mp4`）。推流时发给 `connect("app名")`。
- **streamKey**：推流码（app 之后的部分）。一般是随机串防止别人盗推、或固定串用于设备认证。发给 `publish("streamKey")`。

### 3.2 建连全流程

一条 RTMP 推流建连分为两层握手 + 三层命令交互：

```
步骤          层        做什么                              谁触发
─────────────────────────────────────────────────────────────────
① TCP 连接    传输层    三次握手,连上 1935 端口              内核自动
② C0/C1/C2   应用层    RTMP 自有握手,确认通道+对齐时钟      avformat 内部
③ connect     应用层    声明要连哪个 app                    avformat 内部
④ createStream 应用层   申请一条消息流,拿 message stream id   avformat 内部
⑤ publish     应用层    声明推流码,开始推流                  avformat 内部
⑥ 发音视频    应用层    sequence header + 数据帧             av_write_frame
```

**②~⑤ 全部被 `avformat_write_header()` 封装**——你不需要手动发任何 RTMP 命令。

#### 3.2.1 RTMP 握手（C0/C1/C2）

TCP 三次握手完成后，RTMP 搞一次**应用层握手**——目的不是确认收发能力（TCP 已经做了），而是确认"应用层通道通畅 + 对齐时钟基准 +（Flash 时代）验证身份"。

```
客户端                          服务器
  │ ── C0 + C1 ──────────────►  │   C0: 1 字节版本(=3)
  │                             │   C1: 1536 字节(4B 时间 + 4B 0 + 1528B 随机)
  │ ◄────────── S0 + S1 + S2 ── │   S0/S1 同上;S2 = 回显 C1
  │ ── C2 ───────────────────►  │   C2 = 回显 S1
  │      握手完成,开始通信       │
```

- **C0** 就一个字节 `03`，声明 RTMP 版本 3。
- **C1** 1536 字节：前 4 字节时间戳（简单握手填 0）、再 4 字节填 0、剩下 1528 字节随机数。
- **S2 回显 C1、C2 回显 S1**——双方验证"我发的随机数你确实收到了"，双向通道确认通畅。
- Flash 时代还有"复杂握手"——随机数部分带数字签名验证合法 Flash Player，现在已经不用了。

#### 3.2.2 命令交互：connect → createStream → publish

握手完成后，客户端发三条 AMF 命令完成业务建连：

```
客户端                                服务器
  │ ── connect("app") ────────────►   │  声明要连哪个应用
  │ ◄──────────── _result ──────────   │  (含 Window Ack/Set Peer BW)
  │ ── createStream ───────────────►   │  申请一条消息流
  │ ◄────── _result(messageStreamId)─  │  返回 message stream id
  │ ── publish("streamKey","live") ─►  │  声明"我要推这一路"
  │ ◄────── onStatus(Publish.Start) ─  │  服务器准备好接收
  │ ── @setDataFrame(onMetaData) ───►   │  推元信息(宽高/码率/编码)
  │ ── 视频 sequence header(avcC) ──►   │  先发 SPS/PPS!
  │ ── 音频 sequence header ────────►   │  AudioSpecificConfig
  │ ── 音/视频数据消息(按时间戳交织)─►  │  源源不断推流
```

**publish 成功 ≠ 观众能看到。** publish 之后必须先发 **sequence header（音视频参数集）**，然后才是数据帧。少发 sequence header → 观众黑屏有声音（最常见的推流故障）。

#### 3.2.3 AMF：命令消息的编码格式

**AMF（Action Message Format）** 是 Adobe 的二进制序列化协议，类似 JSON 但更紧凑。RTMP 的控制命令都用它编码。

| 特性 | AMF0 | AMF3 |
|------|------|------|
| 背景 | 老版本，RTMP 默认 | 新版本，更省带宽 |
| 支持类型 | Number/String/Boolean/Object/Null/Array | 加上 ByteArray/Dictionary 等 |
| RTMP 用处 | connect/createStream/publish/onMetaData | 较少用 |

**为什么不用 JSON？** 2000 年代初 JSON 还没普及；二进制编码比 JSON 省 30-50% 带宽；Flash 生态天然支持 AMF 编解码。

面试中知道"AMF 是二进制序列化格式，类似 JSON，省带宽"就够了。

#### 3.2.4 FFmpeg 替你做了什么？（= `avformat_write_header` 的魔力）

**建连全链路，FFmpeg 全部自动完成。** 你调用 `avformat_write_header()` 返回成功时，②~⑤ 已经全部跑完，连接处于可以推数据的状态。

FFmpeg 从 URL 里自动解析出 `<协议=rtmp>`、`<主机>`、`<端口=1935>`、`<app=live>`、`<streamKey=streamKey>`，然后内部走完 TCP 连接 → 握手 → connect → createStream → publish，一气呵成。

**FFmpeg 没替你做什么？**（生产环境必知的三件事）：
1. **自定义鉴权**——如果你的 CDN 要求推流前先调 HTTP 接口拿临时推流地址，FFmpeg 不管。
2. **断线重连**——TCP 断了，你得自己重新 `avformat_write_header` → 重发 sequence header → 从上一个 IDR 继续推。
3. **动态切换分辨率/码率/旋转**——中途改编码参数要重发 sequence header + IDR，需要业务层自己控制。

---

## 四、数据怎么传——Message 与 Chunk

这是 RTMP 最核心的传输设计，也是面试最爱问的一层。

### 4.1 Message：业务语义单元

Message 告诉你**"这条数据是什么、时间戳多少、有多大"**。每条 Message 自带：

| 字段 | 含义 | 举例 |
|------|------|------|
| message type id | 消息类型 | 8=音频、9=视频、20=AMF 命令、18=元数据 |
| message length | 载荷大小 | 一条视频帧可能几万字节 |
| timestamp | 时间戳 | 单位毫秒 |
| message stream id | 业务流编号 | 区分多路 publish/play 流 |
| payload | 真正的数据体 | = FLV Tag Body |

### 4.2 Chunk：传输分片单元

**RTMP 不直接发整条 Message，而是把 Message 切成一个个 Chunk 发出去。** 默认 Chunk Size = 128 字节。

**为什么要分块？**（面试核心题）
1. **多路复用**：音频、视频、控制命令共用一条 TCP。不分块的话，发一帧几十 KB 的关键帧会把后面的音频/控制消息全堵死。分块后可以交织发送——视频发一块、音频插一块、控制再插一块。
2. **头部压缩**：同一路流连续的 Chunk，时间戳/长度/类型大多不变，后续 Chunk 用更短的 header（最省的 fmt=3 几乎没有头），省带宽。
3. **避免大消息抢占**：把大消息切成小块后，急的小消息（如音频帧、ACK 控制消息）能随时插空发，谁的响应都快。

### 4.3 CSID：应用层多路复用的分拣编号

**CSID（Chunk Stream ID）** 是每个 Chunk 都带的"分拣编号"。接收端按 CSID 把零散的 Chunk 归组到不同的"逻辑通道"，各自拼回完整 Message。

```
TCP 连接（物理层，只有一条）
  │
  ├── csid=3  控制通道  ──► Set Chunk Size / ACK / Window Ack Size
  ├── csid=4  音频通道  ──► type id=8 消息
  ├── csid=6  视频通道  ──► type id=9 消息
  └── csid=3  命令通道  ──► type id=20 消息 (可与控制合用 csid)
```

即使视频 Chunk 和音频 Chunk 交替到达：
```
[vid chunk csid=6] [audio chunk csid=4] [vid chunk csid=6] [ctrl chunk csid=3] ...
```
接收端看到 `csid=6` → 丢进"视频拼装缓冲区"，`csid=4` → 丢进"音频拼装缓冲区"，各拼各的，互不干扰。

> ⚠️ **别搞混 csid 和 message stream id**：csid 是 Chunk 层的分组编号（拼 Chunk、复用 header），**每个 Chunk 都有**；message stream id 是 Message 层的业务流编号（区分多路 publish/play），**只在 fmt=0 的 Message Header 里出现**。

### 4.4 Chunk Header 的四种格式（fmt 0/1/2/3）

每个 Chunk 的结构：

```
┌──────────────┬─────────────────┬───────────────────┬───────────┐
│ Basic Header │ Message Header  │ Extended Timestamp│ Chunk Data│
│  (1~3 字节)   │ (0/3/7/11 字节) │  (0 或 4 字节)     │  (载荷)    │
└──────────────┴─────────────────┴───────────────────┴───────────┘
```

**Basic Header**（1 字节起步）：高 2 位是 `fmt`，低 6 位是 `csid`。csid 在 2~63 时 1 字节搞定；更大时扩展为 2~3 字节。

**Message Header 按 fmt 分为四种长度**：

| fmt | 长度 | 携带字段 | 用途 |
|-----|------|---------|------|
| **0** | 11B | timestamp(3) + length(3) + type id(1) + **message stream id(4,小端!)** | 每路流的第一个 Chunk / 时间戳回绕兜底 |
| **1** | 7B | timestamp delta(3) + length(3) + type id(1) | 同流，message stream id 复用，长度或类型变了 |
| **2** | 3B | timestamp delta(3) | 同流，长度类型都不变，时间戳走了 |
| **3** | 0B | 无 | 续块：同一个 Message 的第二块及以后，全复用 |

> ⚠️ **面试细节**：除了 `message stream id` 是**小端（little-endian）**，其余多字节字段都是**大端**。这是 RTMP 里唯一一个小端字段，很爱考。

### 4.5 协议控制消息

除了音视频和命令，RTMP 还有一套**协议控制消息**调节传输参数：

| type id | 名称 | 作用 |
|---------|------|------|
| 1 | Set Chunk Size | 协商 Chunk 大小（默认 128，可调到 4096+） |
| 2 | Abort Message | 通知接收方丢弃某条正在收的消息 |
| 3 | Acknowledgement (ACK) | 收到 Window Ack Size 字节后回确认 |
| 5 | Window Acknowledgement Size | 告诉对方"我每次收到多少字节就回一个 ACK" |
| 6 | Set Peer Bandwidth | 告诉对方"我的接收窗口多大" |

**RTMP 流控 vs TCP 流控**：TCP 保证不丢包（传输层），RTMP ACK/Window 保证不冲垮接收端 buffer（应用层）。两层独立，配合工作。

---

## 五、RTMP 与 FLV 的关系（核心认知）

这一章是理解 RTMP 的关键——搞懂"RTMP 和 FLV 到底什么关系"，前面所有 Message/Chunk 概念才有落脚点。

### 5.1 FLV 是什么

**FLV（Flash Video）** 是 Adobe 设计的流式容器格式。它的作用和 MP4、MKV 一样——把编码好的音视频数据按规则"装箱"，标好类型和时间戳。

**为什么直播用 FLV 而不是 MP4？**

| | FLV | MP4 |
|---|-----|-----|
| 索引 | 无全局索引，每个 Tag 自带时间戳，收到即播 | moov box 记录每帧偏移，播放前要先读 moov |
| 流式友好 | ✅ 天然适合，来一个 Tag 播一个 | ⚠️ 需要 faststart 或完整下载 |
| 结构 | 极简：9 字节头 + 11 字节 Tag 头 | 复杂：box 嵌套 |
| 适用场景 | 直播推流、实时传输 | 点播、本地存储 |

FLV 像一条传送带——一个个贴着标签的包裹按顺序经过，你不需要索引，来一个拆一个。

### 5.2 FLV 结构速览

```
┌─────────────────────────┐
│ FLV Header (9 字节)      │  签名 "FLV"(3B) + 版本(1B) + 标志位(1B) + 头大小(4B)
├─────────────────────────┤
│ PreviousTagSize (4 字节) │  第一个恒为 0
├─────────────────────────┤
│ Tag 1                   │  通常是 Script Tag（onMetaData）
│  ├─ Tag Header (11 字节) │    类型(1B) + 数据大小(3B) + 时间戳(3B+1B扩展) + StreamID(3B,恒0)
│  └─ Tag Data (变长)      │    视频/音频/脚本三种
├─────────────────────────┤
│ PreviousTagSize (4 字节) │  = 前一个 Tag 的总大小
├─────────────────────────┤
│ Tag 2, Tag 3, ...       │  音视频 Tag 交替排列
└─────────────────────────┘
```

三种 Tag 的 data 部分：

- **Script Tag（TagType=0x12）**：AMF 编码的 onMetaData（宽高、码率、编码格式）
- **视频 Tag（TagType=0x09）**：`[FrameType+CodecID 1B] [AVCPacketType 1B] [CompositionTime 3B] [NALU 数据...]`
  - `17 00` = 关键帧 + AVC + sequence header（avcC）
  - `17 01` = 关键帧 + AVC + 普通 NALU 数据
  - `27 01` = 非关键帧 + AVC + 普通 NALU 数据
- **音频 Tag（TagType=0x08）**：`[SoundFormat+Rate+Size+Type 1B] [AACPacketType 1B] [AAC 数据...]`
  - `AF 00` = AAC + AudioSpecificConfig
  - `AF 01` = AAC + 裸帧数据

> FLV 结构的更多细节（onMetaData 字段、AVCDecoderConfigurationRecord 解析、AVCC vs Annex-B），见 [05](./05-H264-MP4-NALU.md) §四 ~ §5.5。

### 5.3 核心认知：换壳不换肉

**RTMP 的音视频 Message Body，就是 FLV Tag 的 body（Tag Data）——逐字节相同。** 头信息（类型、时间戳、大小）被"搬家"到了 RTMP 自己的 Chunk/Message Header 里：

```
FLV Tag:                          RTMP Message:
┌──────────────────┬────────────┐  ┌──────────────────────┬────────────┐
│ Tag Header 11B   │ Tag Data   │  │ Chunk/Message Header │ Body       │
│ 类型 →   TagType │真正的音视频 │  │ 类型 →   type id     │ = Tag Data │
│ 时间戳 → Timestamp│   数据     │  │ 时间戳 → timestamp   │   一模一样  │
│ 大小 →   DataSize│            │  │ 大小 →   msg length  │            │
│ StreamID(恒0)    │            │  │         更灵活        │            │
└──────────────────┴────────────┘  └──────────────────────┴────────────┘
```

| FLV 字段 | RTMP 对应 | 说明 |
|----------|----------|------|
| TagType (08/09/12) | message type id (8/9/18) | 类型映射 |
| Timestamp | timestamp / timestamp delta | 时间戳搬家 |
| DataSize | message length | 大小搬家 |
| StreamID (恒0) | message stream id | RTMP 用自己的流编号 |

### 5.4 这个设计的工程红利

**RTMP 转 HTTP-FLV，服务器几乎零成本。** 因为：

```
RTMP Message Body → 拼上 FLV Tag Header(重算类型/时间戳/大小) → HTTP 发出去
                           ↑
                    肉原封不动，只换壳
```

服务器不需要解码重编码，不需要理解 H.264 内部结构，只是做**二进制级别的拼头和拆头**——上万路并发也只耗极少 CPU。

| 转换 | 服务器要做什么 | CPU 开销 |
|------|--------------|----------|
| RTMP → HTTP-FLV | 拆 Message 头、套 FLV Tag 头 | 几乎为零 |
| RTMP → HLS | 重新切片生成 .m3u8 + .ts | 中等 |
| RTMP → WebRTC | 拆 FLV Tag → 拿裸 H.264/AAC → 封成 RTP | 较高 |

### 5.5 Sequence Header 的重要角色

publish 成功后、发数据帧之前，必须发三样东西：

1. **onMetaData**（宽高、码率、编码格式——播放器做初始化判断，缺了不一定崩但不规范）
2. **视频 sequence header（avcC）**——SPS/PPS 参数集，解码器的"说明书"。**没拿到就没法初始化解码器 → 观众黑屏有声音。**
3. **音频 sequence header（AudioSpecificConfig）**——AAC 参数头。

> 中途改分辨率/旋转 → 必须重发 sequence header + IDR。新观众进来拉流 → 服务器从 GOP 缓存里先补 sequence header 再加最近 GOP。

**RTMP 里的视频格式：AVCC（长度前缀），不是 Annex-B（起始码）！** `17 01` 后面跟 `[4B长度] [NALU]`，不是 `00 00 00 01`。进 RTMP 前必须转：Annex-B → 去起始码 + 转长度前缀 + SPS/PPS 抽进 avcC。

---

## 六、字节级详解：从抓包看懂 RTMP

前面讲的是"流程"，这一章把流程**落到字节**——模拟从握手到推第一帧，服务器收到的字节到底长什么样，怎么一个个解出来。

> 约定：`XX` 是十六进制；省略/随机部分用 `…` 表示。

### 6.1 握手字节

```
C0:  03
     └─ 1 字节版本号，固定 3

C1:  00 00 00 00   00 00 00 00   AB 12 7F … (共 1528 字节随机)
     └─ time(4B)   └─ zero(4B)   └─ random(1528B)
     合计 1 + 1536 = 1537 字节
```

- **C0** 就 `03`。
- **C1** 1536 字节：4B 时间戳 + 4B 零 + 1528B 随机。
- **S0/S1** 同构；**S2 回显 C1、C2 回显 S1**。

### 6.2 Chunk 的字节结构（先看懂这个，后面全用得上）

每个 Chunk 的字节布局：

```
┌──────────────┬─────────────────┬───────────────────┬───────────┐
│ Basic Header │ Message Header  │ Extended Timestamp│ Chunk Data│
│  (1~3 字节)   │ (0/3/7/11 字节) │  (0 或 4 字节)     │  (载荷)    │
└──────────────┴─────────────────┴───────────────────┴───────────┘
```

**Basic Header**：高 2 位 `fmt` + 低 6 位 `csid`。
- csid=2~63 → 1 字节；csid=0 → 再补 1 字节（csid=第二字节+64）；csid=1 → 再补 2 字节。
- 例：视频用 csid=6、fmt=0 → `0000 0110` = `0x06`。

**Message Header**：按 fmt 来（见第四章 §4.4 的四种 fmt 表）。`message stream id` 是小端，其余多字节字段大端。

**Extended Timestamp**：当 timestamp（或 delta）字段写满 `FF FF FF` 时，真实时间戳放到这 4 个额外字节里。

### 6.3 connect 命令的 AMF0 字节

命令消息 type id=20（`0x14`），载荷用 AMF0 编码。常用 AMF0 类型标记：

| 标记 | 类型 | 编码 |
|------|------|------|
| `00` | Number | 8 字节 IEEE754 双精度（大端） |
| `01` | Boolean | 1 字节 |
| `02` | String | 2 字节长度 + UTF8 |
| `03` | Object | 一串键值对，以 `00 00 09` 结尾 |
| `05` | Null | 无 |

`connect` 命令载荷逐字节：

```
02 00 07 63 6F 6E 6E 65 63 74          String "connect"  (长度7 + ASCII 'connect')
00 3F F0 00 00 00 00 00 00             Number 1.0        (transaction id，0x3FF0…=1.0)
03                                     Object 开始
   00 03 61 70 70                        键 "app"
   02 00 04 6C 69 76 65                  值 String "live"
   00 05 74 63 55 72 6C                  键 "tcUrl"
   02 00 1C 72 74 6D 70 3A 2F 2F …       值 String "rtmp://…"
00 00 09                               Object 结束
```

- `63 6F 6E 6E 65 63 74` = `connect` 的 ASCII；`61 70 70` = `app`。
- 事务 ID 用 double 表示（`connect` 固定 1.0），服务器回的 `_result` 带同 ID，客户端靠它对号。
- `00 00 09` = Object 结束标志。

### 6.4 视频 sequence header（avcC）字节

一条 51 字节视频 body，逐字节就是 **FLV VIDEODATA + AVCDecoderConfigurationRecord**：

```
17             FrameType=1(关键帧)<<4 | CodecID=7(AVC)  → 0x17
00             AVCPacketType=0  → 这是 sequence header(参数集)
00 00 00       CompositionTime = 0 (seq header 恒为 0)
── 下面是 avcC ──
01             configurationVersion
64             AVCProfileIndication = 100 (High Profile)
00             profile_compatibility
1F             AVCLevelIndication = 31 (Level 3.1)
FF             111111 + 2位lengthSizeMinusOne(11=3) → NALU 长度前缀 4 字节
E1             111 + 5位 numOfSPS(00001=1) → 1 个 SPS
00 19          SPS 长度 = 25
67 64 00 1F …  SPS 数据 (0x67 = nal_ref_idc + type 7，正是 SPS)
01             numOfPPS = 1
00 05          PPS 长度 = 5
68 EE 3C 80 …  PPS 数据 (0x68 = type 8，正是 PPS)
```

- 头两字节 `17 00` 是"关键帧 + AVC + 序列头"招牌组合。
- `FF` 低 2 位决定后续 NALU 用几字节长度前缀（这里是 4）。
- SPS `0x67`、PPS `0x68`，对应 NALU type 7/8。

### 6.5 视频数据帧字节（AVCC，不是 Annex-B！）

```
27             FrameType=2(非关键帧)<<4 | CodecID=7  → 0x27
01             AVCPacketType=1  → 这是 NALU 数据
00 00 00       CompositionTime(cts) = PTS-DTS；无 B 帧时为 0
00 00 02 5A    NALU 长度 = 0x025A = 602 字节  ← 4 字节大端长度前缀(AVCC!)
41 9A …        NALU 数据 (0x41 = 非 IDR slice；IDR 是 0x65)
```

> **这就是 AVCC vs Annex-B 的铁证**：这里用 `00 00 02 5A`（长度前缀），**不是** `00 00 00 01`（起始码）。裸流/RTP 才用起始码；MP4/FLV/RTMP 全用 AVCC 长度前缀。

### 6.6 音频字节（裸 AAC，不是 ADTS！）

音频 sequence header（先发一次）：

```
AF             SoundFormat=10(AAC)<<4 | Rate=3 | Size=1(16bit) | Type=1(立体声) → 0xAF
00             AACPacketType=0 → AudioSpecificConfig
12 10          AudioSpecificConfig:
               0001 0=AOT 2(AAC LC) | 0100=freqIndex 4(44100) | 0010=声道 2 | 000
```

音频数据帧：

```
AF             招牌字节
01             AACPacketType=1 → 裸 AAC 数据
FF F1 …        裸 AAC 载荷
```

> ⚠️ RTMP/FLV 里的 AAC 是**裸帧（raw），没有 ADTS 头**。配置信息已在 AudioSpecificConfig 里给了。从带 ADTS 的源推流要先剥掉 ADTS 头。

### 6.7 大消息分块 + fmt=3 续块

如果一条消息 body 超过 Chunk Size（默认 128），就拆成多个 Chunk。比如一条 256 字节视频消息：

```
06  [11字节 fmt=0 头]  [前 128 字节 body]     ← 第 1 块：完整头
C6  [后 128 字节 body]                         ← 第 2 块：续块
└─ Basic Header: fmt=3(11) + csid=6(000110) = 0xC6，没有 message header
```

- 续块用 **fmt=3**（`0xC6`），**只有 1 字节 Basic Header + body**——类型、长度、时间戳全沿用。这就是"头部压缩"省带宽的来源。
- 推流端一般开头发 Set Chunk Size（type 1）把 128 调到 4096+，减少分块。

### 6.8 完整推流字节时间线

把上面串起来，一次推流服务器收到的字节序列：

```
1.  C0(03) C1(1536B)                 ── 握手发起
2.  收 S0 S1 S2，回 C2               ── 握手完成
3.  Set Chunk Size(type 1) 把 128→4096
4.  connect 命令(type 20, AMF0)       ── §6.3
5.  收 _result + Window Ack + Set Peer BW
6.  createStream(type 20)
7.  收 _result(message stream id=1)
8.  publish("streamKey","live")(type 20)
9.  收 onStatus(NetStream.Publish.Start)
10. onMetaData(type 18, @setDataFrame)
11. 视频 sequence header(type 9, body 17 00 …avcC)  ── §6.4  必须先发！
12. 音频 sequence header(type 8, body AF 00 …ASC)   ── §6.6
13. 视频/音频数据帧(type 9 / 8，按 DTS 交织，超长就 fmt=3 续块)  ── §6.5/§6.7
```

这条时间线抓包（Wireshark 选 RTMPT/RTMP 解析）能一字不差验证，对着上面逐段读就全通了。

---

## 七、实战推流

### 7.1 推流完整链路

| 环节 | 做什么 | 看哪篇 |
|------|--------|--------|
| ① 采集 | 摄像头出 YUV、麦克风出 PCM | [02](./02-像素格式与内存布局.md) / [04](./04-音频PCM-采样-重采样.md) |
| ② 编码 | YUV→H.264、PCM→AAC；直播要低延迟 | [06](./06-编码参数与码控.md) / [11](./11-H264与H265详解.md) |
| ③ 打包 | 把编码数据封成 FLV Tag 体 | [05](./05-H264-MP4-NALU.md) §5.5 |
| ④ 建连 | RTMP 握手 → connect → createStream → publish | 本篇 §三 |
| ⑤ 分块发送 | 把消息切成 Chunk，按时间戳交织音视频 | 本篇 §四 |

直播编码的关键约束（和点播不同）：**必须低延迟**——无 B 帧（否则 PTS≠DTS 引入重排序延迟）、`tune=zerolatency`、CBR 稳定码率、短 GOP（1-2 秒一个关键帧，方便观众随时切入）。详见 [06](./06-编码参数与码控.md) §6.3。

### 7.2 ffmpeg 命令行

```bash
# 文件转直播推流(注意 -re: 按原始帧率推,否则会瞬间全推完冲垮服务器)
ffmpeg -re -i input.mp4 \
    -c:v libx264 -preset veryfast -tune zerolatency \
    -b:v 2500k -maxrate 2500k -bufsize 5000k -g 50 \
    -c:a aac -b:a 128k -ar 44100 \
    -f flv rtmp://live.example.com/app/streamKey
```

- `**-f flv**`：RTMP 推流的封装格式就是 **flv**（因为 RTMP 内部是 FLV Tag）。
- `**-re**`：按原速读，直播链路必加（实时采集源不需要）。
- 码控参数对应 [06](./06-编码参数与码控.md) §6.3 的直播组合。

### 7.3 C API（avformat 路线，主流）

```cpp
AVFormatContext* outputCtx = nullptr;
// 第三参 "flv" 指定封装格式,URL 用 rtmp://;avformat 会自动选 rtmp 协议
avformat_alloc_output_context2(&outputCtx, nullptr, "flv", "rtmp://live.example.com/app/key");

// ... 给 outputCtx 添加视频流(H264)、音频流(AAC),拷贝好 codecpar(含 extradata/avcC) ...

// rtmp 是网络输出,需要打开 IO(flv muxer 不是 NOFILE)
if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
    avio_open(&outputCtx->pb, outputCtx->url, AVIO_FLAG_WRITE);

avformat_write_header(outputCtx, nullptr);   // 这里完成 RTMP 握手 + connect/publish

// 主循环:把编码好的 AVPacket 按时间戳交织写出
//   关键:packet 的 pts/dts 要用输出流的 time_base、且单调;用 interleaved 自动按 dts 交织音视频
while (haveMorePackets) {
    av_packet_rescale_ts(packet, inputTimeBase, outputStream->time_base);
    av_interleaved_write_frame(outputCtx, packet);
}

av_write_trailer(outputCtx);
avio_closep(&outputCtx->pb);
avformat_free_context(outputCtx);
```

要点：
- 输出格式写 `**"flv"**`，URL 给 `rtmp://`——FFmpeg 自动走 RTMP 协议并完成握手/connect/publish，不用手撸协议。
- **flv muxer 会自动把 H.264 转成 AVCC、生成 sequence header**，前提是视频流 `codecpar->extradata` 里有正确的 avcC。
- **时间戳**：`av_interleaved_write_frame` 按 DTS 自动交织；DTS 必须单调递增、用输出流的 `time_base`，否则花屏/卡顿。

### 7.4 librtmp（底层库，了解即可）

`librtmp` 是独立的 RTMP 库，直接暴露 `RTMP_Connect` / `RTMP_SendPacket` 等，需要自己拼 FLV Tag、管时间戳。除非自研推流 SDK 要做极致定制，否则用 FFmpeg 的 avformat 封装更省心。

---

## 八、常见坑与排查

> 推流出问题，**先分清是花屏、黑屏还是卡顿**——三者根因和排查路径不同。

### 8.1 现象三分法

| 现象 | 观众看到什么 | 大概率原因 | 优先查什么 |
|------|------------|-----------|-----------|
| **黑屏** | 有声音、画面全黑 | 没发/没收 sequence header（avcC）；解码器没初始化 | publish 后是否先发 `0x17 0x00`；SPS/PPS 有效吗 |
| **花屏** | 马赛克、绿块、撕裂 | 码流格式错（Annex-B/AVCC）、参考帧断了、IDR 丢失 | 裸流格式、丢帧策略、GOP/关键帧 |
| **卡顿** | 帧率低、一顿一顿 | TCP 反压、编码/发送同步阻塞、发热降频 | 发送队列水位、本地录制对比、FPS 打点 |
| **忽快忽慢/音画不同步** | 变速、嘴型漂移 | 时间戳(DTS/PTS)乱、没交织、B 帧重排 | DTS 单调性、`av_interleaved_write_frame`、关 B 帧 |

**易混点**：
- **首帧花一下再正常** → sequence header 晚到或首包不是 IDR
- **全程花** → AVCC 封装从头到尾错了
- **弱网后突然花几秒** → 丢参考帧但没补 IDR
- **本地录制也花** → 编码器/颜色格式问题，**别先查 RTMP**

### 8.2 排查流程（公司里也这么干）

```text
Step 1  现象定性
        ├─ 全程花？偶发花？刚开播就花？播久了才花？
        └─ 本地录制正常、只有 RTMP 花？→ 偏网络/发送侧

Step 2  隔离编码/封装
        ├─ dump 推流前裸 H.264 或 FLV，ffplay 本地播
        ├─ 本地也花 → 编码/颜色格式/AVCC 转换问题
        └─ 本地正常、观众花 → 传输/服务器/拉流端

Step 3  查 sequence header
        ├─ publish 后是否先发 avcC + AudioSpecificConfig
        ├─ 中途改分辨率/旋转是否重发 sequence header
        └─ Wireshark：第一个视频包是否 17 00（AVC sequence header）

Step 4  查封装格式
        ├─ 是否误把 Annex-B（00 00 00 01）塞进 RTMP
        ├─ AVCC 长度前缀字节数是否和 avcC 里 lengthSizeMinusOne 一致
        └─ AAC 是否误带 ADTS 头

Step 5  查时间戳
        ├─ 视频 DTS 是否严格单调递增
        ├─ 是否 av_interleaved_write_frame 交织
        └─ time_base 是否正确 rescale

Step 6  查编码与丢帧
        ├─ 队列满时是否随机丢帧（应丢整条 GOP 或立刻 IDR）
        ├─ 弱网后是否 request IDR / insert keyframe
        └─ 直播是否关了 B 帧
```

**面试 30 秒收口**：「封装（AVCC + sequence header）→ 时间戳（DTS 单调 + 交织）→ 参考帧（GOP 级丢帧 + IDR）→ 本地录制隔离编码 vs 网络。」

### 8.3 花屏专项

| 原因 | 典型表现 | 处理方法 |
|------|---------|---------|
| **Annex-B 误当 AVCC 推** | 全程花、马赛克 | 去起始码、改 4 字节长度前缀；SPS/PPS 放进 sequence header |
| **sequence header 缺失/过期** | 开播花、新观众进来花几秒 | publish 后先发 avcC；分辨率/旋转变化后**重发** |
| **DTS 回退 / 时间基准错** | 撕裂、忽快忽慢 | DTS 单调；rescale 到输出 time_base |
| **丢参考帧（丢帧策略错）** | 弱网/队列满后突然花 | **按 GOP 丢**，不随机丢 P 帧；队列满立刻 `request IDR` |
| **IDR 来得太晚** | 中途切入、重连后花 | 缩短 GOP（1~2s）；重连后强制关键帧 |
| **NALU 长度前缀不一致** | 部分机型花、间歇花 | avcC 声明与打包逻辑一致（通常 4 字节） |
| **编码器吐坏流** | **本地录制也花** | 查 stride/颜色格式；CBR + 关 B 帧 |
| **中途 SPS 变了没通知** | 旋转/切分辨率后花 | 停流或重发 sequence header + IDR |

**一眼区分 AVCC vs Annex-B（抓包/xxd）**：
```
AVCC 数据帧：17 01 [4B长度] 65/41/01...   ← RTMP 正确形态
Annex-B：    00 00 00 01 67/68/65...       ← 进 RTMP 必花
```

### 8.4 实用排查命令

```bash
# 同时推流 + 本地录制（隔离编码和网络）
ffmpeg -i input ... -f flv rtmp://... -c copy -f mp4 local_record.mp4

# 拉流存 FLV 本地验证
ffmpeg -i rtmp://... -c copy -t 30 dump.flv && ffplay dump.flv

# 看第一个视频包的头 2 字节
ffmpeg -i rtmp://... -c copy -t 1 -f flv - 2>/dev/null | xxd | head -20
# 必须是 17 00

# 看抓包里的 RTMP 消息
tshark -r rtmp.pcap -Y 'rtmp' -T fields \
  -e frame.number -e rtmp.timestamp -e rtmp.type_id -e rtmp.body_size
```

### 8.5 速查表

| 症状 / 误区 | 根因 | 怎么修 |
|-----------|------|--------|
| 端口连不上 | 1935 偏门端口被企业防火墙封 | 换 RTMPS(443) |
| 文件推流瞬间结束/服务器卡死 | 忘了 `-re` | 文件源加 `-re` |
| 拉流端**黑屏有声音** | 没先发视频 sequence header | publish 后先发 avcC |
| **首帧花屏**后恢复 | sequence header 晚到或首包不是 IDR | avcC 在首帧数据前；首包尽量 IDR |
| **全程花屏/马赛克** | Annex-B 误用、NALU 边界错 | 转 AVCC；查长度前缀 |
| **弱网后突然花屏** | 丢参考帧、没补 IDR | GOP 级丢帧 + `request IDR` |
| 画面花屏、音画不同步 | DTS 不单调或没对齐 | DTS 单调 + `av_interleaved_write_frame` |
| **卡顿/帧率掉成 PPT** | TCP 反压、编码推流同步阻塞 | 异步发送队列 + ABR |
| 误以为 RTMP 视频是 Annex-B | 实际用 AVCC 长度前缀 | 从 Annex-B 源推流先转 AVCC |
| 音频推上去拉流端解不出 | AAC 误带 ADTS 头 | 推流前剥掉 ADTS 头 |
| 推了 B 帧导致延迟高 | PTS≠DTS 引入重排序延迟 | 无 B 帧 + zerolatency |
| 码率忽高忽低冲垮带宽 | CRF/VBR 瞬时尖峰 | 直播 CBR + maxrate + bufsize |
| 推 H.265 拉流端放不出 | 非标扩展，链路不兼容 | enhanced RTMP 或换协议 |

---

## 九、行业现状与协议演进

### 9.1 RTMP 推流的三大流派

市面上的厂家做 RTMP 推流，分化成了三条技术路线：

**流派 A：FFmpeg 深度定制派（最主流）**
- 做法：底层链接 `libavformat`，采集和图像处理走 GPU，编码走端侧硬编（MediaCodec / VideoToolbox）。硬编出的 NALU 丢给 FFmpeg，利用 `av_write_frame` 写入 RTMP 地址。
- 为什么编码不交给 FFmpeg：`libx264` 软编在手机上 1080P 编码 CPU 瞬间满载，商业产品不可接受。
- 为什么网络层不全靠 FFmpeg：默认 RTMP 推流网络层简单，弱网下 buffer 容易阻塞。大厂只让 FFmpeg 负责把音视频打包成 FLV，数据塞入**自研异步发送队列**配合弱网对抗。
- 代表：中大型直播 App、音视频 SDK 服务商（声网、即构、腾讯云 SDK）。

**流派 B：完全自研/轻量级开源库派（追求极致体积与性能）**
- 做法：FFmpeg 裁剪后都有几 MB，嵌入式芯片上选择完全自己用 C/C++ 实现 RTMP 协议栈，或者用轻量级库（`srs_librtmp`、`librtmp`）。
- 代表：对包体积有极致要求的工具类 App、嵌入式安防监控 IPC 芯片。

**流派 C：现代实时互动流派（WebRTC / SRT 取代 RTMP）**
- 现状：抖音、快手、淘宝直播的推流端，核心链路早已切到基于 UDP 的 **SRT** 或 **WebRTC**，实现低于 1 秒甚至低于 500ms 的超低延时。
- RTMP 的剩余价值：作为全行业兼容的保底推流手段——SRT/WebRTC 打不通时降级回 RTMP。

**总结**：FFmpeg 是全行业的基石，但大厂的推流源码里它被改得"面目全非"——采集走 GPU、美颜走 AI、编码走系统硬解、网络走自研。FFmpeg 常只扮演"数据协议打包员"角色。

### 9.2 RTMP vs SRT vs WebRTC

| | RTMP | SRT | WebRTC |
|---|------|-----|--------|
| 传输层 | TCP | 可靠 UDP | UDP + RTP |
| 延迟 | 1~3s | 几百 ms ~ 1s | 几百 ms |
| 抗丢包 | 弱（队头阻塞） | 强（选择性重传+拥塞控制） | 强（NACK/FEC） |
| 生态 | 最成熟，所有 CDN 都支持 | 快速成长，跨国回传首选 | 互动直播标准，架构重 |
| 适用 | 传统直播推流，兼容保底 | 弱网/跨国回传，点对点/多对一 | 连麦/互动/超低延迟直播 |
| 建连复杂度 | 简单（TCP 直连） | 简单（UDP 直连+握手） | 重（ICE 打洞+SFU+TURN） |

一句话：**稳妥选 RTMP，弱网回传选 SRT，要互动选 WebRTC。**

### 9.3 Enhanced RTMP（H.265/AV1 推流）

标准 RTMP 视频消息（type 9）body 首字节 CodecID 只有 4 位（最大 15），原生只支持到 H.264（7）。H.265 用非标 CodecID=12 硬塞，不同 CDN/播放器兼容靠运气。

**Enhanced RTMP**（2023 年 FFmpeg 社区推动）：用 4 字节 FourCC（`hvc1`=HEVC、`av01`=AV1）取代 4 位 CodecID，VideoTag 头从 1 字节扩展到 5 字节。推 H.265 到 RTMP 要么用 Enhanced RTMP（服务器要支持，如 SRS 6.0+），要么换协议（SRT/WebRTC 原生支持 H.265，不用打补丁）。

### 9.4 生态

- **推流软件**：OBS Studio（最广）、FFmpeg、各厂手机直播 SDK。
- **流媒体服务器**：SRS（国产、流行）、nginx-rtmp-module、Wowza、各云厂商直播服务。
- **趋势**：推流仍以 RTMP 为主；低延迟/弱网场景 SRT 和 WebRTC 在抬头。分发端 RTMP 早已退给 HTTP-FLV / HLS / WebRTC。
---

## 十四、MediaCodec 编码推 RTMP：SPS/PPS 发送时序

Android 用 `MediaCodec` 做 H.264 硬编码再推 RTMP 时，最容易漏的是 sequence header。RTMP/FLV 播放端必须先拿到 AVCDecoderConfigurationRecord，也就是通常说的 avcC，里面包含 SPS/PPS。

典型时序：

```text
MediaCodec.configure(encoder)
  -> start
  -> dequeueOutputBuffer 返回 INFO_OUTPUT_FORMAT_CHANGED
  -> 从 outputFormat 读取 csd-0(SPS)、csd-1(PPS)
  -> 组装 FLV AVC sequence header
  -> 发送 RTMP metadata / video sequence header
  -> 后续普通视频帧按 FLV Video Tag 发送
```

Android 侧注意点：

| 点 | 说明 |
|---|---|
| `csd-0` / `csd-1` | 通常分别是 SPS/PPS，可能带 Annex-B start code，组 avcC 前要处理 |
| sequence header | 必须在普通视频帧前发送，否则播放器可能黑屏等待参数集 |
| IDR 前补 SPS/PPS | 弱网、断线重连、新观众切入时更稳，但要注意封装格式要求 |
| Annex-B vs AVCC | `MediaCodec` 输出和 RTMP/FLV 需要的格式不一定一致，发送前要转换 |

面试可以这样收口：MediaCodec 负责产出编码帧，但 RTMP/FLV 需要的是“可被播放器初始化的封装流”。所以除了编码本身，还要处理 SPS/PPS、avcC、Video Tag 类型、时间戳和关键帧标记。
