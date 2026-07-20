# sigaction 信号处理详解：从 API 到内核行为

> **适用方向**：Linux C/C++ / 后端服务 / 智能硬件守护进程
> **难度**：⭐⭐⭐⭐
> **关联文档**：[[02-进程管理与多进程编程]]（信号处理面试速记）、[[03-Linux内存管理机制]]（页错误信号的例子）、[[05-进程间通信IPC]]（信号量 ≠ 信号）

---

## 📌 导读：这个知识点有多重要？面试会怎么考？

### 工程中的重要程度：⭐⭐⭐⭐（必知必会）

`sigaction` 不是你"可以学"的东西，是你"必须会"的东西。理由很简单：

**每个需要长期稳定运行的 Linux 程序都必须处理信号。** 不管你是写后端服务、守护进程、智能硬件网关、音视频流媒体服务器、还是嵌入式 Linux 应用——程序跑起来后，外部随时会通过信号告诉你"该退出了""配置变了""子进程死了"。你不处理，默认行为就是直接终止，用户数据丢失、连接断开、硬件不释放。

而处理信号的**唯一正确方式**就是 `sigaction`。`signal()` 只配出现在面试对比题里。

> 你打开任何一个知名 C/C++ 开源项目的源码——Nginx、Redis、PostgreSQL、Node.js、FFmpeg——搜索 `sigaction`，一定能找到。**没有例外。**

### 生产环境中的典型用法

实际工程中不会只写一个 demo 级别的 `on_term` 就完事了。真实的信号处理架构通常是以下几层的组合：

```
信号到达
  │
  ▼
信号处理函数（极其精简）
  │  async-signal-safe 的操作：
  │  写 volatile sig_atomic_t 标志 / 往 self-pipe 写 1 字节 / waitpid 收尸
  │
  ▼
主循环 / 事件循环（同步检查）
  │  检查标志位 / epoll 读到管道事件
  │
  ▼
业务层清理（不受 async-signal-safe 限制）
  释放资源、刷盘、通知 watchdog、join 线程、优雅关闭连接
```

一个典型的 Linux 后端服务的信号架构通常包含：

| 信号 | 处理策略 |
|------|---------|
| `SIGTERM` / `SIGINT` | 设退出标志 → 主循环收尾 → 优雅退出 |
| `SIGCHLD` | `while + waitpid + WNOHANG` 循环收尸 |
| `SIGHUP` | 设 reload 标志 → 主循环重新加载配置 |
| `SIGPIPE` | 直接忽略（`SIG_IGN`），避免对端断连导致进程崩溃 |
| `SIGSEGV` / `SIGBUS` | 捕获 → 打印堆栈 → `_exit`（生产环境最后的救命日志） |
| `SIGUSR1` / `SIGUSR2` | 自定义用途：日志轮转、dump 状态、触发诊断 |

### 面试一般怎么问？（附标准口语化回答，可直接背）

`sigaction` 很少作为一个独立的"请介绍 sigaction"大题出现。它通常是**被包含在更大的信号处理题里**，考察方式是渐进深入的。下面按三层深度，每题附上可直接背诵的口语化标准回答。

---

**第一层（及格线，60%）——开门题，考察你是否写过生产代码**

> 🎯 考察意图：区分"只在课本上学过信号"和"真的在生产环境处理过信号"的人。

---

**Q1: "信号处理用过吗？`signal` 和 `sigaction` 有什么区别？"**

🗣️ **背诵回答：**

> "用过，我们在项目里的守护进程和崩溃捕获模块都用信号处理。要说区别的话，**`signal` 是老 API，`sigaction` 是新 API，工程上应该全部用 `sigaction`**，`signal` 只配出现在面试对比题里。
>
> 具体来说 `signal` 有三个致命问题：
>
> 第一，**跨平台行为不一致**。System V 的实现里，信号处理函数被调一次就自动重置回默认动作，你得在处理函数里赶紧再 `signal` 一次重新注册，这中间有个竞态窗口——如果同一个信号在这极短的间隙里又来一次，进程就按默认动作走了。比如 `SIGINT` 默认是终止进程，相当于你注册了处理函数但没防住。BSD 和 Linux 没这个问题，但你不能假设代码只跑在 Linux 上。
>
> 第二，**控制不了处理函数执行期间屏蔽哪些信号**。`signal` 自动屏蔽当前信号本身，但你不能额外指定屏蔽其他信号。比如处理 `SIGTERM` 做优雅退出时，你没法阻止 `SIGINT` 打断它。`sigaction` 有 `sa_mask` 字段，你可以精确指定处理函数期间屏蔽哪些信号。
>
> 第三，**控制不了被打断的系统调用是否自动重启**。`read`、`write`、`accept` 这些阻塞调用被信号打断后，`signal` 下你必须手动处理 `EINTR` 然后重试，代码又臭又容易漏。`sigaction` 一个 `SA_RESTART` 标志位就全自动了。
>
> 再加上 `sigaction` 还能通过 `SA_SIGINFO` 拿到信号的附带数据——谁发的、为什么发、带没带附加值。这些都是 `signal` 做不到的。"

---

**Q2: "为什么 `signal` 不安全？一句话总结。"**

🗣️ **背诵回答：**

> "一句话：**行为跨平台不一致，而且给不了你精细控制**。同样一句 `signal(SIGINT, handler)`，在 System V 上处理一次就重置回 `SIG_DFL`，中间有竞态窗口；在 BSD/Linux 上没事。这种不确定性在生产环境就是定时炸弹——代码在开发机上测得好好的，部署到另一个 Unix 变体上行为完全不一样。而且 `signal` 没有 `sa_mask`、没有 `SA_RESTART`、拿不到信号发送者的信息，功能太简陋了。所以 POSIX 搞了 `sigaction`，现在所有严肃项目全用它。"

---

**Q3: "收到 `SIGTERM` 你怎么做优雅退出？"**

🗣️ **背诵回答：**

> "核心思路就是**异步信号转同步处理**。信号处理函数里只做最安全的事——设一个 `volatile sig_atomic_t` 的标志位；主循环每轮迭代检查这个标志，一旦发现被置位，就跳出循环，然后在主流程里安心做清理。
>
> 具体代码骨架是这样的：
> - 定义 `static volatile sig_atomic_t should_stop = 0;`
> - 信号处理函数就一行：`should_stop = 1;`
> - 主循环 `while (!should_stop) { ... }`
> - 跳出循环后调清理函数：刷日志、关连接、释放资源、join 子线程、通知 watchdog，最后 `return 0` 正常退出。
>
> 为什么不能直接在处理函数里做清理？因为处理函数是异步打断主流程的——你可能正卡在 `malloc` 持锁的中间，这时候再调 `malloc`/`printf`/`fclose` 随时死锁。必须回到主流程再清理，那时候不受 async-signal-safe 限制，想调什么调什么。"

---

**第二层（扎实，80%）——追问细节，考察你是否真的理解**

> 🎯 考察意图：区分"背了 API"和"理解了异步信号的本质限制"。

---

**Q4: "`SA_RESTART` 是干什么的？不设会怎样？"**

🗣️ **背诵回答：**

> "`SA_RESTART` 让被这个信号打断的**慢系统调用自动重启**。慢系统调用就是那些可能永久阻塞的调用——`read`、`write`、`accept`、`select`、`wait` 等。
>
> 举个例子：你的主循环正阻塞在 `read(fd, buf, size)` 上等数据，这时 `SIGTERM` 到了。如果**设了 `SA_RESTART`**，内核帮你自动重新发起 `read`，你的代码感知不到被打断过，然后主循环检查 `should_stop` 发现为 1，正常退出。如果**没设 `SA_RESTART`**，`read` 直接返回 -1，`errno` 被设成 `EINTR`，你得手动写 `if (errno == EINTR) goto retry;`。
>
> 几乎所有场景都应该设 `SA_RESTART`，除非你有特殊需求——比如用 `alarm` 做超时，就希望 `read` 被 `SIGALRM` 打断后返回，这时就不设。"

---

**Q5: "信号处理函数里你能调 `printf` 吗？为什么？那你能调什么？"**

🗣️ **背诵回答：**

