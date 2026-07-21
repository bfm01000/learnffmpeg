# TLS 线程局部存储：面试速记与原理详解

> **适用方向**：Linux C/C++ 开发（所有方向），多线程编程的基础设施，中高级面试必问
> **难度**：🔥🔥🔥 **高频核心**
> **定位**：从「会用 `thread_local`」到「能讲清 FS 寄存器怎么找到 TLS 数据」——彻底搞懂 TLS 的实现原理、ELF 访问模型、与 pthread_key 的对比
> **预计阅读**：速记 5 分钟｜全文 35 分钟
> **关联文档**：[[04-多线程与Linux调度]]（clone/futex/线程栈，TLS 在此简述）、cpp 库 `01-多线程与锁`（mutex/条件变量/原子操作）、[[03-Linux内存管理机制]]（线程栈的虚拟内存布局）

---

## 📌 第一部分：面试速记（考前 5 分钟扫一遍）

### 一句话核心

> **TLS（Thread-Local Storage）让一个全局或静态变量在每个线程里各有一份独立的副本，互不干扰。实现上不是靠锁，也不是靠系统调用——编译器把 `thread_local` 变量的访问翻译成「线程寄存器（x86-64 的 `fs`、aarch64 的 `tpidr_el0`）指向的 TLS 基址 + 编译期确定的偏移量」，就是一次普通内存访问，零额外开销。`errno` 就是最经典的 TLS 应用——它看起来是全局变量，实际展开成 `*__errno_location()`，每个线程返回自己那份，互不污染。**

### 面试官常问问题 + 标准口语化回答

---

#### 必考题 1：什么是 TLS？为什么要用它？

**🗣️ 面试标准回答：**

> "TLS 是 Thread-Local Storage，让一个全局/静态变量在每个线程里各存一份。解决的问题很明确：**多线程下，真正需要共享的数据用 mutex 保护；但有些数据天然是『每个线程自己管自己』的**——比如 errno、每个连接的上下文、per-thread 日志缓冲区。如果把这些做成真正的全局变量，就要加锁；如果做成函数局部变量，就得在所有调用链上层层传递。
>
> TLS 是第三种选择：写法上是全局变量，语义上是每个线程一个——兼顾代码简洁和零锁开销。"

**👨‍💻 面试官追问：**

> Q: 那为什么不把所有『每个线程用一次』的变量都设成 thread_local？
> A: TLS 不是免费的——每个线程都要为每个 TLS 变量分配存储空间，即使这个线程根本不用。一万个线程 × 每个 TLS 变量 4KB 缓冲 = 40MB 额外内存。另外 TLS 变量的构造/析构发生在线程起止时，大量短命线程会增加开销。权衡：高频访问 + 线程数可控 → TLS 最优；数据量大 + 线程多 → 考虑参数传递或线程私有堆。

---

#### 必考题 2：TLS 在底层是怎么实现的？`fs` 寄存器扮演什么角色？

**🗣️ 面试标准回答：**

> "TLS 的核心机制是**段寄存器 + 偏移寻址**。
>
> 在 x86-64 Linux 上，`fs` 段寄存器指向当前线程的 TLS 基址。这不是普通的基址寄存器——`fs` 是段寄存器，在 64 位模式下虽然不分段了，但 `fs` 和 `gs` 仍被保留用作**per-CPU 和 per-thread 数据的基址**。Linux 约定：`gs` 给内核（per-CPU 变量），`fs` 给用户态（TLS）。
>
> 编译器对 `thread_local int x` 生成类似 `mov %fs:offset, %eax` 的指令。`offset` 在编译/链接时就确定了，运行时不改；`fs` 基址在每次线程切换时由内核写进线程的 MSR 或 GDT 条目——这样**同一条 `mov %fs:8, %eax`，在线程 A 里读的是 A 的 x，在线程 B 里读的是 B 的 x**。
>
> ARM 架构等价于 `tpidr_el0`（EL0 线程 ID 寄存器），编译器生成 `mrs x0, tpidr_el0; ldr x0, [x0, #offset]`。
>
> 关键结论：**读写 TLS 变量就是一次普通的内存 load/store，外加一次段基址加法（硬件自动完成），无锁、无系统调用、不经过内核。**"

**👨‍💻 面试官追问：**

> Q: 那线程切换时 `fs` 基址是怎么跟着变的？
> A: 线程切换必然经过内核调度器 `context_switch()`。x86-64 上，调度器在切换到新线程时，会从新线程的 `thread_struct` 里读出 TLS 基址，写进 `MSR_FS_BASE`（通过 `wrmsr` 指令）或更新 GDT 里对应条目的基址。这是**内核调度路径的一环**，和保存/恢复通用寄存器、切换页表是同一个流程。所以用户态线程切换后，`fs` 已经指向新线程的 TLS 了，用户代码无感知。

