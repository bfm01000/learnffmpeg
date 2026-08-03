# TCP 协议深度解析与面试指南：从三次握手到弱网下的音视频传输

TCP 是互联网的基石协议，也是音视频工程师面试中的高频考点。很多候选人能说出"三次握手、四次挥手"，但被追问"SYN 队列满了怎么办""TIME_WAIT 为什么是 2MSL""TCP 的队头阻塞如何影响 H.264 码流""TFO 是怎么做到 0-RTT 的"就容易卡壳。

本文按资深 C++ 音视频开发的面试视角，从协议设计原理到内核机制，再到音视频场景下的工程实践，逐层深入。

---

## 一、TCP 在协议栈中的位置

```text
应用层:    HTTP / RTMP / WebSocket / FLV / RTP
              |
传输层:    TCP  <-- 本文主角    |   UDP
              |
网络层:    IP (分片、路由、TTL)
              |
链路层:    Ethernet / Wi-Fi / 4G/5G
```

TCP 向上提供了一条**可靠的、有序的、面向连接的字节流**，向下依赖 IP 的尽力交付。音视频工程师需要记住的关键矛盾：

> **TCP 保证可靠有序 → 队头阻塞 → 音视频帧的实时性被牺牲。**

面试一句话总结：

> TCP 在不可靠的 IP 层之上，通过确认重传、滑动窗口、拥塞控制三大机制，向上提供可靠字节流。代价是延迟不可预测，尤其在丢包场景下会被放大。

---

## 二、TCP 头部格式（必须能画出来）

面试中如果被要求"画出 TCP 头部并解释每个字段"，以下 20 字节固定头部要能默写：

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |  4 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |  4 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number                      |  4 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Data |           |U|A|P|R|S|F|                               |
| Offset| Reserved  |R|C|S|S|Y|I|            Window             |  4 字节
|       |           |G|K|H|T|N|N|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Checksum            |         Urgent Pointer        |  4 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (if Data Offset > 5)               |  可变
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

各字段面试解释：

| 字段 | 位数 | 说明 |
|------|------|------|
| **Source / Dest Port** | 16+16 | 端口号。标识进程。 |
| **Sequence Number** | 32 | 本报文段第一个字节的序号。初始序号 ISN 随机生成（防老包干扰）。 |
| **Acknowledgment Number** | 32 | 期望收到的下一个字节序号。累计确认：确认 N 意味着 N-1 及之前全部收到。 |
| **Data Offset** | 4 | TCP 头部长度，以 4 字节为单位。最小 5（20 字节），最大 15（60 字节）。 |
| **URG** | 1 | 紧急指针有效（几乎不用）。 |
| **ACK** | 1 | 确认号有效。连接建立后几乎所有报文都置 1。 |
| **PSH** | 1 | 提示接收方尽快交给应用层，不要缓存。音视频推流常用。 |
| **RST** | 1 | 强制重置连接。常见于：端口未监听、连接超时、半开连接。 |
| **SYN** | 1 | 建立连接。仅在三次握手时使用。 |
| **FIN** | 1 | 关闭连接。仅在四次挥手时使用。 |
| **Window** | 16 | 接收窗口大小。告诉对方本端还能收多少字节。窗口扩缩因子 Option 可提升上限。 |
| **Checksum** | 16 | 校验 TCP 头部+数据。必选项，不同于 UDP 的可选。 |
| **Options** | 可变 | MSS（最大段大小）、窗口扩缩、时间戳、SACK、TFO Cookie 等。 |
| **Data** | 可变 | 应用层负载。 |

**面试追问：MSS 是什么？和 MTU 什么关系？**

> MTU（Maximum Transmission Unit）是链路层帧的最大载荷，以太网通常是 1500 字节。MSS（Maximum Segment Size）是 TCP 层每段能携带的最大数据量 = MTU - IP 头(20) - TCP 头(20) = 1460 字节。三次握手时双方通过 Option 协商 MSS，取较小值。MSS 设置合理能避免 IP 分片。

**音视频相关追问：PSH 标志在推流中怎么用？**

> 推流端在发送音频帧时通常会设置 PSH，因为音频帧很小（几十到几百字节）且对延迟敏感，不应该等发送缓冲区凑满再发。服务端收到 PSH 后立即将缓冲区数据交给应用层，减少排队延迟。

---

## 三、连接建立：三次握手

### 3.1 标准流程

```text
Client                              Server
  |                                    |
  | -------- SYN, seq=x -------------> |  ① 客户端：我要连你，我初始序号是 x
  |                                    |     客户端：CLOSED -> SYN_SENT
  |                                    |     服务端：LISTEN
  |                                    |
  | <---- SYN+ACK, seq=y, ack=x+1 ---  |  ② 服务端：收到，我初始序号是 y，确认你到 x
  |                                    |     客户端：SYN_SENT
  |                                    |     服务端：LISTEN -> SYN_RCVD
  |                                    |
  | -------- ACK, seq=x+1, ack=y+1 --> |  ③ 客户端：确认收到
  |                                    |     客户端：SYN_SENT -> ESTABLISHED
  |                                    |     服务端：SYN_RCVD -> ESTABLISHED
```

### 3.2 为什么是三次，不是二次或四次？

这是面试必问题。核心答案：

**TCP 是全双工通信，需要双方各自确认对方发送和接收能力正常。**

- **第一次握手**：服务端确认了"客户端能发"。
- **第二次握手**：客户端确认了"服务端能收能发"。
- **第三次握手**：服务端确认了"自己能发、客户端能收"。

