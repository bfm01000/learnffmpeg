# Linux 驱动与内核基础：面试速记与原理详解

> **适用方向**：Linux C/C++ 开发（物联网智能硬件方向），JD 标注「驱动开发/内核开发经验者优先」「物联网智能硬件项目者优先」  
> **难度**：🔥（**加分项，了解为主**）  
> **定位**：听得懂、能聊、了解全貌——能讲清「用户态/内核态边界 + 字符设备 file_operations + ioctl + 设备树是什么」就达标，**不必深到能独立写完整驱动**  
> **预计阅读**：速记 10 分钟｜全文 30 分钟  
> **关联文档**：[[06-文件IO与IO多路复用]]（fd、mmap）、[[03-Linux内存管理机制]]（内核/用户空间、内存映射）、[[10-工具链与调试]]（交叉编译、dmesg）、[[01-Linux系统编程全景导读]]（整体地图）

---

## 📌 第一部分：面试速记（考前 10 分钟扫一遍）

### 一句话核心

> **驱动是内核里替你操作硬件的一段代码，跑在特权级 ring0（内核态）；用户程序通过 `open/read/write/ioctl` 这套「文件接口」把请求送进内核，驱动在 `file_operations` 结构体里实现对应回调，真正去碰寄存器、内存和中断——用户态只能看见 `/dev/xxx` 这个设备节点，碰不到硬件本身。**

### 面试官常问问题 + 标准口语化回答

---

#### 开场题：用户态和内核态有什么区别？为什么驱动要跑内核态？

**🗣️ 面试标准回答：**

> "CPU 有特权级，x86 上常说 **ring0（内核态）和 ring3（用户态）**。内核态能执行特权指令、直接访问全部物理内存和硬件寄存器；用户态被硬件挡住，碰不到这些。
>
> 普通程序跑在用户态，想做 IO、分配内存、操作硬件，必须通过**系统调用**这个『过路口』陷入内核——`open/read/write` 底层都是 syscall，触发一次用户态到内核态的切换。
>
> 驱动之所以在内核态，是因为它要**直接读写设备寄存器、响应中断、管理 DMA**，这些都是特权操作；放用户态既没权限，也没法接中断。所以驱动是内核的一部分（或可加载的内核模块）。"

**👨‍💻 面试官追问：**

> Q: 那为什么不能把所有东西都放内核态，省掉切换开销？
> A: 内核态没有内存保护隔离，一个驱动写错指针就能**整机崩溃（kernel panic）**；用户态崩了只是单个进程挂掉。安全和稳定性换来了切换开销，这是分层的根本原因。

---

#### 必考题：内核为什么不能直接解引用用户态传进来的指针？

**🗣️ 面试标准回答：**

> "内核空间和用户空间是**两套地址空间**。用户程序传进来一个指针，它是**用户态虚拟地址**，可能没映射、可能是恶意的非法地址、也可能在缺页换出状态。内核如果直接 `*userPointer` 解引用，轻则读到垃圾，重则触发 oops 崩溃，还会有安全漏洞（用户骗内核去读写内核内存）。
>
> 所以内核提供 **`copy_from_user(kernelBuffer, userPointer, length)`** 和 **`copy_to_user(userPointer, kernelBuffer, length)`** 这两个函数：它们会**校验地址合法性、处理缺页**，安全地在两个空间之间搬数据。驱动里 `read/write` 回调几乎一定用到它们。"

**👨‍💻 面试官追问：**

> Q: 这两个函数返回什么？
> A: 返回**未能拷贝的字节数**，0 表示全部成功。非 0 时驱动通常返回 `-EFAULT`。

---

#### 高频题：Linux 设备分三类，分别是什么？

**🗣️ 面试标准回答：**

> "经典分三类：
>
> 1. **字符设备**：按字节流顺序读写、不带缓冲块概念，像串口、摄像头、键盘、`/dev/ttyS0`。**这是面试和嵌入式最常碰的一类。**
> 2. **块设备**：以固定大小块（如 512B/4K）随机访问，走内核块层和缓冲，像磁盘、SD 卡、`/dev/sda`。
> 3. **网络设备**：不走 `/dev` 节点，而是通过 `socket` + 网络协议栈访问，像 `eth0`、`wlan0`。
>
> 物联网智能硬件里，传感器、串口、摄像头基本都是**字符设备**，所以重点掌握字符设备驱动模型就够用了。"

---

#### 高频题：/dev、/proc、/sys 这三个目录分别是干嘛的？

**🗣️ 面试标准回答：**

> "都是**伪文件系统**——不是真磁盘文件，是内核把信息『伪装成文件』暴露出来，方便用户态用普通文件接口读写：
>
> - **`/dev`（devtmpfs）**：设备节点。每个 `/dev/xxx` 对应一个设备，记录主次设备号，`open` 它就连到对应驱动。
> - **`/proc`（procfs）**：进程和内核运行信息。`/proc/<pid>/` 看某进程的状态、内存映射、打开的 fd；`/proc/cpuinfo`、`/proc/meminfo` 看系统信息。
> - **`/sys`（sysfs）**：设备和驱动的**结构化属性**，按设备模型组织成树。比如 `/sys/class/gpio/`、`/sys/bus/i2c/`，可以 `cat`/`echo` 读写设备属性。
>
> 再加一个 **udev**：用户态守护进程，监听内核的设备热插拔事件，**动态创建/删除 `/dev` 下的节点**（早期是静态 `mknod`，现在插上 U 盘自动出现 `/dev/sdb` 就是 udev 干的）。"

---

#### 重点题：用户态 open("/dev/xxx") 到底怎么走到驱动里的？