---

#### 必考题 3：C/C++ 里创建 TLS 变量有哪几种方式？各有什么特点？

**🗣️ 面试标准回答：**

> "三种方式，按推荐度排序：
>
> 1. **C++11 `thread_local`**：标准关键字，最推荐。声明 `thread_local int x = 0;`，支持动态初始化、非平凡类型的构造/析构函数在线程起止时自动调用。C++11 起可用。
>
> 2. **GCC `__thread`**：GCC 扩展，C/C++ 都能用。`__thread int x = 0;`。**限制**：只能用于 POD 类型、只能用编译期常量初始化、不能有构造/析构函数。简单高效，老代码里常见。
>
> 3. **POSIX `pthread_key_create` / `pthread_getspecific`**：C 标准方式，动态分配 key，运行时通过 key 存取 `void *` 指针。优势是**可以注册析构回调**（`pthread_key_create` 的第二个参数），在线程退出时自动释放资源。代价是需要函数调用、查表，比前两种慢；而且 key 数量有限制（`PTHREAD_KEYS_MAX`，通常是 1024）。
>
> 选型：能确定编译期变量 → `thread_local`（C++）或 `__thread`（C）；需要动态管理、需要析构回调 → `pthread_key`。"

**👨‍💻 面试官追问：**

> Q: 三种方式的性能差距有多大？
> A: `thread_local` / `__thread`：一次内存访问，约 1~3 个 cycle（和普通全局变量几乎一样）；`pthread_getspecific`：函数调用 + 查表 + 可能锁（取决于实现），几十到上百 ns。前两种快 10~100 倍。所以高频访问路径用 `thread_local`，低频配置类用 `pthread_key` 也够。

---

#### 高频题 4：`errno` 为什么是线程安全的？它是怎么实现的？

**🗣️ 面试标准回答：**

> "`errno` 看起来是个 `extern int errno;` 全局变量，但其实是个**宏**，展开后大约是这样：
>
> ```c
> #define errno (*__errno_location())
> ```
>
> `__errno_location()` 返回一个 `int *`，指向**当前线程自己的 errno 存储位置**。这个位置就在线程的 TLS 区域里，具体是 `fs` 寄存器基址 + 一个固定偏移。
>
> 所以 A 线程 `errno = EAGAIN` 和 B 线程 `errno = EINTR` 写的是两片完全不同的内存——不需要任何同步。
>
> 不仅是 errno，`strtok()` 的静态内部指针（`strtok_r` 解决了）、`localtime()` 返回的 `struct tm *`（`localtime_r` 解决了）——历史上都是全局变量导致线程不安全，现代方案要么改成 TLS（errno），要么提供 `_r` 后缀的 reentrant 版本。"

**👨‍💻 面试官追问：**

> Q: 那你能自己实现一个 `__errno_location()` 吗？
> A: 可以用 `pthread_getspecific` 或直接用 `__thread`：
> ```c
> __thread int my_errno;
> int *my_errno_location(void) { return &my_errno; }
> ```
> 但真正的 glibc 实现更复杂——`errno` 不一定在 `__thread` 变量里，而是在 TCB（Thread Control Block）的固定偏移位置，由 `__errno_location` 通过 `fs` 寄存器直接算出来，确保它跟 pthread 内部结构紧密耦合、偏移稳定。

---

#### 高频题 5：ELF 的 TLS 有四种访问模型，分别是什么？

**🗣️ 面试标准回答：**

> "TLS 变量在不同编译单元（可执行文件、动态库）之间的访问，链接器需要知道偏移怎么算。ELF 定义了四种 TLS 访问模型，按「访问效率递减、灵活性递增」排列：
>
> 1. **Local Exec（LE，局部执行）**：变量和访问它的代码在**同一个可执行文件**里，不涉及动态库。偏移是链接期确定的绝对常量。编译器直接生成 `mov %fs:固定偏移, %reg`——最快，一条指令。
>
> 2. **Initial Exec（IE，初始执行）**：变量可能在动态库里，但保证**在程序启动时就已加载**（不用 `dlopen` 延迟加载）。需要从 GOT（全局偏移表）里间接取一次偏移：`mov var@GOTTPOFF(%rip), %rax; mov %fs:(%rax), %reg`。多一次内存访问，但仍很快。
>
> 3. **Local Dynamic（LD，局部动态）**：代码在动态库里，访问**本库自己的** TLS 变量。需要先调用 `__tls_get_addr` 获取本模块的 TLS 块基址，再加变量在块内的偏移。有函数调用开销。
>
> 4. **Global Dynamic（GD，全局动态）**：最通用的模型——代码在动态库，访问的 TLS 变量可能在任何模块（包括 `dlopen` 动态加载的）。每次访问都调用 `__tls_get_addr`。最慢，但最灵活。
>
> 编译器默认选最保守的 GD 模型；链接时如果确认变量和访问在同一模块，可以**松弛（relax）**到 IE 或 LE。面试能说清这四种模型的名字和应用场景，面试官就知道你不仅会用，还懂 ELF 层面的机理。"

