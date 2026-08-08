# Linux C/C++ 系统性能分析与优化：perf、gdb、Valgrind 面试与实战指南

## 1. 这条 JD 到底要求什么？

JD：

> 有系统性能分析与优化经验，能够使用 perf、gdb、Valgrind 等工具进行问题定位。

拆开来看，实际上要求具备 4 个层次的能力：

1. **知道系统为什么慢**

   * CPU 高
   * 内存占用高
   * 内存泄漏
   * 频繁内存分配
   * 锁竞争
   * I/O 阻塞
   * 线程调度问题
   * Cache Miss
   * 上下文切换过多

2. **知道用什么工具定位**

   * CPU 性能 → `perf`
   * 崩溃 / 逻辑错误 → `gdb`
   * 内存泄漏 / 越界 / UAF → `Valgrind`
   * 系统调用 / 阻塞 → `strace`
   * 系统整体状态 → `top / htop / vmstat / iostat / pidstat`

3. **能够读懂分析结果**

4. **根据结果修改代码并验证优化效果**

所以面试真正想听的是：

> 发现性能问题 → 收集数据 → 找到热点 → 分析根因 → 修改代码 → Benchmark 验证。

这才叫“系统性能分析与优化经验”。

---

# 2. 建议掌握的完整知识体系

建议建立下面这棵知识树：

```text
Linux 性能分析
│
├── CPU
│   ├── CPU Usage
│   ├── user / system
│   ├── 上下文切换
│   ├── Cache Miss
│   ├── 分支预测
│   └── CPU hotspot
│
├── Memory
│   ├── RSS / VSZ
│   ├── Heap / Stack
│   ├── malloc/free
│   ├── 内存泄漏
│   ├── 越界访问
│   ├── Use After Free
│   ├── Page Fault
│   └── Cache locality
│
├── Thread
│   ├── mutex
│   ├── condition_variable
│   ├── 锁竞争
│   ├── 死锁
│   ├── context switch
│   └── CPU affinity
│
├── I/O
│   ├── read/write
│   ├── socket
│   ├── disk
│   ├── blocking
│   ├── poll/epoll
│   └── syscall
│
└── Tools
    ├── top / htop
    ├── pidstat
    ├── vmstat
    ├── iostat
    ├── perf
    ├── gdb
    ├── Valgrind
    └── strace
```

其中面试最重要的是：

**perf + gdb + Valgrind + Linux CPU/内存/线程基础。**

---

# 3. 第一部分：性能分析的方法论

不要一看到程序慢就直接打开 `perf`。

行业里更标准的思路是：

```text
发现问题
   ↓
判断问题属于哪个方向
   ↓
CPU / Memory / IO / Lock / Network？
   ↓
使用对应工具
   ↓
找到热点函数/异常行为
   ↓
定位具体代码
   ↓
优化
   ↓
重新 Benchmark
```

例如一个视频程序出现：

```text
1080P 30fps
        ↓
实际只有 22fps
        ↓
CPU 180%
```

第一步不是猜测 H.264 编码器太慢。

先：

```bash
top
```

发现：

```text
video_server   180% CPU
```

然后：

```bash
perf top -p PID
```

发现：

```text
35% memcpy
28% swscale
15% avcodec_encode_video2
```

这时候才能得到一个非常重要的信息：

> CPU 并不是主要花在编码，而是花在 memcpy 和像素格式转换。

继续分析代码：

```text
Camera
  ↓
memcpy
  ↓
YUV conversion
  ↓
memcpy
  ↓
Encoder
```

于是优化为：

```text
Camera Buffer
      ↓
共享 Buffer
      ↓
Encoder
```

减少两次 memcpy。

重新测试：

```text
CPU：180% → 115%
FPS：22 → 30
```

这就是一套完整的性能优化案例。

---

# 4. perf：CPU 性能分析最重要的工具

如果只能重点学习一个工具，建议优先学 **perf**。

perf 是 Linux 内核提供的性能分析工具，可以分析：

* CPU hotspot
* CPU cycles
* instructions
* cache miss
* branch miss
* context switch
* page fault
* 函数调用栈

---

# 5. perf top：实时看 CPU 热点

最简单：

```bash
perf top
```

类似于：

```text
Overhead  Shared Object       Symbol

25.31%    libc.so             memcpy
18.42%    libavcodec.so       h264_decode_frame
12.33%    app                  VideoDecoder::decode
 8.21%    libc.so             malloc
```

`Overhead` 可以简单理解成：

> CPU 时间有多少比例消耗在这个函数上。

比如：

```text
25% memcpy
```

说明 memcpy 是一个非常明显的 CPU hotspot。

对于指定进程：

```bash
perf top -p PID
```

---

# 6. perf record + perf report