如果用两次握手，经典故障场景——**历史 SYN 的超时重传**：

```text
Client 发 SYN(seq=90)，网络延迟。
Client 超时，重发 SYN(seq=100)。
Server 收到 SYN(seq=100)，回 SYN+ACK(ack=101)，连接建立。
Client 通信完毕，断开。

此时网络中延迟的那个 SYN(seq=90) 终于到达 Server。
如果只有两次握手，Server 回 SYN+ACK(ack=91) 后立即认为连接已建立，
但实际上 Client 早已不需要这个连接，白白浪费 Server 资源。
```

有第三次握手后：Client 收到一个不期望的 SYN+ACK(ack=91) 时会发 RST 拒绝，Server 收到 RST 后释放半连接。

### 3.3 SYN 队列与 SYN Flood 攻击

面试中几乎必问。内核维护两条队列：

| 队列 | 存储内容 | 状态 |
|------|---------|------|
| **半连接队列（syn queue）** | 收到 SYN 但未收到第三次 ACK 的连接 | SYN_RCVD |
| **全连接队列（accept queue）** | 已完成三次握手、等待 accept() 取走的连接 | ESTABLISHED |

**SYN Flood 攻击**：攻击者只发 SYN，不回最后 ACK，塞满半连接队列，让正常请求无法建立连接。

防御手段：

- **SYN Cookie**：收到 SYN 时不分配资源，而是把连接信息（IP/端口/MSS）编码进 ISN 的 Cookie 中返回。收到第三次 ACK 时验证 Cookie，通过才分配资源。`net.ipv4.tcp_syncookies = 1`。
- **增大半连接队列**：`tcp_max_syn_backlog`。
- **缩短 SYN_RCVD 超时时间**：`tcp_synack_retries`。

**面试追问：SYN Cookie 具体是怎么算出来的？**

> 服务端收到 SYN 后，用（源 IP、源端口、目的 IP、目的端口、时间戳）加上一个内核私钥，通过哈希函数计算出一个值，放入返回的 SYN+ACK 的 seq 号中。收到第三次 ACK 时，从 ack 号减去 1 得到该值，用同样的参数和私钥重新算一遍，匹配则验证通过。优点是零存储，缺点是丢失了部分 Option（如窗口扩缩），可配合 TimeStamp Option 补救。

### 3.4 TFO（TCP Fast Open）：0-RTT 建立连接

音视频场景中连接建立延迟很关键。TFO 允许在第三次握手的 ACK 中携带数据：

```text
第一次连接（获取 Cookie）：
  Client -> Server: SYN + TFO Cookie Request
  Server -> Client: SYN+ACK + Cookie

后续连接（使用 Cookie）：
  Client -> Server: SYN + TFO Cookie + Data（数据已在第一个包中）
  Server -> Client: SYN+ACK + Data（Server 数据在第二个包中）
```

**适用场景**：短连接频繁建连（HTTP/1.1 的短连接模式、CDN 回源）。

**音视频意义**：推流重连时，TFO 可以在 SYN 包里直接把第一个音视频帧带出去，省掉一次 RTT 的握手开销。移动网络下 RTT 可能 100-300ms，这个优化很可观。

---

## 四、连接释放：四次挥手

### 4.1 标准流程

```text
Client                              Server
  |                                    |
  | -------- FIN, seq=u -------------> |  ① Client: 我没有数据要发了（半关闭）
  |                                    |     Client: ESTABLISHED -> FIN_WAIT_1
  |                                    |
  | <--- ACK, ack=u+1 --------------- |  ② Server: 收到，我知道了
  |                                    |     Server: ESTABLISHED -> CLOSE_WAIT
  |                                    |     Client: FIN_WAIT_1 -> FIN_WAIT_2
  |                                    |
  |          Server 可能继续发数据      |     ← Server 仍可单向发送
  |                                    |
  | <--- FIN, seq=v, ack=u+1 -------- |  ③ Server: 我也发完了
  |                                    |     Server: CLOSE_WAIT -> LAST_ACK
  |                                    |
  | -------- ACK, ack=v+1 -----------> |  ④ Client: 收到
  |                                    |     Client: FIN_WAIT_2 -> TIME_WAIT
  |                                    |     Server: LAST_ACK -> CLOSED
  |                                    |
  |  Client 等待 2MSL 后 -> CLOSED     |
```

### 4.2 为什么要四次而不是三次？

> TCP 是全双工的，一端说"我发完了"（FIN），只是关闭了自己这一半的发送通道，另一端还可以继续发数据。所以 FIN 和 ACK 需要分开。如果服务端在收到 FIN 时没有要发的数据，可以把步骤②的 ACK 和步骤③的 FIN 合并成 FIN+ACK——这就是"三次挥手"（少一个 ACK）。

实际中你抓包看到的很多关闭都是三次报文，因为服务端通常没有额外数据要发。

### 4.3 同时关闭

两端同时发 FIN 也是合法的：

```text
Client: ESTABLISHED -> FIN_WAIT_1 (发 FIN) -> CLOSING (收到 FIN, 发 ACK) -> TIME_WAIT
Server: ESTABLISHED -> FIN_WAIT_1 (发 FIN) -> CLOSING (收到 FIN, 发 ACK) -> TIME_WAIT
```

双方都进入 TIME_WAIT。

---

## 五、TIME_WAIT 状态：面试重灾区

### 5.1 为什么要有 TIME_WAIT？

主动关闭连接的一方（发出最后一个 ACK 的那端）进入 TIME_WAIT，等待 **2MSL**（Maximum Segment Lifetime，通常 2 分钟，Linux 实际约 60 秒）。

