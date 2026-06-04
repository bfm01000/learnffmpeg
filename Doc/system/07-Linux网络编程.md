# Linux 网络编程：面试速记与原理详解

> **目标岗位**：Linux C/C++ 开发工程师（物联网智能硬件方向，3 年以上）
> **本篇聚焦**：**socket API 与编程模型**——怎么写一个网络程序。协议原理（TCP 握手挥手、HTTPS、MQTT、WebRTC）归 [[08-网络协议原理]]；epoll 内核机制归 [[06-文件IO与IO多路复用]]。
> **重要度**：🔥🔥🔥（JD「网络通讯应用编程」核心项）
> **难度**：⭐⭐⭐⭐
> **预计阅读**：速记 10 分钟｜全文 40 分钟
> **关联文档**：[[06-文件IO与IO多路复用]]（epoll/fd 抽象）、[[08-网络协议原理]]（TCP/UDP/TLS/MQTT 协议层）、[[05-进程间通信IPC]]（socketpair/Unix域套接字也是 socket）、[[01-Linux系统编程全景导读]]（整体地图）、本目录《大小端字节序面试速记与原理详解》（网络字节序）

---

## 📌 第一部分：面试速记（考前 10 分钟扫一遍）

### 一句话核心

> **socket 是 Linux「一切皆文件」思想下对网络通信端点的 fd 抽象：一个连接由五元组（协议 + 源IP + 源端口 + 目的IP + 目的端口）唯一确定。服务端走 `socket→bind→listen→accept→read/write→close`，客户端走 `socket→connect→read/write→close`；TCP 是字节流必须自己处理粘包，UDP 是数据报有边界但会丢包。高并发不是靠多线程而是靠「IO 多路复用 + Reactor 模型」——一个线程用 epoll 盯住成千上万个 socket，这是 muduo/libevent/Redis/Nginx 的共同骨架。**

### 面试官常问问题 + 标准口语化回答

---

#### 开场题：socket 到底是什么？

**🗣️ 面试标准回答：**

> "socket 是操作系统给网络通信端点的一层抽象，本质是一个**文件描述符（fd）**。Linux『一切皆文件』，普通文件、管道、设备是 fd，网络连接也是 fd——所以 `read/write/close` 这套接口对 socket 一样能用。
>
> 一条 TCP 连接在内核里由**五元组**唯一标识：**协议、源 IP、源端口、目的 IP、目的端口**。所以同一个服务端口（比如 80）能同时挂上万条连接，因为客户端的 IP+端口不同，五元组就不同，内核能区分开。"

**👨‍💻 面试官追问：**

> Q: 那服务端 `accept` 返回的 fd 和 `listen` 的 fd 是同一个吗？
> A: 不是。**监听 fd（listenFd）只负责接收连接请求**，每次 `accept` 会**新建一个连接 fd（connFd）**专门和这个客户端通信。listenFd 一个就够，connFd 有几条连接就有几个。

---

#### 必考题：TCP 服务端和客户端的编程流程？

**🗣️ 面试标准回答：**

> "**服务端五步加收发**：`socket()` 建套接字 → `bind()` 绑定本地 IP+端口 → `listen()` 转为监听态并设置半连接/全连接队列 → `accept()` 阻塞等连接、返回连接 fd → `read/write` 收发 → `close` 关闭。
>
> **客户端三步加收发**：`socket()` → `connect()` 发起连接（TCP 这一步触发三次握手）→ `read/write` → `close`。
>
> 关键区别：服务端要 `bind` 到固定端口（客户端要知道连哪），客户端一般不用 `bind`，端口由内核临时分配；服务端是 `listen+accept` 被动接受，客户端是 `connect` 主动发起。"

**👨‍💻 面试官追问：**

> Q: `listen` 的第二个参数 backlog 是什么？
> A: 是**全连接队列（已完成三次握手、等 accept 取走）的长度**。Linux 上半连接队列由 `tcp_max_syn_backlog` 控制，全连接队列长度取 `min(backlog, somaxconn)`。队列满了新连接会被丢弃或拒绝，高并发服务要调大。握手细节见 [[08-网络协议原理]]。

---

#### 必考题：什么是粘包？TCP 为什么会粘包，怎么解决？

**🗣️ 面试标准回答：**

> "**粘包的根因是：TCP 是面向字节流的，不是面向消息的。** 应用层 `write` 三次发了 A、B、C 三条消息，TCP 只把它当成一串连续字节，对端 `read` 可能一次读到『ABC』，也可能读到『AB』再『C』——内核不保留你的消息边界。再加上 Nagle 算法会合并小包、MSS 会拆大包，所以收发次数和消息条数没有对应关系。注意『粘包』是个通俗叫法，本质是**应用层没定义消息边界**。
>
> **解决办法是应用层自己定边界，三种主流方案**：
> 1. **固定长度**：每条消息定长，不足补齐。简单但浪费。
> 2. **长度前缀（最常用）**：消息头放一个固定字节的长度字段（如 4 字节），先读头拿到长度，再按长度读完整 body。muduo、Protobuf-over-TCP、MQTT 剩余长度都是这个套路。
> 3. **分隔符**：用 `\r\n` 之类做结尾，HTTP 头、Redis RESP 协议就是。缺点是 body 里若出现分隔符要转义。
>
> **UDP 不粘包**——它是数据报，一次 `sendto` 对应一次 `recvfrom`，天然保留边界；但 UDP 会丢包、乱序、且单包超过 MTU 会分片，可靠性要应用层自己补。"