**🗣️ 面试标准回答：**

> "核心是 **`file_operations` 结构体**——一张函数指针表，驱动把自己的 `open/read/write/release/ioctl/mmap` 实现填进去注册给内核。
>
> 路径是这样：每个设备节点有**主设备号（major）**和**次设备号（minor）**，主设备号标识『哪个驱动』，次设备号标识『同一驱动管的第几个设备』。驱动初始化时用 `register_chrdev` 或 `cdev_add` 把『主设备号 → file_operations』登记进内核的字符设备表。
>
> 用户态 `open("/dev/xxx")` 时，内核从节点拿到主设备号，查表找到对应的 `file_operations`，之后这个 fd 上的每次 `read/write/ioctl` 都会被**分发到驱动填的那个回调函数**。所以用户态的文件操作和驱动的 ops 是一一对应的。"

**👨‍💻 面试官追问：**

> Q: 主次设备号在哪看？
> A: `ls -l /dev/`，字符设备行首是 `c`，权限后面那两个数字就是 `major, minor`（如 `ttyS0` 常是 `4, 64`）。

---

#### 重点题：ioctl 是什么？为什么 read/write 之外还要它？

**🗣️ 面试标准回答：**

> "`read/write` 只能表达『读一段数据、写一段数据』，但设备还有大量**控制类操作**没法用读写表达：设置串口波特率、查询摄像头支持的分辨率、复位设备、切换工作模式……这些既不是读数据也不是写数据，是**带命令字的控制**。
>
> `ioctl(fd, cmd, arg)` 就是为此设计的：`cmd` 是一个**命令编号**（约定好的常量），`arg` 通常是指向参数结构体的指针。内核把它分发到驱动的 `unlocked_ioctl` 回调，驱动用 `switch(cmd)` 根据命令字执行不同逻辑，再用 `copy_to_user/copy_from_user` 和用户交换参数。
>
> 音视频里这个特别典型：**摄像头 V4L2（Video4Linux2）几乎全靠 ioctl** 配置——`VIDIOC_QUERYCAP` 查能力、`VIDIOC_S_FMT` 设格式分辨率、`VIDIOC_REQBUFS` 申请缓冲、`VIDIOC_QBUF/DQBUF` 入队出队帧。所以做摄像头采集本质就是在和一堆 ioctl 打交道。"

---

#### 了解题：内核模块怎么加载？hello 模块长什么样？

**🗣️ 面试标准回答：**

> "内核模块（`.ko`）是**可动态加载到运行中内核**的代码，不用重新编译整个内核或重启。常用命令：
>
> - `insmod xxx.ko` 加载，`rmmod xxx` 卸载，`lsmod` 列出已加载模块，`modprobe xxx` 加载并**自动处理依赖**。
>
> 代码上两个关键点：`module_init(入口函数)` 注册加载时调用的初始化函数，`module_exit(出口函数)` 注册卸载时的清理函数。模块里不能用 `printf`，要用 **`printk`**，日志通过 **`dmesg`** 查看。编译要借**内核构建系统（Kbuild）**，Makefile 指向内核源码树。
>
> 面试能说清『module_init/module_exit + printk + dmesg + insmod/rmmod』这套就够了。"

---

#### 了解题：中断处理为什么分上半部和下半部？

**🗣️ 面试标准回答：**

> "硬件触发中断时，CPU 会**打断当前工作**去执行中断处理函数，期间往往关着中断、不能睡眠。如果处理太久，会丢失后续中断、拖垮系统响应。所以拆成两半：
>
> - **上半部（top half / 硬中断）**：越快越好。只做最紧急的事——应答硬件、把数据从寄存器搬到内存、标记『有活要干』，然后立刻返回。
> - **下半部（bottom half）**：把耗时的后续处理推迟到稍后、在开中断的环境里做。常见机制有 **softirq、tasklet、workqueue**（workqueue 跑在内核线程里，**可以睡眠**，能做阻塞操作）。
>
> 一句话：**上半部抢时间、下半部干重活**，目的是让中断尽快返回、不丢中断。"

---

#### 物联网重点题：设备树（Device Tree）是什么？为什么需要它？

**🗣️ 面试标准回答：**

> "设备树是一种**描述硬件的数据结构**，主要用在 **ARM 等嵌入式平台**。过去 ARM 内核里把『这块板子有哪些设备、寄存器地址多少、用哪个中断号』全**硬编码**进内核 C 代码，每出一块新板子就要改内核、重新编译，非常臃肿。
>
> 设备树把这些**硬件描述从内核代码里抽出来**，写成独立的 **`.dts`（源文件，文本）**，编译成 **`.dtb`（二进制）**，由 bootloader 传给内核。内核启动时解析 dtb，就知道板子上有什么硬件、地址和中断怎么配。
>
> 结构上是**节点 + 属性**的树：每个设备是一个节点，`reg` 写寄存器地址范围、`interrupts` 写中断号、`compatible` 写和哪个驱动匹配。**换板子只改设备树、不动驱动和内核**，这是它最大的价值。物联网里换主控、换外设很频繁，所以设备树是嵌入式 Linux 的核心概念。"

**👨‍💻 面试官追问：**

> Q: compatible 字段干嘛的？
> A: 它是**设备和驱动配对的钥匙**——驱动声明自己支持哪些 compatible 字符串，内核拿设备树节点的 compatible 去匹配，匹配上就调用该驱动的 probe 函数。

---

#### 物联网了解题：GPIO/UART/I2C/SPI/CAN 这些接口大概知道吗？

**🗣️ 面试标准回答：**

