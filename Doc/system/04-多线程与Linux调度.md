# 多线程与 Linux 调度：面试速记与原理详解

> **适用方向**：Linux C/C++ 开发（物联网智能硬件），音视频采集编码流水线、实时控制线程
> **难度**：🔥🔥
> **预计阅读**：速记 10 分钟｜全文 30 分钟
> **关联文档**：[[02-进程管理与多进程编程]]（task_struct、fork/clone）、[[09-多核架构与缓存一致性]]（绑核与 cache）、[[01-Linux系统编程全景导读]]（整体地图）
> **前置说明**：C++ 同步原语（mutex / 条件变量 / 原子 / 无锁队列 / 信号量）本篇只做摘要与交叉引用，细节见 cpp 库的 `01-多线程与锁`、`02-原子操作与内存序`、`03-无锁队列`、`04-信号量`。本篇聚焦 **Linux 内核与系统调用层面**：clone、futex、调度策略、CPU 亲和性、TLS。

---

## 📌 第一部分：面试速记（考前 10 分钟扫一遍）

### 一句话核心

> **在 Linux 上线程不是独立物种：内核里线程和进程都是 `task_struct`，靠 `clone()` 加不同的共享标志位区分；pthread（NPTL）是 1:1 模型，一个用户线程对应一个内核可调度实体（LWP）。所以「线程调度」本质就是内核对一批共享地址空间的 task 做 CFS / 实时调度，而我们能控制的旋钮是优先级（nice / 实时策略）和 CPU 亲和性。**

### 面试官常问问题 + 标准口语化回答

---

#### 开场题：Linux 下线程和进程到底有什么区别？

**🗣️ 面试标准回答：**

> "在 Linux 内核里**没有单独的线程结构体**，线程和进程都是一个 `task_struct`，都由 `clone()` 创建，区别只在**传给 clone 的共享标志**：建进程时几乎什么都不共享（写时复制地址空间），建线程时带上 `CLONE_VM | CLONE_FILES | CLONE_FS | CLONE_SIGHAND | CLONE_THREAD`，让这些 task **共享同一个地址空间、文件描述符表、信号处理**。
>
> 所以一个『进程的多个线程』在内核眼里就是**一组共享内存、有相同 TGID（线程组 ID）的 task**，每个 task 有自己的 LWP（轻量级进程）号，也就是 `gettid()` 拿到的内核线程 ID。调度器调度的单位是 task，不区分它是『进程』还是『线程』。"

**👨‍💻 面试官追问：**

> Q: getpid() 在多线程里返回什么？
> A: 返回的是 **TGID（线程组 ID）**，也就是主线程的 PID，进程内所有线程调用 `getpid()` 都一样；要拿到当前线程独立的内核 ID 得用 `gettid()`（返回 LWP 号）。`ps -T` 或 `top -H` 看到的就是 LWP。

---

#### 必考题：pthread 和内核线程是什么关系？std::thread 又是什么？

**考察意图：** 区分用户线程模型（1:1 / N:1 / M:N），确认你知道 Linux 走的是哪条路。

**🗣️ 面试标准回答：**

> "Linux 现在用的线程库是 **NPTL（Native POSIX Thread Library）**，是 **1:1 模型**——一个 `pthread_create` 出来的用户线程，对应内核里**一个可调度的 task（LWP）**，由内核直接调度。这和早期 LinuxThreads、以及某些系统的 M:N（用户态调度多个线程到少量内核线程）不同。1:1 的好处是真正能用多核并行、阻塞一个线程不会卡住整组；代价是线程多了内核调度对象也多。
>
> `std::thread` 在 Linux 上**底层就是 pthread**：libstdc++ 的实现里 `std::thread` 的构造最终调用 `pthread_create`。所以 `std::thread` 是跨平台封装，**要用到 Linux 特有能力**（设置 CPU 亲和性、实时优先级、线程取消、命名线程）时，要么用 `native_handle()` 拿到底层 `pthread_t` 再调 pthread API，要么直接用 pthread。"

**👨‍💻 面试官追问：**