两个核心原因：

1. **确保最后一个 ACK 能被对端收到**。如果 ACK 丢了，对端会重传 FIN，本端重发 ACK。TIME_WAIT 期间保持端口不释放就是为了能处理这种重传。

2. **防止"老连接的迷路包"干扰新连接**。如果本端立即释放端口，同一五元组（源IP、源端口、目的IP、目的端口、协议）的新连接可能收到上一个连接在网络中延迟到达的包。等待 2MSL（一个往返最大生存时间的两倍）让网络中该连接的所有报文都消亡。

**面试追问：MSL 为什么定义为 2 分钟？**

> MSL 是一个 IP 包在网络中的最大存活时间，定义为 TTL 耗尽前的最大跳数 × 每跳最大延迟。历史原因是早期互联网带宽低、路由慢，定为 120 秒。现代网络中绝大部分延迟在几十毫秒内，但标准一直保留。Linux 内核中 TIME_WAIT 实际约为 60 秒（`net.ipv4.tcp_fin_timeout` 的竞态在 MSL 计算中有特殊处理）。

### 5.2 TIME_WAIT 过多怎么办？

在反向代理、推流服务器等大量短连接场景下，TIME_WAIT 会耗尽可用端口（单机最多约 6 万个可用端口——`net.ipv4.ip_local_port_range` 默认 32768-60999）。

应对策略（面试要从原理到参数都说清楚）：

| 方案 | 原理 | 命令 |
|------|------|------|
| **增大端口范围** | 扩展可用端口池 | `ip_local_port_range = 1024 65535` |
| **TIME_WAIT 复用** | 同一五元组复用（仅客户端） | `tcp_tw_reuse = 1`（需时间戳支持） |
| **TIME_WAIT 快速回收** | 减少 TIME_WAIT 时间（但不安全，新内核已移除） | 旧内核 `tcp_tw_recycle = 1`（NAT 下会出 bug） |
| **让对端主动关闭** | HTTP 加 `Connection: close` Header 让 Server 关；或者应用层协议由服务端发 FIN |
| **SO_REUSEADDR** | 允许重用处于 TIME_WAIT 的端口用于绑定监听 | `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, …)` |
| **SO_LINGER** | 设置 `l_onoff=1, l_linger=0` 发送 RST 代替 FIN，直接跳过 TIME_WAIT | 但会丢弃发送缓冲区数据，不推荐 |

### 5.3 SO_REUSEADDR vs SO_REUSEPORT

这是高频面试区分点：

| 选项 | 作用 | 场景 |
|------|------|------|
| **SO_REUSEADDR** | 允许绑定到 TIME_WAIT 状态的端口（即使四元组冲突）。还允许同一端口绑定到不同 IP。 | 服务端重启后立即绑定同一端口。 |
| **SO_REUSEPORT** | 允许多个 socket 绑定到**完全相同的 IP+Port**。内核将新连接均衡分发到各 socket。 | 多进程/多线程 accept 同一端口，提高并发性能。Go net.Listen 默认启用。 |

---

## 六、TCP 状态机（完整迁移图）

面试能讲清楚关键状态跃迁即可，不需要逐条背，但要能画这个简化版：

```text
                              +---------+ ---------\      active OPEN
                              |  CLOSED |            \    -----------
                              +---------+<---------\   \   create TCB
                                |     ^              \   \  snd SYN
                   passive OPEN |     |   CLOSE        \   \
                   ------------ |     | ----------       \   \
                    create TCB  |     | delete TCB         \   \
                                V     |                      \   \
                              +---------+            CLOSE    |    \
                              |  LISTEN |          ---------- |     |
                              +---------+          delete TCB |     |
                   rcv SYN      |     |     SEND              |     |
                  -----------   |     |    -------            |     V
 +---------+      snd SYN,ACK  /       \   snd SYN          +---------+
 |         |<-----------------           ------------------>|         |
 |   SYN   |                    rcv SYN                     |   SYN   |
 |   RCVD  |<-----------------------------------------------|   SENT  |
 |         |                    snd ACK                     |         |
 |         |------------------           -------------------|         |
 +---------+   rcv ACK of SYN  \       /  rcv SYN,ACK       +---------+
   |           --------------   |     |   -----------
   |                  x         |     |     snd ACK
   |                            V     V
   |  CLOSE                   +---------+
   | -------                  |  ESTAB  |
   | snd FIN                  +---------+
   |                   CLOSE    |     |    rcv FIN
   V                  -------   |     |    -------
 +---------+          snd FIN  /       \   snd ACK          +---------+
 |  FIN    |<-----------------           ------------------>|  CLOSE  |
 | WAIT-1  |------------------                              |   WAIT  |
 +---------+          rcv FIN  \                            +---------+
   | rcv ACK of FIN   -------   |                            CLOSE  |
   | --------------   snd ACK   |                           ------- |
   V        x                   V                           snd FIN V
 +---------+                  +---------+                   +---------+
 |FINWAIT-2|                  | CLOSING |                   | LAST-ACK|
 +---------+                  +---------+                   +---------+
   |                rcv ACK of FIN |                 rcv ACK of FIN |
   |  rcv FIN       -------------- |                 -------------- |
   |  -------              x       V        snd ACK of FIN         V
   \  snd ACK                 +---------+                   +---------+
    ------------------------->|TIME WAIT|<------------------|  CLOSED |
                              +---------+                   +---------+
                                   |     (2MSL timer)
                                   +-----------
                                   delete TCB

              图：TCP 连接状态机（Jeffrey Mogul, 1992）
```