实际项目中更常用：

```bash
perf record -g ./my_program
```

程序运行一段时间后退出：

```bash
perf report
```

`-g` 非常重要：

```text
-g = 记录 Call Stack
```

没有调用栈的时候，你可能只能看到：

```text
memcpy 30%
```

但不知道是谁调用 memcpy。

有调用栈以后可能看到：

```text
main
 └── VideoPipeline::process
      └── VideoFrame::copy
           └── memcpy
```

这时候就知道：

> VideoFrame::copy 是大量内存拷贝的来源。

---

# 7. perf 的核心原理：Sampling

这是面试经常问的。

perf 通常不是统计：

> 每个函数执行了多长时间。

而是进行 **采样 Sampling**。

例如每隔一段时间检查 CPU 当前正在执行什么：

```text
sample 1 → memcpy
sample 2 → memcpy
sample 3 → decode
sample 4 → memcpy
sample 5 → render
```

10000 次采样：

```text
memcpy 3000 次
decode 2000 次
render 1000 次
...
```

于是近似认为：

```text
memcpy ≈ 30% CPU
```

因此：

> perf 是基于采样的统计分析工具。

它的优点是：

**对程序运行性能影响比较小，可以分析真实运行环境。**

---

# 8. perf stat：看硬件性能指标

这是中高级 C++ 岗位很容易继续追问的地方。

执行：

```bash
perf stat ./program
```

可能看到：

```text
10,000,000,000 cycles
 5,000,000,000 instructions

   100,000,000 cache-misses
       500,000 context-switches
```

几个非常重要的指标：

```text
cycles
instructions
cache-references
cache-misses
branches
branch-misses
context-switches
page-faults
```

---

# 9. IPC：CPU 性能的重要指标

IPC：

```text
Instructions Per Cycle
```

也就是：

```text
IPC = instructions / cycles
```

假设：

```text
instructions = 6B
cycles       = 3B
```

那么：

```text
IPC = 2
```

意味着平均每个 CPU Cycle 完成约 2 条指令。

IPC 很低可能意味着 CPU 经常在等待，例如：

```text
Memory
Cache
Branch Prediction
Dependency
```

---

# 10. Cache Miss

现代 CPU：

```text
CPU
 ↓
L1 Cache
 ↓
L2 Cache
 ↓
L3 Cache
 ↓
RAM
```

访问速度大概可以理解为：

```text
L1      极快
L2      很快
L3      较慢
RAM     非常慢
```

所以程序：

```cpp
for (...) {
    process(data[i]);
}
```

如果数据连续：

```text
Cache locality 很好
```

如果不停随机访问：

```text
data[random()]
```

可能产生大量：

```text
Cache Miss
```

CPU 就会经常等待内存。

可以：

```bash
perf stat -e cache-references,cache-misses ./program
```

查看。

---

# 11. Context Switch

另一个很重要的性能指标：

```text
context-switches
```

线程切换大概发生：

```text
Thread A
   ↓
保存寄存器
   ↓
Scheduler
   ↓
恢复 Thread B
```

大量线程：

```text
Thread1
Thread2
Thread3
...
Thread100
```

不意味着性能更高。

反而可能造成：

```text
频繁 context switch
        ↓
CPU Cache 被破坏
        ↓
性能下降
```

所以音视频 Pipeline 通常不会无限增加线程。

---

# 12. Flame Graph：火焰图

这是性能分析非常重要的可视化方式。

火焰图大概长这样：

```text
                ┌──────── memcpy ────────┐
          ┌──── VideoFrame::copy ────────┐
     ┌────── VideoPipeline::process ──────┐
────────────────── main ───────────────────
```

理解两个方向：

### 横向宽度

代表：

```text
CPU 消耗
```

越宽说明 CPU 占比越高。

### 纵向

代表：

```text
调用栈
```

例如：

```text
main
 ↓
process
 ↓
copy
 ↓
memcpy
```

面试至少要知道：

> Flame Graph 本质上是把 perf 采样得到的调用栈进行可视化，可以非常直观地寻找 CPU hotspot。

---

# 13. gdb：程序崩溃和运行时问题定位

gdb 和 perf 定位的问题不同。

perf：

```text
为什么慢？
```

gdb：

```text
为什么崩？
程序现在执行到哪里？
变量是什么？
线程卡在哪里？
```

---

# 14. gdb 基础操作

编译程序最好加：

```bash
-g
```

例如：

```bash
g++ -g main.cpp -o app
```

进入：

```bash
gdb ./app
```

常见命令必须掌握：

```text
run
break
continue
next
step
print
backtrace
info threads
thread
frame
```

---

# 15. break：断点

例如：

```bash
break main
```

或者：

```bash
break VideoDecoder::decode
```

运行：