**👨‍💻 面试官追问：**

> Q: 那「拆包」又是什么？
> A: 同一个问题的另一面。一条大消息被 TCP 拆成多个段、对端要多次 `read` 才能拼齐一条完整消息，就是拆包/半包。长度前缀方案同时解决粘包和拆包：**没读够 length 就继续攒，读够了才算一条完整消息**。

---

#### 必考题：为什么 server 重启 bind 会报 "Address already in use"？

**🗣️ 面试标准回答：**

> "因为**主动关闭连接的一方会进入 TIME_WAIT 状态**（持续 2*MSL，约 1~4 分钟），这条连接占着原来的 IP+端口。服务端重启时想 `bind` 同一个端口，内核发现还有 TIME_WAIT 连接占用，就返回 `EADDRINUSE`。
>
> **解决：`bind` 之前设置 `SO_REUSEADDR`**——它允许 bind 到一个处于 TIME_WAIT 的地址端口上。这几乎是所有服务端的标配，开发期反复重启尤其需要。
>
> 进阶还有 `SO_REUSEPORT`：允许**多个进程/线程 bind 同一个 IP+端口**，内核做负载均衡分发新连接，是多进程 accept 惊群的现代解法（Nginx 多 worker 用它）。TIME_WAIT 为什么要等 2*MSL 属于协议原理，见 [[08-网络协议原理]]。"

---

#### 高频题：高并发服务器怎么设计？为什么不能一连接一线程？

**🗣️ 面试标准回答：**

> "**一连接一线程/进程的模型不可扩展**：每个线程占 MB 级栈空间，上万连接就是上万线程，内存扛不住、上下文切换开销爆炸，而且大部分连接其实是空闲的，线程白白阻塞在 `read` 上。这是 C10K 问题的根源。
>
> **现代做法是『IO 多路复用 + Reactor』**：用一个（或少数几个）线程跑 epoll，同时盯住成千上万个 socket，**哪个就绪了才处理哪个**，CPU 只花在真正有数据的连接上。epoll 的内核机制（红黑树管 fd、就绪链表、ET/LT）见 [[06-文件IO与IO多路复用]]。
>
> **Reactor 是这套思路的设计模式**：一个 Reactor 负责 epoll 等事件，事件来了**分发（dispatch）**给对应的 handler。演进三档：
> - **单 Reactor 单线程**：一个线程既 epoll 又处理业务。Redis 早期就是，简单、无锁，但一个慢请求阻塞全部。
> - **单 Reactor 多线程**：Reactor 线程只管 IO，业务计算丢给线程池。
> - **主从 Reactor（主流）**：mainReactor 只 accept 新连接，分给多个 subReactor，每个 subReactor 自己 epoll 管一批连接的读写。muduo、Netty、Nginx 都是这个结构。"

**👨‍💻 面试官追问：**

> Q: Reactor 和 Proactor 区别？
> A: **Reactor 是同步 IO**：epoll 只告诉你『fd 可读了』，**数据还得你自己 `read`**。**Proactor 是异步 IO**：你提交读请求，**内核帮你把数据拷到缓冲区后再通知你『读完了』**。Linux 的 `io_uring`、Windows IOCP 是 Proactor。Linux 网络编程绝大多数仍是 Reactor（epoll），因为传统 AIO 支持不完善。

---

#### 高频题：阻塞和非阻塞 IO 有什么区别？epoll 为什么要配非阻塞？

**🗣️ 面试标准回答：**

> "**阻塞模式**下 `read` 没数据就一直睡、`accept` 没连接就一直等、`connect` 要等握手完成才返回——线程被挂起。**非阻塞模式**（fd 设 `O_NONBLOCK`）下这些调用立即返回，没准备好就返回 `-1` 且 `errno == EAGAIN/EWOULDBLOCK`。
>
> **epoll 必须配非阻塞 fd，尤其是 ET（边沿触发）模式**：ET 只在状态变化时通知一次，所以你必须**循环 `read` 直到返回 `EAGAIN`**，把内核缓冲区一次性读干净，否则剩下的数据要等下次新数据到达才被通知，造成『数据卡住』。如果是阻塞 fd，读干净后再 `read` 就会把线程挂死。
>
> 非阻塞 `connect` 也常见：调用立即返回 `EINPROGRESS`，然后用 epoll 监听**可写事件**判断连接是否建立成功，可写后再用 `getsockopt(SO_ERROR)` 确认有没有出错。"