> Q: 那什么时候直接用 pthread 而不用 std::thread？
> A: 需要 **绑核（`pthread_setaffinity_np`）、设实时调度策略（`pthread_attr_setschedpolicy`）、改栈大小、线程取消** 等 Linux/POSIX 特有功能时；或者代码本来就是 C。纯业务并发、要跨平台，用 `std::thread` / `std::jthread` 更安全（RAII、自动 join）。

---

#### 必考题：Linux 的进程调度器是什么？简单讲讲 CFS。

**🗣️ 面试标准回答：**

> "普通线程（`SCHED_OTHER`）走 **CFS（Completely Fair Scheduler，完全公平调度器）**。核心思想是给每个 task 记一个 **虚拟运行时间 `vruntime`**，调度器总是挑 vruntime **最小**的（用红黑树按 vruntime 排序，取最左节点）来跑，跑一会儿 vruntime 增长再放回去。这样长期看每个线程拿到的 CPU 时间趋于公平。
>
> **nice 值**（-20 ~ +19）调节权重：nice 越低权重越大，vruntime 增长越慢，分到的 CPU 越多。注意 CFS **没有固定时间片**，它根据可运行线程数和目标延迟动态算每个线程该跑多久。
>
> 关键结论：**CFS 是『公平』不是『实时』**——它保证大家最终都有份，但不保证某个线程能在确定的时间内被调度。要『优先保证某线程及时跑』，得用实时调度策略。"

**👨‍💻 面试官追问：**

> Q: nice 值改变的是什么，绝对时间还是比例？
> A: 改变的是 **CPU 时间的相对比例（权重）**，不是固定时间。系统空闲时 nice 高低都能跑满；只有 CPU 紧张、多个线程竞争时，nice 低的才明显多拿。

---

#### 高频题：SCHED_OTHER、SCHED_FIFO、SCHED_RR 有什么区别？音视频线程该用哪个？

**🗣️ 面试标准回答：**

> "三种是 Linux 的调度策略：
>
> - **`SCHED_OTHER`**：默认的普通分时策略，走 CFS，用 nice 值调权重，**无实时优先级**。绝大多数线程都是它。
> - **`SCHED_FIFO`**：实时策略，**先进先出**。有实时优先级（1~99），**只要它可运行，就抢占所有更低优先级的线程，且一直跑到它自己阻塞或主动让出**（同优先级不轮转）。
> - **`SCHED_RR`**：实时策略，**时间片轮转**。和 FIFO 一样高优先级抢占低优先级，区别是**同优先级的多个线程按时间片轮流跑**，不会一个独占。
>
> 实时优先级（1~99）**整体高于所有 SCHED_OTHER**，所以一个 SCHED_FIFO 线程可以饿死普通线程。音视频/控制场景里，**采集、编码、低延迟发送这类对延迟敏感的线程**可以设成 `SCHED_FIFO` 或 `SCHED_RR` 并给较高优先级，保证抖动小；但要小心别让它死循环把核占满，否则普通线程（甚至某些内核辅助线程）会被饿死。"

**👨‍💻 面试官追问：**

> Q: Linux 是实时操作系统吗？
> A: **标准 Linux 不是硬实时**。它有实时调度策略，但内核里仍有不可抢占区间、中断延迟、优先级反转等不确定性，只能算『软实时』。要硬实时得上 **PREEMPT_RT 补丁** 或专用 RTOS。面试这么答能体现你知道边界。

---

#### 高频题：什么是 CPU 亲和性（affinity）？绑核有什么好处？

**🗣️ 面试标准回答：**