```bash
run
```

程序运行到断点停止。

---

# 16. next 和 step

这是最基础的区别。

```text
next
```

执行下一行：

**不进入函数。**

```text
step
```

执行下一行：

**进入函数。**

例如：

```cpp
decode(frame);
```

执行：

```text
next
```

直接执行完整个 decode。

执行：

```text
step
```

进入：

```cpp
VideoDecoder::decode()
```

---

# 17. backtrace：查看调用栈

程序崩溃后：

```bash
bt
```

可能看到：

```text
#0 memcpy()
#1 VideoFrame::copy()
#2 VideoDecoder::decode()
#3 VideoPipeline::process()
#4 main()
```

这个东西非常重要。

你马上知道：

```text
main
 ↓
VideoPipeline
 ↓
VideoDecoder
 ↓
VideoFrame::copy
 ↓
memcpy 崩溃
```

然后：

```bash
frame 1
```

进入：

```text
VideoFrame::copy
```

再：

```bash
print src
print dst
print size
```

检查变量。

---

# 18. Core Dump：线上崩溃定位

实际服务器程序经常不是：

```text
开着 gdb 等程序崩
```

而是程序已经在线上崩溃。

这时候需要：

```text
Core Dump
```

Core Dump 可以理解成：

> 程序崩溃瞬间的进程内存和运行状态快照。

例如：

```bash
gdb ./app core
```

然后：

```bash
bt
```

恢复崩溃调用栈。

这是 Linux C++ 开发必须掌握的能力。

---

# 19. 多线程调试

查看线程：

```bash
info threads
```

可能：

```text
1 main
2 CaptureThread
3 DecodeThread
4 RenderThread
5 NetworkThread
```

切换：

```bash
thread 3
```

查看调用栈：

```bash
bt
```

还可以：

```bash
thread apply all bt
```

查看：

> 所有线程调用栈。

这个命令对于：

```text
死锁
卡死
ANR
Pipeline 停止
```

非常有用。

---

# 20. 一个典型死锁分析

假设程序卡住：

```text
CPU 0%
程序不退出
```

使用：

```bash
gdb -p PID
```

然后：

```bash
thread apply all bt
```

发现：

```text
Thread 1
pthread_mutex_lock
VideoQueue::push

Thread 2
pthread_mutex_lock
VideoQueue::pop
```

进一步检查发现：

```text
Thread A：

lock(mutexA)
lock(mutexB)

Thread B：

lock(mutexB)
lock(mutexA)
```

经典死锁。

优化：

```text
统一锁顺序
```

例如永远：

```text
mutexA
 ↓
mutexB
```

---

# 21. Valgrind：内存问题分析

Valgrind 最常用组件：

```text
Memcheck
```

主要检查：

```text
Memory Leak
Invalid Read
Invalid Write
Use After Free
未初始化内存
Double Free
```

这是 C/C++ 非常重要的问题定位工具。

---

# 22. Memory Leak

例如：

```cpp
void foo()
{
    int* p = new int[100];

    // 忘记 delete[]
}
```

循环调用：

```cpp
while (true) {
    foo();
}
```

内存：

```text
100MB
 ↓
200MB
 ↓
500MB
 ↓
1GB
```

典型：

```text
Memory Leak
```

---

# 23. Valgrind 检测内存泄漏

运行：

```bash
valgrind --leak-check=full ./app
```

可能输出：

```text
100 bytes in 1 blocks are definitely lost
```

重点理解几个分类。

### definitely lost

确定泄漏。

例如：

```cpp
new int[100];
```

指针彻底丢失。

这是最应该处理的。

### indirectly lost

某个对象泄漏导致它内部引用的对象也泄漏。

例如：

```text
A 泄漏
 ↓
A -> B
 ↓
B 也无法释放
```

### possibly lost

Valgrind 怀疑泄漏，但不能完全确定。

### still reachable

程序结束时内存还没有释放，但仍然存在有效指针。

不一定是真正的 bug。

面试重点：

> `definitely lost` 是最明确的内存泄漏。

---

# 24. Invalid Read

例如：

```cpp
int* p = new int[10];

delete[] p;

int x = p[0];
```

这就是：

```text
Use After Free
```

Valgrind 可能报告：

```text
Invalid read of size 4
```

---

# 25. Invalid Write

例如：

```cpp
int* p = new int[10];

p[10] = 100;
```

数组范围：

```text
0 ~ 9
```

访问：

```text
10
```

越界。

Valgrind：

```text
Invalid write of size 4
```

---

# 26. Double Free

例如：

```cpp
int* p = new int;

delete p;
delete p;
```

Valgrind 也可以检测。

---

# 27. Valgrind 的缺点

这是面试经常继续追问的。

Valgrind 的问题：