---

#### 高频题：对端关闭了连接，我还往里写会怎样？SIGPIPE 怎么处理？

**🗣️ 面试标准回答：**

> "对端关闭后我再 `write`，**第一次写**对端会回一个 RST，**第二次写**内核就给我的进程发 **`SIGPIPE` 信号，默认行为是直接杀死进程**。这在服务器上是灾难——一个客户端断开就能搞崩服务。
>
> **必须处理**，两种方式：
> 1. **全局忽略**：`signal(SIGPIPE, SIG_IGN)`，之后 `write` 失败只返回 `-1` 且 `errno == EPIPE`，正常错误处理即可。这是服务器标配。
> 2. **单次调用屏蔽**：`send(fd, buf, len, MSG_NOSIGNAL)`，只对这一次发送禁掉 SIGPIPE。
>
> 同时 `read` 返回 **0 表示对端已正常关闭（收到 FIN）**，这是判断连接结束的标准信号，要和返回 -1 的错误区分开。"

---

#### 高频题：close 和 shutdown 有什么区别？

**🗣️ 面试标准回答：**

> "`close` 是**关 fd**：把这个 fd 的引用计数减 1，减到 0 才真正关闭连接、发 FIN。如果 fd 被 fork 出去多个进程共享，`close` 不一定真的断连。而且 `close` 是**全双工一起关**，读写都没了。
>
> `shutdown` 是**关连接的方向**，不管引用计数，直接对连接动手：
> - `SHUT_WR`：关写端，发 FIN 告诉对端『我不再发了』，但**还能继续 read**——这就是**半关闭**，常用于『我发完了请求，等你把响应发完我再彻底关』。
> - `SHUT_RD`：关读端。
> - `SHUT_RDWR`：读写都关。
>
> 优雅断开的典型流程：`shutdown(SHUT_WR)` 发完 FIN，继续 read 直到读到 0（对端也关了），再 `close`。"

---

#### 高频题：怎么保活长连接？TCP keepalive 够用吗？

**🗣️ 面试标准回答：**

> "**TCP 自带 keepalive 但工程上基本不够用**：默认 2 小时才探测一次（`tcp_keepalive_time`），粒度太粗；而且它只能探测『TCP 链路通不通』，**探测不到应用层假死**——进程还在、socket 还连着，但业务线程卡死不响应了，TCP keepalive 照样认为连接正常。
>
> **所以物联网长连接普遍用应用层心跳**：客户端定时（如 30s）发一个心跳包，服务端收到回 ACK 并刷新该连接的『最后活跃时间』；服务端定时扫描，超过 N 个心跳周期没收到就判定连接死了、主动关闭并清理资源。MQTT 的 keepalive、WebSocket 的 ping/pong 都是这个机制（协议细节见 [[08-网络协议原理]]）。
>
> **客户端侧还要做断线重连**：检测到连接断开后，用**指数退避 + 随机抖动**重连（1s、2s、4s…封顶，加随机量避免大量设备同时重连把服务端打垮，即『重连风暴』）。"

---

## 二、原理详解

### 1. socket 与五元组：连接是怎么被区分的

```text
        客户端                                    服务端
   ┌──────────────┐                        ┌──────────────┐
   │ 1.2.3.4:51000│ ─────── TCP 连接 ──────▶│ 9.9.9.9:80   │
   └──────────────┘                        └──────────────┘

  这条连接在内核里的「身份证」= 五元组：
  ┌──────────┬────────────┬──────────┬────────────┬──────────┐
  │ 协议 TCP │ 源IP 1.2.3.4│ 源端口5100│ 目的IP9.9.9.9│ 目的端口80│
  └──────────┴────────────┴──────────┴────────────┴──────────┘

  同一个 9.9.9.9:80 能挂上万条连接：
    1.2.3.4:51000 → 9.9.9.9:80   ┐
    1.2.3.4:51001 → 9.9.9.9:80   ├ 源端口不同，五元组不同，内核区分得开
    5.6.7.8:40000 → 9.9.9.9:80   ┘
```

> 推论：**单台服务器的并发连接数不受「65535 端口」限制**——服务端只占一个端口，区分连接靠的是客户端 IP+端口的组合。真正的瓶颈是 fd 上限、内存、CPU。

### 2. TCP 服务端 / 客户端时序图