> "CPU 亲和性就是**把线程绑定到指定的一个或几个 CPU 核上跑**，用 `sched_setaffinity`（进程/线程）或 `pthread_setaffinity_np`（线程）设置，参数是一个 `cpu_set_t` 位掩码。
>
> 好处主要两点：
> 1. **cache 局部性**：线程一直在同一个核上跑，它的工作集就常驻该核的 L1/L2 cache，不会因为被调度到别的核而 cache 全失效、重新从内存加载（这点和 [[09-多核架构与缓存一致性]] 直接相关）。
> 2. **减少迁移开销与抖动**：不绑核时调度器可能把线程在核之间搬来搬去，每次迁移都有开销、还破坏 cache。绑核后延迟更稳定，适合低延迟、实时线程。
>
> 智能硬件/音视频里常见做法：**把中断处理、网络收发、编码这类热路径线程各绑一个核，并把这些核从普通调度里隔离（`isolcpus`）**，让它们独享，抖动最小。"

**👨‍💻 面试官追问：**

> Q: 绑核一定更快吗？
> A: 不一定。**绑错了反而更糟**：如果绑的核同时还跑别的重负载线程、或线程数多于核数导致排队，反而增加延迟。绑核适合**少量关键线程**独占核；普通线程交给调度器自动均衡通常更好。

---

#### 高频题：什么是优先级反转？怎么解决？

**🗣️ 面试标准回答：**

> "优先级反转是：**高优先级线程被一个低优先级线程间接卡住**。经典场景——低优先级线程 L 持有一把锁，高优先级线程 H 要这把锁被阻塞；这时一个**中优先级**线程 M 就绪了，它抢占了 L（因为 M 比 L 高），导致 L 迟迟跑不完、释放不了锁，于是 H 被 M 间接卡住——本该最高优先级的 H 反而等最久。火星探路者（Mars Pathfinder）就是这个 bug。
>
> 解决办法主要是**优先级继承（Priority Inheritance）**：当 H 等 L 持有的锁时，**临时把 L 的优先级提升到 H 的水平**，让 L 尽快跑完释放锁，M 就抢不走它了。pthread 里用 `pthread_mutexattr_setprotocol` 设 `PTHREAD_PRIO_INHERIT` 开启。另一种是**优先级天花板（Priority Ceiling）**，持锁就直接升到该锁预设的最高优先级。"

---

#### 高频题：TLS 线程局部存储是什么？errno 为什么是线程安全的？

**🗣️ 面试标准回答：**

> "TLS（Thread Local Storage）就是**每个线程各有一份的全局/静态变量**，互不干扰。用法：C++11 的 `thread_local`，或 GCC 扩展 `__thread`，或 POSIX 的 `pthread_key_create` / `pthread_getspecific`（运行期动态、可带析构回调）。
>
> `errno` 经典例子：它看起来是个全局变量，但在多线程下如果真共享，A 线程的系统调用出错改了 errno，B 线程读到就乱了。所以现代 libc 把 `errno` 实现成 **线程局部的**——`errno` 实际是个宏，展开成 `*__errno_location()`，每个线程返回自己那份。这样每个线程检查自己最近一次系统调用的错误码，互不污染。"

---

#### 必考题：futex 是什么？mutex 底层是怎么实现的？

**🗣️ 面试标准回答：**

> "**futex（Fast Userspace muTEX）** 是 Linux 提供的一个系统调用，是 pthread mutex、条件变量、信号量等同步原语的**内核支撑**。核心思想是『**快路径在用户态，慢路径才进内核**』：
>
> - **无竞争时**：加锁就是一条用户态原子操作（CAS 把锁标志 0→1），**完全不进内核**，极快。
> - **有竞争时**：CAS 失败说明锁被占，这时才调用 `futex(FUTEX_WAIT)` 让内核把当前线程挂起睡眠；持锁线程解锁时若发现有人等，调用 `futex(FUTEX_WAKE)` 唤醒。
>
> 所以一句话：**futex 让『没冲突的常见情况零系统调用』，只有真正需要阻塞/唤醒时才付出 syscall 代价**。这就是为什么 pthread mutex 在低竞争下几乎和原子操作一样快。C++ 的 `std::mutex` 在 Linux 上最终也是走 pthread mutex → futex。"

**👨‍💻 面试官追问：**

