# 文件 IO 与 IO 多路复用：面试速记与原理详解

> **适用方向**：Linux C/C++ 开发（物联网智能硬件），网络后台，音视频推流/采集  
> **难度**：⭐⭐⭐⭐（epoll 是 Linux 网络后台必考）  
> **预计阅读**：速记 10 分钟｜全文 35 分钟  
> **关联文档**：[[07-Linux网络编程]]（socket API、Reactor 完整实现）、[[03-Linux内存管理机制]]（mmap 页表与缺页）、[[05-进程间通信IPC]]（管道/socketpair 也是 fd）、[[01-Linux系统编程全景导读]]（整体地图）

---

## 📌 第一部分：面试速记（考前 10 分钟扫一遍）

### 一句话核心

> **Linux「一切皆文件」，进程通过文件描述符（fd）这个整数句柄统一操作普通文件、socket、管道、设备；当一个线程要同时盯住成千上万个 fd 时，靠 IO 多路复用（select/poll/epoll）让内核帮你「哪个 fd 就绪了就告诉你」，其中 epoll 用红黑树管 fd、就绪链表返回结果，做到 O(就绪数) 而非 O(总数)，是 C10K 之后 Linux 高并发服务器的基石。**

### 面试官常问问题 + 标准口语化回答

---

#### 开场题：什么是文件描述符（fd）？

**🗣️ 面试标准回答：**

> "fd 是一个**非负整数**，是进程访问 IO 资源的句柄。内核里有三层结构：**进程的 fd 表**（每个进程一份，下标就是 fd）→ **系统级打开文件表**（记录读写偏移、状态标志）→ **inode 表**（文件真正的元数据）。
>
> 关键点：fd 只是个**下标**，真正的状态在内核。所以 `dup` 出来的两个 fd 指向同一个打开文件表项、**共享读写偏移**；而两次 `open` 同一文件是两个独立表项、偏移各自独立。
>
> 每个进程默认 0/1/2 是 **标准输入/输出/错误**。Linux 一切皆文件，socket、管道、eventfd、定时器、甚至摄像头 /dev/video0 都是 fd——这就是为什么 epoll 能用同一套接口同时管网络连接、串口、设备。"

**👨‍💻 面试官追问：**

> Q: fd 数字是怎么分配的？
> A: 内核总是分配**当前最小的未使用 fd**。所以 close(0) 之后再 open，新文件就拿到 fd=0——shell 重定向就是利用这个规律配合 dup2 实现的。

---

#### 必考题：阻塞 IO 和非阻塞 IO 有什么区别？

**考察意图：** 这是理解 epoll 为什么要配非阻塞的前提。

**🗣️ 面试标准回答：**

> "区别在**数据没准备好时 read/write 怎么办**：
>
> - **阻塞 IO**（默认）：`read` 一个没数据的 socket，线程**挂起睡眠**，直到有数据或出错才返回。一个线程同一时刻只能等一个 fd，所以传统模型要『一连接一线程』。
> - **非阻塞 IO**（`fcntl` 加 `O_NONBLOCK`）：`read` 没数据立刻返回 **-1 且 errno=EAGAIN/EWOULDBLOCK**，线程不挂起，可以掉头去处理别的 fd。
>
> 注意：非阻塞**不等于异步**。非阻塞 read 还是**同步**的——你得自己反复问『好了没』（轮询），数据拷贝仍发生在你这次调用里。真正的异步 IO（aio/io_uring）是你提交请求后内核帮你做完、做完再通知你，拷贝不占用你的调用。"

**👨‍💻 面试官追问：**

> Q: 那非阻塞单独用不就行了，为什么还要 epoll？
> A: 纯非阻塞要你**自己轮询所有 fd**，CPU 空转烧满。epoll 让内核替你监视，没事就让你睡，有事再唤醒——非阻塞解决『不被单个 fd 卡住』，epoll 解决『不用空转地知道谁就绪』，两者配合才完整。

---

#### 必考题：select、poll、epoll 的区别？（核心必考）

**🗣️ 面试标准回答：**