> "**绝对不能调 `printf`。** 原因很经典——`printf` 不是 async-signal-safe 的。
>
> 信号是异步打断主流程的。假设主流程正执行 `malloc`，持有了堆分配器的内部锁，链表刚改了一半——就在这个瞬间信号到了。内核暂停主流程，跳进你的处理函数。如果你的处理函数里调了 `printf`，`printf` 内部要分配缓冲区、可能触发 `malloc`——它再去抢那把锁。但锁还被"正在执行但还没执行完"的 `malloc` 持有着。**死锁**。进程直接卡死，kill -9 都不一定好使（因为这跟 D 状态不一样，是用户态死锁）。
>
> 那能调什么？POSIX 规定了一组 **async-signal-safe 函数**，最常用的就三个：**`write`**（直接写文件描述符，不经过 stdio 缓冲）、**`_exit`**（直接陷进内核终止进程，不走 atexit 和 stdio 刷新）、**`waitpid`**（配合 WNOHANG 收尸）。其他的像 `read`、`open`、`close`、`kill`、`getpid` 也是安全的。实在拿不准就记住：**只调 `write` 和 `_exit`，其他的不碰。**
>
> 还有 `malloc`、`free`、`new`、`delete`、`pthread_mutex_lock`、`exit`（注意不是 `_exit`，少了个下划线就不安全了），这些一律不能调。"

---

**Q6: "`sa_mask` 在处理函数执行期间起什么作用？"**

🗣️ **背诵回答：**

> "`sa_mask` 指定处理函数执行期间**额外**要屏蔽的信号。注意这个'额外'——触发本次调用的信号本身也会被自动屏蔽（除非你设 `SA_NODEFER`），不需要手动加。
>
> 实际场景：我处理 `SIGTERM` 做优雅关闭时，不想被 `SIGINT` 打断——万一处理函数跑到一半用户又 Ctrl+C，可能导致清理不完整。这时候就在 `sa_mask` 里加上 `SIGINT`：处理 `SIGTERM` 期间 `SIGINT` 来了就挂 pending，等处理函数返回、屏蔽字恢复后才递达。
>
> 内核的行为是：进入处理函数时，进程的屏蔽字 = 原来的屏蔽字 | `sa_mask` | 触发信号自身；处理函数返回后自动恢复。整个过程对用户是透明的。这比 `signal` 强太多了——`signal` 你完全控制不了这个。"

---

**Q7: "你说信号处理函数里只设一个标志位，为什么这个标志要加 `volatile`？`sig_atomic_t` 又是什么？"**

🗣️ **背诵回答：**

> "这俩关键字解决的是两个不同的问题，但必须一起用。
>
> **`volatile` 是给编译器看的。** 编译器在优化代码时，如果发现主循环里 `while (!should_stop)` 这个变量从来没被主循环自己改过，它可能自作聪明地把 `should_stop` 缓存到寄存器里，只在循环开始前读一次，之后每次都看寄存器里的旧值。但信号处理函数是异步执行的，它的写操作编译器"看不到"——编译器分析主流程时根本不会考虑信号处理函数的存在。加上 `volatile` 就是告诉编译器：**每次用这个变量必须从内存重新读，别自作聪明。**
>
> **`sig_atomic_t` 是给 CPU 看的。** C 标准保证这个类型的读写是"原子"的——一条 CPU 指令完成，不会被信号打断。比如在 32 位平台上读写一个 `long long`（64 位）可能需要两条指令，如果信号恰好在两条指令之间到达，标志位就可能读到半个旧值半个新值的脏数据。`sig_atomic_t` 通常是 `int`，在绝大多数平台上读写它是一条 `mov` 指令，保证完整性。
>
> 所以规范写法就是：`static volatile sig_atomic_t flag = 0;`——`volatile` 防止编译器优化看不到，`sig_atomic_t` 保证硬件层面读写不被撕开。少了任何一个，理论上都可能出 bug。虽然很多项目直接用 `volatile int` 也没事，但面试时你答到 `sig_atomic_t` 会让面试官知道你读过标准。"

---

**第三层（优秀，95%）——开放设计题，考察是否能设计整套方案**

> 🎯 考察意图：区分"会用 API"和"能设计整套信号处理架构"的人。

---

**Q8: "你有一个多线程服务器程序，收到 `SIGTERM` 后怎么让所有线程优雅停下来？"**

🗣️ **背诵回答：**

> "多线程场景比单线程复杂不少，因为信号是发给**进程**的，任何一个线程都可能接收它。我一般分四步设计：
>
> 第一步，**用一个专门的线程统一收信号**。Linux 下 `pthread_sigmask` 可以在所有工作线程里屏蔽 `SIGTERM` 和 `SIGINT`，只留主线程或一个专门的 signal 线程不屏蔽。这样保证信号一定由这个线程处理，不会随机打断某个正持锁的工作线程。
>
> 第二步，**信号处理函数只设标志**——和单进程一样，`volatile sig_atomic_t` 或者更安全地用 `atomic<bool>`（C++ 的 `std::atomic<bool>`，有内存序保证）。
>
> 第三步，**通知所有工作线程退出**。设完标志后，通过工作线程正在等的东西去唤醒它们：如果工作线程阻塞在条件变量上，就 `notify_all`；如果在 `select`/`epoll` 上，就用 eventfd 或者 self-pipe 发事件让它们醒来；如果线程池在等任务队列，就往队列里塞一个"毒丸"任务让它们知道该退了。
>
> 第四步，**主线程 join 所有工作线程**，确认所有线程都安全返回后，再做全局清理——刷日志、关连接、释放共享内存。加一个超时机制：如果某个线程在规定时间内没退出，记一条告警日志然后强制 `pthread_cancel` 或者直接 `_exit`。
>
> 这套方案的核心就是：**信号是异步的，但你的退出流程必须是有序的**。不能指望信号一来所有线程立刻原地消失——每个线程必须在自己可控的检查点主动退出。"

---

**Q9: "说下你在上一家公司是怎么做信号处理的？遇到过什么坑？"**

🗣️ **背诵回答：**

> "上一家做的是智能硬件的网关程序，多进程架构——一个 watchdog 主进程加上几个采集子进程。信号处理分了几个层次：
>
> 主进程捕获 `SIGTERM` 做优雅关闭——标志位模式，主循环检查后先通知子进程退出、`waitpid` 收尸、再清理共享内存和 IPC 资源。`SIGCHLD` 用 `sigaction` + `SA_NOCLDSTOP` + `SA_RESTART` 注册，处理函数里 `while + waitpid + WNOHANG` 收尸，发现子进程异常退出还要触发告警和退避重启。`SIGPIPE` 直接忽略，因为子进程之间用管道通信，对端可能随时退出。`SIGSEGV` 挂了崩溃处理函数——打印堆栈后 `_exit`，配合外面的 watchdog 自动拉起。
>
> 踩过的坑有几个印象很深的：
>
> 一个是用 `signal` 设了 `SIGCHLD` 处理，某次部署到一个比较老的嵌入式 Linux 上，行为变成了 System V 那种处理一次就重置的，子进程退出大量变成僵尸，PID 耗尽后整个系统 fork 不出新进程。排查了半天才定位到 `signal` 的问题，后来全部改成 `sigaction` 了。
>
> 另一个是 `SIGPIPE` 的坑——最早没处理 `SIGPIPE`，网络模块往一个已关闭的 socket 里写数据，进程直接被内核干掉了，看日志完全没头绪，因为不是我们自己代码崩溃的。后来学会了一开始就 `signal(SIGPIPE, SIG_IGN)`，让 `write` 老老实实返回 -1 并设 `errno = EPIPE`，我们自己在代码里处理。
>
> 还有一个就是 `sa_mask` 没初始化——栈上的 `struct sigaction` 没调 `sigemptyset`，`sa_mask` 是随机垃圾值，导致处理函数执行期间一堆信号被莫名其妙屏蔽了，排查难度很大。后来养成习惯声明完立马 `memset` 或 `sigemptyset`。"

---

**Q10: "`signalfd` 和 self-pipe trick 各有什么优劣？"**

🗣️ **背诵回答：**

> "两个解决的是同一个问题——把异步信号转成同步的 fd 事件，融入 `select`/`epoll` 事件循环。但实现思路相反：
>
> **self-pipe** 是传统方案：信号处理函数里往自管道写 1 字节（`write` 是 async-signal-safe 的），主循环 `select`/`epoll` 监控管道读端，读到数据就知道信号来了。读出来的字节就是信号编号，能区分是哪个信号。
>
> **`signalfd`** 是 Linux 特有的系统调用（2.6.22 开始）：直接创建一个 fd，用 `sigprocmask` 屏蔽对应的信号，然后 `read` 这个 fd 就能拿到信号的详细信息——不止编号，还有发送者 PID、附带数据等。不需要信号处理函数、不需要管道、不需要处理 async-signal-safe 限制。
>
> 优劣很明确：**`signalfd` 更干净**——没有信号处理函数，没有 async-signal-safe 的限制，整个信号处理逻辑都在主流程里，可读性和可维护性好得多。但它只能用在 Linux 上，不是 POSIX 标准。如果你的代码要跨平台——比如要跑在 macOS 或 BSD 上——就只能用 self-pipe。
>
> 还有一个细节：**`signalfd` 必须配合 `sigprocmask` 屏蔽对应信号**。因为信号的默认动作还在，你不屏蔽的话，信号递达时该杀进程还是会杀——`signalfd` 只是给你一个 fd 来读信号，不改变信号的默认动作。这和 `sigaction` 注册处理函数不同，`sigaction` 注册后默认动作就被替换了。
>
> 我个人项目里优先用 `signalfd`（因为没有跨平台需求），面试时说这两个方案都了解、能根据场景选型就够了。"