> Q: 那 C++ 的同步原语和这有什么关系？
> A: `std::mutex`/`std::condition_variable`/`std::counting_semaphore` 在 glibc + Linux 上底层都落到 futex。**用户写代码用 C++ 标准库，性能特性由 futex 决定**。同步原语的 API 用法见 cpp 库的 `01-多线程与锁`、`04-信号量`，本篇不重复。

---

#### 实战题：怎么定位一个线上的死锁？

**🗣️ 面试标准回答：**

> "线上进程卡住、CPU 占用为 0（不是死循环），首先怀疑死锁。步骤：
>
> 1. `top -H -p <pid>` 看各线程状态，死锁线程通常处于 `S`（睡眠）。
> 2. **`gdb -p <pid>` attach 上去**，`info threads` 看所有线程，`thread apply all bt` 打印每个线程的调用栈。
> 3. 看哪些线程卡在 `pthread_mutex_lock` / `__lll_lock_wait`（futex 等待），对照它们各自**已经持有**和**正在等**的锁，画出等待环——A 等 B 持的锁、B 等 A 持的锁，环就是死锁。
>
> 也可以用 `cat /proc/<pid>/task/<tid>/stack` 看内核栈，或上 ThreadSanitizer（`-fsanitize=thread`）在测试期捕获。gdb 调试细节见 [[10-工具链与调试]]。"

---

## 二、原理详解

### 1. clone() 与共享标志：进程和线程的统一

Linux 用同一个 `clone()` 系统调用创建进程和线程，区别只在共享标志：

```text
        fork() ≈ clone(SIGCHLD)              pthread_create ≈ clone(CLONE_VM | CLONE_FILES
        几乎不共享，地址空间写时复制              | CLONE_FS | CLONE_SIGHAND | CLONE_THREAD ...)
        ┌──────────────┐                      ┌──────────────────────────────┐
        │  进程 A       │                      │      进程 B（线程组 TGID=B）    │
        │ task_struct  │                      │  ┌────────┐  ┌────────┐       │
        │  独立 mm      │                      │  │ 主线程  │  │ 线程2  │       │
        └──────────────┘                      │  │task LWP│  │task LWP│ 共享 mm│
                                              │  └────────┘  └────────┘       │
                                              └──────────────────────────────┘
```

常见 clone 标志含义（线程需要的几个）：

| 标志 | 共享什么 |
|---|---|
| `CLONE_VM` | 共享地址空间（同一份内存，线程的根本特征） |
| `CLONE_FILES` | 共享文件描述符表（一个线程 open 的 fd 别的线程能用） |
| `CLONE_FS` | 共享文件系统信息（cwd、umask、root） |
| `CLONE_SIGHAND` | 共享信号处理函数表 |
| `CLONE_THREAD` | 归入同一线程组（共享 TGID，getpid 相同） |

### 2. pthread 1:1 模型（NPTL）

现代 Linux 的线程库是 **NPTL**，采用 **1:1 模型**：每个 `pthread_create` 出来的用户线程，对应内核里**一个独立可调度的 task（LWP）**，由内核直接调度。

```text
        1:1（NPTL，今天的 Linux）             N:1（早期用户态线程库）
        每个用户线程 = 一个内核 task          多个用户线程映射到 1 个内核 task
        ┌────┐ ┌────┐ ┌────┐                 ┌────────────────────┐
        │线程1│ │线程2│ │线程3│               │ 用户态线程 1/2/3      │
        └─┬──┘ └─┬──┘ └─┬──┘                 └─────────┬──────────┘
          ▼      ▼      ▼                              ▼
        ktask  ktask  kask                       单个 kernel task
        内核分别调度，能用多核                    内核只看到 1 个，无法真并行
```

- **1:1 优点**：真正多核并行；某个线程阻塞（系统调用、缺页）不会卡住整组；调度交给成熟的内核调度器。
- **1:1 代价**：线程多了内核调度对象也多，创建/切换有内核介入开销。
- **N:1 缺点**：一个线程进内核阻塞，整组都被卡；用不了多核。早期 LinuxThreads、某些语言的「绿色线程」走这条路（或更复杂的 M:N）。

### 3. 线程栈：默认 8MB，是开线程数量的隐形上限