> "三者都是 IO 多路复用，让一个线程监视多个 fd，差异在**数据结构和效率**：
>
> | 维度 | select | poll | epoll |
> |---|---|---|---|
> | fd 上限 | 1024（FD_SETSIZE 写死） | 无硬上限（数组） | 无硬上限 |
> | 传参方式 | 每次把整个 fd_set 拷进内核 | 每次拷整个 pollfd 数组 | fd 一次注册常驻内核（红黑树） |
> | 找就绪 fd | 内核轮询全部 O(n)，返回后用户再遍历 | 同样 O(n) | 内核回调把就绪 fd 放就绪链表，O(就绪数) |
> | 唤醒后开销 | O(n) | O(n) | O(就绪数) |
>
> 一句话：select/poll 每次调用都要**全量拷贝 + 全量扫描**，连接数一大就线性退化；epoll 把『监视哪些 fd』和『谁就绪了』拆开——`epoll_ctl` 注册一次进红黑树，`epoll_wait` 只返回就绪链表里的 fd，所以**百万连接但活跃的少**时，epoll 的开销只和活跃数相关。这就是 C10K 问题的解法。"

**👨‍💻 面试官追问：**

> Q: 那 select 是不是一无是处？
> A: 不是。fd 数少（几十个）、要跨平台（Windows 也有 select）时它够用且简单；epoll 是 Linux 专有。但 Linux 高并发后台一律用 epoll。

---

### 代码展示

#### select
```cpp
while (running) {

    fd_set readfds;
    FD_ZERO(&readfds);

    FD_SET(cameraFd, &readfds);
    FD_SET(socketFd, &readfds);

    int maxFd = std::max(cameraFd, socketFd);

    int ret = select(
        maxFd + 1,
        &readfds,
        nullptr,
        nullptr,
        nullptr);

    if (ret <= 0)
        continue;

    if (FD_ISSET(cameraFd, &readfds)) {
        // Camera Ready
    }

    if (FD_ISSET(socketFd, &readfds)) {
        // Socket Ready
    }
}
```

#### poll
```cpp
pollfd fds[2];

fds[0].fd = cameraFd;
fds[0].events = POLLIN;

fds[1].fd = socketFd;
fds[1].events = POLLIN;

while (running) {

    int ret = poll(fds, 2, -1);

    if (ret <= 0)
        continue;

    if (fds[0].revents & POLLIN) {
        // Camera Ready
    }

    if (fds[1].revents & POLLIN) {
        // Socket Ready
    }
}
```
#### epoll
```cpp
int epfd = epoll_create1(0);

epoll_event ev{};

ev.events = EPOLLIN;

ev.data.fd = cameraFd;
epoll_ctl(epfd, EPOLL_CTL_ADD, cameraFd, &ev);

ev.data.fd = socketFd;
epoll_ctl(epfd, EPOLL_CTL_ADD, socketFd, &ev);

epoll_event events[10];

while (running) {

    int ret = epoll_wait(
        epfd,
        events,
        10,
        -1);

    for (int i = 0; i < ret; i++) {

        if (events[i].data.fd == cameraFd) {
            // Camera Ready
        }

        if (events[i].data.fd == socketFd) {
            // Socket Ready
        }
    }
}
```

#### 必考题：epoll 的 LT 和 ET 模式区别？（高频深水区）

**🗣️ 面试标准回答：**

> "LT（Level Triggered 水平触发，默认）和 ET（Edge Triggered 边缘触发）的区别是**通知时机**：
>
> - **LT**：只要 fd 的读缓冲区**还有数据**，每次 `epoll_wait` 都会**一直通知**你。你这次没读完，下次还提醒。容错性好、编程简单，可以阻塞或非阻塞。
> - **ET**：只在 fd 状态**从无到有变化的那一刻**通知**一次**。比如来了数据触发一次，你没读完也不再提醒，直到**下一批新数据到达**才再触发。
>
> 所以 **ET 必须配非阻塞 fd + 循环 read 直到返回 EAGAIN**，把缓冲区彻底读干，否则剩下的数据没有新事件来触发就**永久丢失（卡死）**。
>
> 取舍：ET 触发次数少、`epoll_wait` 返回少，**减少了重复唤醒和系统调用**，高性能服务器（nginx、Redis）常用；代价是编程更易出错。LT 简单稳妥，大多数业务够用。"