> "都是嵌入式常见的硬件接口，了解定位即可：
>
> - **GPIO**：通用输入输出引脚，最简单，控制 LED、读按键、拉高拉低电平。
> - **UART（串口）**：异步串行，两根线收发（TX/RX），调试串口、模组通信常用。用户态可直接用 **termios** 配置波特率读写 `/dev/ttySx`。
> - **I2C**：两线（SCL 时钟 + SDA 数据）半双工总线，一主多从、靠地址寻址，接传感器、EEPROM 这类低速器件。
> - **SPI**：四线全双工（时钟/MOSI/MISO/片选），比 I2C 快，接屏幕、Flash。
> - **CAN**：抗干扰的差分总线，**车载和工业**用得多。
>
> 记忆：**GPIO 控引脚、UART 串口调试、I2C 慢速多设备、SPI 快速点对点、CAN 车载工业。**"

---

#### 进阶了解题：mmap 在驱动里有什么用？和零拷贝有什么关系？

**🗣️ 面试标准回答：**

> "`mmap` 能把**设备的内存/缓冲区直接映射到用户态地址空间**，用户程序拿到指针后像访问普通内存一样读写，**不用每帧都 `read` 拷一遍**——省掉了内核态到用户态的数据拷贝，这就是『零拷贝』。
>
> 最典型还是摄像头：V4L2 的 `MMAP` 模式下，驱动分配采集缓冲，用户态 `mmap` 映射过来，硬件 DMA 把图像直接写进这块内存，用户态映射地址上就能直接拿到帧数据，全程不复制。高分辨率高帧率下，省下的拷贝开销非常可观。这部分内存映射机制见 [[03-Linux内存管理机制]]，fd 与 mmap 的用户态用法见 [[06-文件IO与IO多路复用]]。"

---

#### 了解题：听说过用户态驱动吗？UIO、DPDK？

**🗣️ 面试标准回答：**

> "是一种趋势：把部分驱动逻辑**搬到用户态**，避免频繁陷内核、也更好开发调试。
>
> - **UIO（Userspace I/O）**：内核留一个极薄的壳处理中断和内存映射，把寄存器空间通过 mmap 暴露给用户态，主体逻辑在用户程序里写。
> - **DPDK**：高性能网络场景的代表，**绕过内核协议栈**，网卡数据直接到用户态轮询处理，省掉中断和拷贝，用于超高吞吐转发。
>
> 了解到『有用户态驱动这条路线，用空间换性能/易开发』即可，物联网普通岗位一般不深究。"

---

#### 收尾题：想做嵌入式驱动开发，交叉编译怎么搞？

**🗣️ 面试标准回答：**

> "开发机（x86）上编译出能在目标板（ARM）跑的模块，就要**交叉编译**：用 ARM 工具链（如 `arm-linux-gnueabihf-gcc`），编译内核模块时还要指定**目标板对应的内核源码树**和 `ARCH=arm`、`CROSS_COMPILE=arm-linux-gnueabihf-`。编出来的 `.ko` 拷到板子上 `insmod`。
>
> 关键是**模块的内核版本要和目标板运行的内核完全一致**，否则加载会报版本不匹配。交叉编译工具链、sysroot 这些细节见 [[10-工具链与调试]]。"

---

## 二、原理详解

### 1. 用户态 / 内核态的边界

```text
        用户态（ring3）                          内核态（ring0）
  ┌────────────────────────┐            ┌──────────────────────────────┐
  │  应用程序               │            │  内核 + 驱动                  │
  │  open/read/write/ioctl  │  syscall   │  虚拟文件系统(VFS)             │
  │  (libc 封装)            │ ─────────► │     │                         │
  │                         │  陷入内核   │     ▼                         │
  │  只能访问自己的         │            │  file_operations 回调         │
  │  用户虚拟地址空间       │ ◄───────── │     │                         │
  └────────────────────────┘  返回结果   │     ▼                         │
            ▲                            │  直接操作硬件寄存器/中断/DMA  │
            │                            └──────────────────────────────┘
   碰不到硬件，碰不到内核内存                 特权级，能碰一切，但崩了就整机挂
```

要点：**系统调用是唯一合法的『过路口』**；数据跨越边界必须经 `copy_to_user/copy_from_user`。

### 2. open("/dev/xxx") 的分发链路

```text
  用户态                内核                          驱动模块
 open("/dev/mychar")
      │  syscall
      ▼
   VFS 拿到节点的 major/minor
      │
      ▼  按 major 查字符设备表
   找到注册的 file_operations ───────────►  .open    = my_open
      │                                     .read    = my_read
   返回 fd                                  .write   = my_write
      │                                     .unlocked_ioctl = my_ioctl
 read(fd, ...) ──── 按 fd 找到 ops ───────►  my_read 被调用
                                            └─ copy_to_user 把数据给用户
```

#### 补充：VFS（虚拟文件系统）是什么？

上图中「VFS 拿到节点的 major/minor」这一步经常被一笔带过，但它才是 `open` 能同时打开磁盘文件、设备节点、pipe、socket 的根本原因。

**一句话定义**：VFS（Virtual File System，虚拟文件系统）是 Linux 内核里的一层**抽象层**，它定义了一套统一的「文件操作接口」，所有文件系统（ext4：Linux 最常用的磁盘文件系统、xfs：高性能 64 位日志文件系统、btrfs：新一代写时复制文件系统、procfs：进程与内核信息伪文件系统、sysfs：设备与驱动属性树伪文件系统、devtmpfs：设备节点内存文件系统……）和所有设备驱动都按这个接口实现自己的操作——上层用户程序看到的只是一个 fd，不关心底层到底是磁盘上的普通文件、还是 `/dev` 下的设备节点、还是 `/proc` 里的伪文件。