每个线程都有**自己独立的栈**（局部变量、函数调用帧都在上面），主线程栈和子线程栈分开。

- **默认大小**：Linux 上 pthread 子线程栈默认 **8MB**（受 `ulimit -s` 影响），主线程栈也是 8MB 量级。
- **设置栈大小**：用 `pthread_attr_setstacksize` 在创建前指定，单位字节。

```c
pthread_attr_t threadAttribute;
pthread_attr_init(&threadAttribute);
pthread_attr_setstacksize(&threadAttribute, 512 * 1024);  // 每线程栈设为 512KB
pthread_create(&threadHandle, &threadAttribute, workerRoutine, NULL);
pthread_attr_destroy(&threadAttribute);
```

- **线程多了内存吃紧**：默认 8MB×线程数，开 1000 个线程光栈就预留 ~8GB 虚拟地址空间（实际按需缺页，但 32 位进程地址空间会直接爆）。高并发场景要么调小栈，要么改用线程池 / 协程 / 事件驱动，别无脑一连接一线程。
- **栈溢出**：递归过深、超大局部数组（`char buffer[10*1024*1024]`）会越过栈边界。Linux 在栈末尾放一个 guard page（保护页），踩到它立刻 `SIGSEGV`，而不是悄悄踩坏别的内存。

### 4. TLS 线程局部存储的实现简述

TLS 让一个「全局/静态」变量**每个线程各持有一份**，互不干扰。

```c
__thread int errorCodeCache;        // GCC 扩展，C 也能用
thread_local int frameCounter;      // C++11 标准写法，语义相同
```

实现上，每个线程的控制块（TCB）里挂着一段 TLS 区域，编译器把 `thread_local` 变量的访问翻译成「**线程寄存器（x86-64 用 `fs`，aarch64 用 `tpidr_el0`）指向的基址 + 固定偏移**」。所以读写一个线程局部变量仍是普通内存访问，没有锁、没有系统调用，开销极低，只是基址按线程不同而已。

`errno` 就是典型 TLS：它展开成 `*__errno_location()`，每个线程返回自己那份，A 线程系统调用出错不会污染 B 线程读到的错误码。

### 5. futex 快慢路径：mutex 高效的根本原因

**futex（Fast Userspace muTEX）** 的设计精髓是「**快路径在用户态，慢路径才进内核**」：

```text
  加锁 lock：
    CAS(锁标志 0 → 1) 成功  ──►  直接返回，全程用户态，零系统调用（快路径）
    CAS 失败（已被占）      ──►  futex(FUTEX_WAIT) 进内核睡眠（慢路径）
  解锁 unlock：
    无人等待                ──►  原子置 0，直接返回（快路径）
    有人等待                ──►  futex(FUTEX_WAKE) 进内核唤醒一个（慢路径）
```

**结论**：没有竞争的常见情况下，加解锁就是一两条原子指令，**完全不进内核**——这就是 pthread mutex（以及 `std::mutex`）在低竞争下几乎和裸原子操作一样快的原因。只有真正发生争抢、必须让线程睡眠或唤醒时，才付出一次系统调用的代价。

### 6. 上下文切换开销：线程切换为什么比进程便宜

调度器从一个 task 切到另一个 task，要保存/恢复一批状态：

| 切换内容 | 进程间切换 | 同进程线程间切换 |
|---|---|---|
| 通用寄存器、程序计数器、栈指针 | 要 | 要 |
| 浮点/向量寄存器（FPU/SIMD） | 要 | 要 |
| **页表基址寄存器（CR3 / TTBR）** | **要换**（不同地址空间） | **不换**（共享同一地址空间） |
| **TLB（地址翻译缓存）** | **基本全部失效**，需重新填充 | **可保留**，命中率不受影响 |
| 内核栈、内核态上下文 | 要 | 要 |

关键差异在**地址空间**：进程切换要换页表基址寄存器，导致 **TLB 大面积失效**，切换后头一段时间频繁 TLB miss、性能凹陷；同进程的线程共享地址空间，**页表和 TLB 不用动**，所以线程切换比进程切换便宜。这也是「线程比进程轻量」最实在的一条。