```text
   服务端 (listenFd)                          客户端 (connFd)
   ─────────────────                          ───────────────
   socket()      创建监听套接字
   setsockopt()  SO_REUSEADDR
   bind()        绑定 IP+端口
   listen()      转监听态，设 backlog
        │                                      socket()   创建套接字
   accept() ◀───────── 三次握手 ──────────────  connect()  发起连接(握手)
   (返回 connFd)                               (握手完成返回)
        │                                           │
   read()  ◀──────────── 请求数据 ──────────────  write()
   write() ──────────── 响应数据 ──────────────▶  read()
        │                                           │
   close(connFd) ───────── 四次挥手 ─────────────  close()
   (listenFd 继续 accept 下一个)

   注：三次握手/四次挥手的报文细节(SYN/ACK/FIN)见 [[08-网络协议原理]]，
       本篇只关心「API 在哪一步触发了它」。
```

### 3. TCP 字节流 vs UDP 数据报：粘包从哪来

```text
  发送端 write 三次：  [ A ][ B ][ C ]
  ─────────────────────────────────────────────────────
  TCP（字节流，无边界）：
     内核发送缓冲区拼成一串字节： A B C
     对端 read 可能的结果：
        一次读到:  "ABC"          ← 粘包
        分两次读:  "AB" / "C"     ← 拆包/半包
        →→ 必须应用层自己切分消息边界

  UDP（数据报，保留边界）：
     sendto(A) sendto(B) sendto(C)
     recvfrom 依次拿到: "A" "B" "C"   ← 边界天然保留
        但：可能丢 B、可能 C 先到、单包超 MTU 会分片
```

**长度前缀协议的解析逻辑（解决粘包/拆包的核心）：**

```text
  报文格式:  ┌── 4 字节长度 ──┬──────── body (length 字节) ────────┐
             │   length(网络序)│        实际消息内容                 │
             └────────────────┴────────────────────────────────────┘

  接收循环（维护一个 readBuffer 累积字节）：
    1. readBuffer 不足 4 字节         → 继续收，攒头
    2. 读出 length（ntohl 转主机序）
    3. readBuffer 不足 4+length 字节  → 继续收，攒 body（半包）
    4. 攒够一条完整消息 → 取出处理，从 readBuffer 移除这段
    5. 回到 1，处理可能粘在后面的下一条消息
```

### 4. 网络字节序与地址结构

网络协议规定多字节整数一律用**大端（网络字节序）**传输，而 x86/ARM 多为小端，所以发端口、IP 这类整数前必须转换：

| 函数 | 作用 | 助记 |
|---|---|---|
| `htons` | host to network short（16 位，端口） | h→n s |
| `htonl` | host to network long（32 位，IPv4 地址） | h→n l |
| `ntohs` | network to host short（收端口） | n→h s |
| `ntohl` | network to host long（收 IP / 长度前缀） | n→h l |

> 大端小端的本质、为什么网络选大端、如何判断本机字节序，见本目录《大小端字节序面试速记与原理详解》，本篇不重复。**只要记住：端口、IP、自定义协议里的长度字段，进出网络都要转序。**

```text
  IPv4 地址结构 sockaddr_in：
  ┌─────────────────┬───────────────────────────────────┐
  │ sin_family      │ AF_INET（IPv4）                    │
  │ sin_port        │ 端口，必须 htons()                  │
  │ sin_addr.s_addr │ IP，inet_pton 填入（已是网络序）     │
  │ sin_zero[8]     │ 填充，置 0                          │
  └─────────────────┴───────────────────────────────────┘

  字符串 IP ↔ 二进制：
    inet_pton(AF_INET, "192.168.1.10", &addr.sin_addr)  // 字符串→二进制(presentation→network)
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof buf) // 二进制→字符串
    （旧代码常见 inet_addr/inet_ntoa，不支持 IPv6 且非线程安全，新代码用 pton/ntop）
```

### 5. Reactor 模型三种形态

```text
  ① 单 Reactor 单线程（Redis 早期）
  ┌────────────────────────────────────────┐
  │  Reactor 线程                           │
  │  ┌──────┐  事件就绪   ┌──────────────┐   │
  │  │epoll │ ─────────▶ │ accept/读/写 │   │
  │  │_wait │            │ + 业务处理    │   │
  │  └──────┘            └──────────────┘   │
  └────────────────────────────────────────┘
   简单无锁；但一个慢业务卡住所有连接

  ② 单 Reactor 多线程
  ┌──────────────┐  就绪事件   ┌──────────────────┐
  │ Reactor 线程  │ ─────────▶ │ 只做 accept/读/写 │
  │ (epoll)      │            └────────┬─────────┘
  └──────────────┘                解码后丢任务
                                       ▼
                              ┌──────────────────┐
                              │   业务线程池      │ ← 耗时计算在这里
                              └──────────────────┘
   IO 与计算分离；单个 epoll 线程仍可能成瓶颈

  ③ 主从 Reactor（muduo / Netty / Nginx 主流）
            ┌──────────────┐
            │ mainReactor  │  只负责 accept 新连接
            │ (epoll)      │
            └──────┬───────┘
        新连接按策略分发(round-robin)
        ┌─────────┼─────────┐
        ▼         ▼         ▼
  ┌──────────┐┌──────────┐┌──────────┐
  │subReactor││subReactor││subReactor│ 各自 epoll，
  │(epoll)   ││(epoll)   ││(epoll)   │ 管一批连接的读写
  └──────────┘└──────────┘└──────────┘
   充分利用多核；连接均摊到多个 epoll 线程
```