**面试中最关键的几条跃迁路径：**

| 角色 | 路径 |
|------|------|
| Client 主动建连 | CLOSED → SYN_SENT → ESTABLISHED |
| Server 被动接受 | CLOSED → LISTEN → SYN_RCVD → ESTABLISHED |
| Client 主动关连 | ESTABLISHED → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED |
| Server 被动关连 | ESTABLISHED → CLOSE_WAIT → LAST_ACK → CLOSED |
| 同时关闭 | 双方都走 ESTABLISHED → FIN_WAIT_1 → CLOSING → TIME_WAIT → CLOSED |

---

## 七、可靠传输：确认、重传与滑动窗口

### 7.1 累计确认（Cumulative ACK）

TCP 的确认不是逐个确认每个包，而是确认"连续收到的最长前缀"。

```text
发送方发送 seq 1-1000, 1001-2000, 2001-3000, 3001-4000
接收方收到：seq 1-1000 ✓, seq 1001-2000 ✓, seq 3001-4000 ✓（2001-3000 丢了）
接收方回复 ACK num = 2001（期望收到 2001）
```

即使 seq 3001-4000 已经到达，因为 2001-3000 没到，ACK 仍然是 2001。这就是**队头阻塞**的来源。

### 7.2 超时重传（RTO）

RTO（Retransmission Timeout）是重传定时器。如果发送一个包后 RTO 内没有收到 ACK，触发重传。

**RTO 的计算——Jacobson/Karels 算法**（面试要能讲思路）：

```text
SRTT        = (1 - α) × SRTT + α × RTT_sample      // 平滑 RTT，α 通常 1/8
RTTVAR      = (1 - β) × RTTVAR + β × |SRTT - RTT_sample|  // RTT 偏差，β 通常 1/4
RTO         = SRTT + max(G, 4 × RTTVAR)             // G 是时钟粒度

RTT_sample 取哪一次？Karn 算法：重传的包不参与 RTT 计算（不知道 ACK 对应原始还是重传）。
用 Timestamp Option 可以解决这个问题——每个包带上时间戳，ACK 回带。
```

RTO 的初始值通常为 1 秒，最小 200ms（Linux 下 `TCP_RTO_MIN`）。

### 7.3 快速重传（Fast Retransmit）

不等超时，靠重复 ACK（Duplicate ACK）来检测丢包：

```text
发送方连续发送 seq=1, 1001, 2001, 3001, 4001 五个包
seq=2001 丢了，接收方收到 3001, 4001 时，每次都回复 ACK=2001（重复 ACK）

发送方收到 3 个重复 ACK（dup ACK）后，不等超时，立即重传 seq=2001。
```

**为什么是 3 个？** 1-2 个重复 ACK 可能是乱序而非丢包，3 个重复 ACK 基本可以断定丢包。这个值在 Linux 中可以配置（但通常不动）。

### 7.4 SACK（Selective ACK）

累计 ACK 的问题是：发送方不知道哪一段丢了。SACK 在 TCP Option 中报告已收到的非连续块：

```text
ACK=2001, SACK=3001-4000, 5001-6000
意思是：期望 2001，但已经收到了 [3001,4000) 和 [5001,6000)
```

发送方据此可以只重传 [2001,3000)，不重传 [3001,6000)——这就是**选择性重传**。

**音视频场景**：SACK 对高带宽长肥网络（LFN）很重要。但要注意，SACK 仍然解决不了队头阻塞——应用层依然要按照顺序读数据，即使后面的包已经到达。

**面试追问：SACK 和 DSACK 有什么区别？**

> DSACK（Duplicate SACK）在 SACK 块中记录"收到了重复的段"，帮助发送方区分是"网络真的丢包了"还是"ACK 丢了"还是"网络复制了包"。DSACK 能让拥塞控制算法更准确地判断网络状况。

---

## 八、滑动窗口与流量控制

### 8.1 基本原理

TCP 的发送窗口由接收方通过 TCP 头部的 Window 字段通告：

```text
发送方
  [已发送已确认] [已发送未确认] [可发送未发送] [不可发送]
                 |<------- 发送窗口 --------->|
                 |  由接收方 Window 字段控制   |

接收方
  [已确认已递交] [已接收未递交] [空闲缓冲区]
  |            |                |<-- Win --->|
  |            |<--- 接收窗口 --->|
```

**接收方通告窗口决定发送方的发送量，防止慢接收端被打爆（bufferbloat）。**

### 8.2 窗口扩缩因子（Window Scale Option）

TCP 头部 Window 字段只有 16 位，最大 65535 字节。高带宽延迟积（BDP）的网络（如 1Gbps × 100ms RTT = 12.5MB BDP）需要更大的窗口。

Window Scale Option 在 SYN 中协商扩缩因子（最大 14，即左移 14 位），使最大窗口达到：65535 × 2^14 ≈ 1GB。

### 8.3 零窗口探测（Zero Window Probe）

当接收方通告窗口为 0 时，发送方停止发送数据，并启动**持续定时器（Persist Timer）**，周期性发零窗口探测包（ZWP）查询窗口是否恢复。

**面试追问：零窗口和接口缓冲区有什么关系？**

> 接收方应用层没有及时 `read()`/`recv()`，内核 socket 接收缓冲区逐渐填满，最终 TCP 头部通告窗口降为 0。所以零窗口问题本质上是**应用层处理速度跟不上网络速度**的信号。排查方向：应用线程是否阻塞、CPU 是否打满、是否有锁争用（这在推流服务端非常常见）。