> 但「线程切换便宜」不等于「切换免费」：寄存器保存恢复、调度器红黑树操作、cache 工作集被打散都有成本。**减少不必要的切换（少 contention、绑核、批处理）** 往往比纠结单次切换开销更值。

### 7. 代码：设置 CPU 亲和性（绑核）

把线程绑到指定核，提升 cache 局部性、减少迁移抖动（绑核与 cache 的关系见 [[09-多核架构与缓存一致性]]）。

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

// 把指定线程绑定到编号为 cpuCore 的核上
int bindThreadToCore(pthread_t threadHandle, int cpuCore) {
    cpu_set_t coreMask;
    CPU_ZERO(&coreMask);            // 清空掩码
    CPU_SET(cpuCore, &coreMask);    // 置位目标核
    // pthread 版本：绑某个具体线程
    return pthread_setaffinity_np(threadHandle, sizeof(coreMask), &coreMask);
}

// 进程/当前线程版本：sched_setaffinity，传 0 表示调用线程自己
int bindCurrentThreadToCore(int cpuCore) {
    cpu_set_t coreMask;
    CPU_ZERO(&coreMask);
    CPU_SET(cpuCore, &coreMask);
    return sched_setaffinity(0, sizeof(coreMask), &coreMask);  // 0 = 当前线程
}
```

### 8. 代码：设置实时调度策略（SCHED_FIFO + 优先级）

把线程设成实时 FIFO 策略并给定优先级，让它抢占普通线程，降低延迟抖动。**需要 root 或 `CAP_SYS_NICE` 权限。**

```c
#include <pthread.h>
#include <sched.h>

// 把线程设为 SCHED_FIFO 实时策略，优先级 realtimePriority(1~99)
int setRealtimeFifo(pthread_t threadHandle, int realtimePriority) {
    struct sched_param schedParam;
    schedParam.sched_priority = realtimePriority;
    // 第二参 policyType 选 SCHED_FIFO（先进先出，不轮转）
    return pthread_setschedparam(threadHandle, SCHED_FIFO, &schedParam);
}
```

> 优先级合法范围用 `sched_get_priority_min/max(SCHED_FIFO)` 查询（Linux 上是 1~99）。SCHED_RR 用法相同，只是同优先级会按时间片轮转。

### 9. 代码：读写锁（pthread_rwlock）

读多写少时，多个读者可并发持锁，写者独占——比 mutex 一刀切吞吐更高。

```c
#include <pthread.h>

pthread_rwlock_t configLock = PTHREAD_RWLOCK_INITIALIZER;

void readConfig(void) {
    pthread_rwlock_rdlock(&configLock);   // 读锁：多个读者可同时持有
    // ... 只读访问共享配置 ...
    pthread_rwlock_unlock(&configLock);
}

void updateConfig(void) {
    pthread_rwlock_wrlock(&configLock);   // 写锁：独占，排斥所有读者和其他写者
    // ... 修改共享配置 ...
    pthread_rwlock_unlock(&configLock);
}
```

### 10. 代码：屏障（pthread_barrier）

让若干线程在某一点**集合等齐**，全部到达后再一起放行——适合分阶段并行计算。

```c
#include <pthread.h>

pthread_barrier_t stageBarrier;

// 初始化：等齐 workerCount 个线程才放行
pthread_barrier_init(&stageBarrier, NULL, workerCount);