> 一句话记忆：**Reactor = 「epoll 等事件 + 把就绪事件分发给对应 handler」的设计模式**。它和 [[06-文件IO与IO多路复用]] 讲的 epoll 是「设计模式」和「系统调用」的关系——epoll 是砖，Reactor 是用砖砌墙的图纸。

---

## 三、代码示例（C，中文注释）

> ⚠️ **安全提醒**：以下示例为讲清 socket 流程，**均未做身份认证、未加 TLS 加密、未做输入长度上限校验**。生产环境（尤其设备直连公网）必须：传输层套 TLS（见 [[08-网络协议原理]] 的 HTTPS/MQTTS）、对长度前缀做上限检查防止恶意大包打爆内存、做接入鉴权。

### 1. TCP 阻塞式服务端（最小骨架）

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    // 1. 创建监听套接字：AF_INET=IPv4，SOCK_STREAM=TCP
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) { perror("socket"); return 1; }

    // 2. SO_REUSEADDR：允许重启时立即 bind 处于 TIME_WAIT 的端口
    int reuseEnable = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuseEnable, sizeof(reuseEnable));

    // 3. 绑定本地地址
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(8888);          // 端口转网络字节序
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网卡
    if (bind(listenFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind"); return 1;                 // 端口被占会在这里报 EADDRINUSE
    }

    // 4. 转监听态，backlog=128 是全连接队列长度
    if (listen(listenFd, 128) < 0) { perror("listen"); return 1; }
    printf("listening on 8888...\n");

    // 5. 循环接受连接（一连接一处理，演示用，不可扩展）
    while (1) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int connFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (connFd < 0) { perror("accept"); continue; }

        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        printf("client connected: %s:%d\n", clientIp, ntohs(clientAddr.sin_port));

        char recvBuffer[1024];
        ssize_t receivedBytes = read(connFd, recvBuffer, sizeof(recvBuffer));
        if (receivedBytes > 0) {
            write(connFd, recvBuffer, receivedBytes);  // echo 回去
        }
        close(connFd);   // 关这条连接，listenFd 继续 accept
    }
    close(listenFd);
    return 0;
}
```

### 2. TCP 客户端（最小骨架）

```c
int connectToServer(const char* serverIp, unsigned short serverPort) {
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0) return -1;

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(serverPort);
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr); // 字符串 IP→二进制

    // connect 触发三次握手；阻塞模式下握手完成才返回
    if (connect(clientFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(clientFd);
        return -1;
    }
    return clientFd;   // 之后直接 read/write，客户端无需 bind（端口内核临时分配）
}
```

### 3. UDP 收发（无连接）

```c
// UDP 服务端：不需要 listen/accept，sendto/recvfrom 直接带对端地址
int udpFd = socket(AF_INET, SOCK_DGRAM, 0);   // SOCK_DGRAM = UDP
// ... bind 同上 ...

struct sockaddr_in peerAddr;
socklen_t peerAddrLen = sizeof(peerAddr);
char recvBuffer[1500];                         // 一般按 MTU 给缓冲
ssize_t receivedBytes = recvfrom(udpFd, recvBuffer, sizeof(recvBuffer), 0,
                                 (struct sockaddr*)&peerAddr, &peerAddrLen);
// 一次 recvfrom 对应对端一次 sendto，天然保留消息边界（不粘包）
sendto(udpFd, recvBuffer, receivedBytes, 0,
       (struct sockaddr*)&peerAddr, peerAddrLen); // 回给来源地址
```

### 4. 完整收发：处理「写不完」和「半包」的可靠收发函数

> `write` 不保证一次写完（发送缓冲区满会只写一部分），`read` 不保证一次读够——必须循环。

```c
// 可靠发送：循环写直到 totalBytes 全部发出
ssize_t writeAll(int connFd, const char* data, size_t totalBytes) {
    size_t sentBytes = 0;
    while (sentBytes < totalBytes) {
        // MSG_NOSIGNAL：对端已关时返回 EPIPE 而不是触发 SIGPIPE 杀进程
        ssize_t writtenBytes = send(connFd, data + sentBytes,
                                    totalBytes - sentBytes, MSG_NOSIGNAL);
        if (writtenBytes < 0) {
            if (errno == EINTR) continue;        // 被信号打断，重试
            return -1;                           // 真错误（含 EPIPE）
        }
        sentBytes += writtenBytes;
    }
    return sentBytes;
}