### 本文阅读导航

| 你的目标 | 读这些章节 | 预计时间 |
|----------|-----------|---------|
| 面试前快速复习 | 导读 + 第八章（面试速记） + 第八章自测 | 5 分钟 |
| 搞清楚 signal vs sigaction 区别 | 第〇章（背景）+ 第一章（signal 缺陷） | 10 分钟 |
| 会用 sigaction 写代码 | 第二章（API）+ 第四章（实战模板） | 20 分钟 |
| 彻底理解内核行为 | 第三章（内核行为） + 第六章（async-signal-safe） | 15 分钟 |
| 全部掌握 | 从头到尾 + 做自测题 | 45 分钟 |

---

## 〇、背景：信号是什么，为什么处理它这么麻烦

### 程序的正常执行流 vs 异步事件

一个程序正常运行，CPU 按指令一条条往下走，偶尔跳转、函数调用、函数返回——一切尽在掌控。但真实世界里，程序**不能只活在自己的逻辑里**。外部随时有事情发生，需要立刻通知程序：

- 用户按了 **Ctrl+C**（→ `SIGINT`）
- 系统要关机了，给所有进程发"请你自己退出"（→ `SIGTERM`）
- 进程访问了非法内存地址（→ `SIGSEGV`）
- 子进程退出了，父进程应该知道（→ `SIGCHLD`）
- 定时器到期了（→ `SIGALRM`）
- 对端断开了连接，你还在往里写数据（→ `SIGPIPE`）
- 一个硬件中断触发了某些内核事件

这些事件的共同特点是 **异步** ——你不知道它们什么时候来，它们也不管你当前正在执行什么代码。就像你正在算一道数学题，突然有人拍你肩膀——你必须中断手头的事，先处理这个"通知"。

**信号（Signal）就是 Unix/Linux 对这种"异步通知"的统一抽象**——一种**软件中断**。

### 常见信号一览：每个编号代表什么

Linux 标准信号共 31 个（编号 1~31，即 `SIGRTMIN` 之前），每个有固定的语义和默认动作。下面按使用频率和重要程度排列：

#### 🔴 不能被捕获、不能被忽略、不能被阻塞——终极杀招

| 信号 | 编号 | 含义 | 默认动作 | 说明 |
|------|------|------|----------|------|
| **`SIGKILL`** | 9 | **无条件杀死进程** | 终止进程 | 连内核都不给进程**任何**反应机会——不会调用任何信号处理函数、不会跑 atexit 回调、不会做任何清理。进程直接被内核从调度队列里摘掉、地址空间回收。**`kill -9 <PID>` 的终极武器**。 |
| **`SIGSTOP`** | 19 | **无条件暂停进程** | 暂停进程 | 进程被挂起（状态 T），直到收到 `SIGCONT` 才能恢复运行。同样不能被捕获或忽略。调试器（gdb）就是靠 `SIGSTOP` + `SIGCONT` 控制进程的启停。 |

> **面试高频**：`SIGKILL`(9) 和 `SIGSTOP`(19) 是两个**不能被捕获、不能被忽略、不能被阻塞（block）** 的信号。原因很简单——如果它们能被捕获，一个有 bug 的程序就可以无视 kill 指令继续赖在系统里，系统管理员唯一的终极手段就失效了。这是有意为之的设计，不是疏忽。

#### 🟡 终止类信号——"请你退出"，但你可以优雅关闭

| 信号 | 编号 | 触发方式 | 默认动作 | 说明 |
|------|------|----------|----------|------|
| **`SIGTERM`** | 15 | `kill <PID>`（默认） | 终止进程 | **"礼貌地请你退出"**。可以被捕获，在信号处理函数里做优雅关闭：刷磁盘缓存、关闭网络连接、释放资源、通知子进程。所有守护进程都应该捕获它。`kill` 命令不带信号编号时发的就是这个。 |
| **`SIGINT`** | 2 | 终端 Ctrl+C | 终止进程 | 用户在终端按 Ctrl+C。绝大多数 CLI 程序应该捕获它，行为和 `SIGTERM` 类似。区别：`SIGINT` 是"用户从键盘打断"，`SIGTERM` 是"别人（或系统）让你退"。 |
| **`SIGQUIT`** | 3 | 终端 Ctrl+\ | 终止进程 + core dump | 比 `SIGINT` 更"暴力"——用户按 Ctrl+\，默认不但退出还要 dump core（把整个进程内存镜像写到磁盘），方便事后 gdb 分析。 |
| **`SIGHUP`** | 1 | 终端关闭 | 终止进程 | 历史上是"modem 挂断（hangup）了"。现在主要两个场景：① 终端窗口被关了，shell 给前台进程组发 `SIGHUP`；② **守护进程的 reload 信号**——很多 daemon 约定：收到 `SIGHUP` = 重新加载配置文件（Nginx: `nginx -s reload` → 实际是发 `SIGHUP`）。 |
| **`SIGPIPE`** | 13 | 向已关闭的管道/socket 写数据 | 终止进程 | **网络编程和管道编程头号坑。** 对端已经关了连接，你还在往里 `write`，内核给你发 `SIGPIPE`，默认动作是直接杀进程。如果不显式忽略或捕获它，一个对端断连就能让你的服务器进程崩掉。惯用法：`signal(SIGPIPE, SIG_IGN);`。 |

#### 🟠 硬件异常类信号——程序出 bug 了，OS 通知你"你干了非法操作"

| 信号 | 编号 | 触发原因 | 默认动作 | 说明 |
|------|------|----------|----------|------|
| **`SIGSEGV`** | 11 | 访问非法内存地址 | 终止进程 + core dump | **Segmentation Fault（段错误）**：空指针解引用、访问已释放的内存、往只读段（.rodata 或 .text）写数据、访问不属于你的地址空间。是 C/C++ 程序员最熟悉的崩溃信号。 |
| **`SIGBUS`** | 7 | 总线错误 | 终止进程 + core dump | 和 `SIGSEGV` 类似但不同：通常是**硬件层面的内存访问错误**——比如未对齐的内存访问（某些 CPU 架构上）、访问的物理地址不存在、mmap 的文件被截断了但还在访问被截掉的部分。 |
| **`SIGFPE`** | 8 | 算术异常 | 终止进程 + core dump | 名字有误导性——不光是浮点异常。整数除零、整数溢出（某些架构上）都会触发它。 |
| **`SIGILL`** | 4 | 非法指令 | 终止进程 + core dump | CPU 执行到了一条它不认识的机器指令。可能原因：代码段被破坏了、试图执行数据段的内容、编译器生成了当前 CPU 不支持的指令集。 |
| **`SIGABRT`** | 6 | `abort()` 调用 | 终止进程 + core dump | 程序自己调用 `abort()`（通常来自 `assert` 失败），主动要求崩溃。和上面四个的区别：这是**主动**的，不是硬件触发的。一般不捕获它——让 `abort` 正常生成 core dump 用于事后排查。 |

#### 🟢 通知类信号——内核告诉你"有事情发生了，你看着办"