void* workerRoutine(void* arg) {
    // ... 第一阶段计算 ...
    pthread_barrier_wait(&stageBarrier);   // 在此等所有线程都算完第一阶段
    // ... 第二阶段：依赖所有线程的第一阶段结果 ...
    return NULL;
}
// 用完销毁
pthread_barrier_destroy(&stageBarrier);
```

### 11. 代码：音视频流水线片段（采集绑核0 + 编码绑核1 + 高优先级）

把采集线程绑到核 0、编码线程绑到核 1，并给编码线程实时优先级，保证编码不被普通线程抢占、延迟稳定。

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

void* captureRoutine(void* arg) { /* 采集 */ return NULL; }
void* encodeRoutine(void* arg)  { /* 编码 */ return NULL; }

void startPipeline(void) {
    pthread_t captureThread, encodeThread;
    pthread_create(&captureThread, NULL, captureRoutine, NULL);
    pthread_create(&encodeThread,  NULL, encodeRoutine,  NULL);

    bindThreadToCore(captureThread, 0);   // 采集独占核 0
    bindThreadToCore(encodeThread,  1);   // 编码独占核 1

    // 编码是延迟敏感热路径：给实时优先级，抖动最小
    setRealtimeFifo(encodeThread, 80);

    pthread_join(captureThread, NULL);
    pthread_join(encodeThread,  NULL);    // 必须 join，否则资源泄漏
}
```

> 工程上还会用 `isolcpus=0,1` 把核 0/1 从普通调度隔离出来，让流水线线程独享，进一步压低抖动。

---

## 三、Linux 同步原语速览（C++ 标准库部分见 cpp 库）

下表只做摘要与定位。**互斥、条件变量、原子、信号量的 API 用法和内存序细节，本篇不重复，深讲见 cpp 库的 `01-多线程与锁`、`02-原子操作与内存序`、`03-无锁队列`、`04-信号量`。** 本篇补 Linux 特有的几个。

| 原语 | 一句话用途 | 底层 | 深讲位置 |
|---|---|---|---|
| `pthread_mutex` / `std::mutex` | 互斥保护临界区，谁加锁谁解锁 | futex | cpp 库的 `01-多线程与锁` |
| `pthread_cond` / `std::condition_variable` | 配合 mutex 等复杂条件 | futex | cpp 库的 `01-多线程与锁` |
| `std::atomic` | 单变量无锁同步、内存序控制 | CPU 原子指令 | cpp 库的 `02-原子操作与内存序` |
| 无锁队列 | 高频场景免锁 SPSC/MPMC | atomic + 内存序 | cpp 库的 `03-无锁队列` |
| `sem_t` / `std::counting_semaphore` | 资源份数 / 事件计数 | futex | cpp 库的 `04-信号量` |

**Linux 特有、cpp 库不一定覆盖的：**

| 原语 | 用途 | 关键点 |
|---|---|---|
| `pthread_rwlock` | 读写锁，读多写少提升并发 | 多读者并发、写者独占；注意写者饥饿（大量读者时写者长期拿不到锁） |
| `pthread_barrier` | 屏障，N 个线程集合点等齐再放行 | 分阶段并行；初始化时定好参与线程数 |
| `futex` | 上述原语的内核支撑系统调用 | 快路径用户态原子、慢路径才进内核睡眠/唤醒，是 mutex 高效的根本 |
| `pthread_spinlock` | 自旋锁，忙等不睡眠 | 仅适合临界区极短、且确定不会被抢占的场景，否则空转烧 CPU |

---

## 四、常见坑与面试加分点

| 坑 | 说明 |
|---|---|
| 忘了 join / detach | 线程结束后资源（栈、TCB）不回收，造成泄漏；`pthread_join` 回收，或 `pthread_detach` 让其自动回收。`std::jthread` 析构自动 join 更安全 |
| 绑核绑错 NUMA 节点 | 把线程绑到的核和它访问的内存不在同一 NUMA 节点，每次访存跨节点，比不绑还慢；绑核要连同内存分配（`numactl` / `mbind`）一起考虑 |
| SCHED_FIFO 高优先级线程死循环 | 实时线程一旦忙等不阻塞，会把整个核占满，饿死同核普通线程甚至内核辅助线程，系统看起来「卡死」；实时线程必须有明确的阻塞/让出点 |
| 线程栈太大 + 开太多线程 OOM | 默认 8MB×线程数，高并发下虚拟地址/内存爆掉；调小栈或改线程池 |
| 用 `volatile` 替代原子 | `volatile` 只防编译器优化掉读写，**不保证原子性、不提供内存序**，多线程下读写共享变量该用 `std::atomic`，不是 `volatile` |
| `getpid()` 当线程 ID 用 | 多线程里所有线程 `getpid()` 返回相同 TGID；要线程唯一内核 ID 用 `gettid()` |
| 忘记设栈大小就大量递归 | 子线程默认栈虽 8MB，但若被改小又深递归，直接踩 guard page 段错误 |