**VFS 的四个核心对象**：

| 对象 | 内核结构体 | 存什么 | 生命周期 |
| :--- | :--- | :--- | :--- |
| **super_block** | `struct super_block` | 已挂载文件系统的元信息（类型、块大小、根目录 dentry） | 挂载时创建，卸载时销毁 |
| **inode** | `struct inode` | 一个文件/目录/设备节点的**静态**元数据：权限、大小、主次设备号、`i_fop`（指向 file_operations） | 文件存在就存在（可能在磁盘上，也可能在内存缓存中） |
| **dentry** | `struct dentry` | 目录项缓存：把路径名（如 `"/dev/mychar"`）和 inode 关联起来 | 内核为加速路径查找做了 dentry cache（dcache），不用的 dentry 会被回收 |
| **file** | `struct file` | 一个**已打开文件**的实例：当前读写位置（f_pos）、打开标志（O_RDONLY/O_WRONLY）、指向 inode 和 file_operations 的指针 | `open` 时创建，`close` 时销毁 |

**`open("/dev/mychar")` 在 VFS 层的完整路径**：

下面把 `open("/dev/mychar", O_RDWR)` 从用户代码到拿到 fd 的每一步拆开来讲。整体分两个阶段：**第一阶段是把路径字符串变成一个 inode**（路径查找），**第二阶段是拿 inode 创建 file 结构体并分配 fd**。

---

**用现实类比理解整个过程**：

你去政府办事大厅办业务。大厅里有一个统一的「综合窗口」（VFS），你告诉它你要办什么、找哪个部门。综合窗口内部做这几件事：先查黄页（路径查找）确认你要找的部门确实存在、而且你有权限进；然后给你一张叫号票，票上写了你的编号（fd）和你要去的柜台（file，含 f_op）；之后不管你是去税务窗口（ext4）、社保窗口（xfs）、还是公安窗口（设备驱动），你都凭叫号票去对应的柜台——柜台后面的办事员（驱动或文件系统的 open/read/write 回调）具体办理你的业务。你全程只跟综合窗口打交道，不关心柜台后面是谁。

---

**第一阶段：路径查找——把 `"/dev/mychar"` 这个字符串变成一个 inode**

你要打开一个文件，传给内核的是一个路径字符串 `"/dev/mychar"`。内核面对的是一片磁盘（或内存文件系统）上的目录树，它需要把这个字符串翻译成「这个文件到底是谁」——也就是找到它对应的 inode。这个过程叫**路径遍历（path walking）**。

以下是逐步拆解：

**Step 0：从哪里出发？**

每个进程的 `task_struct` 里都有一个 `fs` 字段（`struct fs_struct`），里面存了两个关键路径：

| 字段 | 含义 | 示例 |
| :--- | :--- | :--- |
| `fs→root` | 当前进程的**根目录**（通常是 `/`；如果进程被 `chroot` 了，根目录可能是 `/some/jail`） | dentry of `/` |
| `fs→pwd` | 当前进程的**工作目录**（`cd` 改的就是这个） | dentry of `/home/user` |

路径 `"/dev/mychar"` 以 `/` 开头，是**绝对路径**，所以内核从 `fs→root` 指向的 dentry 出发。如果路径是 `"mychar"`（相对路径），内核从 `fs→pwd` 出发。本例从根目录 `/` 开始。

**Step 1：解析 `"/"` —— 找到根目录的 dentry**

内核已经知道根目录的 dentry（`fs→root` 直接指着它），不需要再查找。dentry（目录项）是内核在内存里缓存的一个结构体，核心字段：

```text
  dentry of "/"
  ├─ d_name       = "/"                    ← 名字
  ├─ d_inode      → inode of "/"           ← 指向对应的 inode
  ├─ d_parent     → 指向自己的 dentry       ← 根目录的 parent 就是自己
  ├─ d_children   → {dentry of "dev", dentry of "home", dentry of "proc", ...}
  │                 └─ 哈希表（按目录内的文件名做 key，O(1) 查找）
  └─ d_op         → dentry_operations      ← 这个文件系统自己实现的 dentry 操作
```

根目录是一个目录（inode→i_mode 标记为 `S_IFDIR`），既然是目录，它就有「子项」——`/dev`、`/home`、`/proc` 等等。这些子项在内核内存里各自对应一个 dentry，挂在父 dentry 的 `d_children` 哈希表里。

**Step 2：解析 `"dev"` —— 在 `"/"` 的 children 里找名为 `"dev"` 的子项**

内核拿到根目录的 dentry 后，需要找它的子目录 `"dev"`。这里就是 **dcache（目录项缓存）起作用的地方**：

- 每个 dentry 的 `d_children` 是一个**哈希表**，key 是**文件/目录名**
- 内核对字符串 `"dev"` 算一个哈希值，直接去 `d_children` 哈希表里查对应的桶
- 如果找到了（cache hit），直接拿到 dentry of `"dev"`——这是**最快路径**，纯内存操作
- 如果没找到（cache miss）——说明这个路径是第一次被人访问，或者之前被回收了——内核就要**真的去读磁盘**（或调用文件系统的 lookup 回调）。对于 devtmpfs（`/dev` 使用的内存文件系统），不是读磁盘，而是调用 devtmpfs 的 lookup 函数，在内存里创建 dentry 和 inode，再挂到哈希表里