| 信号 | 编号 | 触发时机 | 默认动作 | 说明 |
|------|------|----------|----------|------|
| **`SIGCHLD`** | 17 | 子进程状态变化 | **忽略** | 子进程退出、被 STOP、或被 CONTINUE 时，内核给父进程发这个信号。**默认动作是忽略**（注意不是终止），所以不主动注册处理函数不会有任何影响——但子进程会变僵尸。注册后配合 `waitpid` 异步收尸，是避免僵尸的标准做法（详见 [[02-进程管理与多进程编程]]）。 |
| **`SIGALRM`** | 14 | `alarm()` 或 `setitimer()` 到期 | 终止进程 | 定时器到期通知。`alarm(5)` = 5 秒后收到一个 `SIGALRM`。默认会杀进程，所以用的时候必须注册处理函数或忽略它。 |
| **`SIGUSR1`** | 10 | 用户自定义 | 终止进程 | **两个保留给用户自定义用途的信号之一**。没有固定语义，进程之间协商好即可。常用来做：通知另一个进程"有新数据了"、守护进程的日志轮转信号、自定义 IPC。 |
| **`SIGUSR2`** | 12 | 用户自定义 | 终止进程 | 同上，第二个自定义信号。 |
| **`SIGCONT`** | 18 | 继续被暂停的进程 | 继续进程 | 让被 `SIGSTOP`/`SIGTSTP` 暂停的进程恢复运行。**不能被阻塞**（可以被捕获，但很少这么做）。shell 的 `fg`/`bg` 命令就是发 `SIGCONT`。 |
| **`SIGTSTP`** | 20 | 终端 Ctrl+Z | 暂停进程 | 用户在终端按 Ctrl+Z，进程被挂起到后台。可以被捕获（vim 就捕获了它），和 `SIGSTOP` 的区别是**能被捕获**。 |

#### 按场景速记

```
想杀一个进程，不给它反抗机会       → SIGKILL(9) 或 SIGSTOP(19)
请进程自己优雅退出                  → SIGTERM(15)
用户 Ctrl+C 打断                    → SIGINT(2)
守护进程重新加载配置                → SIGHUP(1)
程序崩了（段错误、除零等）          → SIGSEGV(11)、SIGFPE(8)、SIGBUS(7)
子进程退出了                        → SIGCHLD(17)
网络/管道对端关闭了                  → SIGPIPE(13)
定时器                              → SIGALRM(14)
自定义 IPC                          → SIGUSR1(10)、SIGUSR2(12)
调试：暂停/恢复                     → SIGSTOP(19) / SIGCONT(18)
```

### 信号机制的本质矛盾

这就引出了信号处理的核心矛盾，也是为什么`sigaction` 这套 API 如此复杂的原因：

> **信号随时可能打断你的程序，但你又要安全地响应它，不能把正在做的事搞坏。**

举个具体的例子：假设你的程序正在执行 `malloc(100)`——`malloc` 内部需要操作一个全局空闲链表，它正**持有一把锁**，链表刚改了一半。就在这个瞬间，一个信号到了。内核暂停当前执行流，跳转到你的信号处理函数。

如果你的处理函数里又调了 `malloc`，它会**再次尝试获取同一把锁**——但锁已经被"刚才正在执行但还没执行完"的那个 `malloc` 持有了。死锁。进程挂死。

这就是为什么信号处理函数里只能调少数 **async-signal-safe（异步信号安全）** 的函数。

### 一段历史：从 `signal` 到 `sigaction`

Unix 最早提供的是 `signal()` 函数（1970 年代，Unix V1 就有了），它是一个极其简陋的 API：

```c
void (*signal(int sig, void (*handler)(int)))(int);
//                    ^^^^^^^^^^^^^^^^^^^^^^^^
//                    函数指针套函数指针，类型又臭又长
```

这个 API 自带几个根本性问题，几十年来一直是 bug 的温床：

1. 不同 Unix 变体对"处理一次后是否重置为默认"有**截然不同**的实现
2. 你控制不了"信号处理函数执行时，哪些信号应该被暂时挡住"
3. 你控制不了"被信号打断的慢系统调用（如 `read`）要不要自动重试"
4. 信号只能传一个编号，传不了任何附加数据（谁发的？为什么发？）

这些问题在生产环境一炸一个准——守护进程写 `signal` 处理信号，要么漏了竞态窗口导致进程被意外 kill，要么死锁，要么在 System V 和 BSD 之间移植时行为完全不一样。

**POSIX.1 标准（1988 年起）引入了 `sigaction()`，一次性解决了上面所有问题。** 它给了你三个维度的精细控制：① `sa_mask`（处理函数期间屏蔽哪些信号）、② `sa_flags`（一组行为控制标志位）、③ 可选的 `sa_sigaction`（能拿到发送者 PID 和附带数据的三种参数版处理函数）。系统调用 `sigaction` 本质上就是为了"安全且精确地管理异步通知"而生的一套现代化的信号注册函数接口。

**所以，本文的逻辑是**：先讲清 `signal` 哪里不行 → 再讲 `sigaction` 怎么解决 → 然后深入内核行为 → 最后给出工业级代码模板。读完你应该能回答：为什么所有严肃的 C/C++ 项目（Nginx、Redis、PostgreSQL）的信号处理代码全部用 `sigaction`。

---

## 一、`signal` 的三个致命缺陷——为什么必须换掉它

`signal()` 是 ANSI C 标准（C89）库函数，历史比 POSIX 早得多。它的行为在不同 Unix 实现之间**根本不一致**，写出跨平台可靠的信号处理代码基本不可能。具体问题有三个：

### 缺陷一：处理一次后是否重置回默认动作？（System V vs BSD）

这是最著名的分歧：

- **System V（传统 Unix）**：信号处理函数被调用**一次**后就自动重置为 `SIG_DFL`（默认动作）。如果你想持续处理，必须在处理函数里**再次调用 `signal` 重新注册自己**。
- **BSD（及 Linux）**：信号处理函数**不会被重置**，注册一次持续有效。

```c
// System V 语义下必须这样写（有竞态窗口！）：
void handler(int sig) {
    signal(sig, handler);   // ⚠️ 立即重新注册——但中间有一瞬间还是默认动作
    // ... 真正的处理逻辑
}
```

> 这个竞态窗口：`handler` 被调用 → 自动重置为 `SIG_DFL` → 你重新 `signal` → 如果在这**极短间隙**里同样的信号又到了，进程按默认动作执行（比如 `SIGINT` 默认是终止进程），直接炸。

### 缺陷二：不能精细控制"处理函数执行期间屏蔽哪些信号"

`signal()` 没有 `sa_mask` 的概念。处理函数执行时，**至少当前信号本身会被自动屏蔽**（这是 POSIX 保证的），但你**不能指定额外屏蔽哪些信号**。比如处理 `SIGTERM` 做优雅退出时，你无法阻止 `SIGINT` 打断它——这在需要原子性操作时很麻烦。

### 缺陷三：不能控制"被信号打断的系统调用是否自动重启"

一个正在 `read()` 阻塞的进程收到信号，`read` 会返回 `-1` 并设 `errno = EINTR`（被中断）。你需要在代码里手动处理这个情况：

```c
// signal() 下必须手动重试：
int n;
do {
    n = read(fd, buf, size);
} while (n == -1 && errno == EINTR);  // 烦人
```

`sigaction` 用 `SA_RESTART` 一个标志位就能让内核帮你自动重启。

### 底线

> **所有新代码用 `sigaction`，`signal` 只配出现在面试题里讨论它与 `sigaction` 的区别。**

---

## 二、API 与核心数据结构

### 2.1 函数签名

```c
#include <signal.h>

int sigaction(int signum,
              const struct sigaction *act,    // 新的处理设置（可为 NULL）
              struct sigaction *oldact);      // 保存旧的设置（可为 NULL）

// 返回值：成功 0，失败 -1 并设 errno
```

- `signum`：要操作的信号编号（`SIGINT`、`SIGTERM`、`SIGCHLD` 等），**不能是 `SIGKILL` 或 `SIGSTOP`**——这两个信号根本不能被捕获或忽略。
- `act`：传入新的处理配置；传 `NULL` 表示"我只想查询当前配置"。
- `oldact`：传出调用前的旧配置；传 `NULL` 表示我不关心旧的。

### 2.2 `struct sigaction` 字段逐一拆解

```c
struct sigaction {
    void     (*sa_handler)(int);        // 方式一：简单处理函数
    void     (*sa_sigaction)(int, siginfo_t *, void *);  // 方式二：带详细信息的处理函数
    sigset_t  sa_mask;                  // 处理函数执行期间额外屏蔽的信号集
    int       sa_flags;                 // 行为控制标志位
    void     (*sa_restorer)(void);      // ⚠️ 已废弃，不要碰
};
```

#### ① `sa_handler` — 信号处理函数（简单版）

```c
void handler(int sig) {
    // sig 是触发本次调用的信号编号
}

// 三个特殊值：
// SIG_DFL  — 恢复该信号的默认动作
// SIG_IGN  — 忽略该信号
// 自定义函数指针 — 你的处理函数
```

函数签名只拿到一个参数：**信号编号**。如果同一个 handler 注册给了多个信号（比如 `SIGTERM` 和 `SIGINT` 用同一个退出处理），可以通过 `sig` 区分是谁触发的。