**👨‍💻 面试官追问：**

> Q: ET 模式下写数据（EPOLLOUT）要注意什么？
> A: 平时**别一直注册 EPOLLOUT**，否则发送缓冲区一有空间就疯狂触发空转。正确做法：只在 `write` 返回 EAGAIN（发送缓冲满）时才临时注册 EPOLLOUT，等触发后把积压数据写完，写完立刻取消注册。

---

#### 高频题：缓冲 IO 和无缓冲 IO 的区别？printf 为什么不立刻输出？

**🗣️ 面试标准回答：**

> "区别在**有没有用户态缓冲**：
>
> - **无缓冲**：`open/read/write/close` 这套是**裸系统调用**，每次 write 直接陷入内核。fd 级别。
> - **标准 IO（缓冲）**：`fopen/fread/fwrite/printf` 是 C 库（FILE*）在用户态又包了一层**缓冲区**，攒够一批再统一 `write` 系统调用，减少陷入内核的次数。
>
> printf 不立刻输出就是因为这层缓冲：连终端时是**行缓冲**（遇 `\n` 才刷），重定向到文件时变**全缓冲**（缓冲区满或 `fflush`/程序正常退出才刷）。所以程序崩溃前的 printf 可能丢——调试时要 `fflush(stdout)` 或用 stderr（无缓冲）。
>
> 工程上：日志、文本处理用带缓冲的标准 IO 省 syscall；网络 socket、要精确控制时机的场景用裸 fd。"

---

#### 高频题：什么是零拷贝？sendfile 怎么减少拷贝？

**🗣️ 面试标准回答：**

> "传统『读文件发网络』要 **4 次拷贝 + 4 次上下文切换**：磁盘→内核页缓存（DMA）、页缓存→用户缓冲区（CPU 拷贝）、用户缓冲区→内核 socket 缓冲区（CPU 拷贝）、socket 缓冲区→网卡（DMA）。中间两次用户态↔内核态的 CPU 拷贝是纯浪费——数据根本没被业务逻辑碰过。
>
> **零拷贝**就是消掉这些无意义拷贝：
> - **`sendfile(out_fd, in_fd)`**：数据在内核里直接从文件页缓存送到 socket，**完全不经用户态**，CPU 拷贝降到 0（配合网卡 SG-DMA）。
> - **`mmap + write`**：把文件映射进内存，省掉『页缓存→用户缓冲』那次拷贝，但 write 那次还在。
> - **`splice`**：通过内核管道在两个 fd 间移动数据，适合 socket↔socket。
>
> 和音视频的关系：**推流服务器发大文件/视频切片、点播回源**时，sendfile 能大幅降 CPU 和内存带宽——这正是物联网设备（CPU 弱、内存小）和流媒体服务器都看重的优化。"

**👨‍💻 面试官追问：**

> Q: 那为什么不所有场景都用 sendfile？
> A: sendfile 要求数据**原样转发、业务不修改**。如果要在用户态对数据加密、转码、改协议头，就必须拷到用户态处理，零拷贝用不上。

---

#### 高频题：read 返回值要怎么处理？为什么要循环读写？

**🗣️ 面试标准回答：**

> "`read`/`write` 的返回值**绝不能假设一次就读写完**：
>
> - 返回 **>0**：实际读/写的字节数，**可能小于你要的**（部分读写）。比如想读 4096 字节只返回 1000，剩下要再读。
> - 返回 **0**：read 返回 0 表示**对端关闭/文件结束（EOF）**，这是连接断开的信号。
> - 返回 **-1**：出错，看 errno——`EAGAIN/EWOULDBLOCK`（非阻塞且暂无数据，正常）、`EINTR`（被信号打断，应重试）、其它才是真错误。
>
> 所以写网络程序必须**循环写**直到写完，**循环读**直到 EAGAIN（ET 模式）或够了为止，并单独处理 EINTR 重试。这是新手最容易踩的坑：以为 `write(fd, buf, n)` 一定写 n 个，结果大包被截断。"

