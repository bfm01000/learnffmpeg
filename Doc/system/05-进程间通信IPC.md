# 进程间通信 IPC：面试速记与原理详解

> **适用方向**：Linux C/C++ / 物联网智能硬件 / 多进程协作 / 共享内存大数据传输
> **难度**：⭐⭐⭐⭐（JD 点名"共享内存、网络通讯等应用编程"，重要度 🔥🔥🔥）
> **预计阅读**：速记 12 分钟｜全文 45 分钟
> **关联文档**：[[01-Linux系统编程全景导读]]（地图与定位）、[[02-进程管理与多进程编程]]（fork 出的进程怎么通信）、[[03-Linux内存管理机制]]（mmap、虚拟内存映射原理）、[[06-文件IO与IO多路复用]]（管道/eventfd 的 fd 怎么被 epoll 监听）、[[07-Linux网络编程]]（socket、跨机通信）

---

## 📌 第一部分：面试速记（考前 12 分钟扫一遍）

### 一句话核心

> **进程之间地址空间彼此隔离，一个进程改不了另一个进程的内存，所以必须借助内核提供的「公共通道」来交换数据——这就是 IPC。按数据怎么流动分两大类：一类「经内核中转」（管道、消息队列、socket，需要一次或两次拷贝），一类「共享同一块物理内存」（共享内存，零拷贝但必须自己配同步）。选型口诀：小数据/控制命令走管道或 UDS，大数据/图像帧走共享内存 + 信号量，跨机器走 socket。**

### 面试官常问问题 + 标准口语化回答

---

#### 开场题：进程之间为什么不能像线程那样直接共享变量，非要专门的 IPC？

**🗣️ 面试标准回答：**

> "因为**进程有独立的虚拟地址空间**。同一个虚拟地址在进程 A 和进程 B 里经过各自的页表，映射到的是**不同的物理页**，所以 A 写自己的全局变量，B 完全看不到。这是内核刻意做的隔离——一个进程崩了不会踩坏另一个进程的内存，安全性和稳定性都靠它。
>
> 代价就是：想交换数据就必须找一块**两个进程都能访问的内核对象或共享物理页**。内核为此提供了一整套机制：管道、FIFO、消息队列、信号量、共享内存（这套叫 System V / POSIX IPC）、Unix Domain Socket、信号、socket。线程之间共享同一个地址空间，所以直接用全局变量 + 锁就行，根本不需要这些。"

**👨‍💻 面试官追问：**

> Q: 那共享内存不是说两个进程能访问同一块内存吗，和你说的隔离矛盾？
> A: 不矛盾。共享内存是内核把**同一块物理页**分别映射进两个进程各自的虚拟地址空间（虚拟地址可以不一样），属于「主动开了一个共享窗口」，其余内存仍然是隔离的。映射原理见 [[03-Linux内存管理机制]]。

---

#### 必考题：常见的 IPC 方式有哪些，怎么对比和选型？

**考察意图：** 看你脑子里有没有一张全景表，能不能按场景快速选对。

**🗣️ 面试标准回答：**

> "我一般按四个维度记：**能不能跨机器、数据量大小、速度、要不要自己做同步**。
>
> - **匿名管道 pipe**：单向、字节流、只能有亲缘关系的进程（父子）用，shell 的 `|` 就是它。
> - **命名管道 FIFO**：在文件系统里有个路径名，无亲缘关系的进程也能用，仍是单向字节流。
> - **Unix Domain Socket（UDS）**：本机进程通信，走 socket API 但不经过网络协议栈，比 TCP 回环快，能双向、能传文件描述符。
> - **共享内存**：最快，多个进程映射同一块物理内存，读写不经过内核拷贝（零拷贝），但**必须自己配信号量或互斥做同步**，否则数据竞争。
> - **消息队列**：内核维护一个带类型/优先级的消息链表，天然有边界、可按优先级取。
> - **信号量**：本身不传数据，是给共享内存等做**同步**用的。
> - **信号 signal**：只能传一个『编号』，是异步通知，不适合传数据。
> - **socket（TCP/UDP）**：唯一能**跨机器**的，也最通用，代价是要走协议栈、有拷贝。
>
> 选型口诀：**小数据/控制命令 → 管道或 UDS；大数据/图像帧 → 共享内存 + 信号量；跨机 → socket。**"

**👨‍💻 面试官追问：**