#### ② `sa_sigaction` — 信号处理函数（详细信息版）

```c
void handler_detail(int sig, siginfo_t *info, void *ucontext) {
    // sig:       信号编号（同上）
    // info:      信号的详细信息（谁发的、为什么发、附加数据）
    // ucontext:  信号送达时被打断的上下文（寄存器值等，很少直接用）
}
```

**启用条件**：必须在 `sa_flags` 中设置 **`SA_SIGINFO`**。设了这个标志后，`sa_sigaction` 字段生效，`sa_handler` 被忽略。

> ⚠️ `sa_handler` 和 `sa_sigaction` 在标准里是一个 union，但多数实现中它们是两个独立字段，编译器会自动处理。你只需要关注：不设 `SA_SIGINFO` → 填 `sa_handler`；设了 `SA_SIGINFO` → 填 `sa_sigaction`。

`struct siginfo_t` 的关键字段：

| 字段 | 含义 |
|------|------|
| `si_signo` | 信号编号（跟 `sig` 参数一样） |
| `si_code` | 信号来源：`SI_USER`（`kill` 发来的）、`SI_KERNEL`（内核生成的）、`SI_QUEUE`（`sigqueue` 队列发来的）等 |
| `si_pid` | 发送者的 PID（仅当来自其他进程时有效） |
| `si_uid` | 发送者的 UID |
| `si_value` | 附带数据（仅 `sigqueue` 发送时有效，见第五节） |
| `si_addr` | 触发段错误的那个非法地址（仅 `SIGSEGV` / `SIGBUS` 等硬件异常信号） |

```c
// 示例：SIGSEGV 处理函数里打印非法地址
void segv_handler(int sig, siginfo_t *info, void *ctx) {
    write(STDERR_FILENO, "SIGSEGV! bad addr: ", 19);
    // 把地址打印出来...（省略格式化，因为 printf 不安全）
    _exit(1);
}
```

#### ③ `sa_mask` — 处理函数执行期间的信号屏蔽字

这可能是 `sigaction` **最容易被忽视但最重要的字段**。

**在信号处理函数被调用的整个过程中，`sa_mask` 中指定的信号会被自动加入进程的信号屏蔽字（blocked set）；处理函数返回后，内核自动恢复原来的屏蔽字。**

注意：**触发本次调用的信号本身也会被自动屏蔽**（这是 POSIX 保证的，不需要你手动加到 `sa_mask` 里），除非你设了 `SA_NODEFER`。

```c
// 场景：SIGTERM 做优雅退出，期间不想被 SIGINT 打断
struct sigaction sa;
sa.sa_handler = graceful_shutdown;
sigemptyset(&sa.sa_mask);
sigaddset(&sa.sa_mask, SIGINT);    // 处理 SIGTERM 期间屏蔽 SIGINT
sigaddset(&sa.sa_mask, SIGHUP);    // 也屏蔽 SIGHUP
sa.sa_flags = 0;
sigaction(SIGTERM, &sa, NULL);
```

**操作 `sigset_t` 的工具函数：**

```c
#include <signal.h>

sigset_t set;

sigemptyset(&set);           // 全部清空（初始化必做这一步）
sigfillset(&set);            // 全部置 1（屏蔽所有信号）
sigaddset(&set, SIGINT);     // 加入某个信号
sigdelset(&set, SIGTERM);    // 移除某个信号
sigismember(&set, SIGCHLD);  // 检查某个信号是否在集合中（返回 1 = 在，0 = 不在）
```

#### ④ `sa_flags` — 行为控制标志位（逐位讲解）

`sa_flags` 是零个或多个标志的**按位或**（`|`）。下面是工程中最常用的几个：

| 标志位 | 作用 | 什么时候用 |
|--------|------|-----------|
| `SA_RESTART` | 被此信号打断的**慢系统调用**（`read`/`write`/`accept`/`wait` 等）**自动重启**，不用你手动处理 `EINTR` | 几乎所有场景都该设，除非你故意要中断阻塞调用 |
| `SA_SIGINFO` | 使用 `sa_sigaction`（三参数版本）而非 `sa_handler`（单参数版本） | 需要知道信号附带数据、发送者 PID 时——比如 `sigqueue` 发信号带附加值 |
| `SA_NOCLDSTOP` | **仅对 `SIGCHLD`**：子进程被 STOP（暂停）时**不**发送 `SIGCHLD`。子进程退出时照常发 | 注册 `SIGCHLD` 处理时**几乎必设**——你通常只关心子进程退出，不关心它被 `SIGSTOP` 暂停 |

> **🤔 为什么 `SA_NOCLDSTOP` 几乎必设？不设会怎样？**
>
> 这牵扯到 `SIGCHLD` 的触发时机。很多人以为 `SIGCHLD` 只在"子进程退出"时才发，**其实不是**。
>
> `SIGCHLD` 在以下三种情况都会发送：
> 1. 子进程**退出**（正常 `exit` 或被信号杀死）← 你关心的
> 2. 子进程被**暂停**（收到 `SIGSTOP`/`SIGTSTP`/`SIGTTIN`/`SIGTTOU`）← 你通常不关心的
> 3. 子进程被**恢复执行**（收到 `SIGCONT`）← 你通常也不关心的
>
> **场景还原——不设 `SA_NOCLDSTOP` 出 bug 的过程：**
>
> 你注册了 `SIGCHLD` 处理函数，里面写了 `while (waitpid(-1, &status, WNOHANG) > 0)` 循环收尸。这个逻辑假设"收到 SIGCHLD = 有子进程死掉了需要收尸"。某天运维用 gdb attach 了一个子进程排查问题——gdb 给子进程发 `SIGSTOP` 让它暂停，内核立刻给你发 `SIGCHLD`。你的处理函数跑起来，`waitpid` 加 `WNOHANG` 去收尸——但子进程根本没死，只是被暂停了。`waitpid` 配上 `WNOHANG` 发现"没有已退出的子进程"，返回 0。暂时不出事。
>
> 但如果你的 `waitpid` 用了 `WUNTRACED` 标志（为了知道子进程是否被暂停），那情况更微妙——`waitpid` 会返回子进程的 PID 并报告"这个进程被 STOP 了"。你的收尸代码可能会错误地认为这个子进程已经退出了，记录错误日志、触发重启逻辑、甚至删掉对这个子进程的追踪——但人家根本没死。
>
> **设了 `SA_NOCLDSTOP` 之后**：只有子进程真正退出时，内核才发 `SIGCHLD`。暂停和恢复继续事件统统不触发。你的处理函数里 `while (waitpid(-1, &status, WNOHANG) > 0)` 拿到的每一个子进程都是"真死了需要收尸"的，没有假警报。
>
> 所以那句话「子进程被 STOP 时不触发 SIGCHLD」翻译成人话就是：**"我只在子进程真死的时候才通知我，它被调试器暂停了别来烦我。"** 这就是为什么注册 `SIGCHLD` 时，`SA_NOCLDSTOP` 几乎必设。

| `SA_NOCLDWAIT` | **仅对 `SIGCHLD`**：子进程退出后**自动回收，不留僵尸**，等价于 `SIG_IGN` 的效果 | 不关心子进程退出码、纯粹 fire-and-forget 的场景（详见 [[02-进程管理与多进程编程]] 的三种避免僵尸方法） |
| `SA_NODEFER` / `SA_NOMASK` | 处理函数执行期间**不**自动屏蔽触发本次调用的信号（默认行为是自动屏蔽的） | 极少用——除非你想允许同一信号嵌套：处理函数正跑着，又来一个同样的信号，再进一次处理函数 |
| `SA_RESETHAND` / `SA_ONESHOT` | 处理函数被调用一次后，自动恢复为 `SIG_DFL`（模拟 System V 的 `signal` 行为） | 极少用，除非你有特殊的一次性处理需求 |

**不常用但需了解的标志：**

| 标志位 | 作用 |
|--------|------|
| `SA_SIGINFO` | 见上 |
| `SA_ONSTACK` | 在 `sigaltstack()` 指定的独立栈上执行处理函数。栈溢出场景下 `SIGSEGV` 处理函数无法用已经溢出的栈，必须用独立栈 |

**常见组合（直接抄）：**

```c
// 组合 1：普通信号处理（最常用）
sa.sa_flags = SA_RESTART;

// 组合 2：SIGCHLD 收尸专用
sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

// 组合 3：需要信号附带数据的（sigqueue）
sa.sa_flags = SA_RESTART | SA_SIGINFO;

// 组合 4：不关心子进程退出码，内核自动收尸
sa.sa_flags = SA_NOCLDWAIT;
sa.sa_handler = SIG_DFL;   // 注意：设 SA_NOCLDWAIT 时 handler 必须为 SIG_DFL
```