**👨‍💻 面试官追问：**

> Q: `__tls_get_addr` 做了什么？
> A: 它是动态链接器 `ld.so` 提供的函数。输入是一个 `(module_id, offset)` 对（存在 GOT 里），输出是当前线程这个模块的 TLS 块中该变量的绝对地址。它内部维护一个每个线程的 TLS 块链表——第一次访问某模块的 TLS 时，动态分配一块内存挂到当前线程的 TLS 链表上，返回其内偏移处的地址。

---

#### 加分题 6：一个线程的 TLS 数据在内存里是怎么布局的？

**🗣️ 面试标准回答：**

> "从 `fs` 基址出发，TLS 块的布局大致是这样的（x86-64）：
>
> ```text
> 低地址                                             高地址
> ┌──────────────┬───────────┬──────────┬──────────┐
> │  TCB (线程    │ 可执行文件的 │ libA.so   │ libB.so   │  ...动态库的 TLS
> │  控制块)      │  TLS 变量   │ TLS 变量  │ TLS 变量  │
> │  (pthread 内部│            │           │           │
> │   结构)       │            │           │           │
> └──────────────┴───────────┴──────────┴──────────┘
>  ◄── fs 基址
> ```
>
> 注意 `fs` 基址**不一定**指向 TLS 块的起始——x86-64 的 TLS ABI 约定 `fs` 指向 TCB，TLS 变量用**负偏移**访问（`mov %fs:-8, %rax` 之类）。这是因为 TCB 在 TLS 区域的低地址端，fs 指向 TCB 的某个位置，可执行文件/动态库的 TLS 变量放在正/负偏移处。
>
> 更精确地，x86-64 TLS 有两种 ABI：
> - **Variant I**（多数 UNIX，包括 Solaris）：`fs` 指向 TLS 块末尾，TLS 变量用负偏移。
> - **Variant II**（Linux x86-64 实际使用）：`fs` 指向 TCB（在 TLS 数据之前），可执行文件的 TLS 变量在 TCB 之后用正偏移，动态库的 TLS 变量在 TCB 之前用负偏移。
>
> 面试通常不需要背到 Variant I/II，但如果你能说清楚『fs 不直接指向 TLS 数据，而是指向 TCB，TLS 变量通过正负偏移访问』，就已经是非常扎实的认知了。"

---

#### 加分题 7：TLS 变量在线程创建和销毁时发生了什么？

**🗣️ 面试标准回答：**

> "**创建时**：`pthread_create` → `clone` 系统调用。内核创建新线程的 task_struct 和栈。然后回到用户态，`libpthread`（或 `ld.so`）负责为新线程分配和初始化 TLS 区域：
> 1. 分配一块内存放 TCB + 所有已加载模块的 TLS 数据
> 2. 把**主线程的 TLS 变量的初始值**拷贝过来（每个 `thread_local` 变量的初始值是线程创建时从主线程或模板复制，不是构造函数的返回值——除非有动态初始化）
> 3. 设置 `fs` 基址指向这个线程的 TLS 区域
> 4. 遍历所有 `thread_local` 变量，调用它们的构造函数（C++ 非平凡类型）
>
> **销毁时**：线程退出时，先调用所有 `thread_local` 变量的析构函数（按构造的逆序），再调用所有 `pthread_key` 注册的 destructor，最后释放 TLS 内存。
>
> 关键点：TLS 变量的**初始值**是所有线程共享的一份模板，存在 ELF 的 TLS 段里。新线程创建时 memcpy 过去，所以每个线程的初始状态一致。之后各自修改、互不影响。"

---

## 二、原理详解

### 1. 为什么需要 TLS：一个现实场景

假设你正在写一个多线程的 HTTP 服务器。每个连接有独立的缓冲区、独立的统计计数器、独立的错误信息：