> 非常慢。

因为它需要对程序进行动态二进制插桩和模拟分析。

程序可能：

```text
正常运行：1x

Valgrind：
10x
20x
甚至更慢
```

所以：

```text
perf
```

比较适合性能 Profiling。

而：

```text
Valgrind
```

主要用于：

```text
Memory Debugging
```

不要混淆。

---

# 28. AddressSanitizer：现代项目也必须了解

虽然 JD 没写，但面试很可能问：

> Valgrind 之外还用过什么？

应该知道：

```text
ASan
```

AddressSanitizer。

编译：

```bash
-fsanitize=address
```

例如：

```bash
g++ -fsanitize=address -g main.cpp
```

它可以检测：

```text
Heap Buffer Overflow
Stack Buffer Overflow
Use After Free
Double Free
Memory Leak
```

现代 C++ 项目非常常用。

相较 Valgrind：

```text
ASan
性能开销通常更小
```

但是需要重新编译程序。

---

# 29. strace：系统调用分析

虽然 JD 没写，但建议一起学习。

strace 可以看程序进行了哪些：

```text
system call
```

例如：

```bash
strace ./app
```

可能看到：

```text
open()
read()
write()
poll()
recvfrom()
sendto()
mmap()
futex()
```

---

# 30. strace 特别适合定位什么？

假设：

```text
程序 CPU 不高
但是特别慢
```

perf 可能发现：

```text
poll
read
recv
```

这时候：

```bash
strace -p PID
```

发现程序一直：

```text
recvfrom(...)
```

说明：

> 程序不是计算慢，而是在等待网络数据。

对于网络、音视频程序非常实用。

---

# 31. futex 为什么非常重要？

perf 或 strace 中可能经常看到：

```text
futex
```

你应该知道：

> futex 是 Linux 用户态锁实现的重要基础机制。

例如：

```cpp
std::mutex
```

发生竞争的时候，底层可能进入：

```text
futex()
```

如果 perf 发现大量 CPU 时间或者阻塞发生在：

```text
futex
pthread_mutex
```

应该开始怀疑：

```text
锁竞争
```

而不是业务算法慢。

---

# 32. top：性能问题第一现场

性能分析第一步通常可以：

```bash
top
```

重点观察：

```text
CPU
Memory
Load Average
Process
```

进程 CPU：

```text
%CPU
```

例如：

```text
200%
```

在多核 Linux 中并不奇怪。

通常可以理解为：

```text
100% ≈ 一个 CPU Core
200% ≈ 两个 CPU Core
```

---

# 33. top 中几个 CPU 指标

常见：

```text
us
sy
id
wa
```

### us

User Space CPU：

```text
用户代码
```

高的话可能：

```text
算法计算量大
编码/解码
memcpy
图像处理
```

### sy

Kernel Space CPU：

```text
内核
system call
driver
network
```

### id

Idle：

```text
CPU 空闲
```

### wa

I/O Wait：

```text
等待 IO
```

如果 `wa` 很高：

> CPU 本身可能不忙，而是在等待磁盘等 I/O。

---

# 34. pidstat：非常推荐掌握

例如：

```bash
pidstat -p PID 1
```

每秒观察一次进程。

查看线程：

```bash
pidstat -t -p PID 1
```

对于音视频 Pipeline 特别有价值。

例如：

```text
CaptureThread     5%
DecodeThread     80%
RenderThread     20%
NetworkThread     3%
```

马上知道：

```text
DecodeThread
```

可能是主要瓶颈。

---

# 35. vmstat：系统整体状态

运行：

```bash
vmstat 1
```

可以观察：

```text
CPU
Memory
Swap
IO
Context Switch
```

例如：

```text
cs
```

代表：

```text
context switches
```

如果非常高：

> 可能存在线程过多、锁竞争或者频繁 wakeup。

---

# 36. iostat：磁盘 I/O

如果程序：

```text
录像
文件读写
大视频读取
```

出现卡顿：

可以：

```bash
iostat -x 1
```

分析：

```text
Disk throughput
IOPS
latency
utilization
```

---

# 37. C++ 性能优化必须掌握的几个方向

工具只是帮助定位。

真正优化代码还需要理解 C++ 性能。

重点掌握下面几个方向。

---

# 38. 减少内存拷贝

音视频中这是最重要的优化之一。

例如：

```text
Camera Buffer
     ↓ memcpy
Frame
     ↓ memcpy
Encoder Buffer
     ↓
Encoder
```

1080P YUV420：

```text
1920 × 1080 × 1.5
≈ 3MB/frame
```

30 FPS：

```text
3MB × 30
≈ 90MB/s
```

如果复制三次：

```text
270MB/s
```

4K：

```text
3840 × 2160 × 1.5
≈ 12MB/frame
```