---

#### 实战题：mmap 读文件比 read 快在哪？

**🗣️ 面试标准回答：**

> "`mmap` 把文件直接映射到进程虚拟地址空间，访问内存就等于访问文件，**省掉了 read 那次『内核页缓存→用户缓冲区』的 CPU 拷贝**——你读的内存页就是内核页缓存本身（缺页时由内核映射过去，原理见 [[03-Linux内存管理机制]]）。
>
> 何时更快：**大文件随机访问、多进程共享同一文件、反复读同一区域**。何时不划算：小文件（建立映射、缺页中断有固定开销）、顺序一次性读（read 的预读也很高效）。
>
> 注意 mmap 的坑：访问未加载页触发**缺页中断**有延迟、文件被截断后访问映射区会 **SIGBUS**、写回时机要 `msync` 控制。"

---

## 二、原理详解

### 1. 文件描述符的三层结构

fd 只是个整数下标，真正的状态在内核三张表里：

```text
进程 A                      系统级打开文件表              inode 表（文件本体）
┌──────────────┐          ┌────────────────────┐      ┌──────────────────┐
│ fd 表         │          │ 表项①              │      │ inode (file.txt) │
│ 0 → stdin ───┼────┐     │  offset=1024       │      │  大小/权限/数据块 │
│ 1 → stdout    │    └────▶│  flags=O_RDONLY ───┼─────▶│                  │
│ 2 → stderr    │          │  refcount=2        │      └──────────────────┘
│ 3 → ─────────┼─────────▶│ 表项②（dup 来的） │
│ 4 → ─────────┼────┐     │  ...               │
└──────────────┘    │     └────────────────────┘
                    │     ┌────────────────────┐      ┌──────────────────┐
进程 B              └────▶│ 表项③              │─────▶│ inode (socket)   │
┌──────────────┐          │  offset 独立        │      └──────────────────┘
│ 3 → ─────────┼─────────▶│                    │
└──────────────┘          └────────────────────┘
```

关键结论：
- **`dup`/`dup2`** 让两个 fd 指向**同一打开文件表项**，共享读写偏移和状态标志。
- **两次 `open`** 同一文件得到**两个独立表项**，偏移互不影响。
- `fork` 后父子进程的 fd 表是**拷贝**，但指向同一打开文件表项（共享偏移）。

### 2. 基础系统调用速查

```c
// 打开：flags 如 O_RDONLY/O_WRONLY/O_RDWR | O_CREAT | O_APPEND | O_NONBLOCK
int fileDescriptor = open("/data/a.log", O_RDONLY);
if (fileDescriptor < 0) { /* 看 errno，如 ENOENT 文件不存在 */ }

char readBuffer[4096];
ssize_t bytesRead = read(fileDescriptor, readBuffer, sizeof(readBuffer));
// bytesRead > 0：实际字节数（可能小于请求）；== 0：EOF；< 0：错误看 errno

lseek(fileDescriptor, 0, SEEK_SET);  // 移动读写偏移到文件头
close(fileDescriptor);
```

### 3. 部分读写必须循环（网络编程铁律）

```c
// 把 length 字节全部写出去，处理部分写和 EINTR
ssize_t writeFull(int connFd, const char *data, size_t length) {
    size_t totalWritten = 0;
    while (totalWritten < length) {
        ssize_t writtenThisTime = write(connFd, data + totalWritten,
                                        length - totalWritten);
        if (writtenThisTime < 0) {
            if (errno == EINTR) continue;        // 被信号打断，重试
            if (errno == EAGAIN) break;          // 非阻塞且发送缓冲满，稍后再写
            return -1;                            // 真错误
        }
        totalWritten += writtenThisTime;
    }
    return totalWritten;
}
```

### 4. 五种 IO 模型一图对比