### 8.4 糊涂窗口综合征（Silly Window Syndrome）

**现象**：接收方每次只读几字节，通告窗口只扩大几字节，发送方发送小包，效率极低。

**解决**：

- **接收方**：David Clark 方案——窗口增大不到 MSS 或缓冲区的一半时不通告新窗口。
- **发送方**：Nagle 算法——只有前面发的包被 ACK 后，或者数据凑满 MSS，或者连接空闲时才发（参见下文 Nagle vs TCP_NODELAY）。

---

## 九、拥塞控制：TCP 怎么"感知"网络

拥塞控制是 TCP 最复杂的部分，也是面试中最能体现深度的话题。核心要区分：**拥塞窗口（cwnd）** 由网络状况决定，**接收窗口（rwnd）** 由接收方能力决定。实际发送窗口 = min(cwnd, rwnd)。

### 9.1 经典算法演进（面试至少要能讲 Reno）

网络丢包有两种信号：

- **超时（RTO）**：网络严重拥塞，大量丢包。
- **重复 ACK（3 dup ACK）**：网络轻度拥塞，个别包丢了。

不同信号触发不同响应：

| 信号 | 严重程度 | 响应 |
|------|---------|------|
| RTO 超时 | 重度 | ssthresh = cwnd/2, cwnd = 1 MSS, 重新慢启动 |
| 3 dup ACK | 轻度 | ssthresh = cwnd/2, cwnd = ssthresh + 3 MSS, 快速恢复 |

### 9.2 Tahoe → Reno → New Reno

**Tahoe（最早）**：无论超时还是 dup ACK，一律 `cwnd=1, 慢启动`。效率低，丢一个包就回到解放前。

**Reno（主流基准）**：引入**快速重传**和**快速恢复**。dup ACK 触发快速重传后不降到慢启动，而是 cwnd 减半后从 ssthresh 继续拥塞避免。但一次 RTT 内只能处理一个丢包。

**New Reno**：改进 Reno 的"一个 RTT 只能恢复一个丢包"问题。引入部分 ACK（Partial ACK）——如果新 ACK 确认了部分数据但不完全，说明还有丢包，继续重传而不退出快速恢复。

**面试追问：New Reno 的快速恢复具体怎么做？**

```text
1. 收到 3 dup ACK: ssthresh = cwnd/2
2. 快速重传丢包
3. cwnd = ssthresh + 3 MSS（3 dup ACK 说明有 3 个包已经被收到了）
4. 每次再收到 dup ACK: cwnd += 1 MSS（说明网络还能消化）
5. 收到新 ACK（Partial ACK 或 Full ACK）:
   - Partial ACK：说明还有丢包，继续重传，保持快速恢复
   - Full ACK：所有包都确认了，cwnd = ssthresh，进入拥塞避免
```

### 9.3 Cubic（Linux 默认）

Cubic 是 BIC 的改进版，使用三次函数而不是线性探测。核心思想：**丢包后窗口的"二项式增长"和到达上一次拥塞窗口时的"谨慎探测"**。

```text
W_max  = 上次丢包时的 cwnd
W(t)   = C × (t - K)^3 + W_max

其中 K = ∛(W_max × β / C)，β 是窗口减小因子（通常 0.2），C 是缩放常数。

曲线形状：离 W_max 远时快速增长，接近 W_max 时变平缓，超过 W_max 后加速寻找新上限。
```

**Cubic 对音视频的影响**：突发性强。Cubic 在某些阶段会很快抢占带宽，推流丢包后窗口会剧烈波动。这就是为什么 BBR 更适合有 BDP 的网络。

### 9.4 BBR（Bottleneck Bandwidth and RTT）

Google 2016 年提出，完全改变了建模思路：不再以丢包为拥塞信号（丢包 ≠ 拥塞，在有 buffer 的路由器上可能丢包时网络已经严重拥塞了——bufferbloat），而是**直接测量瓶颈带宽和最小 RTT**。

```text
BBR 的状态机：

1. STARTUP：类似慢启动，但用 pacing 节奏发包，而不是粗暴翻倍。
   检测到带宽不再增长时退出（不是等丢包！）。

2. DRAIN：把 STARTUP 阶段在网络缓冲区中堆积的数据排空。

3. PROBE_BW：稳态。周期性小幅调高/调低发包速率来探测最新带宽。
   大部分时间（约 7/8）cwnd = BDP = 瓶颈带宽 × 最小 RTT。

4. PROBE_RTT：周期性（每 10s）大幅降低 cwnd，探测更小的最小 RTT。
```

**BBR 对音视频的好处**：

- 更平滑（pacing），不发 burst，适合需要稳定码率的直播。
- 不因 bufferbloat 而误判拥塞，延迟更低。
- 在高丢包率（如 1%-5%）的网络中吞吐量远优于 Cubic。

### 9.5 拥塞控制算法对比总结（面试加分表）

| 算法 | 核心信号 | 特点 | 适用 |
|------|---------|------|------|
| **Reno** | 丢包 | 经典，快速重传/快速恢复 | 教学理解 |
| **Cubic** | 丢包 | 三次函数增长，稳定 | 默认，大部分场景 |
| **Vegas** | RTT 变化 | RTT 增大说明排队，主动降速 | 低延迟场景 |
| **BBR** | 带宽+RTT | 建模而非响应，抗 bufferbloat | 跨洋、弱网、直播 |
| **BBRv2** | 带宽+RTT+丢包 | 对 BBR v1 的公平性/丢包率改进 | 更好的 BBR |