**加分一句（音视频实时线程）：**

> "采集和编码线程我会绑核 + 给实时优先级降低抖动，但绝不让实时线程里出现忙等死循环——它会把核占满饿死别人。实时线程的循环里一定有阻塞点（等帧、等信号量），让出 CPU 给系统喘息，必要时配 `isolcpus` 隔离核独享。"

---

## 五、速记对照表：什么场景用什么

| 场景 / 需求 | 选择 | 理由 |
|---|---|---|
| 任务相互独立、要隔离崩溃（一个挂了不连累别的） | **多进程** | 地址空间独立，互不影响（见 [[02-进程管理与多进程编程]]） |
| 任务共享大量数据、要低开销通信 | **多线程** | 共享地址空间，切换便宜，无需 IPC |
| 关键热路径线程要 cache 稳定、低抖动 | **绑核（亲和性）** | 工作集常驻同一核 cache，减少迁移 |
| 延迟敏感线程要确定性被调度 | **实时优先级（SCHED_FIFO/RR）** | 抢占普通线程；务必有阻塞点 |
| 普通业务并发、跨平台 | **`std::thread` / `std::jthread`** | RAII、自动 join，省心 |
| 高并发短连接、线程会爆 | **线程池 / 事件驱动** | 复用线程，避免一连接一线程 OOM |

---

## 六、自测题

1. Linux 内核里线程和进程的本质区别是什么？`getpid()` 和 `gettid()` 在多线程里分别返回什么？
2. NPTL 是几比几模型？相比 N:1 有什么优劣？
3. 线程默认栈多大？开大量线程时它为什么是个隐患，怎么缓解？
4. 用「快路径 / 慢路径」解释 futex 为什么让 mutex 在低竞争下很快。
5. 为什么同进程线程间切换比进程间切换便宜？关键差在哪一项状态上？
6. SCHED_FIFO 高优先级线程里写了个忙等死循环会发生什么？怎么避免？

<details>
<summary>参考答案</summary>

1. 内核里两者都是 `task_struct`、都由 `clone()` 创建，区别只在传给 clone 的共享标志（线程带 `CLONE_VM` 等共享地址空间/fd/信号）。`getpid()` 返回线程组 TGID（进程内所有线程相同），`gettid()` 返回各线程独立的 LWP 内核 ID。
2. **1:1 模型**：一个用户线程对应一个内核可调度 task。优点是能真多核并行、单线程阻塞不卡整组；代价是线程多则内核调度对象多。N:1 用不了多核且一个阻塞卡全组。
3. 默认 **8MB**。隐患：8MB×线程数会吃掉大量虚拟地址/内存，高并发易 OOM（32 位尤甚）。缓解：`pthread_attr_setstacksize` 调小栈，或改用线程池 / 协程 / 事件驱动。
4. **无竞争（快路径）**：加解锁只是用户态 CAS 原子操作，完全不进内核，极快。**有竞争（慢路径）**：CAS 失败才调 `futex(FUTEX_WAIT)` 进内核睡眠，解锁时 `futex(FUTEX_WAKE)` 唤醒。常见情况零系统调用，所以快。
5. 同进程线程共享地址空间，**不用换页表基址寄存器、TLB 可保留**；进程切换要换页表导致 TLB 大面积失效、切换后频繁 miss。关键差在地址空间（页表/TLB）这一项。
6. 它会一直可运行、抢占并占满所在核，饿死同核的普通线程甚至内核辅助线程，系统表现为卡死。避免：实时线程循环里必须有阻塞/让出点（等帧、等信号量、`sched_yield`），并可用 `isolcpus` 隔离专核。

</details>

---