30fps：

```text
≈ 360MB/s
```

三次复制：

```text
> 1GB/s
```

所以音视频系统非常重视：

```text
Zero Copy
Buffer Pool
shared memory
DMA-BUF
AHardwareBuffer
```

---

# 39. 减少 malloc/free

错误设计：

```cpp
while (...) {

    uint8_t* buffer =
        new uint8_t[frame_size];

    process(buffer);

    delete[] buffer;
}
```

30fps / 60fps 长时间运行，会不断：

```text
malloc
free
malloc
free
```

更好的方式：

```text
Buffer Pool
```

例如：

```text
Pool
 ├── Buffer1
 ├── Buffer2
 ├── Buffer3
 └── Buffer4
```

循环复用。

perf 如果看到：

```text
malloc
free
operator new
```

占比非常高，就值得检查这个问题。

---

# 40. 锁粒度

例如：

```cpp
mutex.lock();

decode();
render();
network_send();

mutex.unlock();
```

这是非常危险的。

锁里面进行了大量耗时操作。

应该尽量：

```cpp
mutex.lock();

取数据

mutex.unlock();

decode();
render();
network_send();
```

原则：

> 临界区尽可能小。

---

# 41. 避免 Busy Loop

例如：

```cpp
while (queue.empty()) {
}
```

CPU：

```text
100%
```

但实际上什么都没做。

应该：

```cpp
condition_variable
```

例如：

```cpp
cv.wait(lock);
```

没有任务时线程进入：

```text
sleep
```

而不是空转。

---

# 42. 数据结构和 Cache Locality

例如：

```cpp
std::vector<Frame>
```

连续内存。

通常 Cache Locality 比：

```cpp
std::list<Frame>
```

更好。

list：

```text
Node → Node → Node
```

节点散落在 Heap。

CPU 经常：

```text
Cache Miss
```

所以性能敏感场景下：

> 数据结构的内存布局非常重要。

---

# 43. 音视频 Pipeline 性能分析案例

假设：

```text
V4L2
 ↓
Video Capture
 ↓
Frame Queue
 ↓
FFmpeg Decoder
 ↓
YUV Convert
 ↓
Renderer
```

出现：

```text
30fps → 20fps
```

正确分析过程：

### Step 1

```bash
top
```

发现：

```text
CPU = 190%
```

说明：

```text
CPU bound
```

可能性较高。

### Step 2

```bash
pidstat -t -p PID 1
```

发现：

```text
Capture       5%
Decode       130%
Render       20%
```

瓶颈：

```text
DecodeThread
```

### Step 3

```bash
perf record -g -p PID
```

然后：

```bash
perf report
```

发现：

```text
30% memcpy
25% sws_scale
20% avcodec_decode
```

说明：

> FFmpeg Decode 本身并不是唯一瓶颈。

### Step 4

分析调用栈：

```text
DecodeThread
   ↓
VideoDecoder
   ↓
copyFrame
   ↓
memcpy
```

发现：

```text
每帧进行了两次 YUV memcpy
```

### Step 5

改为：

```text
Decoder AVFrame
      ↓
shared_ptr / reference
      ↓
Renderer
```

减少复制。

### Step 6

重新测试：

```text
Before:

CPU = 190%
FPS = 20

After:

CPU = 120%
FPS = 30
```

这就是一个完整的性能优化闭环。

---

# 44. Memory Leak 实战分析

假设视频程序：

```text
启动：
RSS = 200MB

10 min：
RSS = 500MB

30 min：
RSS = 1GB
```

首先：

```bash
top
```

确认：

```text
RSS 持续增长
```

然后：

```bash
valgrind --leak-check=full ./app
```

发现：

```text
VideoFrame::create()
```

存在：

```text
definitely lost
```

进一步分析：

```cpp
Frame* frame = new Frame();

queue.push(frame);
```

但是队列清理时没有：

```cpp
delete frame;
```

改成：

```cpp
std::shared_ptr<Frame>
```

或者明确 RAII 所有权。

重新运行：

```text
RSS 稳定在 220MB
```

问题解决。

---

# 45. Crash 实战分析

程序：

```text
Segmentation Fault
```

产生：

```text
core
```

使用：

```bash
gdb ./video_app core
```

执行：

```bash
bt
```

发现：

```text
#0 memcpy
#1 VideoFrame::copy
#2 Decoder::output
#3 DecodeThread
```

查看：

```bash
frame 1
```

然后：

```bash
p src
p dst
p size
```

发现：

```text
dst = nullptr
```

于是定位：

```text
VideoFrame 生命周期管理错误
```

进一步通过：

```text
ASan
```

发现：

```text
heap-use-after-free
```

最终确认：