```c
// ❌ 方案 A: 真正的全局变量——必须加锁
int request_count;               // 所有线程争抢
pthread_mutex_t count_lock;      // 每次统计都加锁 → 性能灾难
void handle_request() {
    pthread_mutex_lock(&count_lock);
    request_count++;
    pthread_mutex_unlock(&count_lock);
}

// ❌ 方案 B: 函数局部变量——要层层传递
void handle_request() {
    int request_count = 0;       // 生命周期太短，每次函数调用就没了
    parse_header(&request_count);
    process_body(&request_count);
    // 每个被调函数都要加参数 → 接口污染
}

// ✅ 方案 C: TLS——全局写法、线程私有
__thread int request_count;      // 每个线程自动有一份
void handle_request() {
    request_count++;             // 无锁！和普通变量一样快
    // 任何被调函数都能直接用，无需传参
}
```

### 2. 硬件基础：FS 寄存器和段机制

#### 2.1 x86-64 遗留的段寄存器

64 位模式下，x86 的段机制大部分被废弃（`cs/ds/es/ss` 的基址被强制为 0，不分段了），但 **`fs` 和 `gs` 是例外**——它们的基址可以通过 MSR（Model Specific Register）或 GDT（Global Descriptor Table）编程设置成任意值。

```
 通用寻址:  [RIP + offset]        → 代码和数据，基址始终为 0
 FS 寻址:   [FS : offset]         → FS 基址 + offset = 线程 TLS 中的地址
 GS 寻址:   [GS : offset]         → GS 基址 + offset = per-CPU 内核数据
```

`FS.base` 和 `GS.base` 分别存在 `MSR_FS_BASE`（地址 0xC0000100）和 `MSR_GS_BASE`（0xC0000101）里。内核在上下文切换时读写这些 MSR。

#### 2.2 读写 FS 基址

```c
// 用户态读 FS 基址：arch_prctl
#include <asm/prctl.h>
#include <sys/prctl.h>

unsigned long fs_base;
int rc = arch_prctl(ARCH_GET_FS, &fs_base);
// fs_base 指向当前线程的 TLS 基址

// 设置 FS 基址（需要特权，通常只有内核和 libpthread 初始化用）
// arch_prctl(ARCH_SET_FS, &new_base);
```

```bash
# 用 gdb 直接看 fs_base
gdb -p $(pidof my_program)
(gdb) info register fs_base
# fs_base      0x7ffff7a00740   140737346832192
(gdb) x/16gx $fs_base
# 打印当前线程 TCB/TLS 区域的开头
```

#### 2.3 编译器生成什么指令

```c
__thread int tls_counter = 0;

void increment(void) {
    tls_counter++;
}
```

编译后（x86-64，IE 访问模型）：

```asm
increment:
    mov    %fs:0xfffffffffffffffc,%eax   # FS:偏移 → 读 tls_counter
    add    $0x1,%eax
    mov    %eax,%fs:0xfffffffffffffffc   # 写回
    retq
```

注意 `0xfffffffffffffffc` = -4，这是链接时计算的偏移量。**同一条指令，A 线程执行时 `fs.base` 指向 A 的 TLS，B 线程执行时指向 B 的 TLS——这就是 TLS 隔离的硬件本质。**

### 3. ELF TLS 四种访问模型详解

#### 3.1 为什么需要不同模型

TLS 变量在不同场景下的「可见性」不同：

| 场景 | 变量在哪 | 代码在哪 | 能否确定偏移 | 最佳模型 |
| :--- | :--- | :--- | :--- | :--- |
| 可执行文件访问自己的 TLS | 可执行文件 | 可执行文件 | ✅ 链接期绝对确定 | LE |
| 可执行文件访问动态库的 TLS | 动态库 | 可执行文件 | 动态库在启动时已加载 | IE |
| 动态库访问自己的 TLS | 本动态库 | 本动态库 | 共享库内的偏移确定 | LD |
| 动态库访问任意 TLS | 任何模块 | 动态库 | ❌ 运行时才知道 | GD |

#### 3.2 四种模型的代码生成

```c
// 假设 func() 访问 thread_local int x;
```

| 模型 | 生成的伪汇编 | 开销 | 适用场景 |
| :--- | :--- | :--- | :--- |
| **LE** | `mov %fs:OFFSET, %eax` | 1 次内存访问 | 主程序访问自己的 TLS |
| **IE** | `mov x@GOTTPOFF(%rip), %rax`; `mov %fs:(%rax), %eax` | 2 次内存访问 | 主程序访问启动时加载的 .so |
| **LD** | `lea x@TLSLDM(%rip), %rdi`; `call __tls_get_addr`; `mov OFFSET(%rax), %eax` | 函数调用 | .so 访问自己的 TLS |
| **GD** | `lea x@TLSGD(%rip), %rdi`; `call __tls_get_addr`; `mov (%rax), %eax` | 函数调用 | .so 访问任意 TLS（含 dlopen） |