> Q: 共享内存最快，那是不是都该用它？
> A: 不是。共享内存只解决「数据放哪」，**通知和同步要自己额外做**（信号量/条件变量/eventfd），代码复杂、容易出竞争 bug。如果数据量不大、对延迟不敏感，管道或 UDS 一把梭更省心。共享内存是为「大数据 + 高频 + 怕拷贝」准备的，比如逐帧传图像。

---

#### 高频题：共享内存为什么是最快的 IPC？快在哪？

**🗣️ 面试标准回答：**

> "因为它是**唯一真正零拷贝**的 IPC。其他机制（管道、消息队列、socket）数据都要『从发送进程的用户缓冲区 → 拷进内核缓冲区 → 再拷到接收进程的用户缓冲区』，至少**两次拷贝 + 两次系统调用**。
>
> 共享内存是内核把同一块物理页同时映射进两个进程的地址空间，建立映射之后，A 进程直接往这块内存写、B 进程直接读，**完全不经过内核中转、没有拷贝、没有 read/write 系统调用**。传一帧 1080p 图像（几 MB）时这个差距非常明显。
>
> 但天下没有免费午餐：共享内存**不带任何同步**，A 写到一半 B 就来读会读到半帧脏数据，所以必须额外用信号量/互斥来协调读写时序——这部分开销和复杂度是它的代价。"

**👨‍💻 面试官追问：**

> Q: 既然要映射，建立映射本身不也有开销吗？
> A: 有，`shmat` / `mmap` 建立页表映射是有一次性成本的，但这是**建立连接时一次性**的；之后每次读写都是纯内存访问，零拷贝。而管道/socket 是**每次传输**都要拷贝，高频大数据场景下共享内存净赚。

---

#### 高频题：共享内存为什么必须配同步？只用共享内存会出什么问题？

**🗣️ 面试标准回答：**

> "因为共享内存**只提供『共享的存储』，不提供任何『时序协调』**。两个进程并发读写同一块内存，会出三类问题：
>
> 1. **数据竞争**：A 正在写第 500 行像素，B 已经从第 0 行开始读，B 读到的是『新一半 + 旧一半』的撕裂帧。
> 2. **没有通知**：B 不知道 A 什么时候写完了一帧，只能轮询（浪费 CPU）或者读到脏数据。
> 3. **没有背压**：A 写得比 B 读得快，会直接覆盖掉 B 还没读的帧。
>
> 解决办法是配一套同步原语，最经典的是**信号量**：用一个信号量表示『有几帧可读』、一个表示『有几个空槽可写』，生产者写完 `post` 通知消费者、消费者读完 `post` 归还空槽。这其实就是有界生产者消费者模型，只不过跨进程，用的是**命名信号量**（POSIX `sem_open` 或 System V `semop`）。C++ 内部的信号量语义可参考 [[04-信号量]]（cpp 库），跨进程时换成 POSIX 命名信号量即可。"

**👨‍💻 面试官追问：**

> Q: 用 pthread_mutex 行不行？
> A: 普通 `pthread_mutex` 默认是进程内的，跨进程要在初始化时设 `PTHREAD_PROCESS_SHARED` 属性，并且这把锁本身得放在共享内存里两个进程才看得见。能用，但工程上跨进程同步更常用命名信号量，少踩坑。

---

#### 高频题：System V IPC 和 POSIX IPC 有什么区别？

**🗣️ 面试标准回答：**

> "两套并存的历史产物，共享内存/信号量/消息队列都各有一套。
>
> - **System V**：API 是 `shmget/shmat`、`semget/semop`、`msgget/msgsnd`，用一个整数 `key`（常用 `ftok` 生成）来标识对象，资源用 `ipcs` 命令查、`ipcrm` 删。缺点是接口老、key 容易冲突、不好和文件描述符体系/epoll 配合。
> - **POSIX**：API 是 `shm_open + mmap`、`sem_open`、`mq_open`，用一个**名字**（像 `/myshm` 的路径）标识，返回的是**文件描述符**，能和 `mmap`、`select/epoll` 自然配合，接口更现代。POSIX 共享内存实际落在 `/dev/shm` 下（tmpfs）。
>
> 面试结论：**新项目优先 POSIX**（接口干净、能配 fd/epoll、`/dev/shm` 可见好调试）；维护老代码或某些嵌入式平台才碰 System V。"

**👨‍💻 面试官追问：**

> Q: POSIX 共享内存的 `/dev/shm` 是什么？
> A: 一个挂在内存里的 tmpfs 文件系统。`shm_open("/foo")` 实际就是在 `/dev/shm/foo` 建了个文件，再 `mmap` 它。好处是能 `ls /dev/shm` 直接看到、`rm` 直接删，调试和清理都直观。