**dcache 为什么叫「加速」**：如果没有 dcache，每次访问 `/dev/mychar` 都要从磁盘或文件系统驱动一层层查——先打开 `/` 目录读它的内容找 `dev`，再打开 `/dev` 目录读它的内容找 `mychar`。每次路径遍历都要多次 I/O。dcache 把已经访问过的 dentry 缓存在内存里，后面再来直接哈希查找，省掉了所有重复的目录读取。这就是「加速」的含义。

**Step 3：解析 `"mychar"` —— 在 `"dev"` 的 children 里找名为 `"mychar"` 的子项**

和上一步完全一样的逻辑：内核拿到 dentry of `"/dev"`，在它的 `d_children` 哈希表里查 `"mychar"`。找到了 → 拿到 dentry of `"/dev/mychar"`。

**Step 4：从 dentry 拿到 inode**

路径走完了，内核拿到了最终文件的 dentry。dentry 只是一个「名字 → inode」的映射关系，真正的文件信息在 inode 里：

```text
  dentry of "/dev/mychar"
  └─ d_inode → inode of "/dev/mychar"
                ├─ i_ino      = 12345        ← inode 编号（内存文件系统里随便分配）
                ├─ i_mode     = S_IFCHR | 0666  ← 文件类型=字符设备, 权限=rw-rw-rw-
                ├─ i_rdev     = MKDEV(4, 64) ← 主设备号=4, 次设备号=64（这就是 ttyS0 的设备号）
                ├─ i_fop      = def_chr_fops ← 字符设备的默认 file_operations
                ├─ i_uid/i_gid= 0/0          ← root:root
                └─ i_atime/i_mtime/...       ← 时间戳
```

`i_mode` 里的 `S_IFCHR` 告诉内核这是**字符设备**，`i_rdev` 存的就是主设备号（major）和次设备号（minor）——major 标识「哪个驱动」，minor 标识「同驱动下的第几个设备」。

`i_fop` 此时还是 devtmpfs 给的**默认值** `def_chr_fops`，它只是一个占位的——里面的 open 函数会触发后续的真正驱动查找。往下看。

---

**第二阶段：创建 file 结构体并分配 fd**

**Step 5：分配一个空的 file 结构体**

内核从全局的 file 缓存池（`filp_cache`，一个 slab 缓存）里拿一个空闲的 `struct file`，或者缓存空了就新分配一页内存。初始化核心字段：

```c
struct file *f = alloc_empty_file(O_RDWR, 0);  // 概念示意
f->f_inode = inode;          // 指向刚才找到的 inode
f->f_op    = inode->i_fop;   // 此时是 def_chr_fops（占位）
f->f_pos   = 0;              // 文件读写位置从 0 开始
f->f_flags = O_RDWR;         // 用户传的打开标志
f->f_mode  = FMODE_READ | FMODE_WRITE;
```

**Step 6：（关键！）按 major 查字符设备表，替换 f_op**

`f_op` 现在是占位的 `def_chr_fops`，指向的 open 实现其实是 `chrdev_open()`。内核调用 `f_op→open(inode, f)`——注意这不是驱动里的 my_open，这是 VFS 的字符设备通用 open：

```c
// fs/char_dev.c 里的 chrdev_open() 简化逻辑
static int chrdev_open(struct inode *inode, struct file *filp) {
    // 从 inode 取出主设备号
    int major = MAJOR(inode->i_rdev);   // 比如 major = 4

    // 在 cdev_map（一个哈希表，按主设备号索引）里查找
    // 驱动用 register_chrdev / cdev_add 时就是往这表里注册的
    struct cdev *cdev = cdev_map_lookup(major);

    // cdev→ops 就是驱动填的 file_operations（my_open/my_read/my_write/...）
    // 把占位的 def_chr_fops 替换成真正的驱动 ops
    filp->f_op = cdev->ops;

    // 再次调用 f_op→open，这次就是驱动里的 my_open 了！
    if (filp->f_op->open)
        return filp->f_op->open(inode, filp);  // → my_open()
}
```

这就解释了为什么 `open` 能被正确分发到驱动——关键动作就是**从 `cdev_map` 里用 major 查表，把 file 的 f_op 从占位值替换为驱动注册的真正的 ops**。替换完成后，之后的 `read/write/ioctl` 就会直接走到驱动的回调，不再经过 `chrdev_open`。

**Step 7：在当前进程的 fd 表里找一个最小的空闲位置**

每个进程的 `task_struct` 里有一个 `files` 字段（`struct files_struct`），里面有一个**fd 数组**（`fdt`，file descriptor table）。内核从 0 开始扫描这个数组，找到第一个空位（指针为 NULL 的位置）：

```text
  进程的 fd 表 (current→files→fdt)
  ┌───┬───┬───┬───┬───┬───┬───┐
  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │...│
  ├───┼───┼───┼───┼───┼───┼───┤
  │std│std│std│NUL│NUL│NUL│   │
  │in │out│err│ L │ L │ L │   │
  └───┴───┴───┴───┴───┴───┴───┘
               ↑
          fd=3 是第一个空位 → 返回 3
```

```c
// 简化逻辑
int fd = find_next_zero_bit(fdt->open_fds, 0);  // 找最小空位
fdt->fd[fd] = filp;                              // 把 file 挂到 fd=3 的位置
```

**Step 8：返回 fd 给用户态**

系统调用返回，fd=3 被写入 `RAX` 寄存器，glibc 把 `RAX` 的值返回给调用者。用户代码拿到 `int fd = 3`，之后 `read(3, buf, 100)` 时，内核反向操作：从 `current→files→fdt[3]` 拿到 `struct file` → 拿到 `f_op` → 调 `f_op→read` → 驱动里的 `my_read`。