---

## 三、内核行为：信号递达的全流程

理解信号从产生到处理函数执行完成的完整路径，才能真正用好 `sigaction`。

### 3.1 信号的生命周期三阶段

```
  产生（Generation）──▶ 未决（Pending）──▶ 递达（Delivery）
```

- **产生**：某个事件触发了信号——硬件异常（除零、段错误）、另一个进程 `kill`、内核通知（`SIGCHLD`）、定时器到期（`SIGALRM`）等。
- **未决（Pending）**：信号已产生但还没被递达。内核在每个进程的 `task_struct` 里维护一个**未决信号位图**（`pending` bitmap）——每位对应一个信号，置 1 表示"这个信号在排队"。注意**常规信号是不排队的**——在未决期间再来一个同样的信号，它还是那一位上的 1，不会变成 2，这就是"信号合并"的根因。
- **递达（Delivery）**：内核在合适的时机（从内核态返回用户态时），检查未决位图，挑一个未被屏蔽的未决信号，调用对应的处理函数。

### 3.2 屏蔽字（Blocked Set）决定信号能不能递达

每个进程维护一个**信号屏蔽字**（blocked signal mask），本质是个 `sigset_t`。**如果某个信号在屏蔽字中，它就是"产生了、在未决位图上挂着，但暂时不递达"——一直挂着直到被解除屏蔽。**

和 `sa_mask` 的关系：
- **常态屏蔽字**：进程平时的屏蔽字（通过 `sigprocmask()` 设置）。
- **处理函数执行期间的屏蔽字** = 常态屏蔽字 | `sa_mask` | 触发信号本身。

当处理函数返回后，内核自动把屏蔽字恢复到进入处理函数前的值。

```text
时间线示例（SIGTERM 处理函数，sa_mask 含 SIGINT）：

1. 主循环：屏蔽字 = 空                — 所有信号都可递达
2. SIGTERM 递达，进入 handler：        — 屏蔽字自动 = {SIGTERM, SIGINT}
3. 此时 SIGINT 来了 → 挂 pending      — 递达不了，因为被屏蔽
4. handler 返回                        — 屏蔽字恢复为空
5. 内核检查 pending → SIGINT 还在      — 递达 SIGINT
```

### 3.3 信号处理函数的执行上下文

这是面试高频题。信号处理函数：

- **不在主线程的某个调用栈帧里**，而是内核在用户态栈上临时压了一个新帧
- **异步于主流程**，所以需要考虑并发问题——它可能打断任何位置的代码
- **信号处理函数内部不能安全调用大部分库函数**（见第六节 async-signal-safe）
- **不会像线程那样有独立的调度**——同一个线程被信号暂停、处理函数跑完、恢复原执行流

---

## 四、实战代码模式

### 4.1 基础模板：注册一个信号处理函数

```c
// ===== 必需的三个头文件 =====
#include <signal.h>   // sigaction()、struct sigaction、SIGTERM 等信号宏
#include <stdio.h>    // （本例未直接使用，生产代码通常需要 perror 等）
#include <stdlib.h>   // _exit()

// ===== 信号处理函数 =====
// 参数 sig：触发本次调用的信号编号（比如收到 SIGTERM 时 sig == SIGTERM == 15）
//           如果同一个函数注册给多个信号，可以通过 sig 区分是谁触发的
static void on_terminate(int sig) {
    // ★ 这里只演示最简单的做法；实际生产代码只设 volatile sig_atomic_t 标志（见 4.2）
    // 定义一个栈上的字符串常量，内容在 .rodata 段，不需要 malloc
    const char msg[] = "Received terminate signal, shutting down...\n";

    // write() 是 async-signal-safe 的——能在信号处理函数里安全调用
    // STDERR_FILENO = 2（标准错误的文件描述符）
    // sizeof(msg) - 1：去掉末尾的 '\0'，只写可见字符
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    // _exit(0) 直接陷入内核终止进程，不跑 atexit 回调、不刷 stdio 缓冲
    // ⚠️ 绝不能用 exit(0)——exit 会刷 stdio 缓冲和执行 atexit 回调，不安全
    _exit(0);
}

int main(void) {
    // ===== 第 1 步：声明一个 struct sigaction 并清空（C 自动变量的初始值是垃圾值）=====
    struct sigaction sa;

    // ===== 第 2 步：指定处理函数 =====
    // sa_handler 是函数指针，指向收到信号时要调用的函数
    // 也可以设成 SIG_DFL（恢复默认动作）或 SIG_IGN（忽略该信号）
    sa.sa_handler = on_terminate;

    // ===== 第 3 步：初始化 sa_mask——处理函数执行期间额外屏蔽哪些信号 =====
    // sigemptyset() 把 sa_mask 的所有位清 0（"空集"）
    // ★ 这一步绝对不能省！栈上的 sa.sa_mask 是随机垃圾值，不初始化会导致随机信号被屏蔽
    sigemptyset(&sa.sa_mask);

    // ===== 第 4 步：设置行为控制标志 =====
    // SA_RESTART：被这个信号打断的慢系统调用（read/write/accept 等）自动重启
    //             而不是返回 -1 并设 errno = EINTR——省去手动重试的麻烦
    sa.sa_flags = SA_RESTART;

    // ===== 第 5 步：调用 sigaction 注册 =====
    // 参数 1: SIGTERM——我们要捕获的信号（kill 命令默认发的就是这个）
    // 参数 2: &sa——新的处理配置
    // 参数 3: NULL——不关心旧的配置是什么（如果传一个 struct sigaction*，会把旧配置写进去）
    // 返回值：成功 0，失败 -1 并设 errno
    sigaction(SIGTERM, &sa, NULL);

    // ===== 第 6 步：同一个处理函数也注册给 SIGINT（Ctrl+C）=====
    // sa 的内容没变，直接复用即可
    sigaction(SIGINT, &sa, NULL);

    // ===== 主循环：正常业务逻辑 =====
    // 这里用死循环代替；实际代码可能是事件循环、帧循环、或者阻塞在 select/epoll 上
    while (1) {
        // 正常业务：处理一帧、收一条消息、等待事件...
        // 一旦收到 SIGTERM 或 SIGINT，程序跳到 on_terminate，然后 _exit 退出
    }

    return 0;  // 实际上走不到这里，因为 on_terminate 里 _exit 了
}
```

### 4.2 生产级模板：只设标志、主循环处理

信号处理函数只能调 async-signal-safe 函数（见第六节）。要做**优雅退出**（释放资源、刷盘、关连接），必须把信号转成同步事件——信号处理函数里只写一个标志，主循环检查：