---

#### 实战题：让你设计两个进程之间传图像帧，你怎么做？

**🗣️ 面试标准回答：**

> "这是共享内存 + 信号量的典型场景，按生产者消费者建模：
>
> 1. **共享内存放帧缓冲区**：`shm_open` + `mmap` 出一块够放 N 帧的环形缓冲区（N 个槽位），避免每帧重新分配。
> 2. **两个命名信号量做同步 + 背压**：`emptySlots` 初值 N（还能写几帧）、`filledSlots` 初值 0（还能读几帧）。
> 3. **生产者**：`sem_wait(emptySlots)` 等到有空槽 → 把帧数据写进对应槽 → `sem_post(filledSlots)` 通知消费者多了一帧。
> 4. **消费者**：`sem_wait(filledSlots)` 等到有帧 → 直接从共享内存读（零拷贝）→ `sem_post(emptySlots)` 归还空槽。
>
> 这样**零拷贝**（帧数据始终在共享内存里，不经内核中转）、**有同步**（信号量保证不读到撕裂帧）、**有背压**（消费慢时 emptySlots 耗尽，生产者自动阻塞，内存不会爆）。
>
> 对比走 socket：每帧要 `write` 拷进内核、`read` 再拷出来，两次拷贝 + 两次系统调用，几 MB 的帧在高帧率下 CPU 和延迟都吃不消。所以本机大数据传输几乎一定选共享内存。"

**👨‍💻 面试官追问：**

> Q: 共享内存里只放裸帧数据吗？元信息（宽高、格式、帧序号）放哪？
> A: 通常在共享内存头部放一个**控制结构体**（环形缓冲区的读写下标、每个槽的宽高/格式/时间戳/数据长度），后面才是各个帧数据槽。消费者先读控制结构知道这帧多大、什么格式，再去读对应数据区。

---

#### 收尾题：管道、共享内存、socket 三个的拷贝次数各是多少？

**🗣️ 面试标准回答：**

> "记拷贝次数最能体现理解：
>
> - **共享内存**：**0 次**拷贝（双方直接读写同一物理页），代价是自己做同步。
> - **管道 / FIFO / UDS / 消息队列**：**2 次**拷贝（发送方用户态 → 内核缓冲 → 接收方用户态）。
> - **socket（本机回环）**：也是 2 次拷贝，但还多走一段网络协议栈处理（即使是 loopback）。
> - **socket（跨机）**：2 次拷贝 + 真正的网卡收发 + 协议栈。
>
> 所以本机高频大数据用共享内存，本机小数据用管道/UDS，跨机用 socket，这条选型链本质就是在**拷贝开销和编程复杂度之间权衡**。"

---

## 二、原理详解

### 1. 为什么需要 IPC：地址空间隔离

```text
        进程 A                                    进程 B
   ┌──────────────┐                          ┌──────────────┐
   │ 虚拟地址 0x600│                          │ 虚拟地址 0x600│
   └──────┬───────┘                          └──────┬───────┘
          │ 页表 A                                   │ 页表 B
          ▼                                          ▼
   ┌──────────────┐                          ┌──────────────┐
   │  物理页 #1234 │                          │  物理页 #5678 │   ← 不同物理页，互不可见
   └──────────────┘                          └──────────────┘

   共享内存例外：内核让两个进程的页表都指向同一物理页
          │ 页表 A                                   │ 页表 B
          ▼                                          ▼
   ┌─────────────────────────────────────────────────────────┐
   │                  同一块物理页 #9999                        │  ← 共享窗口，零拷贝
   └─────────────────────────────────────────────────────────┘
```

进程隔离是默认的、安全的；IPC 就是在隔离的墙上**按需开一扇受控的门**。

### 2. 全景对比表（背下来）

| IPC 方式 | 能否跨机 | 方向 | 数据量 | 速度 | 是否要自己同步 | 典型场景 |
|---|---|---|---|---|---|---|
| 匿名管道 pipe | 否 | 单向 | 小 | 中（2 拷贝） | 否（内核串行化） | 父子进程、shell `|` |
| 命名管道 FIFO | 否 | 单向 | 小 | 中（2 拷贝） | 否 | 无亲缘进程、日志投递 |
| Unix Domain Socket | 否 | 双向 | 中 | 较快（2 拷贝、不走协议栈） | 否 | 本机 C/S、传 fd |
| 共享内存 | 否 | 双向 | **大** | **最快（0 拷贝）** | **是（必须配信号量）** | 图像帧、大缓冲 |
| 消息队列 | 否 | 双向 | 中 | 中（2 拷贝） | 否（内核带边界/优先级） | 任务分发、优先级消息 |
| 信号量 | 否 | —（只同步） | 不传数据 | — | 自身就是同步工具 | 给共享内存配同步 |
| 信号 signal | 否 | 单向通知 | 极小（仅编号） | 快 | 否 | 异步通知、kill |
| socket (TCP/UDP) | **是** | 双向 | 中大 | 慢（协议栈 + 2 拷贝） | 否 | 跨机、网络服务 |