**面试一句话总结：**

> 传统算法（Reno/Cubic）用丢包作为拥塞信号——这好比等水位溢出才关水龙头。BBR 改为直接测量水管直径（瓶颈带宽）和水流速度（RTT），理论上限接近 BDP，延迟也更低。Cubic 追求带宽利用率，BBR 追求低延迟和高吞吐的平衡。

---

## 十、Nagle 算法与 TCP_NODELAY

### 10.1 Nagle 算法

RFC 896 定义，初衷是解决**小包泛滥**（Tinygram）问题——telnet 每次按键发一个字节，网络效率极低。

**规则**：在任何时刻，最多只能有一个未被确认的小段（< MSS）。如果有未确认的小段，新数据必须等待该小段被确认后才能发送。但数据凑满 MSS 时不管，立即发送。

```c
if (available_data >= MSS || (available_data > 0 && nothing_in_flight))
    send_now();
else
    wait(); // 等凑满 MSS 或等 ACK
```

### 10.2 TCP_NODELAY

禁用 Nagle 算法，有小数据就立刻发。

```c
int flag = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

### 10.3 音视频场景的选择

**推流必须禁用 Nagle**：音视频数据是实时产生的，如果等 Nagle 凑小包，编码器出的一帧数据可能被延迟 40ms 等凑包，后面播放端会卡顿。RTMP 推流程序里通常默认设 `TCP_NODELAY`。

**HTTP API 调用（信令、鉴权）通常保留 Nagle**：请求响应模式，Nagle 对吞吐有益。

**面试追问：TCP_NODELAY 和 TCP_CORK 有什么区别？**

> `TCP_NODELAY`：**立刻发**，不要等。适合实时场景。
> `TCP_CORK`：**先攒着**，等我打开 cork 再一起发。适合需要把多个小 buffer 拼成一个 MSS 再发出去的场景（如 HTTP Response Header + Body 合并）。比 Nagle 更精确——由应用主动控制何时 flush。

---

## 十一、TCP Keepalive

### 11.1 机制

TCP Keepalive 不是协议标准的一部分，是很多实现的扩展。它不传数据，只发一个不带任何数据的 ACK 包（seq 号比对方期望的少 1，触发对方回一个 ACK）。

默认参数（Linux）：

| 参数 | 含义 | 默认值 |
|------|------|--------|
| `tcp_keepalive_time` | 连接空闲多久后开始探测 | 7200 秒（2 小时！） |
| `tcp_keepalive_intvl` | 探测间隔 | 75 秒 |
| `tcp_keepalive_probes` | 最大探测次数 | 9 次 |

连接无响应的最长检测时间：7200 + 75 × 9 = 7875 秒 ≈ 2 小时 11 分钟。**对音视频推拉流来说完全不可接受。**

### 11.2 音视频场景的正确做法

应用层心跳。推流端每 N 秒发一个空数据包或信令消息，服务端 N×3 秒没收到心跳就主动断开。常见取值：推流心跳间隔 10-30 秒。

**面试追问：为什么长连接不能依赖 TCP Keepalive？**

> （1）检测太慢——默认 2 小时才启动探测，中间设备（NAT、防火墙、负载均衡）的连接超时通常只有几分钟到几十分钟。（2）无法检测应用层假死——socket 存活但应用线程卡住了。应用层心跳可以附加业务状态，一举两得。

---

## 十二、TCP 与音视频的关键矛盾

### 12.1 队头阻塞（Head-of-Line Blocking）

这是 TCP 在音视频场景下最大的问题：

```text
Packet:   [P1] [P2] [P3] [P4] [P5]
接收:      ✓    ✗    ✓    ✓    ✓
应用读取:  读到 P1，然后阻塞等 P2 重传，P3/P4/P5 已经在内核 buffer 里了但读不到
```

H.264 码流中，如果 P2 是某个 P 帧的前半部分，P3-P5 可能是后半个 P 帧和一个 B 帧，全部卡住。播放端出现卡顿，用户体验极差。

### 12.2 重传放大延迟

丢包 → 等 RTO 或 3 dup ACK → 重传 → 等 ACK 确认。从丢包到成功交付，最少是一个 RTT（快速重传），最多是几秒（RTO 退避）。音频帧 20ms 一个，视频帧 33ms（30fps），延迟几个 RTT 就过时了。

### 12.3 为什么 RTMP 还敢用 TCP？

> RTMP 容忍 TCP 的延迟，是因为：（1）它是"推流"协议，推流端和服务端之间通常是有线网络/好 WiFi，丢包率低。（2）RTMP 的延迟目标是 1-3 秒，不是 200ms 以下。CDN 内部转发需要低延迟，所以会改用 SRT/QUIC/私有 UDP 协议。（3）TCP 的可靠传输屏蔽了底层细节，开发简单。

### 12.4 弱网场景的应对策略

面试需要能说出几个工程方案：

1. **更换协议**：TCP → SRT（基于 UDT，有 FEC/重传控制/加密）→ QUIC（0-RTT、多路复用无队头阻塞）→ WebRTC（基于 UDP 的 RTP/RTCP 栈）。
2. **FEC（前向纠错）**：发 N 个原始包 + M 个冗余包，丢失 ≤ M 个可以原地恢复，不用等重传。
3. **ARC（自适应重传控制）**：根据帧的 PTS（显示时间戳）判断：如果帧的解码截止时间已过，跳过重传，直接丢弃。
4. **SVC（可伸缩视频编码）**：丢包只影响增强层，基础层保持流畅。

---

## 十三、实战：Linux 内核 TCP 关键参数

面试时如果能结合生产环境参数调优，差异化优势非常明显。

```bash
# === 连接相关 ===
net.ipv4.tcp_syn_retries = 6          # 客户端 SYN 重试次数（建连超时 ~3min）
net.ipv4.tcp_synack_retries = 5       # 服务端 SYN+ACK 重试次数
net.ipv4.tcp_max_syn_backlog = 8192   # 半连接队列长度
net.core.somaxconn = 4096             # 全连接队列长度
net.ipv4.tcp_syncookies = 1           # 开启 SYN Cookie
net.ipv4.tcp_abort_on_overflow = 0    # 全连接队列满时不发 RST（让客户端重试）