#### 3.3 链接器松弛

编译器保守地生成 GD 模型。链接时，如果链接器发现变量和调用者在同一个模块，就把 GD **松弛（relax）** 成更高效的模型：

```
  编译:  .c → .o        →    总是 GD
  链接:  .o → exe/.so   →    松弛为 LE/IE/LD
```

查看 TLS 模型的实际使用：

```bash
# 看 .o 文件里标记了什么 TLS 重定位类型
readelf -r myfile.o | grep TLS

# 常见重定位类型:
# R_X86_64_TLSGD    → Global Dynamic
# R_X86_64_TLSLD    → Local Dynamic
# R_X86_64_GOTTPOFF → Initial Exec
# R_X86_64_TPOFF32  → Local Exec (最终链接后的绝对偏移)

# 看最终可执行文件
readelf -r myprogram | grep TLS
# 如果结果为空——说明链接器已松弛为 LE，全变成绝对偏移了
```

### 4. __tls_get_addr 和动态 TLS 分配

#### 4.1 函数的作用

`__tls_get_addr` 是 TLS 访问通用模型（GD/LD）背后的核心函数。它的签名：

```c
// tls_index: { module_id, offset }
typedef struct {
    unsigned long ti_module;  // 哪个 .so
    unsigned long ti_offset;  // 变量在该 .so 的 TLS 块中的偏移
} tls_index;

void *__tls_get_addr(tls_index *ti);
// 返回: 当前线程中，ti_module 模块的 TLS 块中 ti_offset 处的地址
```

#### 4.2 多级查找结构

```text
  线程 A                        线程 B
  TCB                          TCB
  │                            │
  ├─ DTV (Dynamic Thread      ├─ DTV
  │   Vector，指针数组)        │
  │   [0] ─→ 静态TLS块        │   [0] ─→ 静态TLS块
  │   [1] ─→ libA TLS ────┐   │   [1] ─→ libA TLS ────┐
  │   [2] ─→ libB TLS      │   │   [2] ─→ libB TLS      │
  │   [3] = NULL (未分配)  │   │   [3] ─→ libC TLS      │
  │                        │   │                        │
  └────────────────────────┘   └────────────────────────┘
                                    ↑ dlopen 加载 libC 时，ld.so
                                      为每个线程分配 libC 的 TLS 块
                                      并更新各线程的 DTV[3]
```

关键动态：当 `dlopen` 加载一个包含 TLS 变量的新 .so 时，`ld.so` 必须**遍历所有线程**，为每个线程的 DTV 追加新 .so 的 TLS 块。这就是 `dlopen` 加载 TLS-heavy 库开销大的原因之一。

### 5. TLS 与线程生命周期

#### 5.1 线程创建时的 TLS 初始化流程

```text
  pthread_create()
      │
      ▼
  clone(CLONE_VM|CLONE_FS|..., 新栈)
      │  syscall，内核创建 task_struct + 内核栈
      ▼
  返回到新线程的用户态
      │
      ▼
  libpthread 初始化:
  ├─ 分配 TCB + 静态 TLS 块
  ├─ 从 ELF TLS 段模板拷贝初始值
  ├─ 写 MSR_FS_BASE = TLS 区域地址
  ├─ 初始化 DTV 结构
  ├─ 调用 thread_local 变量构造函数 (C++)
  └─ 跳转到用户指定的 start_routine
```

#### 5.2 线程退出时的 TLS 清理流程

```text
  线程 start_routine 返回 / pthread_exit()
      │
      ▼
  libpthread 清理:
  ├─ 按构造逆序调用 thread_local 变量析构函数 (C++)
  ├─ 遍历 pthread_key 调 destructor
  │   └─ 如果 destructor 又 set 了值 → 重复调用
  │      (PTHREAD_DESTRUCTOR_ITERATIONS 次，通常 4)
  ├─ 释放动态 TLS 块
  └─ 回收线程栈
```

**常见坑**：`pthread_key` 的 destructor 如果每次执行后又调用 `pthread_setspecific` 重新设值，会导致无限循环。libpthread 有限次（通常是 4 次）重试保护，超过后放弃。

### 6. `__thread` vs `thread_local` vs `pthread_key` 完整对比