> 记忆主线：**只有 socket 跨机；只有共享内存零拷贝且必须自己同步；信号量不传数据只做同步；信号只传一个编号。**

### 3. 经内核中转 vs 共享物理页（拷贝路径）

```text
【管道 / 消息队列 / socket】 两次拷贝
  进程A用户缓冲 ──write拷贝1──▶ 内核缓冲 ──read拷贝2──▶ 进程B用户缓冲

【共享内存】 零拷贝
  进程A ──直接写──▶ ┌────────────┐ ◀──直接读── 进程B
                    │ 共享物理页  │
                    └────────────┘
  （但要额外用信号量协调「写完了没」「读完了没」）
```

---

## 三、各机制要点与代码

### 1. 匿名管道 pipe（父子进程、单向字节流）

`pipe()` 返回一对 fd：`fileDescriptors[0]` 读端、`fileDescriptors[1]` 写端。数据单向流动（要双向得开两个管道）。只能在**有亲缘关系**的进程间用，因为子进程靠 `fork` 继承这对 fd。

```c
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    int fileDescriptors[2];          // [0]读端 [1]写端
    pipe(fileDescriptors);           // 创建匿名管道

    pid_t childPid = fork();
    if (childPid == 0) {
        // 子进程：只读，关掉写端
        close(fileDescriptors[1]);
        char readBuffer[128];
        ssize_t bytesRead = read(fileDescriptors[0], readBuffer, sizeof(readBuffer));
        readBuffer[bytesRead] = '\0';
        printf("子进程收到: %s\n", readBuffer);
        close(fileDescriptors[0]);
    } else {
        // 父进程：只写，关掉读端
        close(fileDescriptors[0]);
        const char* message = "hello from parent";
        write(fileDescriptors[1], message, strlen(message));
        close(fileDescriptors[1]);   // 关写端，对端 read 才会返回 0(EOF)
    }
    return 0;
}
```

要点：
- **shell 的 `|` 就是匿名管道**：`ls | grep x` 是 shell 先 `pipe`，再 fork 出两个进程，把前者的 stdout 接到管道写端、后者的 stdin 接到读端。
- **SIGPIPE**：所有读端都关了之后还往管道写，进程会收到 `SIGPIPE` 信号（默认终止进程）。写网络/管道的健壮代码通常 `signal(SIGPIPE, SIG_IGN)` 忽略它，改为检查 `write` 返回 `EPIPE`。信号细节见 [[02-进程管理与多进程编程]]。
- 管道 fd 本身可以被 `epoll` 监听，做事件驱动，见 [[06-文件IO与IO多路复用]]。

### 2. 命名管道 FIFO（无亲缘进程也能用）

匿名管道只能父子用；FIFO 在文件系统里有一个**路径名**，任何进程只要能打开这个路径就能通信。

```c
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// 进程甲：创建并写
mkfifo("/tmp/myfifo", 0666);              // 在文件系统留一个管道节点
int writeFd = open("/tmp/myfifo", O_WRONLY);
write(writeFd, "frame ready", 11);
close(writeFd);

// 进程乙（另一个程序）：打开同一路径来读
int readFd = open("/tmp/myfifo", O_RDONLY);
char buffer[64];
read(readFd, buffer, sizeof(buffer));
close(readFd);
```

要点：FIFO 文件本身只是个**入口标记**，数据并不落盘，仍走内核缓冲；`open` 默认会阻塞直到读写两端都就位。适合无亲缘关系进程间的小数据/控制流投递。

### 3. Unix Domain Socket（UDS，本机进程通信）

用的是和网络一样的 socket API，但地址族是 `AF_UNIX`，地址是一个**文件系统路径**而不是 IP:端口。数据**不经过 TCP/IP 协议栈**，所以比走 `127.0.0.1` 的回环 TCP 更快、更省。

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