```text
            数据未就绪阶段        数据拷贝阶段(内核→用户)
阻塞 IO      线程睡眠等待          线程等待拷贝完成      ← 全程阻塞
非阻塞 IO    轮询(EAGAIN空转)      调用时阻塞拷贝        ← 自己反复问
IO 多路复用  epoll_wait 阻塞       read 时阻塞拷贝       ← 一个线程管多个 fd
信号驱动     注册信号，干别的      收 SIGIO 后再 read    ← 较少用
异步 IO(aio) 提交后干别的          内核拷贝完才通知你    ← 拷贝都不占你
```

前四种都是**同步 IO**（数据拷贝阶段要你自己 read，会阻塞）；只有 aio/io_uring 是**真异步**。

### 5. epoll 内部结构：红黑树 + 就绪链表

```text
epoll_create  →  内核创建 eventpoll 对象
                 ┌────────────────────────────────────────┐
                 │  eventpoll                              │
                 │  ┌──────────────┐   ┌────────────────┐  │
epoll_ctl(ADD) → │  │ 红黑树        │   │ 就绪链表        │  │
监视的所有 fd    │  │ (所有被监视fd)│   │ (已就绪的 fd)  │  │
挂在红黑树上     │  │  connFd1     │   │  connFd3 ──────┼──┼─▶ epoll_wait
增删查 O(logN)   │  │  connFd2     │   │                │  │    只拷贝这些
                 │  │  connFd3     │   └────────────────┘  │    O(就绪数)
                 │  └──────────────┘         ▲             │
                 └───────────────────────────┼─────────────┘
                                              │
                       网卡数据到达 → 内核回调把对应 fd 挂入就绪链表
```

为什么高效：
1. **fd 只注册一次**（进红黑树），不像 select/poll 每次全量拷贝。
2. **回调驱动**：数据到达时内核直接把就绪 fd 挂到就绪链表，`epoll_wait` 不用扫描全部 fd，只取就绪链表，**开销与活跃连接数成正比**而非总连接数。

### 6. LT 与 ET 触发逻辑对比

```text
缓冲区状态：      [收到 2KB 数据]        [你只读了 1KB]        [又来 1KB]
                       │                      │                    │
LT 水平触发：    epoll_wait 通知 ──▶  还有数据，再次通知 ──▶  继续通知
                 (只要有数据就一直提醒，没读完下次还提醒)

ET 边缘触发：    epoll_wait 通知一次 ─▶  不再通知(数据还在!) ──▶ 新数据到，再通知一次
                 (只在"变化"时通知，没读完且无新数据 = 数据卡死)
```

**ET 的铁律**：必须非阻塞 fd + `while(read>0)` 循环读到 `EAGAIN`，确保把内核缓冲区**读干净**。

---

## 三、重点实战：epoll + 非阻塞 socket 的 ET echo server 骨架

下面是 Reactor 模式的雏形（socket API 细节见 [[07-Linux网络编程]]，这里只演示 epoll 主循环与 ET 处理）：