> Frame 已经被另外一个线程释放，DecodeThread 仍然持有裸指针。

解决：

```text
RAII
shared_ptr
明确线程间 Buffer Ownership
```

---

# 46. 系统性能问题的分类能力

面试官如果说：

> 程序很卡，你怎么排查？

不要直接回答：

> 我用 perf。

应该先分类。

```text
程序慢
 │
 ├── CPU 高
 │     ↓
 │    perf
 │
 ├── Memory 持续增长
 │     ↓
 │    Valgrind / ASan
 │
 ├── CPU 不高但是卡
 │     ↓
 │    IO / Lock / Network
 │
 ├── 程序 Crash
 │     ↓
 │    gdb / core / ASan
 │
 └── 程序卡死
       ↓
      gdb
      thread apply all bt
      strace
```

这才体现“系统性能分析能力”。

---

# 47. perf、gdb、Valgrind 的区别

| 工具       | 核心用途                      |
| -------- | ------------------------- |
| perf     | CPU 性能、热点、硬件事件            |
| gdb      | Crash、变量、调用栈、线程           |
| Valgrind | 内存错误、内存泄漏                 |
| ASan     | 内存越界、UAF 等                |
| strace   | 系统调用                      |
| top      | 系统整体 CPU/Memory           |
| pidstat  | 进程/线程 CPU                 |
| vmstat   | CPU/Memory/Context Switch |
| iostat   | Disk IO                   |

一定不要把三者混在一起。

最简单记忆：

```text
CPU 慢 → perf

程序崩 → gdb

内存错 → Valgrind / ASan
```

---

# 48. 面试必须掌握的 perf 命令

至少熟悉：

```bash
perf top

perf top -p PID

perf stat ./app

perf record ./app

perf record -g ./app

perf record -g -p PID

perf report
```

进一步可以了解：

```bash
perf annotate
```

用于：

> 分析热点具体落在哪些代码/指令。

---

# 49. 面试必须掌握的 gdb 命令

建议至少记住：

```text
gdb app

run
break
continue

next
step

print

bt
frame

info threads
thread

thread apply all bt
```

Core Dump：

```bash
gdb app core
```

Attach：

```bash
gdb -p PID
```

---

# 50. Valgrind 必须掌握

至少：

```bash
valgrind ./app
```

以及：

```bash
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    ./app
```

能够解释：

```text
definitely lost
indirectly lost
possibly lost
still reachable

Invalid Read
Invalid Write
```

基本就够面试使用。

---

# 51. 建议继续学习 Sanitizer

除了：

```text
AddressSanitizer
```

还应该知道：

```text
ASan
TSan
UBSan
```

分别：

```text
ASan
Address Sanitizer
→ 内存问题

TSan
Thread Sanitizer
→ Data Race

UBSan
Undefined Behavior Sanitizer
→ 未定义行为
```

特别是：

```text
TSan
```

对多线程 C++ 很重要。

例如两个线程：

```cpp
Thread A:
value++;

Thread B:
value++;
```

没有：

```text
mutex
atomic
```

就是 Data Race。

TSan 可以帮助发现。

---

# 52. Debug 和 Release 的区别也必须知道

如果：

```bash
g++ -O3
```

编译器可能：

```text
inline
删除变量
调整指令顺序
删除代码
```

这会导致 gdb 调试时：

```text
optimized out
```

或者调用栈和源码不完全直观。

所以 Debug：

```bash
-O0 -g
```

比较方便。

但是做真实性能测试：

> 不能拿 `-O0` Debug 版本测试性能。

应该测试接近生产环境的：

```text
-O2 / -O3
```

版本，同时保留必要的符号信息。

---

# 53. 符号表为什么重要？

perf / gdb 看到：

```text
0x7f872193
0x7f872213
```

几乎没法分析。

有 Symbol：

```text
VideoDecoder::decode()
VideoFrame::copy()
VideoRenderer::render()
```

才能定位代码。

所以性能分析和 Crash 分析中：

```text
Debug Symbol
```

非常重要。

线上通常可以：

```text
Binary：strip

Symbol：单独保存
```

发生 Crash 后使用对应版本 Symbol 进行符号化。

---

# 54. 你应该达到什么水平？

针对普通 C++ / Linux / 音视频岗位，我建议达到：

### Level 1：必须会

```text
top
gdb
perf top
perf record
perf report
Valgrind
```

### Level 2：中级工程师

理解：

```text
CPU hotspot
Call Stack
Flame Graph
Memory Leak
Cache Miss
Context Switch
Lock Contention
Page Fault
Core Dump
```

### Level 3：高级工程师

能够分析：

```text
IPC
CPU Cache
Branch Prediction
False Sharing
NUMA
CPU Affinity
Scheduler
Memory Bandwidth
Lock contention
```