// 可靠读满 needBytes 字节（read 返回 0 表示对端关闭）
ssize_t readN(int connFd, char* outBuffer, size_t needBytes) {
    size_t receivedBytes = 0;
    while (receivedBytes < needBytes) {
        ssize_t readBytes = read(connFd, outBuffer + receivedBytes,
                                 needBytes - receivedBytes);
        if (readBytes == 0) return 0;            // 对端关闭(收到 FIN)
        if (readBytes < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        receivedBytes += readBytes;
    }
    return receivedBytes;
}
```

### 5. 重点实战：epoll + 长度前缀协议的 echo server 骨架

> epoll 的红黑树/就绪链表/ET vs LT 等**机制细节见 [[06-文件IO与IO多路复用]]**；这里侧重 **socket 编程流程 + 粘包处理**：每个连接维护一个累积缓冲区，按「4 字节长度前缀 + body」切分完整消息。用 LT（水平触发）模式，逻辑最直观。

```c
#include <sys/epoll.h>
#include <fcntl.h>
// ... 省略前面 include ...

#define MAX_EVENTS 1024
#define MAX_MSG_LEN (1 << 20)   // 1MB 上限，防恶意大包打爆内存

// 每个连接的接收上下文：累积未处理完的字节
typedef struct {
    char    readBuffer[MAX_MSG_LEN];
    size_t  bufferedBytes;       // 当前已攒了多少字节
} ConnContext;

// 设非阻塞：epoll 配非阻塞是标准做法
static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 从累积缓冲区里尽可能多地切出完整消息并 echo（核心：粘包/半包处理）
static void processMessages(int connFd, ConnContext* ctx) {
    size_t parsedOffset = 0;
    while (ctx->bufferedBytes - parsedOffset >= 4) {        // 至少够一个长度字段
        uint32_t bodyLength;
        memcpy(&bodyLength, ctx->readBuffer + parsedOffset, 4);
        bodyLength = ntohl(bodyLength);                     // 长度前缀转主机序
        if (bodyLength > MAX_MSG_LEN) { /* 协议非法，应关连接 */ return; }
        if (ctx->bufferedBytes - parsedOffset < 4 + bodyLength)
            break;                                          // body 没收全(半包)，等下次
        // 攒够一条完整消息：原样 echo（4 字节头 + body 一起回）
        writeAll(connFd, ctx->readBuffer + parsedOffset, 4 + bodyLength);
        parsedOffset += 4 + bodyLength;                     // 跳到下一条
    }
    // 把没处理完的剩余字节挪到缓冲区开头，等后续数据拼接
    if (parsedOffset > 0) {
        memmove(ctx->readBuffer, ctx->readBuffer + parsedOffset,
                ctx->bufferedBytes - parsedOffset);
        ctx->bufferedBytes -= parsedOffset;
    }
}
```

```c
int main(void) {
    signal(SIGPIPE, SIG_IGN);    // 关键：对端断开后 write 不会杀死进程

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    int reuseEnable = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuseEnable, sizeof(reuseEnable));
    // ... bind + listen 同前面骨架 ...
    setNonBlocking(listenFd);

    int epollFd = epoll_create1(0);
    struct epoll_event listenEvent = { .events = EPOLLIN, .data.fd = listenFd };
    epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &listenEvent);

    // 简单用数组按 fd 索引连接上下文（生产环境用 hash/对象池）
    ConnContext* contexts[MAX_EVENTS] = {0};
    struct epoll_event readyEvents[MAX_EVENTS];

    while (1) {
        int readyCount = epoll_wait(epollFd, readyEvents, MAX_EVENTS, -1);
        for (int eventIndex = 0; eventIndex < readyCount; ++eventIndex) {
            int activeFd = readyEvents[eventIndex].data.fd;

            if (activeFd == listenFd) {
                // 监听 fd 就绪：循环 accept 把积压连接全收掉
                while (1) {
                    int connFd = accept(listenFd, NULL, NULL);
                    if (connFd < 0) break;          // EAGAIN：本轮收完了
                    setNonBlocking(connFd);
                    struct epoll_event connEvent = { .events = EPOLLIN, .data.fd = connFd };
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, connFd, &connEvent);
                    contexts[connFd] = calloc(1, sizeof(ConnContext));
                }
            } else {
                // 连接 fd 可读：读数据追加到累积缓冲区，再切分消息
                ConnContext* ctx = contexts[activeFd];
                ssize_t readBytes = read(activeFd,
                        ctx->readBuffer + ctx->bufferedBytes,
                        sizeof(ctx->readBuffer) - ctx->bufferedBytes);
                if (readBytes > 0) {
                    ctx->bufferedBytes += readBytes;
                    processMessages(activeFd, ctx);  // 处理粘包/半包
                } else if (readBytes == 0 || (readBytes < 0 && errno != EAGAIN)) {
                    // 对端关闭或出错：清理连接
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, activeFd, NULL);
                    close(activeFd);
                    free(ctx);
                    contexts[activeFd] = NULL;
                }
            }
        }
    }
    return 0;
}
```

> **这段骨架体现的工程要点**：① `SIGPIPE` 忽略；② 非阻塞 + epoll；③ 监听 fd 循环 accept；④ **每连接独立累积缓冲区**做粘包/半包切分；⑤ `read` 返回 0 判定对端关闭并清理 fd。**升级到 ET 模式**时，读和 accept 都要循环到 `EAGAIN` 才停（见 [[06-文件IO与IO多路复用]]）。生产级实现（事件回调、定时器管心跳、多线程 subReactor）建议直接读 muduo 源码。

### 6. 结合智能硬件：设备↔云端长连接的保活与重连

物联网设备和云端通常维持一条**长连接**（MQTT/自定义 TCP 协议），核心诉求是「连接尽量不断、断了快速恢复、服务端能感知设备死活」：

```text
  设备端                                       云端服务
  ───────                                      ─────────
  connect ────────────────────────────────▶  accept，记录 lastActiveTime
     │                                            │
     │  ── 每 30s 发心跳(PING) ───────────────▶  收到 → 刷新 lastActiveTime，回 PONG
     │  ◀────────────── PONG ───────────────     │
     │                                            │  定时扫描：now - lastActiveTime
     │                                            │  > 3 个心跳周期 → 判定掉线，
     │                                            │  关 socket、清理会话/订阅
     │
   [网络抖动断开]
     │  检测到 read=0 或 写失败
     ▼
   断线重连（指数退避 + 随机抖动）:
     第1次等 1s，第2次 2s，第3次 4s … 封顶 60s，
     每次叠加 0~1s 随机量 → 避免万台设备同时重连造成「重连风暴」