// 服务端
int listenFd = socket(AF_UNIX, SOCK_STREAM, 0);   // 也可 SOCK_DGRAM
struct sockaddr_un serverAddress = {0};
serverAddress.sun_family = AF_UNIX;
strcpy(serverAddress.sun_path, "/tmp/uds.sock");
unlink("/tmp/uds.sock");                           // 防止上次残留
bind(listenFd, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
listen(listenFd, 8);
int connectionFd = accept(listenFd, NULL, NULL);   // 接受一个连接
char buffer[256];
read(connectionFd, buffer, sizeof(buffer));
```

要点：
- **`SOCK_STREAM`**（字节流，像 TCP）vs **`SOCK_DGRAM`**（数据报，带边界，像 UDP），本机都可靠。
- **比 TCP 回环快**：不计算校验和、不走拥塞控制/协议栈，纯内核内存搬运。
- **能传文件描述符（进阶亮点）**：通过 `sendmsg` 的辅助数据 `SCM_RIGHTS`，一个进程可以把一个打开的 fd（socket、文件、甚至 `memfd`）**直接传给另一个进程**，对端拿到的是指向同一个内核文件对象的新 fd。这是很多高性能架构（如把已建立的连接交给 worker 进程、传递共享内存的 memfd）的关键技巧，面试能说出来是加分项。socket 系统调用细节见 [[07-Linux网络编程]]。

### 4. 共享内存（重点：JD 点名）

最快的 IPC，零拷贝。两套 API：

**POSIX 风格（推荐，shm_open + mmap）：**

```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

const size_t regionSize = 4096;

// 创建/打开一个共享内存对象，落在 /dev/shm 下
int sharedFd = shm_open("/myregion", O_CREAT | O_RDWR, 0666);
ftruncate(sharedFd, regionSize);                  // 设定大小

// 映射进本进程地址空间，返回可直接读写的指针
void* mappedAddress = mmap(NULL, regionSize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, sharedFd, 0);

// 之后像操作普通内存一样读写 mappedAddress —— 零拷贝
// 另一个进程用同样的名字 shm_open + mmap，就看到同一块物理内存

// 清理：解除映射 + 删除对象（谁创建谁负责 unlink）
munmap(mappedAddress, regionSize);
shm_unlink("/myregion");
```

**System V 风格（老接口，shmget/shmat）：**

```c
#include <sys/ipc.h>
#include <sys/shm.h>

key_t sharedKey = ftok("/tmp", 'A');              // 用路径+编号生成 key
int shmId = shmget(sharedKey, 4096, IPC_CREAT | 0666);   // 创建/获取
void* mappedAddress = shmat(shmId, NULL, 0);      // attach 到地址空间
// ... 读写 mappedAddress ...
shmdt(mappedAddress);                             // detach
shmctl(shmId, IPC_RMID, NULL);                    // 标记删除（谁创建谁销毁）
```

**关键认识：**
- **为什么最快**：建立映射后双方直接读写同一物理页，**零拷贝、无系统调用**（对比管道/socket 每次两次拷贝），见上文拷贝路径图。映射的虚拟内存原理见 [[03-Linux内存管理机制]]。
- **必须配同步**：共享内存自身不带任何锁，**裸用必然数据竞争**（读到撕裂数据）。标准做法是配命名信号量（下一节）。
- **生命周期管理**：
  - POSIX 对象在 `/dev/shm/` 下，`shm_unlink` 删名字；进程都退出且都 `munmap` 后物理内存才真正释放。
  - System V 用 `shmctl(IPC_RMID)` 标记删除，`ipcs -m` 查看、`ipcrm` 手动删。**忘记删会泄漏**——重启前一直占着内存，这是 System V 的经典坑。
  - 原则：**谁创建谁负责销毁**；进程崩溃不会自动清理共享内存对象，要么靠创建者收尾，要么用 `/dev/shm` 手动清。

### 5. 信号量做跨进程同步

共享内存的标配搭档。信号量本身不传数据，只表达「还剩几份资源 / 发生了几次事件」。跨进程要用**命名信号量**（两个进程靠同一个名字拿到同一个内核信号量对象）。

**POSIX 命名信号量（推荐）：**

```c
#include <semaphore.h>
#include <fcntl.h>

// 两个进程用同样的名字打开，得到同一个信号量
sem_t* emptySlots = sem_open("/empty", O_CREAT, 0666, 4);  // 初值4：还能写4帧
sem_t* filledSlots = sem_open("/filled", O_CREAT, 0666, 0); // 初值0：还能读0帧

sem_wait(emptySlots);   // P：计数减1，为0则阻塞
sem_post(filledSlots);  // V：计数加1，唤醒等待者

sem_close(emptySlots);
sem_unlink("/empty");   // 用完删除名字
```

**System V 信号量（老接口）：** `semget` 创建一组信号量、`semop` 做 P/V（支持一次对多个信号量原子操作）、`semctl` 控制和删除。接口繁琐，新项目少用。

> 信号量的 P/V 语义、二值 vs 计数、和 mutex 的区别等，和 C++ 线程内信号量是同一套思想，详见 [[04-信号量]]（cpp 库）；跨进程只是把无名信号量换成命名信号量。

### 6. 消息队列（带边界和优先级）

内核维护一个消息链表，发送方 `msgsnd`/`mq_send` 投递一条**完整消息**，接收方 `msgrcv`/`mq_receive` 取一条。和管道的区别是**天然有消息边界**（不用自己切包）且**可带优先级/类型**。

- **System V**：`msgget` / `msgsnd` / `msgrcv`，消息带一个 `long mtype` 类型字段，接收方可按类型挑选。
- **POSIX**：`mq_open` / `mq_send` / `mq_receive`，发送时带优先级，**高优先级消息先被取走**；返回 fd 可被 epoll 监听。

适合：任务分发、需要优先级的控制消息。数据量不大、要边界和优先级时比管道省心。

### 7. 信号 signal 作为 IPC（只传一个编号）

`kill(targetPid, SIGUSR1)` 给另一个进程发一个信号，对方在信号处理函数里响应。本质是**异步事件通知**，**不能传数据**（最多用 `sigqueue` 捎带一个整数/指针值）。适合「通知对方该做某事了」（如重载配置 `SIGHUP`、优雅退出 `SIGTERM`），不适合传输数据。信号机制、可重入、`signalfd` 等细节见 [[02-进程管理与多进程编程]]。

### 8. eventfd / 管道做事件通知（进阶）

当只需要「跨进程/跨线程发一个『有事了』的轻量通知」，`eventfd` 是比管道更省的选择：它就是一个内核里的 64 位计数器，`write` 加值、`read` 取值并清零，**只占一个 fd**，天生能被 `epoll` 监听。常见用法是配合共享内存——数据放共享内存，用 eventfd 通知「新数据到了」，把「传输」和「通知」分开。事件驱动整合见 [[06-文件IO与IO多路复用]]。

---

## 四、重点实战：共享内存 + 信号量逐帧传图像

这是 JD 场景（音视频 / 智能硬件本机进程间传大数据）的标准答案。目标：进程 A（生产者，比如采集/解码）把图像帧写进共享内存，进程 B（消费者，比如编码/显示）读出来，要求**零拷贝 + 同步不撕裂 + 有背压**。

### 设计：共享内存里放一个环形帧缓冲区

```text
共享内存布局（一块 shm_open + mmap 出来的连续内存）：
┌──────────────────────────────────────────────────────────────┐
│  控制头 ControlHeader  │  帧槽0  │  帧槽1  │  ...  │  帧槽N-1   │
│  (读写下标、各槽元信息) │ (像素)  │ (像素)  │       │  (像素)    │
└──────────────────────────────────────────────────────────────┘

两个命名信号量做同步 + 背压：
  emptySlots  初值 N  —— 还能写几帧（生产者等它）
  filledSlots 初值 0  —— 还能读几帧（消费者等它）
```

### 共享的数据结构（生产者消费者都映射这一份）

```c
#include <stdint.h>

#define SLOT_COUNT      4               // 环形缓冲槽数 = 信号量初值
#define MAX_FRAME_BYTES (1920 * 1080 * 3)  // 单帧上限

// 一个帧槽：元信息 + 像素数据
typedef struct {
    uint32_t frameWidth;
    uint32_t frameHeight;
    uint32_t pixelFormat;               // 约定的格式枚举，如 RGB24
    uint32_t dataLength;                // 本帧实际字节数
    uint64_t timestamp;                 // 帧时间戳（用于音视频同步）
    uint8_t  pixelData[MAX_FRAME_BYTES];
} FrameSlot;

// 整块共享内存的布局：控制头 + N 个帧槽
typedef struct {
    uint32_t  writeIndex;               // 生产者下一个要写的槽
    uint32_t  readIndex;                // 消费者下一个要读的槽
    FrameSlot slots[SLOT_COUNT];
} SharedFrameRegion;
```

### 生产者进程（写帧）

```c
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

// 1) 建立共享内存
int sharedFd = shm_open("/frame_region", O_CREAT | O_RDWR, 0666);
ftruncate(sharedFd, sizeof(SharedFrameRegion));
SharedFrameRegion* region = mmap(NULL, sizeof(SharedFrameRegion),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, sharedFd, 0);
region->writeIndex = 0;
region->readIndex = 0;

// 2) 建立同步信号量
sem_t* emptySlots  = sem_open("/empty",  O_CREAT, 0666, SLOT_COUNT); // 空槽数
sem_t* filledSlots = sem_open("/filled", O_CREAT, 0666, 0);          // 满槽数

// 3) 逐帧生产
while (capturing) {
    sem_wait(emptySlots);               // 等一个空槽（满了就阻塞 → 背压）

    FrameSlot* slot = &region->slots[region->writeIndex];
    slot->frameWidth  = 1920;
    slot->frameHeight = 1080;
    slot->dataLength  = produce_one_frame(slot->pixelData); // 直接写进共享内存，零拷贝

    region->writeIndex = (region->writeIndex + 1) % SLOT_COUNT;
    sem_post(filledSlots);              // 通知消费者：多了一帧可读
}
```

### 消费者进程（读帧）

```c
// 用同样的名字映射同一块共享内存、打开同一组信号量
int sharedFd = shm_open("/frame_region", O_RDWR, 0666);
SharedFrameRegion* region = mmap(NULL, sizeof(SharedFrameRegion),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, sharedFd, 0);
sem_t* emptySlots  = sem_open("/empty",  0);
sem_t* filledSlots = sem_open("/filled", 0);

while (running) {
    sem_wait(filledSlots);              // 等一帧就绪（没有就阻塞）

    FrameSlot* slot = &region->slots[region->readIndex];
    consume_one_frame(slot->pixelData, slot->dataLength); // 直接从共享内存读，零拷贝

    region->readIndex = (region->readIndex + 1) % SLOT_COUNT;
    sem_post(emptySlots);              // 归还空槽，生产者可以继续写
}
```

### 三个核心收益讲清楚

| 收益 | 怎么做到的 |
|---|---|
| **零拷贝** | 帧数据自始至终待在共享内存里，生产者直接写、消费者直接读，不经内核中转、无 read/write 拷贝 |
| **同步不撕裂** | `filledSlots` 保证消费者只在生产者 `sem_post` 之后才去读那个槽，绝不会读到写一半的帧 |
| **背压** | 消费慢时 `emptySlots` 被耗尽，生产者阻塞在 `sem_wait(emptySlots)`，自动降速，内存不会无界增长 |

### 对比走 socket 传同一帧

```text
共享内存方案：  生产者写共享内存 ──(0拷贝)──▶ 消费者读     CPU几乎只花在产/消费本身
socket 方案：   生产者帧 ─write拷贝1─▶内核─read拷贝2─▶ 消费者   每帧2次几MB大拷贝 + 2次系统调用
```

1080p RGB 一帧约 6 MB，60fps 下 socket 方案每秒要多搬 ~720 MB（两次拷贝合计），CPU 和延迟都明显劣于共享内存。所以**本机大数据 + 高频几乎一定选共享内存 + 信号量**，跨机才退而用 socket（见 [[07-Linux网络编程]]）。

---

## 五、选型建议表

| 需求 | 首选方案 | 理由 |
|---|---|---|
| 父子进程传小数据 | 匿名管道 pipe | 最简单，fork 直接继承 |
| 无亲缘进程传控制命令 | 命名管道 FIFO / UDS | 有路径名，谁都能连 |
| 本机 C/S、要双向、要传 fd | Unix Domain Socket | 不走协议栈，比回环 TCP 快，可 SCM_RIGHTS 传 fd |
| **本机大数据 / 图像帧 / 高频** | **共享内存 + 信号量** | **零拷贝 + 同步 + 背压，性能天花板** |
| 带优先级的任务/消息分发 | 消息队列（POSIX） | 天然消息边界 + 优先级 |
| 只需通知「该干活了」 | 信号 / eventfd | 轻量，能配 epoll |
| **跨机器通信** | **socket（TCP/UDP）** | 唯一跨机方案，见 [[07-Linux网络编程]] |
| 纯做同步、不传数据 | 信号量 / 进程共享 mutex | 配合共享内存使用 |

---

## 六、常见坑与面试加分点

| 坑 | 说明 |
|---|---|
| 共享内存裸用不加同步 | 必然读到撕裂数据，**必须配信号量/进程共享 mutex** |
| System V 共享内存忘记 `ipcrm` | 进程退出也不自动回收，`ipcs -m` 能看到泄漏，重启前一直占内存 |
| 把信号量当成「能传数据」 | 信号量只表达计数/事件，**不携带数据**，数据要放共享内存 |
| 所有读端关了还往管道写 | 触发 `SIGPIPE`，默认杀进程；健壮代码 `SIG_IGN` 后查 `EPIPE` |
| 用回环 TCP 做本机大数据 | 走协议栈 + 两次拷贝，本机首选 UDS 或共享内存 |
| 共享内存里存裸指针 | 两进程虚拟地址可能不同，**存指针无效**，要存偏移量 offset |
| 命名信号量/共享内存忘了 unlink | 名字残留在 `/dev/shm`，下次 `O_CREAT` 拿到旧脏对象 |

**加分点（音视频 / 智能硬件）：**

> "我会把**数据传输和事件通知分开**：大数据（图像帧）走共享内存零拷贝，通知走 eventfd 或信号量，再用 epoll 统一驱动。共享内存里**只存偏移量不存指针**（两进程映射的虚拟地址可能不同），头部放控制结构和环形缓冲下标。需要把已建立的连接或共享缓冲跨进程交接时，用 UDS + `SCM_RIGHTS` 传 fd。这套组合是本机高性能 IPC 的标准骨架。"

---

## 七、速记对照表

| 维度 | 一句话结论 |
|---|---|
| 为什么要 IPC | 进程地址空间隔离，改不了对方内存 |
| 唯一跨机 | 只有 socket |
| 唯一零拷贝 | 只有共享内存（但必须自己同步） |
| 拷贝次数 | 共享内存 0 次；管道/消息队列/UDS/socket 2 次 |
| 共享内存搭档 | 信号量做同步、eventfd 做通知 |
| System V vs POSIX | 新项目用 POSIX（fd 化、`/dev/shm` 可见）；老代码 System V |
| 信号能传什么 | 只能传一个编号，是异步通知不是数据通道 |
| 大数据本机方案 | 共享内存 + 信号量（零拷贝 + 同步 + 背压） |

---

## 八、自测 8 题

1. 进程之间为什么不能直接共享全局变量，线程却可以？
2. 共享内存为什么是最快的 IPC？它和管道的拷贝次数各是多少？
3. 为什么共享内存必须配同步？不配会出什么问题？
4. System V 共享内存和 POSIX 共享内存（`shm_open`）主要区别是什么？
5. 跨进程同步为什么要用「命名」信号量？普通 pthread_mutex 能跨进程吗？
6. 匿名管道和命名管道（FIFO）的本质区别是什么？
7. Unix Domain Socket 比回环 TCP 快在哪？它的进阶亮点能力是什么？
8. 设计两进程逐帧传图像，怎么同时做到零拷贝、不撕裂、有背压？

<details>
<summary>参考答案</summary>

1. 进程有独立虚拟地址空间，同一虚拟地址经各自页表映射到不同物理页，互不可见；线程共享同一地址空间，所以全局变量直接可见。
2. 双方映射同一物理页，读写零拷贝、无系统调用；共享内存 0 次拷贝，管道 2 次（用户→内核→用户）。
3. 共享内存只提供存储不提供时序协调，裸并发读写会撕裂、无通知、无背压；要配信号量/进程共享 mutex。
4. System V 用整数 key + `shmget/shmat`，靠 `ipcs/ipcrm` 管理；POSIX 用名字 + `shm_open/mmap`，返回 fd、落在 `/dev/shm`、能配 epoll，更现代。
5. 命名信号量两个进程靠同一个名字拿到同一个内核对象才能互通；普通 pthread_mutex 默认进程内，要设 `PTHREAD_PROCESS_SHARED` 并把锁放进共享内存才能跨进程。
6. 匿名管道只能有亲缘关系（fork 继承 fd）的进程用；FIFO 在文件系统有路径名，无亲缘进程也能打开通信。
7. 不走 TCP/IP 协议栈、不算校验和、无拥塞控制，纯内核内存搬运；进阶亮点是用 `SCM_RIGHTS` 跨进程传文件描述符。
8. 共享内存放环形帧缓冲（数据零拷贝）；`filledSlots`/`emptySlots` 两个信号量分别表示可读帧数/空槽数，生产者写完 post filled、消费者读完 post empty，满了生产者阻塞形成背压，信号量时序保证不读到半帧。

</details>