---

**完整流程总览（每一步的输入/输出）**：

```text
  Step 0:  输入: 当前进程 current→fs→root (dentry of "/")
  Step 1:  输入: dentry of "/"  → 它本身就是根目录, 跳过查找
  Step 2:  输入: dentry of "/" + 名字 "dev"
           操作: d_children 哈希表查 "dev"
           输出: dentry of "/dev" (hit) 或调文件系统 lookup 创建 (miss)
  Step 3:  输入: dentry of "/dev" + 名字 "mychar"
           操作: d_children 哈希表查 "mychar"
           输出: dentry of "/dev/mychar"
  Step 4:  输入: dentry of "/dev/mychar"→d_inode
           输出: inode (含 i_mode=S_IFCHR, i_rdev=设备号, i_fop=def_chr_fops)
  Step 5:  输入: inode
           操作: 从 slab 缓存 alloc 一个 struct file, 填充 f_inode/f_pos/f_flags
           输出: struct file (f_op 暂时 = def_chr_fops)
  Step 6:  输入: struct file + inode→i_rdev
           操作: MAJOR(i_rdev) → 查 cdev_map → 找到驱动注册的 cdev
           操作: filp→f_op = cdev→ops (替换为真正的驱动 ops)
           操作: 调用 f_op→open(inode, filp) → my_open()
  Step 7:  输入: struct file
           操作: 扫描 current→files→fdt[] 找最小空位
           输出: fd = 3
  Step 8:  输入: fd = 3
           操作: fdt[3] = filp, 返回 3 给用户态
           输出: 用户代码拿到 int fd = 3
```

**关键理解：**

- **同一个 inode，多次 open → 多个 file**。每个 file 有自己的 `f_pos`（读写位置），所以两个进程可以同时打开同一个文件、各自独立移动读写指针，互不干扰。
- **设备节点的 inode 不在磁盘上**——`/dev` 是 devtmpfs（一个内存文件系统），inode 在内存里动态创建，`i_rdev` 存设备号，`i_fop` 初始为占位 `def_chr_fops`，Step 6 的 `chrdev_open` 才替换为真正驱动的 ops。
- **为什么 f_op 可以先占位、后替换**：因为 `/dev` 下的每个设备节点都有一个不同的 major，对应不同的驱动不同的 f_op。devtmpfs 不知道每个节点该用什么 ops——它只管创建节点（绑定 major/minor），真正分发到驱动是 VFS 在 `open` 时才做的。这就像大厅综合窗口给你叫号票时，票上先写「请去 X 号柜台」，等你真走到柜台前，才发现 X 号柜台后面坐着的是税务还是社保——关键是柜台号（major）对就行。
- **一切皆文件的本质就是 VFS**：用户看到的是统一的 `open/read/write/close`，VFS 负责把这些调用**多路分发**到不同底层实现——磁盘文件走文件系统的读写函数，设备节点走驱动的 fops，socket 走网络协议栈的 ops，`/proc` 文件走 procfs 的回调。用户和应用程序代码不需要知道这些差异。

**VFS + 设备驱动 + 普通文件系统的关系图**：

```text
               用户态
    ┌───────────┼───────────┐
    │           │           │
  open()     read()     write()
    │           │           │
    ▼           ▼           ▼
  ┌──────────────────────────────┐
  │        VFS 抽象层             │
  │  struct file / inode / dentry │
  │  统一的 f_op 函数指针调用      │
  └───┬────────┬────────┬────────┘
      │        │        │
      ▼        ▼        ▼
  ┌──────┐ ┌──────┐ ┌──────────┐
  │ ext4 │ │ xfs  │ │ 设备驱动  │    ← 具体实现层
  │f_op  │ │f_op  │ │ (cdev)   │
  │      │ │      │ │ f_op     │
  └──┬───┘ └──┬───┘ └────┬─────┘
     │        │          │
     ▼        ▼          ▼
   磁盘     磁盘      硬件寄存器
```

**面试能说出来的一句话**：

> "VFS 是内核的文件操作抽象层，定义 `file/inode/dentry/super_block` 四个核心对象和统一的 `file_operations` 接口。`open` 一条路径背后是两阶段：先路径遍历（dcache 哈希逐级查找 dentry → inode），再创建 file 结构体（alloc file → chrdev_open 查 cdev_map 替换 f_op → 调驱动 open → 分配 fd）。之后同一条 `read(fd)`，内核从 fd 表拿到 file，走 f_op→read 分发——不管是磁盘文件还是设备节点还是 socket，用户代码完全不用改。这就是 Linux『一切皆文件』的工程实现。"

### 3. 主设备号 / 次设备号

| 概念 | 作用 | 举例 |
|---|---|---|
| 主设备号 majorNumber | 标识「哪个驱动」 | 串口驱动占某个 major |
| 次设备号 minorNumber | 标识「同驱动下第几个设备」 | ttyS0、ttyS1 同 major 不同 minor |
| 设备节点 /dev/xxx | 用户态入口，绑定 major+minor | `ls -l` 看类型 c/b 和号码 |

### 4. 字符设备驱动骨架（C，内核模块）

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>   // copy_to_user / copy_from_user

static int majorNumber;                      // 主设备号
static char deviceBuffer[128];               // 设备内部缓冲（示意）

// 用户 open("/dev/xxx") 时触发
static int my_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "mychar: open\n");      // 日志用 dmesg 看
    return 0;
}