# === 关闭连接相关 ===
net.ipv4.tcp_fin_timeout = 60         # FIN_WAIT_2 超时（实际 TIME_WAIT 约 60s）
net.ipv4.tcp_tw_reuse = 1             # TIME_WAIT 复用（客户端,需时间戳）
net.ipv4.tcp_max_tw_buckets = 5000    # TIME_WAIT 最大数量限制

# === Keepalive ===
net.ipv4.tcp_keepalive_time = 7200    # 启动探测前的空闲时间
net.ipv4.tcp_keepalive_intvl = 75     # 探测间隔
net.ipv4.tcp_keepalive_probes = 9     # 探测次数

# === 发送接收缓冲区 ===
net.core.rmem_default = 212992
net.core.wmem_default = 212992
net.core.rmem_max = 16777216          # 接收缓冲最大值
net.core.wmem_max = 16777216
net.ipv4.tcp_rmem = 4096 87380 6291456  # min/default/max
net.ipv4.tcp_wmem = 4096 16384 4194304

# === 拥塞控制 ===
net.ipv4.tcp_congestion_control = cubic  # 当前算法
net.core.default_qdisc = fq              # BBR 需配合 fq 队列调度
```

**面试追问：BBR 为什么需要配合 fq（Fair Queuing）？**

> BBR 依赖 pacing（平滑发包）来避免冲击瓶颈缓冲区。tc-fq 队列调度器可以精确控制发包间隙。没有 fq 的话，内核仍然可能把 BBR 攒的包一次性 burst 出去，破坏 BBR 对 BDP 的测量精度。

---

## 十四、TCP 故障排查工具速查

| 问题 | 工具 | 示例命令 |
|------|------|---------|
| 连接的当前状态 | `ss` | `ss -tanp \| grep ESTAB` |
| 连接数量统计 | `ss -s` | TCP 各状态数量汇总 |
| 丢包/重传统计 | `netstat -s` | 看 segments retransmitted |
| 实时抓包 | `tcpdump` | `tcpdump -i eth0 port 1935 -w rtmp.pcap` |
| 分析 pcap | `wireshark` / `tshark` | `tshark -r rtmp.pcap -Y "tcp.analysis.retransmission"` |
| 查看拥塞控制 | `ss -ti` | 输出 cwnd, rtt, rto, mss, pmtu 等 |
| 查看重传率 | `/proc/net/snmp` 或 `netstat -s` | 计算 retrans/segs 的比值 |
| 延迟追踪 | `tcptrace` | 可视化 TCP 流的行为 |

推流故障排查思路：

```text
1. ss -tnp 看看连接状态，TIME_WAIT 过多？SYN_SENT 长时间不消？
2. tcpdump 抓包看三次握手是否完成，seq/ack 是否对得上。
3. 看看 netstat -s 重传统计，重传率 > 2% 说明链路质量差。
4. ss -ti 看 cwnd 和 rtt，cwnd 长期很小说明拥塞控制压着。
5. 检查 iptables/nftables 是否有丢包规则。
6. 确认 conntrack 表是否满了（dmesg 看 "nf_conntrack: table full"）。
```

---

## 十五、高频面试题汇总

### Q1：TCP 是如何保证可靠性的？

> 四点：**确认重传**（ACK + 超时重传/快速重传）、**校验和**（头部+数据的 16 位校验）、**序号**（去重 + 排序）、**流量控制**（滑动窗口防止溢出）。另外拥塞控制保证不对网络造成过载。

### Q2：三次握手过程中如果最后一次 ACK 丢了会怎样？

> Client 认为连接已建立（进入 ESTABLISHED），开始发数据。Server 还在 SYN_RCVD 状态，收到 Client 发来的数据包时发现 ack 号不对（不是它期待的 ack=ISN+1），回复 RST 或丢弃。Client 收到 RST 后连接重置。如果 Client 一直不发数据，Server 在 SYN_RCVD 超时（synack_retries × 重传间隔）后释放半连接。

### Q3：SYN 攻击怎么防御？

> 见第三章 3.3 节。核心：SYN Cookie（零存储验证）、增大队列、缩短超时。进阶：synproxy（在 netfilter 层面做三次握手代理，建立完成后再把连接交给真实服务）。

### Q4：为什么 TIME_WAIT 是 2MSL 而不是 1MSL？

> 见第五章。主动关闭方发出最后一个 ACK 后，需要等两个 MSL：(1) 1 个 MSL 等网络中可能重传的 FIN 到达；(2) 再加 1 个 MSL 让自己对重传 FIN 回复的 ACK 也能在网络中消失。总共 2MSL，保证同一五元组的老包在新连接建立前全部消亡。

### Q5：TIME_WAIT 过多的危害？如何处理？

> 见第五章 5.2/5.3 节。危害：耗尽端口（客户端）/无法绑定（服务端）。方案：tw_reuse、增大端口范围、让对端关、SO_REUSEADDR、调整 long/short living connection 策略。

### Q6：TCP 拥塞控制和流量控制的区别？

> **流量控制**（Flow Control）是端到端的，防止发送方打爆接收方。信号是接收方的 Window 字段。**拥塞控制**（Congestion Control）是对网络的，防止注入太多数据压垮中间路由器。信号是丢包（超时/dup ACK）或 RTT 变化。实际发送窗口 = min(cwnd, rwnd)。

### Q7：Nagle 算法和 TCP_NODELAY 的关系？音视频推流该不该禁用 Nagle？

> 见第十章。音视频推流必须禁用 Nagle（启用 TCP_NODELAY），因为音频帧/视频 slice 可能是小包，等 Nagle 凑包会造成 40ms+ 的不必要延迟，使播放端卡顿。

### Q8：TCP 的最大连接数受什么限制？

> 五元组（src_ip, src_port, dst_ip, dst_port, protocol）唯一标识一个连接。
> 客户端最大连接数：可用端口数 × 每个服务端 (IP, Port) 组合。如果源 IP 只有一个，最大约 28232（65535 - 32768 - 1024 预留端口）。
> 服务端最大连接数：**文件描述符限制最优先**（`ulimit -n`, `fs.file-max`，每连接占用一个 fd）。其次是内存（每个连接约 4-10KB 内核内存）。理论上单机百万连接（需要 `net.ipv4.ip_local_port_range` 足够 + C10M 调优）。

### Q9：抓包发现大量 TCP Retransmission，怎么排查？

> 见第十四章排查思路。(1) 确认链路质量：ping 看延迟和丢包率。(2) 检查 CPU：softirq 过高？网卡中断均衡？(3) 接口速率：有队列丢包？`ip -s link show`。(4) 拥塞控制：`ss -ti` 看 cwnd 是否合理。(5) 接收缓冲区：是否有 `RcvPruned` 或 `collapsed`（`netstat -s`）。(6) PMTU 黑洞：是否触发了 IP 分片？

### Q10：为什么 HTTP/3 要换 QUIC 而不用 TCP？

> TCP 的问题：(1) 队头阻塞——TCP 字节流的有序交付和 HTTP/2 多路复用叠加后，一个流的丢包会阻塞所有流。(2) 握手延迟——TCP 三次握手 + TLS 三次握手，需要 2-3 个 RTT。(3) 协议僵化——中间件（防火墙、NAT）对 TCP Option 的修改/丢弃使得新特性难以部署。
> QUIC 的优势：(1) 基于 UDP，传输层无队头阻塞，每个流独立。(2) 0-RTT 握手（已连接过的对端）。(3) 用户态实现，快速迭代。

---

## 十六、面试答题框架

被问到 TCP 相关问题，建议按以下层次组织回答：

```text
第一层（概念）：一句话定义 + 核心特征。
第二层（机制）：怎么做到的？（三次握手、滑动窗口、拥塞控制、重传）
第三层（细节）：具体字段/参数/状态/算法名称。
第四层（场景）：你做的音视频项目里怎么用的？遇到过什么问题？怎么调优的？
```

**示例：面试官问"说一下 TCP 的拥塞控制"**

> "TCP 拥塞控制是为了防止发送端向网络注入过多数据导致中间路由器过载。核心是通过丢包信号或 RTT 变化来调整拥塞窗口 cwnd，实际发送窗口取 cwnd 和接收窗口的较小值。
>
> 经典算法从 Reno 开始分为慢启动、拥塞避免、快速重传、快速恢复四个阶段。Linux 现在默认是 Cubic，它用三次函数而不是 AIMD 线性探测，收敛更快。音视频场景下我更关注 BBR，因为它直接测量瓶颈带宽和最小 RTT，不把丢包当拥塞信号，在存在 bufferbloat 的链路上延迟更低。
>
> 我们推流 SDK 之前默认 Cubic，跨运营商推流时重传率 3-5%，后来切 BBR 后降到 1% 以内，但 BBR v1 在和多路 Cubic 流竞争时不太公平，后续会考虑 BBR v2。"

这样你的答案就有：理论 + 细节 + 工程经验，面试官基本不会追问。

---

## 十七、推荐阅读

- **RFC 793**：TCP 协议原始标准。
- **RFC 1122**：TCP 实现要求。
- **RFC 2018**：TCP SACK。
- **RFC 2581**：TCP 拥塞控制（Reno）。
- **RFC 5681**：TCP 拥塞控制更新。
- **RFC 7323**：TCP 扩展（时间戳、窗口扩缩、PAWS）。
- **RFC 7413**：TCP Fast Open。
- **《TCP/IP 详解 卷一》**：必读经典。第 17-24 章涵盖了本文绝大部分内容。
- **BBR 论文**：Cardwell et al., "BBR: Congestion-Based Congestion Control", ACM Queue 2016。
- **Google QUIC / HTTP/3**：了解 TCP 局限才能理解 QUIC 的设计动机。

---

> **最后的面试心法**：TCP 协议已经 40+ 年了，面试官不会期待你说出一个没人知道的隐藏参数。他们想听的，是你对"可靠传输要在不可靠网络上实现"这个核心矛盾的理解，以及你能用协议知识解释生产环境的真实现象——丢包、重传、卡顿、连接泄漏、端口耗尽——而不是只会调 API。
>
> **记住这个公式：可靠 = 确认重传 + 滑动窗口 + 拥塞控制。用这三个词，就能还原出 TCP 的全部机制。**