```

**为什么是应用层心跳而不是 TCP keepalive：**

| 维度 | TCP keepalive | 应用层心跳 |
|---|---|---|
| 探测内容 | 仅链路是否通 | 业务进程是否真能响应（防假死） |
| 默认周期 | 2 小时，粒度太粗 | 自定义（设备常 30s~几分钟） |
| 可移植性 | 各 OS 默认值不同，需调内核参数 | 应用自己控制，一致 |
| 携带信息 | 无 | 可顺带上报状态/续约会话 |

> 设备侧心跳周期要在**省电（间隔大）**和**及时感知掉线（间隔小）**之间权衡；NAT/防火墙会回收空闲连接（常 5 分钟），心跳间隔必须小于这个超时，否则连接会被中间设备静默掐断。

---

## 四、常见坑与面试加分点

| 坑 | 说明 / 后果 | 正确做法 |
|---|---|---|
| 没忽略 SIGPIPE | 对端断开后写第二次，进程被信号杀死 | `signal(SIGPIPE, SIG_IGN)` 或 `send(..., MSG_NOSIGNAL)` |
| 重启 bind 报 EADDRINUSE | TIME_WAIT 占着端口 | bind 前设 `SO_REUSEADDR` |
| 把 TCP 当消息流，不处理粘包 | 收到半条/多条消息解析错乱 | 长度前缀 / 分隔符 / 定长 |
| `read`/`write` 只调一次 | 没读够、没写完就以为完成 | 循环 `readN`/`writeAll` 直到满 |
| ET 模式用阻塞 fd 或不读干净 | 读到一半被挂死 / 数据卡住不再通知 | ET 必配非阻塞 + 循环读到 EAGAIN |
| 忽略 `read` 返回 0 | 把对端正常关闭当错误，或漏清理 fd | 返回 0 = 收到 FIN，主动清理连接 |
| 端口/IP 忘记转字节序 | 连错端口、地址乱码 | 端口 `htons`、长度前缀 `htonl/ntohl` |
| `accept`/`read` 不判 EINTR | 被信号打断误以为失败 | `EINTR` 时重试 |
| 长连接只靠 TCP keepalive | 应用层假死探测不到 | 加应用层心跳 + 超时清理 |
| 断线立即密集重连 | 服务恢复瞬间被重连风暴打垮 | 指数退避 + 随机抖动 |
| 长度前缀不做上限校验 | 恶意发超大 length 撑爆内存 | 校验 `length <= MAX_MSG_LEN` |

**加分一句（音视频背景结合）：**

> "我做音视频推流时，编码线程产包、网络线程发送，中间用带背压的队列；网络层就是一个 Reactor：epoll 管 socket 可写事件，发送缓冲区满（`EAGAIN`）时注册 `EPOLLOUT`、把没发完的数据挂起，可写了再续发——这就是高性能网络库处理『写不完』的标准手法，避免阻塞编码线程。"

---

## 五、速记对照表

### socket API 流程速查

| 角色 | 调用顺序 |
|---|---|
| TCP 服务端 | `socket → setsockopt(SO_REUSEADDR) → bind → listen → accept →` 循环 `read/write → close` |
| TCP 客户端 | `socket → connect →` 循环 `read/write → close` |
| UDP 双方 | `socket → (bind) →` 循环 `recvfrom/sendto → close`（无 listen/accept/connect） |

### 关键返回值与 errno

| 现象 | 含义 |
|---|---|
| `read` 返回 0 | 对端正常关闭（收到 FIN） |
| `read/write` 返回 -1 且 `EAGAIN` | 非阻塞下「暂时没数据 / 写缓冲满」，不是错误 |
| `read/write` 返回 -1 且 `EINTR` | 被信号打断，应重试 |
| `write` 返回 -1 且 `EPIPE` | 对端已关，配合忽略 SIGPIPE |
| `connect` 返回 -1 且 `EINPROGRESS` | 非阻塞 connect 进行中，用 EPOLLOUT 等结果 |
| `bind` 返回 -1 且 `EADDRINUSE` | 端口被占（多为 TIME_WAIT），设 SO_REUSEADDR |

### 概念怎么选

| 需求 | 选择 |
|---|---|
| 可靠、有序、面向连接 | TCP（`SOCK_STREAM`） |
| 低延迟、可丢、保留消息边界 | UDP（`SOCK_DGRAM`），可靠性应用层补 |
| 上万并发连接 | epoll + Reactor，**不要**一连接一线程 |
| 切分 TCP 消息 | 长度前缀（首选）/ 分隔符 / 定长 |
| 长连接保活 | 应用层心跳 + 超时清理 + 退避重连 |
| 优雅半关闭 | `shutdown(SHUT_WR)` 后继续 read 到 0 再 close |
| 多 worker 共享端口 | `SO_REUSEPORT` |

---

## 六、自测 8 题

1. socket 的五元组是哪五个？为什么单端口能挂上万条连接？
2. 写出 TCP 服务端和客户端各自的系统调用顺序。
3. TCP 为什么会粘包？列出三种解决方案，长度前缀方案如何同时解决半包？
4. 为什么 server 重启 `bind` 报 `EADDRINUSE`？怎么解决？`SO_REUSEADDR` 和 `SO_REUSEPORT` 区别？
5. 对端关闭后继续 `write` 会发生什么？怎么避免进程被杀？
6. `close` 和 `shutdown` 的区别？半关闭怎么实现？
7. Reactor 三种形态分别是什么？主从 Reactor 各 Reactor 负责什么？
8. 物联网长连接为什么用应用层心跳而非 TCP keepalive？断线重连为什么要加随机抖动？

<details>
<summary>参考答案</summary>

1. 协议 + 源 IP + 源端口 + 目的 IP + 目的端口。服务端只占一个端口，连接靠客户端 IP+端口的不同组合区分，所以并发数不受 65535 端口数限制，瓶颈是 fd/内存/CPU。
2. 服务端：`socket→setsockopt→bind→listen→accept→read/write→close`；客户端：`socket→connect→read/write→close`。
3. 因为 TCP 是字节流、不保留消息边界（叠加 Nagle 合并、MSS 拆分）。方案：定长 / 长度前缀 / 分隔符。长度前缀先读固定长度的头拿到 body 长度，没读够就继续攒，读够才算一条完整消息，从而同时解决粘包和半包。
4. 主动关闭方进入 TIME_WAIT（2*MSL）占着端口，重启 bind 同端口失败。设 `SO_REUSEADDR` 解决。`SO_REUSEADDR` 允许 bind 到 TIME_WAIT 的地址；`SO_REUSEPORT` 允许多进程/线程 bind 同一 IP+端口，内核负载均衡分发。
5. 第一次写对端回 RST，第二次写内核发 `SIGPIPE`，默认杀死进程。用 `signal(SIGPIPE, SIG_IGN)` 全局忽略或 `send(..., MSG_NOSIGNAL)`，之后失败只返回 `EPIPE`。
6. `close` 减 fd 引用计数、减到 0 才发 FIN、读写一起关；`shutdown` 不管引用计数直接关指定方向。半关闭：`shutdown(SHUT_WR)` 发 FIN 表示「我不再发了」，但仍可 read 接收对端剩余数据，读到 0 再 close。
7. 单 Reactor 单线程（IO+业务同线程）、单 Reactor 多线程（Reactor 管 IO、业务丢线程池）、主从 Reactor（mainReactor 只 accept、subReactor 各自 epoll 管一批连接读写）。
8. TCP keepalive 只探测链路通断、探测不到应用层假死，且默认 2 小时粒度太粗；应用层心跳能验证业务可响应、周期可控、可携带状态。随机抖动避免大量设备在服务恢复瞬间同时重连造成「重连风暴」打垮服务端。

</details>

---

> **延伸阅读**：协议层（TCP 状态机、握手挥手、HTTPS/TLS、MQTT、WebRTC）见 [[08-网络协议原理]]；epoll/select/poll 内核机制与 ET/LT 见 [[06-文件IO与IO多路复用]]；网络字节序与大小端见本目录《大小端字节序面试速记与原理详解》；整体知识地图见 [[01-Linux系统编程全景导读]]。