```c
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// 把 fd 设为非阻塞（ET 模式前提）
static int setNonBlocking(int targetFd) {
    int oldFlags = fcntl(targetFd, F_GETFL, 0);
    return fcntl(targetFd, F_SETFL, oldFlags | O_NONBLOCK);
}

void runEchoServer(int listenFd) {
    setNonBlocking(listenFd);

    int epollFd = epoll_create1(0);                 // 创建 epoll 实例
    struct epoll_event listenEvent;
    listenEvent.events  = EPOLLIN | EPOLLET;        // 监听 fd 也用 ET
    listenEvent.data.fd = listenFd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &listenEvent);  // 注册进红黑树

    const int kMaxEvents = 1024;
    struct epoll_event readyEvents[kMaxEvents];

    while (1) {
        // 阻塞等待，返回就绪 fd 个数，只遍历就绪的（O(就绪数)）
        int eventCount = epoll_wait(epollFd, readyEvents, kMaxEvents, -1);

        for (int eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
            int currentFd = readyEvents[eventIndex].data.fd;

            if (currentFd == listenFd) {
                // ET 下要循环 accept，把已到达的连接全部收完
                while (1) {
                    int connFd = accept(listenFd, NULL, NULL);
                    if (connFd < 0) {
                        if (errno == EAGAIN) break;  // 连接都收完了
                        else break;                  // 其它错误
                    }
                    setNonBlocking(connFd);          // 新连接必须非阻塞
                    struct epoll_event connEvent;
                    connEvent.events  = EPOLLIN | EPOLLET;
                    connEvent.data.fd = connFd;
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, connFd, &connEvent);
                }
            } else {
                // 数据可读：ET 必须循环读到 EAGAIN，否则剩余数据丢失
                char readBuffer[4096];
                while (1) {
                    ssize_t bytesRead = read(currentFd, readBuffer,
                                             sizeof(readBuffer));
                    if (bytesRead > 0) {
                        writeFull(currentFd, readBuffer, bytesRead);  // 回显
                    } else if (bytesRead == 0) {     // 对端关闭
                        epoll_ctl(epollFd, EPOLL_CTL_DEL, currentFd, NULL);
                        close(currentFd);
                        break;
                    } else {
                        if (errno == EAGAIN) break;  // 读干净了，正常退出
                        if (errno == EINTR) continue;
                        close(currentFd);             // 真错误
                        break;
                    }
                }
            }
        }
    }
}
```

这就是 **Reactor 雏形**：epoll_wait 等事件 → 分发（dispatch）到对应 fd 的处理逻辑。生产级会把每个 connFd 封装成连接对象、用线程池处理业务、加超时管理——完整网络框架见 [[07-Linux网络编程]]。

> ⚠️ **安全提醒**：上面的 echo server 没有任何认证、限流、TLS。真实物联网设备暴露在网络上的服务端口必须加身份认证和加密（如 MQTT over TLS），否则等于把设备控制权敞开给公网。

---

## 四、结合音视频 / 智能硬件场景

物联网网关常要**一个事件循环同时管多种 fd**，epoll 的「一切皆文件」威力就在这：

```text
              ┌──────────── epoll_wait 单线程事件循环 ────────────┐
              │                                                   │
   MQTT TCP 连接 fd ──┐                                           │
   HTTPS 上云 fd ─────┤                                           │
   串口 /dev/ttyS0 fd ┤──▶ epoll 红黑树统一监视 ──▶ 哪个就绪处理哪个
   摄像头 /dev/video0 ┤    (V4L2 的 fd)                           │
   eventfd(内部唤醒) ─┘                                           │
              └───────────────────────────────────────────────────┘
```

| fd 来源 | 用途 | 备注 |
|---|---|---|
| MQTT socket | 上报传感器数据、接收云端指令 | TCP fd，加 TLS |
| HTTPS socket | OTA 固件下载、大文件上云 | 大文件传输考虑 sendfile 零拷贝 |
| 串口 fd | 读单片机/传感器数据 | `/dev/ttySx`，open + O_NONBLOCK |
| V4L2 摄像头 fd | 采集视频帧 | `/dev/video0`，帧就绪触发 EPOLLIN |
| eventfd / timerfd | 线程间唤醒、定时任务 | 也是 fd，纳入同一 epoll |

一个 epoll 线程就能驱动整台设备的 IO，**不用每类设备开一个线程**，省内存、省上下文切换——这对资源紧张的嵌入式硬件至关重要。视频帧拿到后送编码、推流，发送大缓冲时再用零拷贝降 CPU。

---

## 五、常见坑与面试加分点

| 坑 | 说明 |
|---|---|
| ET 模式用阻塞 fd | 循环 read 到最后会**永久阻塞**在没数据的 read 上，必须非阻塞 |
| ET 模式没读到 EAGAIN | 缓冲区剩的数据没新事件触发，**永久丢失/连接卡死** |
| 假设 read/write 一次完成 | 必须循环，处理部分读写、EINTR、EAGAIN 三种情况 |
| 把 EAGAIN 当错误关连接 | EAGAIN 是非阻塞下的**正常**返回，不是错误 |
| ET 长期注册 EPOLLOUT | 发送缓冲一有空间就空转触发，应只在 write 返回 EAGAIN 时临时注册 |
| select 监视 fd ≥ 1024 | 超出 FD_SETSIZE 会越界踩内存，高并发别用 select |
| read 返回 0 没当断开 | read==0 是 EOF/对端关闭信号，要清理 fd |
| 忘了 close fd | fd 是有限资源，泄漏会 EMFILE（too many open files） |