目前准备面试，不需要立刻把 Level 3 全部学完。

---

# 55. 面试高频问题

下面这些问题建议全部能回答。

### Q1：程序 CPU 占用特别高怎么排查？

推荐回答：

> 我会先用 top 或 pidstat 确认是整个进程还是某个线程 CPU 高，然后通过 perf top 初步观察实时热点。如果需要进一步分析，就使用 perf record -g 采样一段时间，再通过 perf report 或 Flame Graph 查看函数热点和调用栈。
>
> 找到热点之后再判断是算法计算、memcpy、频繁 malloc/free、锁竞争还是系统调用造成的，针对性优化，最后使用相同测试条件重新 Benchmark，对比 CPU、FPS、延迟等指标。

这个回答已经比较像有实际经验的人。

---

### Q2：程序内存不断增长怎么办？

> 首先通过 top/pidstat 等确认 RSS 是否持续增长。如果怀疑内存泄漏，可以使用 Valgrind Memcheck 或 ASan。
>
> Valgrind 重点关注 definitely lost，同时检查 Invalid Read/Write 等问题。定位具体 allocation stack 后检查对象生命周期和所有权，修复后进行长时间压力测试确认 RSS 是否稳定。

---

### Q3：程序 Crash 怎么排查？

> 如果能够复现，可以直接使用 gdb 调试；线上问题通常保留 core dump 和对应版本的 debug symbol，通过 `gdb executable core` 加载，然后使用 bt 查看调用栈，再通过 frame、print 等检查崩溃现场变量。
>
> 如果怀疑越界、UAF 等内存问题，还会配合 ASan 或 Valgrind。

---

### Q4：程序卡死但是 CPU 不高怎么办？

首先怀疑：

```text
Lock
IO
Network
Condition Variable
```

可以：

```bash
gdb -p PID
```

然后：

```bash
thread apply all bt
```

查看所有线程：

```text
是不是等待 mutex
是不是等待 condition_variable
是不是阻塞在 recv
是不是 poll
```

必要时：

```text
strace
```

观察系统调用。

---

### Q5：perf 的原理是什么？

核心回答：

> perf 主要通过 Linux 内核的性能监控机制和 CPU PMU 进行事件统计和采样，可以采集 cycles、instructions、cache miss、branch miss 等硬件事件，也可以对程序执行位置进行周期性采样，再结合调用栈和符号信息分析 CPU hotspot。

这个回答已经可以应对中级面试。

---

# 56. 最值得做的实战 Demo

不要只背命令。

建议专门写一个：

```text
performance_lab/
```

包含 5 个案例。

```text
performance_lab/

├── cpu_hotspot/
│   └── perf

├── memory_leak/
│   └── valgrind

├── use_after_free/
│   └── asan

├── deadlock/
│   └── gdb

└── lock_contention/
    └── perf
```

---

# 57. Demo 1：CPU Hotspot

故意写：

```cpp
while (...) {
    memcpy(...);
    heavy_calculation();
}
```

然后：

```bash
perf record -g ./app
perf report
```

亲自观察：

```text
heavy_calculation
memcpy
```

占比。

---

# 58. Demo 2：Memory Leak

写：

```cpp
while (...) {
    new char[1024];
}
```

然后：

```bash
valgrind --leak-check=full ./app
```

观察：

```text
definitely lost
```

---

# 59. Demo 3：Use After Free

写：

```cpp
int* p = new int;

delete p;

*p = 10;
```

分别使用：

```text
Valgrind
ASan
```

观察两者输出区别。

---

# 60. Demo 4：Deadlock

两个线程：

```text
Thread A：

lock A
lock B
```

另一个：

```text
Thread B：

lock B
lock A
```

制造死锁。

然后：

```bash
gdb -p PID
```

执行：

```bash
thread apply all bt
```

亲自找到：

```text
pthread_mutex_lock
```

---

# 61. Demo 5：锁竞争

例如：

```cpp
std::mutex mutex;

void worker()
{
    while (...) {

        lock_guard lock(mutex);

        heavy_work();
    }
}
```

启动：

```text
8 threads
```

然后使用 perf 分析：

```text
pthread_mutex
futex
```

再把：

```text
heavy_work
```

移出临界区。

对比：

```text
Before
After
```

这就是一个非常好的面试项目。

---

# 62. 针对音视频岗位应该额外掌握什么？

音视频性能分析和普通后台 C++ 又有一些区别。

建议特别关注：

```text
FPS
Frame Time
End-to-End Latency
Encode Time
Decode Time
Render Time
Queue Depth
Dropped Frames
Bitrate
Memory Bandwidth
CPU Usage
GPU Usage
```

例如：