// 用户 read 时触发：把内核数据拷给用户
static ssize_t my_read(struct file *filp, char __user *userBuffer,
                       size_t length, loff_t *offset) {
    if (length > sizeof(deviceBuffer))
        length = sizeof(deviceBuffer);
    // 不能直接写 userBuffer，必须经 copy_to_user 校验+搬运
    if (copy_to_user(userBuffer, deviceBuffer, length))
        return -EFAULT;                      // 拷贝失败返回错误
    return length;                           // 返回实际读取字节数
}

// 用户 write 时触发：把用户数据拷进内核
static ssize_t my_write(struct file *filp, const char __user *userBuffer,
                        size_t length, loff_t *offset) {
    if (length > sizeof(deviceBuffer))
        length = sizeof(deviceBuffer);
    if (copy_from_user(deviceBuffer, userBuffer, length))
        return -EFAULT;
    return length;
}

static int my_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "mychar: close\n");
    return 0;
}

// 函数指针表：把用户态文件操作映射到驱动实现
static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .read    = my_read,
    .write   = my_write,
    .release = my_release,
};
```

### 5. 模块注册与最简 hello 模块

```c
// 加载时调用：注册字符设备
static int __init my_init(void) {
    // 注册字符设备，0 表示让内核自动分配主设备号
    majorNumber = register_chrdev(0, "mychar", &fops);
    if (majorNumber < 0)
        return majorNumber;                  // 返回负值表示加载失败
    printk(KERN_INFO "mychar: registered, major=%d\n", majorNumber);
    return 0;
}

// 卸载时调用：清理资源，否则下次 insmod 会冲突
static void __exit my_exit(void) {
    unregister_chrdev(majorNumber, "mychar");
    printk(KERN_INFO "mychar: unregistered\n");
}

module_init(my_init);                        // 声明加载入口
module_exit(my_exit);                        // 声明卸载入口
MODULE_LICENSE("GPL");                       // 不写会污染内核、部分符号不可用
MODULE_DESCRIPTION("最简字符设备示例");
```

配套 **Makefile**（借内核构建系统 Kbuild）：

```makefile
obj-m += mychar.o                            # 要编成模块的目标
KDIR := /lib/modules/$(shell uname -r)/build # 内核源码树（交叉编译时换成目标板内核）

all:
	make -C $(KDIR) M=$(PWD) modules         # 进内核树用其规则编译本目录模块
clean:
	make -C $(KDIR) M=$(PWD) clean
```

加载验证流程：

```bash
make                 # 生成 mychar.ko
sudo insmod mychar.ko
lsmod | grep mychar  # 确认已加载
dmesg | tail         # 看 printk 输出的注册日志
sudo rmmod mychar    # 卸载
```

### 6. ioctl：read/write 之外的控制通道

```c
#include <linux/ioctl.h>

// 用宏构造命令字：含「方向/幻数/序号/参数大小」，避免命令冲突
#define MYDEV_MAGIC 'k'
#define MYDEV_RESET      _IO(MYDEV_MAGIC, 0)               // 无参
#define MYDEV_SET_SPEED  _IOW(MYDEV_MAGIC, 1, int)         // 用户→内核写参
#define MYDEV_GET_STATE  _IOR(MYDEV_MAGIC, 2, int)         // 内核→用户读参