| 维度 | `__thread` (GCC) | `thread_local` (C++11) | `pthread_key` (POSIX) |
| :--- | :--- | :--- | :--- |
| 语言 | C / C++ | C++11+ | C / C++ |
| 类型限制 | 只能 POD + 编译期常量初始化 | 任何类型，支持动态初始化 | `void *`，需要自己管理类型 |
| 构造/析构 | ❌ 不支持 | ✅ 线程起止时自动调用 | ✅ destructor 回调 |
| 声明方式 | 编译期，带 `__thread` 关键字 | 编译期，带 `thread_local` 关键字 | 运行期，`pthread_key_create` |
| 访问方式 | 直接内存访问 | 直接内存访问 | `pthread_getspecific(key)` 函数调用 |
| 性能 | ~1-3 cycles | ~1-3 cycles | ~几十 ns |
| 数量限制 | 无硬限制（受 TLS 段大小影响） | 同左 | `PTHREAD_KEYS_MAX` (通常 1024) |
| 动态分配 | ❌ 编译期确定 | ❌ 编译期确定 | ✅ 运行期 `pthread_setspecific` |
| 跨 .so | 支持（需链接器配合） | 同左 | ✅ key 是进程全局的 |
| 推荐场景 | C 项目、性能敏感 | C++ 项目（首选） | 需要动态 key、需要析构回调 |

### 7. 手写一个 TLS 变量的最小系统调用级实现

下面的代码展示 `thread_local` 变量在用户态不需要任何系统调用——但**设置 FS 基址**本身是需要内核配合的（通过 `arch_prctl`，这是一个系统调用）。普通用户代码不应该这样做（libpthread 已经做了），但写一遍能真正理解原理：

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <sys/syscall.h>

// 模拟 TLS 变量: 在 TLS 区域的固定偏移 8 处放一个 int
#define TLS_VAR_OFFSET  8

// 新线程的启动函数
static int thread_func(void *arg) {
    // 此时 fs 已经被 libpthread 设置好指向本线程的 TLS
    // 直接通过 fs 读我们的'模拟 TLS 变量'
    int value;
    asm volatile("movl %%fs:%c1, %0"
                 : "=r"(value)
                 : "i"(TLS_VAR_OFFSET));
    printf("Thread %ld: TLS value = %d\n", syscall(SYS_gettid), value);

    // 修改它
    value = *(int *)arg;
    asm volatile("movl %0, %%fs:%c1"
                 :
                 : "r"(value), "i"(TLS_VAR_OFFSET));

    // 再读回来确认
    asm volatile("movl %%fs:%c1, %0"
                 : "=r"(value)
                 : "i"(TLS_VAR_OFFSET));
    printf("Thread %ld: TLS value now = %d\n", syscall(SYS_gettid), value);
    return 0;
}

// 更实际的演示: 直接用 __thread 让编译器生成 fs 相对寻址
__thread int tls_real_counter = 0;   // 编译器自动处理 fs 偏移

void *real_thread_func(void *arg) {
    int id = *(int *)arg;
    tls_real_counter = id * 100;     // 无锁、纯用户态写
    printf("Thread %d: tls_counter = %d\n", id, tls_real_counter);
    return NULL;
}