```text
Capture     3ms
Decode     12ms
Convert     8ms
Render      7ms
----------------
Total      30ms
```

30fps：

```text
一帧预算 ≈ 33.3ms
```

基本还能跑。

如果：

```text
Decode     20ms
Convert    15ms
Render      8ms
----------------
Total      43ms
```

就一定跟不上 30fps。

所以音视频性能优化最重要的一个概念是：

> **Frame Budget。**

---

# 63. Pipeline 分阶段 Profiling

例如：

```text
V4L2
 ↓
Capture
 ↓
Queue
 ↓
Decode
 ↓
Convert
 ↓
Render
```

建议给每一阶段打：

```text
timestamp
```

得到：

```text
Capture  = 2ms
Decode   = 14ms
Convert  = 10ms
Render   = 5ms
```

然后再通过：

```text
perf
```

分析为什么：

```text
Convert = 10ms
```

可能发现：

```text
sws_scale
memcpy
```

占用了大量 CPU。

这就是：

```text
业务 Profiling
+
System Profiling
```

结合。

这个思路非常重要。

---

# 64. 性能优化最重要的原则

一定记住：

> **Measure, Don't Guess.**

不要：

```text
我感觉 memcpy 慢。
```

应该：

```text
perf 显示 memcpy 占 CPU 28%。
```

不要：

```text
我感觉线程太多。
```

应该：

```text
perf stat / vmstat 显示 context switch 很高。
```

不要：

```text
我感觉这里内存泄漏。
```

应该：

```text
RSS 持续增长 + Valgrind definitely lost。
```

性能工程最重要的是：

```text
数据
↓
证据
↓
根因
↓
优化
↓
数据验证
```

---

# 65. 建议你的学习顺序

不建议直接研究 CPU 微架构。

按照下面顺序学习效率最高：

```text
第一阶段
Linux Process / Thread / Memory
        ↓
第二阶段
top / pidstat / vmstat
        ↓
第三阶段
gdb
        ↓
第四阶段
Valgrind + ASan
        ↓
第五阶段
perf top
perf stat
perf record
perf report
        ↓
第六阶段
Flame Graph
        ↓
第七阶段
strace
        ↓
第八阶段
CPU Cache / IPC / Context Switch
        ↓
第九阶段
锁竞争 / False Sharing / CPU Affinity
```

---

# 66. 面试前的优先级

如果时间有限，按照这个优先级：

## P0：必须掌握

```text
gdb
perf
Valgrind
ASan
```

重点：

```text
perf top
perf record
perf report

gdb bt
gdb core
gdb 多线程

Valgrind leak
Invalid Read/Write
```

## P1：强烈建议

```text
top
pidstat
strace
Flame Graph
```

## P2：中高级加分项

```text
CPU Cache
Cache Miss
IPC
Context Switch
Page Fault
Lock Contention
False Sharing
CPU Affinity
```

## P3：以后深入

```text
NUMA
PMU
Memory Bandwidth
Scheduler
eBPF
bcc
bpftrace
```

---

# 67. 最终你应该形成的能力

看到：

```text
CPU 200%
```

你的第一反应应该是：

```text
pidstat
↓
哪个线程？
↓
perf
↓
哪个函数？
↓
Call Stack
↓
为什么慢？
```

看到：

```text
Memory 一直涨
```

应该想到：

```text
RSS
↓
Valgrind / ASan
↓
Allocation Stack
↓
Object Lifetime
```

看到：

```text
Segmentation Fault
```

应该想到：

```text
Core Dump
↓
gdb
↓
bt
↓
frame
↓
变量
↓
ASan
```

看到：

```text
程序卡住 CPU 0%
```

应该想到：

```text
gdb attach
↓
thread apply all bt
↓
mutex / condition_variable / IO
↓
strace
```

看到：

```text
CPU 很高但吞吐上不去
```

进一步想到：

```text
perf stat
↓
IPC
Cache Miss
Context Switch
Lock Contention
```

当这些形成条件反射以后，基本就真正具备了 JD 中所谓：

> **“系统性能分析与优化经验，能够使用 perf、gdb、Valgrind 等工具进行问题定位。”**

---

# 68. 一句话建立整个知识框架

最后可以把整个体系记成：

```text
             Linux 程序出现问题
                     │
        ┌────────────┼─────────────┐
        ↓            ↓             ↓
      CPU高         内存异常       Crash/卡死
        │            │             │
     pidstat      Valgrind        gdb
        │           ASan           │
        ↓                          ↓
      perf                     Call Stack
        │
        ↓
    Flame Graph
        │
        ↓
 Hotspot / Cache / Lock
        │
        ↓
       优化
        │
        ↓
    Benchmark
        │
        ↓
  Before vs After
```

这张图实际上就是整条 JD 的核心。