static long my_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int speedValue;
    switch (cmd) {                           // 按命令字分发
    case MYDEV_RESET:
        printk(KERN_INFO "mychar: reset\n");
        break;
    case MYDEV_SET_SPEED:                     // arg 是用户态指针
        if (copy_from_user(&speedValue, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        printk(KERN_INFO "mychar: set speed=%d\n", speedValue);
        break;
    default:
        return -ENOTTY;                       // 不认识的命令
    }
    return 0;
}
// 别忘了在 file_operations 里挂上： .unlocked_ioctl = my_ioctl
```

用户态调用：

```c
int fd = open("/dev/mychar", O_RDWR);
int speed = 115200;
ioctl(fd, MYDEV_SET_SPEED, &speed);          // cmd + 参数指针
ioctl(fd, MYDEV_RESET);
```

> **关联音视频**：摄像头 V4L2 就是这一套——`ioctl(fd, VIDIOC_S_FMT, &format)` 设分辨率像素格式，`VIDIOC_QBUF/DQBUF` 收发帧。理解了这个 demo，看 V4L2 采集代码就不陌生。

### 7. 用户态串口读写（termios，不写驱动也能玩串口）

物联网调试模组最常用，**纯用户态**操作字符设备 `/dev/ttySx`，无需写内核驱动：

```c
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

int openSerial(const char *devicePath) {
    int serialFd = open(devicePath, O_RDWR | O_NOCTTY); // 打开串口设备节点
    struct termios options;
    tcgetattr(serialFd, &options);                      // 取当前配置
    cfsetispeed(&options, B115200);                     // 输入波特率
    cfsetospeed(&options, B115200);                     // 输出波特率
    options.c_cflag |= (CLOCAL | CREAD);                // 本地连接、允许接收
    options.c_cflag &= ~PARENB;                         // 无校验位
    options.c_cflag &= ~CSTOPB;                         // 1 位停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;                             // 8 数据位
    tcsetattr(serialFd, TCSANOW, &options);             // 立即生效
    return serialFd;
}

// 之后就是普通文件操作
// write(serialFd, "AT\r\n", 4);
// read(serialFd, readBuffer, sizeof(readBuffer));
```

### 8. 中断上半部 / 下半部

```text
   硬件中断到来
        │
        ▼
  ┌──────────────┐  尽量快、不睡眠、常关中断
  │  上半部       │  只做：应答硬件、搬寄存器数据、调度下半部
  │ (硬中断)      │
  └──────┬───────┘
         │ 调度
         ▼
  ┌──────────────┐  开中断环境下慢慢做
  │  下半部       │  softirq / tasklet（不可睡眠）
  │              │  workqueue（内核线程，可睡眠、可阻塞）
  └──────────────┘
```

---

## 三、常见坑与面试加分点

| 坑 / 易错 | 说明 |
|---|---|
| 内核里直接解引用用户指针 | 必须 `copy_to_user/copy_from_user`，否则 oops/安全漏洞 |
| 模块里用 `printf`/标准库 | 内核没有 libc，用 `printk`，日志看 `dmesg` |
| 忘记 `module_exit` 清理 | 资源泄漏，重复 `insmod` 冲突 |
| 不写 `MODULE_LICENSE("GPL")` | 内核会标记 tainted，部分符号用不了 |
| 把 ioctl 当万能口袋 | 命令字要用 `_IO/_IOR/_IOW` 宏规范构造，避免冲突 |
| 交叉编译内核版本不匹配 | `.ko` 必须对应目标板**完全一致**的内核版本 |
| 混淆设备类型 | 串口/摄像头=字符设备，磁盘=块设备，网卡走 socket |

**加分一句（结合音视频 + 物联网）：**

> "我虽然没独立写过完整驱动，但理解这条链路：用户态 `open/ioctl/mmap` → 内核 `file_operations` 回调 → 操作硬件。做摄像头采集时和 V4L2 的一堆 ioctl、mmap 零拷贝缓冲打过交道，知道帧数据是怎么从硬件 DMA 一路到用户态的；嵌入式板子上也清楚设备树负责描述硬件、驱动靠 compatible 匹配。需要时我能看懂驱动代码、配合调试。"

---

## 四、速记对照表

| 概念 | 一句话 |
|---|---|
| 用户态/内核态 | ring3 受限 / ring0 特权，syscall 是过路口 |
| copy_to/from_user | 跨空间安全搬数据，不能裸解引用用户指针 |
| 字符/块/网络设备 | 流式 / 块随机 / 走 socket |
| /dev /proc /sys | 设备节点 / 进程信息 / 设备属性树 |
| udev | 用户态动态创建 /dev 节点 |
| file_operations | 函数指针表，用户文件操作→驱动回调 |
| major/minor | 哪个驱动 / 第几个设备 |
| ioctl | read/write 之外的控制命令通道 |
| 内核模块 | .ko，insmod/rmmod，module_init/exit |
| printk + dmesg | 内核日志输出与查看 |
| 中断上/下半部 | 抢时间 / 干重活（tasklet/workqueue） |
| 设备树 .dts/.dtb | 描述硬件，节点+属性，compatible 配对驱动 |
| GPIO/UART/I2C/SPI/CAN | 引脚/串口/慢速多设备/快速点对点/车载工业 |
| mmap | 设备内存映射用户态，零拷贝（见 [[03-Linux内存管理机制]][[06-文件IO与IO多路复用]]） |
| UIO/DPDK | 用户态驱动趋势，空间换性能/易开发 |

---

## 五、自测 8 题

1. 用户态和内核态的本质区别是什么？驱动为什么在内核态？  
2. 内核为什么不能直接解引用用户传进来的指针？用什么函数解决？  
3. 设备分哪三类？串口和摄像头属于哪类？  
4. /dev、/proc、/sys 各放什么？udev 干嘛？  
5. `open("/dev/xxx")` 后的 `read` 是怎么走到驱动函数的？主次设备号各表示什么？  
6. ioctl 解决了什么 read/write 解决不了的问题？举一个音视频里的例子。  
7. 设备树是什么？为什么 ARM 平台需要它？compatible 字段干嘛？  
8. 中断为什么分上半部下半部？下半部哪种机制能睡眠？

<details>
<summary>参考答案</summary>

1. 特权级不同：内核态（ring0）能执行特权指令、访问全部内存和硬件，用户态（ring3）受限，靠 syscall 陷入。驱动要直接操作寄存器/中断/DMA，是特权操作，故在内核态。  
2. 用户指针是用户态虚拟地址，可能非法/未映射/恶意；裸解引用会崩溃或造成安全漏洞。用 `copy_to_user`/`copy_from_user`，它们校验地址并安全搬运。  
3. 字符设备（流式）、块设备（块随机访问）、网络设备（走 socket）。串口和摄像头都是**字符设备**。  
4. `/dev` 设备节点、`/proc` 进程与内核运行信息、`/sys` 设备/驱动结构化属性树；udev 是用户态守护进程，按热插拔事件动态创建/删除 `/dev` 节点。  
5. 节点带主设备号，内核按主设备号查字符设备表找到注册的 `file_operations`，把 `read` 分发到驱动填的回调。主设备号标识哪个驱动，次设备号标识同驱动下第几个设备。  
6. read/write 只能传数据，无法表达「设波特率、查能力、复位、切模式」等控制命令；ioctl 用命令字 + 参数指针实现。音视频例子：V4L2 用 `VIDIOC_S_FMT` 设分辨率、`VIDIOC_QBUF/DQBUF` 收发帧。  
7. 设备树是描述硬件的数据结构（.dts 文本→.dtb 二进制），由 bootloader 传给内核；避免把板级硬件信息硬编码进内核，换板只改设备树。compatible 是设备与驱动配对的钥匙，匹配上才调用该驱动 probe。  
8. 中断处理要快、不能丢中断且常不能睡眠；上半部只做最紧急的事并尽快返回，下半部把耗时处理推迟做。**workqueue**（跑在内核线程）可以睡眠。

</details>