```c
// ===== 必需的三个头文件 =====
#include <signal.h>   // sigaction()、struct sigaction、信号宏（SIGTERM、SIGINT）
#include <unistd.h>   // （本例未直接使用，但通常需要 pause()/sleep() 等）
#include <stdio.h>    // printf()——注意只在主循环用，信号处理函数里不用

// ===== 全局标志位——信号处理函数和主循环的"通信变量" =====
// volatile：告诉编译器"每次用这个变量时必须从内存重新读，别缓存到寄存器"
//          因为信号处理函数是异步执行的，编译器分析主流程时"看不到"它的写操作
// sig_atomic_t：C 标准保证该类型读写是一条 CPU 指令完成，不会被信号打断
//               （普通 int 在 32 位平台上读写 64 位值时可能需要两条指令）
// static：限制作用域在本文件内，避免污染全局符号表
// 初始值 0：程序启动时还没有收到退出信号
static volatile sig_atomic_t should_stop = 0;

// ===== 信号处理函数——极其精简，只做一件事：把标志从 0 翻成 1 =====
// 参数 sig：触发本函数的信号编号，这里不关心具体是哪个（都用同一个逻辑），所以 (void)sig 显式忽略
static void on_term(int sig) {
    // (void)sig：消除编译器"未使用参数"的警告
    //            因为 SIGTERM 和 SIGINT 都注册到同一个函数，不需要区分
    (void)sig;

    // ★ 唯一能安全做的事：给 volatile sig_atomic_t 变量赋值
    //    这条赋值通常编译成一条 mov 指令，不会被信号中断
    should_stop = 1;
}

int main(void) {
    // ===== 第 1 步：声明 struct sigaction =====
    struct sigaction sa;

    // ===== 第 2 步：指定处理函数 =====
    // on_term 只负责把 should_stop 置 1，真正的清理逻辑在主循环之后
    sa.sa_handler = on_term;

    // ===== 第 3 步：清空 sa_mask（必须做！栈上的初始值是垃圾）=====
    // 这里不需要额外屏蔽其他信号，因为 on_term 只做一次赋值就返回了，不会跟其他信号冲突
    sigemptyset(&sa.sa_mask);

    // ===== 第 4 步：设 SA_RESTART——系统调用被信号打断后自动重启 =====
    // 假设主循环里正在阻塞 read() 等数据，SIGTERM 来了：
    //   有 SA_RESTART → read 自动重试，然后主循环检查 should_stop 发现为 1，退出循环
    //   没有 SA_RESTART → read 返回 -1，errno = EINTR，需要手动检查并重试
    sa.sa_flags = SA_RESTART;

    // ===== 第 5 步：注册给 SIGTERM 和 SIGINT =====
    // SIGTERM(15)：kill 命令默认发送的信号，systemd/监控脚本通常用这个停止进程
    // SIGINT(2)：用户在终端按 Ctrl+C 时发送的信号
    // 两个信号用同一个处理函数和配置——都是"请优雅退出"
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // ===== 第 6 步：主循环——正常干活，每轮检查标志 =====
    // while (!should_stop)：只要标志还是 0，就继续工作
    //                      收到信号后 on_term 把标志置 1，下一次条件检查时退出循环
    while (!should_stop) {
        // === 正常的业务逻辑 ===
        // 比如：处理一帧视频、收一条网络消息、等一个 epoll 事件...
        // 每次循环迭代都在开头（或结尾）检查 should_stop，信号来了下一轮就退出
        //
        // 如果主循环阻塞在 select/epoll 上，两种做法：
        //   方案 A（依赖 SA_RESTART）：select 被信号打断后自动重试，然后在 while 条件处退出
        //   方案 B（self-pipe）：把信号管道 fd 也加入 select，收到事件直接处理（见 4.3）
    }

    // ===== 第 7 步：跳出循环 = 收到了退出信号，安全地做清理 =====
    // ★ 现在已经在主流程里了，不再受 async-signal-safe 的限制
    //    可以安全地调用 printf、malloc、free、fclose 等任何函数

    printf("Shutting down gracefully...\n");  // 现在可以安全用 printf 了——不在信号处理函数里

    // 在这里做所有需要的清理工作：
    // cleanup_resources();    // 释放 malloc 的内存、关闭网络连接
    // flush_logs();           // 刷日志到磁盘
    // notify_watchdog();      // 通知外部监控"我是正常退出，别报警"
    // join_threads();         // 等待其他工作线程结束

    return 0;  // 正常退出，会跑 atexit 回调、刷 stdio 缓冲
}
```

> **`volatile sig_atomic_t` 为什么是这个类型？**
>
> `sig_atomic_t` 是 C 标准规定的"读写保证不被信号打断的整数类型"——CPU 读写它只需一条指令（对 `int` 在多数平台也是，但标准不保证）。`volatile` 告诉编译器**每次用这个变量时从内存重新读**，别优化到寄存器里——因为信号处理函数的写操作编译器"看不到"（不在主流程里），不加 `volatile` 编译器可能把主循环里的 `should_stop` 优化成只在循环开始读一次，后面的检查永远看到的是旧值。

### 4.3 self-pipe trick：信号处理函数里写一字节，主循环 epoll/select 读

比标志位更强大的是 **self-pipe 模式**：信号处理函数里往自管道写 1 字节，主循环用 `select`/`epoll`/`poll` 监控管道读端，收到可读事件就处理。这样信号完美融入事件循环，不需要轮询。

```c
#include <signal.h>
#include <unistd.h>
#include <errno.h>

static int signal_pipe[2];   // [0]=读端, [1]=写端

// 信号处理函数：往管道写 1 字节（write 是 async-signal-safe）
static void on_signal(int sig) {
    int saved_errno = errno;
    unsigned char byte = (unsigned char)sig;   // 把信号编号写进去，主循环区分
    write(signal_pipe[1], &byte, 1);
    errno = saved_errno;
}

int setup_signal_pipe(void) {
    if (pipe(signal_pipe) == -1) return -1;

    struct sigaction sa;
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);    // 子进程退出也走同一个管道

    return signal_pipe[0];             // 返回读端 fd 给主循环 select/epoll
}

// 主循环：
// int signal_fd = setup_signal_pipe();
// 把 signal_fd 加入 epoll 或 select，读到数据就知道哪个信号来了
```

> self-pipe 比标志位的优势：① 不仅知道"有信号来了"，还能知道**哪个信号**来了（读出的字节就是信号编号）；② 多个信号排队不会合并（管道里有 N 个字节），而标志位只有一个 bit；③ 主循环不需要轮询，阻塞在 `select`/`epoll` 上等事件即可。

> **Linux 专属替代方案**：`signalfd()` 系统调用可以直接创建一个 fd，把信号事件转成 fd 可读事件，省去自管道的麻烦。但它不是 POSIX 标准，仅 Linux 支持。跨平台还是 self-pipe 稳。

### 4.4 SIGSEGV 崩溃捕获：打印堆栈后退出

```c
#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>    // backtrace / backtrace_symbols

static void crash_handler(int sig, siginfo_t *info, void *ctx) {
    // 只做最基本的最安全操作
    const char msg[] = "\n!!! CRASH: signal ";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    char sig_str[4];
    int n = (sig >= 100 ? 3 : (sig >= 10 ? 2 : 1));
    for (int i = n - 1; i >= 0; i--) { sig_str[i] = '0' + (sig % 10); sig /= 10; }
    write(STDERR_FILENO, sig_str, n);
    write(STDERR_FILENO, "\n", 1);

    // backtrace_symbols_fd 是 async-signal-safe 的
    void *frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);

    _exit(128 + sig);   // 模仿 shell 的退出码惯例
}

void install_crash_handler(void) {
    struct sigaction sa;
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;   // SA_SIGINFO 启用三参数版
    // 捕获常见的崩溃信号
    sigaction(SIGSEGV, &sa, NULL);  // 段错误
    sigaction(SIGBUS, &sa, NULL);   // 总线错误（未对齐访问等）
    sigaction(SIGFPE, &sa, NULL);   // 浮点异常（除零等）
    sigaction(SIGILL, &sa, NULL);   // 非法指令
    // ⚠️ 不要捕获 SIGABRT——让 abort() 正常生成 core dump
}
```

> ⚠️ 崩溃处理函数能做的最有用的事：① 打印堆栈；② 刷关键日志；③ 通知 watchdog；④ `_exit`。**不要在这类处理函数里尝试 `malloc`、`printf`、或任何复杂清理**——你正处在 crash 状态，堆可能已经坏了。

### 4.5 SIGCHLD 完整示例（参考 [[02-进程管理与多进程编程]] 第 6 节）

此文档不再重复，核心模式：
- `sa_flags = SA_RESTART | SA_NOCLDSTOP`
- 处理函数内 `while (waitpid(-1, NULL, WNOHANG) > 0);`

### 4.6 `sigqueue` + `SA_SIGINFO`：带数据的信号

`kill` 只能发信号编号，传不了数据。`sigqueue` 可以附带一个 union：

```c
// 发送方：
#include <signal.h>

union sigval value;
value.sival_int = 42;               // 带一个整数
// 或 value.sival_ptr = some_ptr;   // 带一个指针（仅同进程内有意义）

sigqueue(target_pid, SIGUSR1, value);

// 接收方：
void handler(int sig, siginfo_t *info, void *ctx) {
    if (info->si_code == SI_QUEUE) {    // 确认是 sigqueue 发来的
        int data = info->si_value.sival_int;   // 拿到 42
    }
}
// 注册时必须设 SA_SIGINFO
```

---

## 五、`signal` vs `sigaction` 对比速查

| | `signal()` | `sigaction()` |
|---|---|---|
| **标准** | ANSI C（C89） | POSIX.1 |
| **可移植性** | 差——System V vs BSD 行为不同 | 好——语义明确，POSIX 保证 |
| **处理一次后是否重置** | System V: 是；BSD/Linux: 否（不可预测） | 否，除非显式设 `SA_RESETHAND` |
| **屏蔽其他信号** | 只能自动屏蔽触发信号自身 | `sa_mask` 可指定任意信号集 |
| **被打断的系统调用** | 不可控；必须手动处理 `EINTR` | `SA_RESTART` 标志一键自动重启 |
| **信号附带数据** | 不支持 | `SA_SIGINFO` + `sa_sigaction` 拿到 `siginfo_t` |
| **保存旧配置** | 返回值是旧的 handler（但类型不安全） | `oldact` 参数，完整保存 |
| **子进程 STOP 发 SIGCHLD** | 会发 | `SA_NOCLDSTOP` 可抑制 |
| **自动回收子进程** | `signal(SIGCHLD, SIG_IGN)` 行为不统一 | `SA_NOCLDWAIT` 语义明确 |