int main() {
    // 演示 __thread 的用法
    printf("=== __thread demo ===\n");
    tls_real_counter = 999;          // 主线程自己的副本
    printf("Main thread: tls_counter = %d\n", tls_real_counter);

    pthread_t t1, t2;
    int id1 = 1, id2 = 2;
    pthread_create(&t1, NULL, real_thread_func, &id1);
    pthread_create(&t2, NULL, real_thread_func, &id2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    // 主线程的值没变——隔离生效
    printf("Main thread again: tls_counter = %d (still 999?)\n",
           tls_real_counter);

    return 0;
}
```

编译运行：

```bash
gcc -O2 -pthread -o tls_demo tls_demo.c
./tls_demo
# === __thread demo ===
# Main thread: tls_counter = 999
# Thread 1: tls_counter = 100
# Thread 2: tls_counter = 200
# Main thread again: tls_counter = 999 (still 999?) → 999! 互不干扰
```

## 三、常见坑与面试加分点

| 坑 / 易错 | 详细说明 |
| :--- | :--- |
| `__thread` 变量用函数返回值初始化 | `__thread` 只能用编译期常量初始化。`__thread int x = rand();` 编译不过。用 `thread_local`（C++11）可以，它的动态初始化在线程创建时执行 |
| 在动态库中用 `__thread`，主程序 `dlopen` 加载 | 如果编译主程序时没链接这个 .so，TLS 变量访问模型可能退化为 GD——每次访问都要调 `__tls_get_addr`，性能骤降。解决办法：链接期注意重定位松弛，或尽量用 `-ftls-model=initial-exec` 显式指定 |
| `pthread_key` 的 destructor 设值后触发无限循环 | destructor 里不要继续 `pthread_setspecific` 写同一个 key。glibc 最多重试 `PTHREAD_DESTRUCTOR_ITERATIONS`（4）次，超过后直接放弃 |
| 在 signal handler 里用 `thread_local` | signal handler 运行在触发线程的上下文中，`thread_local` 变量是可以访问的（它是用户态内存）。但 handler 里别调用 `pthread_getspecific`——它不保证是 async-signal-safe |
| 假设 `thread_local` 变量的地址跨线程不变 | 每个线程都有自己的副本，地址当然不同。**不要把 TLS 变量的地址传到另一个线程去**，另一个线程解引用它就是在读自己的副本，逻辑完全错误 |
| fork 后子进程的 TLS | `fork` 只复制调用线程。子进程里只有**一个线程**（调用 fork 的那个的副本），其他线程全部消失——它们的 TLS 数据也随之消失。如果其他线程的 TLS 里有什么重要的未写回数据，直接丢失。所以多线程程序 `fork` 后子进程通常只做 `exec`，否则极易出诡异 bug |
| 误以为 TLS 变量不需要初始化 | `__thread` 变量的初始值存在 ELF 的 `.tdata`（有初值）或 `.tbss`（零初始化）段。如果声明 `__thread int x;` 没给初值，它在 `.tbss` 里，每个线程拿到的是 0。但这不是「自动初始化」——是 ELF 加载器把模板拷贝过去的 |
| 大量短命线程 + 大 TLS 变量 | 每个线程创建都 memcpy 整份 TLS 模板。如果每个线程 TLS 有 1MB，每秒创建 100 个线程，光 memcpy 就吃掉 100MB/s 内存带宽。大量短命线程 + 大 TLS → 用线程池或把大数据移到 pthread_key 按需分配 |

---

## 四、速记对照表

| 概念 | 一句话 |
| :--- | :--- |
| TLS | 全局/静态声明的变量，每个线程各有一份副本 |
| `thread_local` | C++11 标准关键字，支持非平凡类型 + 构造/析构 |
| `__thread` | GCC 扩展，仅 POD 类型 + 常量初始化 |
| `pthread_key` | POSIX 动态 TLS，`void *`，支持 destructor 回调 |
| fs 寄存器 | x86-64 用户态 TLS 基址寄存器 |
| tpidr_el0 | aarch64 TLS 基址寄存器 |
| `MSR_FS_BASE` | x86-64 存 fs 基址的 MSR，上下文切换时内核写它 |
| TCB | Thread Control Block，pthread 的线程控制块 |
| DTV | Dynamic Thread Vector，每个线程存各模块 TLS 块地址的数组 |
| `__tls_get_addr` | GD/LD 模型调用的运行时函数，返回当前线程某模块 TLS 变量地址 |
| LE (Local Exec) | 最快模型：一条 `mov %fs:OFFSET`，偏移在链接期确定 |
| IE (Initial Exec) | 次快：通过 GOT 间接取偏移，适用启动时已加载的 .so |
| LD (Local Dynamic) | 需要调 `__tls_get_addr`，但只查本模块，偏移可缓存 |
| GD (Global Dynamic) | 最通用也最慢，每次访问都调 `__tls_get_addr` |
| 链接器松弛 | 编译生成 GD → 链接时确定可见范围 → 降级为 IE/LE |
| `errno` | 最经典的 TLS 应用：`#define errno (*__errno_location())` |
| `.tdata` / `.tbss` | ELF 的 TLS 数据段：有初值/零初始化，是各线程 TLS 的模板 |

---

## 五、自测 10 题

1. 什么是 TLS？它解决了什么问题？和加锁全局变量、函数传参相比有什么优势？
2. x86-64 上 `fs` 寄存器在 TLS 中扮演什么角色？线程切换时 `fs` 基址怎么跟着变？
3. `__thread`、`thread_local`、`pthread_getspecific` 三种方式各有什么特点和适用场景？
4. `errno` 为什么是线程安全的？它是怎么实现的？
5. ELF TLS 有哪四种访问模型？GD（Global Dynamic）和 LE（Local Exec）的性能差距在哪？
6. `__tls_get_addr` 函数的作用是什么？DTV（Dynamic Thread Vector）干什么？
7. 线程创建和销毁时，TLS 变量分别经历了什么？初始值从哪来？
8. 多线程程序 `fork()` 后子进程的 TLS 变量是什么状态？有什么坑？
9. 为什么大量短命线程 + 大 `thread_local` 变量会有性能问题？
10. 怎么用 `readelf` 和 `gdb` 观察一个程序的 TLS 段和当前线程的 `fs` 基址？

<details>
<summary>参考答案</summary>

1. **TLS** 让全局/静态变量在每个线程里各有一份独立副本。vs 加锁全局变量：无锁、无竞争；vs 函数传参：不污染调用接口、任何被调函数都能直接访问。本质是用空间换时间——每线程多一份存储，省掉了同步开销。

2. **fs 寄存器**的基址（存于 `MSR_FS_BASE`）指向当前线程的 TLS 区域。编译器把 TLS 变量访问翻译成 `mov %fs:偏移, %reg`——同一条指令在不同线程读到不同值，靠的就是 `fs.base` 不同。线程切换时，内核 `context_switch` 从新线程的 `thread_struct` 里读取 TLS 基址，通过 `wrmsr` 写入 `MSR_FS_BASE`，和保存/恢复通用寄存器是同一个流程。

3. **对比**：`__thread`（GCC，C/C++，仅 POD+常量初始化，最快）；`thread_local`（C++11 标准，支持任何类型+构造/析构，同样快）；`pthread_getspecific`（POSIX，动态 key+`void *`+destructor 回调，有函数调用开销，key 数量有限制）。首选 `thread_local`（C++）或 `__thread`（C）；需要动态分配/析构回调时用 `pthread_key`。

4. **errno** 不是真正的全局变量，而是宏：`#define errno (*__errno_location())`。`__errno_location()` 返回当前线程自己 TLS 区域里 errno 的地址。线程 A 写 errno 和线程 B 读 errno 是两片物理上不同的内存。不仅是 errno——`strtok`→`strtok_r`、`localtime`→`localtime_r` 都是同样的「全局变局部」套路。

5. **四种模型**：LE（`mov %fs:OFFSET`，偏移链接期确定，最快）、IE（从 GOT 间接取偏移，适用启动时加载的 .so）、LD（调 `__tls_get_addr` 一次缓存模块基址，后续 + 固定偏移）、GD（每次调 `__tls_get_addr`，最通用最慢，支持 `dlopen`）。编译器默认 GD，链接器尽量松弛到 IE/LE。

6. **`__tls_get_addr(tls_index *ti)`** 是动态链接器提供的函数，输入 `(module_id, offset)` 对，返回当前线程里该模块 TLS 块中该偏移处的绝对地址。**DTV**（Dynamic Thread Vector）是每个线程的一个指针数组，`dtv[module_id]` 指向该线程为该模块分配的 TLS 块。第一次访问某模块时，`__tls_get_addr` 分配 TLS 块并挂到 DTV 上。

7. **创建时**：libpthread 分配 TCB + TLS 内存 → 从 ELF `.tdata`/`.tbss` 段模板 memcpy 初始值 → 设置 `fs.base` → 调用 C++ 构造函数。**销毁时**：调 C++ 析构（逆序）→ 调 pthread_key destructor → 释放 TLS 内存。初始值来自 ELF 的 TLS 段模板——每个线程的起点一致，之后各自修改。

8. **fork 只复制调用线程**——其他线程在子进程中直接消失，它们的 TLS 数据丢失。如果其他线程持有锁、有未刷盘的缓冲数据、有未完成的 IO，子进程会面对一个不完整的、不一致的状态。标准做法：多线程程序中 fork 后子进程立即 `exec`，不做任何其他操作。

9. 每个线程创建时，都要 memcpy **整个** TLS 模板（`.tdata` + `.tbss`）。如果 TLS 模板总大小 1MB，每秒创建 100 个短命线程，光是 memcpy TLS 就消耗 100MB/s 的内存带宽。加上 mmap/munmap 线程栈的开销，CPU 全花在线程生命周期管理上。解决：线程池复用、大缓冲放 pthread_key 按需分配、或用协程。

10. **readelf**：`readelf -S myprogram \| grep tls` 看 `.tdata`/`.tbss` 段；`readelf -r myprogram \| grep TLS` 看 TLS 重定位类型。**gdb**：`info register fs_base` 看当前线程的 fs 基址；`x/16gx $fs_base` 打印 TLS 区域开头；`info threads` + `thread N` 切线程后再看 $fs_base，会发现值变了。还可以 `p &tls_variable` 在不同线程里打印地址——各不相同。

</details>

---

> **延伸阅读**：TLS 是线程实现的一部分，线程创建（`clone`）、调度（CFS）、CPU 亲和性等完整内容见 [[04-多线程与Linux调度]]；C++ 同步原语（mutex/原子/内存序）见 cpp 库 `01-多线程与锁`；线程栈的虚拟内存布局细节见 [[03-Linux内存管理机制]]；`fork` 与多线程的交互坑见 [[02-进程管理与多进程编程]]。