**加分一句（零拷贝 + 音视频）：**

> "做流媒体回源/点播时，把『读磁盘文件→发 socket』用 `sendfile` 替代 `read+write`，省掉两次用户态拷贝，CPU 占用和内存带宽明显下降；如果中途要改协议头或加密就退回用户态——这是吞吐和灵活性的权衡。"

**加分一句（epoll 惊群与 SO_REUSEPORT）：**

> "多进程/多线程共享一个 listenFd 时，新连接来了可能唤醒所有等待者（惊群）。现代做法是每个进程用 `SO_REUSEPORT` 各自 bind 同一端口、各自 epoll，由内核做连接的负载均衡，避免惊群——nginx 就是这么干的。"

---

## 六、速记对照表

| 需求 | 选择 |
|---|---|
| 监视少量 fd、要跨平台 | select / poll |
| Linux 高并发、海量连接 | epoll |
| 简单稳妥、容错优先 | epoll LT 模式 |
| 极致性能、减少唤醒 | epoll ET + 非阻塞 + 循环读 |
| 文件随机访问/共享 | mmap |
| 文件原样转发到 socket | sendfile（零拷贝） |
| 真异步、不占调用线程拷贝 | io_uring / aio |
| 日志、文本批量写 | 标准 IO（FILE*，带缓冲） |
| 网络、精确控制时机 | 裸 fd（open/read/write） |

---

## 七、自测 8 题

1. fd 的三层内核结构是什么？`dup` 出来的两个 fd 共享什么？  
2. 非阻塞 read 在无数据时返回什么？errno 是什么？  
3. select、poll、epoll 在「找就绪 fd」上的时间复杂度分别是多少？  
4. epoll 内部用什么数据结构管理 fd？用什么返回就绪结果？  
5. LT 和 ET 的通知时机有何不同？为什么 ET 必须配非阻塞 + 循环读？  
6. read 返回 0、返回 -1 且 errno=EAGAIN，分别代表什么？  
7. sendfile 相比 read+write 省掉了哪几次拷贝？什么情况下用不了零拷贝？  
8. 为什么物联网网关适合用一个 epoll 同时管 socket、串口、摄像头 fd？  

<details>
<summary>参考答案</summary>

1. 进程 fd 表 → 系统级打开文件表（含偏移、状态）→ inode 表；`dup` 的两个 fd 指向同一打开文件表项，**共享读写偏移**。  
2. 返回 -1，errno = **EAGAIN/EWOULDBLOCK**，表示暂无数据、线程不挂起。  
3. select、poll 都是 **O(n)**（全量扫描）；epoll 是 **O(就绪数)**。  
4. **红黑树**管理所有被监视 fd（epoll_ctl 增删 O(logN)），**就绪链表**保存已就绪 fd 供 epoll_wait 返回。  
5. LT 只要有数据就**反复通知**；ET 只在状态**变化时通知一次**。ET 不读干净且无新数据就不再触发，数据会丢，所以必须非阻塞 + 循环 read 到 EAGAIN。  
6. read==0 表示**对端关闭/EOF**；read==-1 且 EAGAIN 表示**非阻塞下暂无数据，正常**，不是错误。  
7. 省掉「内核页缓存→用户缓冲」和「用户缓冲→socket 缓冲」**两次 CPU 拷贝**；当需要在用户态修改数据（加密、转码、改协议头）时用不了零拷贝。  
8. 一切皆文件，socket/串口/摄像头/eventfd 都是 fd，可纳入同一 epoll；单线程事件循环驱动全部 IO，省线程、省内存、省上下文切换，契合嵌入式资源约束。  

</details>