**面试一句话**："`signal` 老、`sigaction` 新，工程上永远用 `sigaction`。`signal` 最大的坑是 System V 和 BSD 在处理一次后是否重置行为不一致，而且控制不了 `sa_mask` 和 `SA_RESTART`。"

---

## 六、async-signal-safe（异步信号安全）函数清单

信号处理函数**只能安全调用** POSIX 明确列出的 async-signal-safe 函数。因为信号可能在任何时刻打断主流程——包括**正在 `malloc` 的中间**（持有内部锁）——如果处理函数里又调 `malloc`，死锁板上钉钉。

### ✅ 安全（可以在信号处理函数里调）

| 类别 | 函数 |
|------|------|
| **退出** | `_exit()`, `_Exit()` |
| **写文件** | `write()` |
| **读文件** | `read()` |
| **打开文件** | `open()`, `close()` |
| **信号管理** | `signal()`, `sigaction()`, `sigprocmask()`, `sigdelset()`, `sigemptyset()` |
| **进程管理** | `fork()`, `waitpid()`, `getpid()`, `getppid()`, `kill()` |
| **时间** | `time()` |
| **文件描述符** | `dup()`, `dup2()`, `fcntl()`（部分操作） |
| **管道** | `pipe()` |
| **socket** | `socketpair()` |
| **目录** | `chdir()`, `unlink()`, `rename()` |
| **错误** | 保存/恢复 `errno` |

### ❌ 不安全（严禁在信号处理函数里调）

| 函数 | 为什么不安全 |
|------|-------------|
| `printf` / `fprintf` / `sprintf` | stdio 内部有锁和缓冲区，可能死锁 |
| `malloc` / `free` / `calloc` / `realloc` | 堆分配器内部有锁 |
| `new` / `delete`（C++） | 底层调用 `malloc` / `free` |
| `pthread_mutex_lock` | 信号打断了持有锁的线程 → 无法释放 → 死锁 |
| `exit()` | 会跑 atexit 回调 → 回调可能调不安全函数 |
| `getaddrinfo()` | 内部调用复杂库函数 |
| `syslog()` | 部分实现内部有锁 |

> 记忆口诀：**实在拿不准就只调 `write` 和 `_exit`，其他一律不碰。**

---

## 七、常见坑

| 坑 | 说明 |
|----|------|
| **用 `signal` 不用 `sigaction`** | 新代码没有理由用 `signal` |
| **`sigemptyset(&sa.sa_mask)` 忘了** | `sa_mask` 初始值是栈上的垃圾值，不初始化会导致随机信号被屏蔽 |
| **`sa_flags` 没设 `SA_RESTART`** | `read`/`write`/`accept` 等系统调用被信号打断返回 `EINTR`，没处理导致丢数据 |
| **SIGCHLD 没设 `SA_NOCLDSTOP`** | 子进程被调试器 STOP 也触发 SIGCHLD，干扰正常收尸逻辑 |
| **处理函数里调了 `printf`** | 绝大多数情况下"凑巧能跑"，但某个时刻一定会死锁 |
| **信号处理函数设了标志，但标志没加 `volatile`** | 编译器优化后主循环可能永远看不到标志被改 |
| **`while (waitpid(...) > 0)` 忘了 `WNOHANG`** | 没有 `WNOHANG`，`waitpid` 会阻塞——而你现在在信号处理函数里，整个进程等那个永远退不出的子进程，进程卡死 |
| **`sa_handler` 和 `sa_sigaction` 同时设** | 取决于 `SA_SIGINFO` 标志决定用哪个；同时设又没有 `SA_SIGINFO` 时用 `sa_handler`，反之用 `sa_sigaction`——不要依赖这个行为 |

---

## 八、面试速记

| 问题 | 一句话答案 |
|------|-----------|
| `signal` vs `sigaction` 区别？ | `signal` 老，行为跨平台不一致；`sigaction` 新，控制 `sa_mask` 和 `sa_flags`，语义明确 |
| `SA_RESTART` 干嘛的？ | 被信号打断的系统调用自动重启，不用手动处理 `EINTR` |
| `SA_NOCLDSTOP` 干嘛的？ | 子进程 STOP 时不发 `SIGCHLD`，只关心退出 |
| `SA_SIGINFO` 干嘛的？ | 启用三参数处理函数，拿到发送者 PID 和附带数据 |
| `sa_mask` 什么用？ | 处理函数执行期间**额外**屏蔽的信号 |
| 信号处理函数里能调 `printf` 吗？ | 绝对不能——非 async-signal-safe，会死锁 |
| 信号处理函数里能干嘛？ | 设 `volatile sig_atomic_t` 标志；`write` 一字节到管道；`waitpid` + `WNOHANG` 收尸；`_exit` |
| 为什么信号不排队？ | 常规信号用位图（per-bit），不是队列——pending 期间再来同一信号还是 1，不是 2 |

---

## 九、自测

1. 为什么工程代码应该用 `sigaction` 而不是 `signal`？至少说出两个理由。
2. `SA_RESTART` 标志的作用是什么？不设的话会有什么后果？
3. `sa_mask` 和进程的常态信号屏蔽字是什么关系？处理函数退出后会发生什么？
4. 信号处理函数为什么不能调用 `printf` 和 `malloc`？
5. 为什么 `sig_atomic_t` 要加强 `volatile`？
6. `sigqueue` 和 `kill` 发信号有什么区别？接收方怎么拿到附带数据？
7. `SA_NOCLDSTOP` 和 `SA_NOCLDWAIT` 有什么区别？各自用在什么场景？
8. self-pipe trick 解决的核心问题是什么？比单纯设标志位好在哪？

<details>
<summary>参考答案</summary>

1. ① `signal` 在不同 Unix（System V vs BSD）上行为不一致——处理一次后可能自动重置回 `SIG_DFL`；② `signal` 不能通过 `sa_mask` 屏蔽其他信号，也不能通过 `SA_RESTART` 自动重启被打断的系统调用。`sigaction` 解决了所有这些问题。
2. `SA_RESTART` 让被此信号打断的慢系统调用（`read`/`write`/`accept` 等）自动重启，而不是返回 `-1` 并设 `errno = EINTR`。不设的话，所有阻塞系统调用都可能返回 `EINTR`，你需要手动检查并重试，代码又烦又容易漏。
3. 处理函数执行期间的屏蔽字 = 常态屏蔽字 | `sa_mask` | 触发信号本身（除非设了 `SA_NODEFER`）。处理函数返回后，内核自动恢复到进入前的常态屏蔽字。此时之前被屏蔽挂在 pending 上的信号会被递达。
4. 因为它们不是 async-signal-safe。信号随时异步打断主流程——如果主流程正持有 `malloc` / `stdio` 的内部锁，处理函数里再调 `malloc` / `printf` 会死锁。
5. `sig_atomic_t` 保证该类型的读写不会被信号打断（一条 CPU 指令完成）。`volatile` 防止编译器把变量缓存在寄存器里——信号处理函数写的值是编译器"看不到"的（不在主流程），不加 `volatile` 主循环可能永远读到旧值。
6. `kill` 只能发信号编号；`sigqueue` 可以通过 `union sigval` 附带一个整数或指针。接收方在 `sa_flags` 中设 `SA_SIGINFO`，通过 `siginfo_t` 的 `si_value` 字段拿到附带数据，并用 `si_code == SI_QUEUE` 确认来源。
7. `SA_NOCLDSTOP`：子进程被 STOP（暂停）时不发 `SIGCHLD`，子进程退出仍会发。`SA_NOCLDWAIT`：子进程退出后内核自动回收不留僵尸，等价于 `signal(SIGCHLD, SIG_IGN)` 但语义更明确。前者用于"我关心退出但不关心暂停"，后者用于"退出码我也不关心"。
8. self-pipe 把异步信号转成同步 fd 事件，能直接融入 `select`/`epoll` 事件循环。比标志位好在：① 不丢失信号（管道有 N 个字节 vs 标志只有 1 个 bit）；② 能传信号编号（读出来的就是哪个信号）；③ 主线程不需要轮询。

</details>
