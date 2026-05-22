# Linux 系统开发知识补齐路线

这篇文档面向你的转型路线：

**移动端音视频 -> Linux 音视频 -> 边缘设备音视频/AI 推理 -> 机器人感知/具身智能**

如果未来想从移动端音视频往 Linux 音视频、嵌入式 Linux、机器人系统方向转，Linux 不是只会几个命令就够了，而是要补齐一整套系统能力：

- Linux 基础使用能力。
- Linux C/C++ 系统编程。
- 进程、线程、网络、IPC。
- 文件系统和 IO。
- 性能分析与调试。
- 编译、部署、交叉编译。
- 设备访问和驱动基础。
- 实时性和嵌入式 Linux。
- 音视频/机器人场景下的 Linux 工程能力。

---

## 面试高频问题与标准回答

### Q1：从移动端音视频转 Linux 音视频，最先补什么？

最先补 **Linux C/C++ 系统编程**。

移动端音视频更多接触平台 API，比如 Android `MediaCodec`、iOS `VideoToolbox`，但 Linux 音视频更强调：

- 进程线程模型。
- socket 网络通信。
- epoll 事件驱动。
- 文件描述符。
- mmap。
- V4L2 摄像头采集。
- FFmpeg/GStreamer 工程化。
- gdb、perf、strace 调试。

如果这些不熟，到了 Linux 上就只能调用库，很难排查性能、延迟、丢帧、卡死、内存泄漏等问题。

### Q2：Linux 系统编程和嵌入式 Linux 有什么区别？

Linux 系统编程更偏应用层和系统调用，比如：

- 进程。
- 线程。
- 网络。
- 文件 IO。
- IPC。
- 定时器。
- 共享内存。
- epoll。

嵌入式 Linux 更偏硬件和系统裁剪，比如：

- ARM 平台。
- 交叉编译。
- Bootloader。
- 设备树。
- 驱动。
- I2C/SPI/UART/CAN。
- rootfs。
- 摄像头、IMU、电机等设备接入。

对你的路线来说，建议顺序是：

**先 Linux 系统编程，再 Linux 音视频，再嵌入式 Linux。**

不要一开始就深挖内核驱动，否则很容易和当前音视频能力脱节。

### Q3：为什么 Linux 对机器人和具身智能很重要？

机器人系统里，高层计算通常跑在 Linux 上，包括：

- 相机采集。
- 图像处理。
- AI 推理。
- ROS2 通信。
- 日志记录。
- 远程调试。
- 数据回放。
- 网络传输。
- 多进程模块管理。

但底层强实时控制通常不会直接跑普通 Linux，而是放在：

- MCU。
- RTOS。
- 实时 Linux。
- 专用控制器。

所以你要理解 Linux 在机器人中的位置：**Linux 负责高层感知、通信、推理和系统管理；MCU/RTOS 负责强实时控制。**

### Q4：Linux 音视频方向最重要的能力是什么？

最重要的是把下面这条链路跑通并能优化：

```text
摄像头采集
    -> 图像格式转换
    -> 编码/推理
    -> 封装/传输
    -> 播放/可视化
    -> 性能统计和问题排查
```

涉及的核心技术包括：

- V4L2。
- FFmpeg。
- GStreamer。
- RTSP/RTP/WebRTC/SRT。
- 硬件编解码。
- DMA Buffer。
- 多线程队列。
- 时间戳和同步。
- perf/strace/gdb 调试。

### Q5：Linux 需要学到内核源码级别吗？

短期不需要。

你的主线应该是：

```text
会用 Linux 做工程
    -> 会排查 Linux 上的问题
    -> 会接入 Linux 上的设备
    -> 会做 Linux 音视频和边缘部署
    -> 再逐步补驱动、设备树、内核机制
```

内核源码很重要，但不能一开始就陷进去。对转型来说，优先级更高的是应用层系统编程、音视频链路、设备采集和性能优化。

---

## 1. Linux 基础使用

### 1.1 必须掌握的命令

文件和目录：

- `ls`
- `cd`
- `pwd`
- `cp`
- `mv`
- `rm`
- `mkdir`
- `ln`
- `tree`

查看文件：

- `less`
- `more`
- `tail`
- `head`
- `wc`
- `diff`

搜索和过滤：

- `grep`
- `find`
- `xargs`
- `awk`
- `sed`
- `sort`
- `uniq`

压缩解压：

- `tar`
- `gzip`
- `zip`
- `unzip`

权限管理：

- `chmod`
- `chown`
- `chgrp`
- `umask`

系统信息：

- `uname`
- `df`
- `du`
- `free`
- `top`
- `htop`
- `ps`
- `kill`
- `uptime`
- `dmesg`

网络相关：

- `ip`
- `ifconfig`
- `ping`
- `netstat`
- `ss`
- `curl`
- `wget`
- `tcpdump`

### 1.2 要理解的基础概念

- 用户和用户组。
- 文件权限。
- 环境变量。
- shell。
- 标准输入、标准输出、标准错误。
- 管道。
- 重定向。
- 软链接和硬链接。
- 前台进程和后台进程。
- daemon 进程。
- package manager。

### 1.3 达标标准

你应该能做到：

- 熟练在 Linux 终端里定位文件、查看日志、搜索内容。
- 能看懂常见 shell 脚本。
- 能写简单自动化脚本。
- 能用命令快速定位进程、端口、磁盘、CPU、内存问题。

---

## 2. Linux 文件系统和 IO

### 2.1 核心知识点

Linux 里很多东西都被抽象成文件：

- 普通文件。
- 目录。
- 设备文件。
- 管道。
- socket。
- procfs。
- sysfs。

需要重点理解：

- 文件描述符。
- `open` / `read` / `write` / `close`。
- 阻塞 IO 和非阻塞 IO。
- 同步 IO 和异步 IO。
- 缓冲 IO 和直接 IO。
- `mmap`。
- `select` / `poll` / `epoll`。
- page cache。
- 零拷贝。

### 2.2 和音视频的关系

Linux 音视频里经常会遇到：

- 读取媒体文件。
- 写入录像文件。
- 摄像头设备文件，比如 `/dev/video0`。
- 音频设备文件。
- socket 推流。
- mmap 采集摄像头帧。
- DMA Buffer 跨模块传递图像。

如果不理解文件描述符和 IO 模型，就很难理解 V4L2、socket、epoll、mmap 这些机制。

### 2.3 需要练习的 Demo

#### Demo 1：文件拷贝工具

要求：

- 使用 `open/read/write/close` 实现文件拷贝。
- 支持大文件。
- 统计拷贝耗时和速度。

重点：

- buffer 大小如何影响性能。
- 系统调用次数如何影响性能。
- 顺序读写和随机读写的差异。

#### Demo 2：mmap 文件读取

要求：

- 使用 `mmap` 映射文件。
- 读取文件内容。
- 对比普通 `read` 的性能和使用方式。

重点：

- `mmap` 并不是永远更快。
- `mmap` 更适合随机访问或共享映射。
- 映射后仍然可能触发缺页。

---

## 3. 进程、线程和并发

### 3.1 进程

需要掌握：

- 进程和线程的区别。
- 虚拟地址空间。
- `fork`。
- `exec`。
- `wait` / `waitpid`。
- 僵尸进程。
- 孤儿进程。
- 守护进程。
- 进程退出状态。

### 3.2 线程

需要掌握：

- pthread。
- `std::thread`。
- mutex。
- condition variable。
- semaphore。
- read-write lock。
- atomic。
- thread local storage。
- 线程栈。
- 线程亲和性。

### 3.3 并发设计

音视频和机器人系统里，多线程非常常见：

```text
采集线程 -> 队列 -> 编码线程 -> 队列 -> 网络发送线程
```

或：

```text
相机采集线程 -> 推理线程 -> ROS2 发布线程 -> 录像线程
```

需要重点理解：

- 生产者消费者模型。
- 有界队列。
- 无界队列的风险。
- 背压。
- 丢帧策略。
- 锁粒度。
- 死锁。
- 优先级反转。
- 线程退出和资源释放。

### 3.4 需要练习的 Demo

#### Demo 1：生产者消费者队列

要求：

- 一个线程生产数据。
- 一个线程消费数据。
- 使用 mutex + condition variable。
- 支持优雅退出。

扩展：

- 改成多个生产者多个消费者。
- 加入队列最大长度。
- 队列满时支持阻塞、丢弃旧帧、丢弃新帧三种策略。

#### Demo 2：音视频帧队列模拟

要求：

- 模拟摄像头 30 FPS 产生帧。
- 模拟编码线程处理帧。
- 模拟网络线程发送帧。
- 统计队列长度、延迟、丢帧。

重点：

- 采集速度大于处理速度时系统会怎样。
- 队列不是越大越好。
- 低延迟系统通常宁可丢帧，也不能无限排队。

---

## 4. IPC 进程间通信

### 4.1 必须掌握

- pipe。
- FIFO。
- Unix Domain Socket。
- TCP/UDP socket。
- shared memory。
- mmap。
- message queue。
- semaphore。
- signal。

### 4.2 优先级建议

对音视频和机器人开发来说，优先级可以这样排：

1. socket。
2. shared memory。
3. Unix Domain Socket。
4. pipe/FIFO。
5. signal。
6. message queue/semaphore。

### 4.3 和实际项目的关系

常见场景：

- 采集进程和 AI 推理进程之间传递图像。
- 主进程和子进程之间传递控制命令。
- 机器人多个模块之间通过 socket 或 DDS 通信。
- 大图像帧通过共享内存传递，避免频繁拷贝。

### 4.4 需要练习的 Demo

#### Demo 1：Unix Domain Socket 通信

要求：

- 一个 server。
- 一个 client。
- client 发送命令。
- server 返回结果。

重点：

- 本机进程通信为什么可以用 Unix Domain Socket。
- 它和 TCP socket 的区别。

#### Demo 2：共享内存传递图像帧

要求：

- 一个进程写入模拟图像帧。
- 一个进程读取图像帧。
- 使用共享内存传递数据。
- 使用信号量或 eventfd 做同步。

重点：

- 大数据传递不适合频繁走 socket 拷贝。
- 共享内存需要额外设计同步和生命周期管理。

---

## 5. Linux 网络编程

### 5.1 核心知识点

必须掌握：

- TCP。
- UDP。
- socket API。
- bind/listen/accept/connect。
- send/recv。
- 阻塞和非阻塞。
- 粘包和拆包。
- 心跳。
- 超时。
- 重连。
- epoll。
- Reactor 模型。

### 5.2 音视频相关协议

你需要重点关注：

- RTP。
- RTCP。
- RTSP。
- RTMP。
- WebRTC。
- SRT。
- HLS。
- HTTP-FLV。

不是每个协议都要从源码级别精通，但要知道它们适合什么场景：

- RTMP：传统直播推流，生态成熟，延迟通常不是最低。
- RTSP/RTP：摄像头、安防、局域网实时流常见。
- WebRTC：低延迟实时通信。
- SRT：抗弱网传输。
- HLS：点播和大规模分发，延迟偏高。
- HTTP-FLV：直播播放端常见，延迟比 HLS 低。

### 5.3 和机器人场景的关系

机器人系统里网络也很重要：

- 远程监控视频。
- 远程控制。
- 日志上传。
- 多机通信。
- 机器人和服务器之间传输传感器数据。
- 云端下发任务。

如果做具身智能工程化，网络不是可选项。

### 5.4 需要练习的 Demo

#### Demo 1：epoll TCP server

要求：

- 支持多个 client。
- 每个 client 能发送消息。
- server 回显或广播消息。
- 使用非阻塞 socket + epoll。

重点：

- epoll 为什么适合大量连接。
- 边缘触发和水平触发的区别。
- 非阻塞 IO 下如何处理 `EAGAIN`。

#### Demo 2：UDP/RTP 模拟传输

要求：

- 模拟发送视频帧包。
- 每个包带序号和时间戳。
- 接收端统计丢包、乱序、抖动。

重点：

- UDP 不保证可靠。
- 音视频实时传输更关注延迟和连续性。
- 不是所有丢包都应该重传。

---

## 6. 定时器、时间戳和同步

### 6.1 必须掌握

- 系统时间。
- 单调时间。
- `gettimeofday`。
- `clock_gettime`。
- `CLOCK_REALTIME`。
- `CLOCK_MONOTONIC`。
- `CLOCK_MONOTONIC_RAW`。
- timerfd。
- POSIX timer。
- sleep/usleep/nanosleep。
- NTP。
- PTP。

### 6.2 为什么对音视频重要

音视频里时间戳非常核心：

- PTS。
- DTS。
- audio/video sync。
- 帧间隔。
- 编码延迟。
- 网络抖动。
- 播放缓冲。

很多音画不同步、延迟越来越大、播放卡顿的问题，本质上都是时间戳和时钟问题。

### 6.3 为什么对机器人重要

机器人里时间更重要：

- 相机帧时间戳。
- IMU 时间戳。
- 雷达时间戳。
- 电机状态时间戳。
- 多传感器同步。
- 轨迹估计。
- SLAM。

如果时间戳不准，感知和控制都会出问题。

### 6.4 需要练习的 Demo

#### Demo：定时采集模拟器

要求：

- 模拟 30 FPS 摄像头采集。
- 每帧记录时间戳。
- 模拟处理耗时波动。
- 统计帧间隔、抖动、最大延迟。

重点：

- 不能简单依赖 `sleep(33ms)` 保证 30 FPS。
- 要区分采集时间、处理时间、发送时间。
- 低延迟链路必须记录每个阶段耗时。

---

## 7. 内存管理和性能优化

### 7.1 必须掌握

- 虚拟内存。
- 栈和堆。
- malloc/free。
- new/delete。
- 内存泄漏。
- 内存碎片。
- page cache。
- mmap。
- copy-on-write。
- cache line。
- CPU cache。
- NUMA 基础。
- 内存对齐。
- SIMD 基础。

### 7.2 和音视频的关系

音视频处理的数据量很大：

- 1080p YUV420 一帧大约 3MB。
- 4K YUV420 一帧大约 12MB。
- 30 FPS 或 60 FPS 下，每秒数据吞吐非常大。

所以要特别关注：

- 减少内存拷贝。
- 复用 buffer。
- 使用内存池。
- 避免频繁 malloc/free。
- 注意 cache locality。
- 使用零拷贝链路。

### 7.3 需要练习的 Demo

#### Demo 1：内存池

要求：

- 实现一个固定大小 buffer pool。
- 模拟视频帧申请和释放。
- 对比频繁 malloc/free 的性能。

重点：

- 实时系统里频繁分配内存会引入抖动。
- 音视频帧适合使用 buffer pool 管理。

#### Demo 2：图像处理缓存命中率实验

要求：

- 生成一张大图。
- 分别按行遍历和按列遍历。
- 对比耗时。

重点：

- 内存访问顺序会影响 CPU cache。
- 图像处理要尽量顺序访问内存。

---

## 8. 调试和问题排查

### 8.1 必须掌握的工具

进程和线程：

- `ps`
- `top`
- `htop`
- `pidstat`
- `pstree`

系统调用：

- `strace`

动态库：

- `ldd`
- `ldconfig`
- `readelf`
- `nm`
- `objdump`

网络：

- `ss`
- `netstat`
- `tcpdump`
- `wireshark`

性能：

- `perf`
- `time`
- `vmstat`
- `iostat`
- `sar`

内存：

- `valgrind`
- `asan`
- `lsan`
- `massif`

日志：

- `dmesg`
- `journalctl`
- `syslog`

### 8.2 常见问题排查思路

#### 程序 CPU 高

排查：

- `top` 看进程。
- `top -H` 看线程。
- `perf top` 看热点函数。
- `perf record/report` 分析调用栈。

常见原因：

- 死循环。
- 忙等。
- 锁竞争。
- 编解码耗时。
- 图像格式转换耗时。
- 内存拷贝过多。

#### 程序内存涨

排查：

- `top` / `ps` 看 RSS。
- `pmap` 看内存分布。
- `valgrind` 查泄漏。
- ASan/LSan 查问题。

常见原因：

- 对象没有释放。
- 队列无限增长。
- buffer pool 没有归还。
- 回调持有引用。
- 线程退出时资源没回收。

#### 程序卡住

排查：

- `gdb attach`。
- 查看所有线程调用栈。
- `strace -p` 看卡在哪个系统调用。

常见原因：

- 死锁。
- 条件变量没有唤醒。
- socket 阻塞。
- 文件 IO 阻塞。
- 等待子进程。

#### 延迟越来越大

排查：

- 打点每个阶段耗时。
- 统计队列长度。
- 统计丢帧。
- 检查编码器缓存。
- 检查网络发送阻塞。

常见原因：

- 队列无限堆积。
- 推理速度低于采集速度。
- 编码器内部缓存。
- 网络带宽不足。
- 没有合理丢帧策略。

---

## 9. 编译、链接和部署

### 9.1 必须掌握

- gcc/g++。
- CMake。
- Makefile。
- 静态库。
- 动态库。
- 头文件搜索路径。
- 库搜索路径。
- rpath。
- pkg-config。
- 交叉编译。
- toolchain file。
- Docker。

### 9.2 常见问题

#### 找不到头文件

重点看：

- `-I`。
- CMake `include_directories`。
- CMake `target_include_directories`。

#### 找不到库

重点看：

- `-L`。
- `-lxxx`。
- `LD_LIBRARY_PATH`。
- `rpath`。
- `ldconfig`。

#### 运行时找不到 so

排查：

- `ldd ./app`。
- 检查动态库路径。
- 检查架构是否匹配。
- 检查交叉编译环境。

### 9.3 对你的重要性

Linux 音视频工程经常依赖：

- FFmpeg。
- GStreamer。
- OpenCV。
- WebRTC。
- x264/x265。
- libdrm。
- CUDA。
- TensorRT。
- ONNX Runtime。

如果编译、链接、部署不熟，会在环境问题上浪费大量时间。

---

## 10. 设备访问和嵌入式 Linux 基础

### 10.1 必须了解

- `/dev`。
- `/proc`。
- `/sys`。
- udev。
- 设备文件。
- ioctl。
- mmap。
- sysfs 属性。
- 字符设备。
- 块设备。

### 10.2 常见硬件接口

需要逐步补齐：

- UART。
- I2C。
- SPI。
- CAN。
- GPIO。
- PWM。
- USB。
- MIPI CSI。

### 10.3 和音视频/机器人关系

音视频方向：

- USB 摄像头。
- MIPI 摄像头。
- HDMI 采集卡。
- 麦克风阵列。
- 硬件编码器。

机器人方向：

- IMU。
- 电机控制器。
- 编码器。
- 激光雷达。
- 深度相机。
- CAN 总线设备。

### 10.4 需要练习的 Demo

#### Demo 1：V4L2 摄像头采集

要求：

- 打开 `/dev/video0`。
- 查询摄像头支持格式。
- 设置分辨率和像素格式。
- 使用 mmap 采集帧。
- 保存为 YUV 或 JPEG。

重点：

- V4L2 buffer 队列。
- `VIDIOC_REQBUFS`。
- `VIDIOC_QBUF`。
- `VIDIOC_DQBUF`。
- `VIDIOC_STREAMON`。

#### Demo 2：串口通信

要求：

- Linux 端打开串口。
- 配置波特率。
- 发送和接收数据。
- 设计简单协议。

重点：

- termios。
- 阻塞和非阻塞读写。
- 数据帧边界。
- 校验。

---

## 11. systemd、日志和服务化

### 11.1 必须掌握

- systemd service。
- service 启动、停止、重启。
- 开机自启动。
- 环境变量配置。
- 日志查看。
- 崩溃自动拉起。

常用命令：

- `systemctl start xxx`
- `systemctl stop xxx`
- `systemctl restart xxx`
- `systemctl status xxx`
- `journalctl -u xxx`

### 11.2 为什么重要

真实项目里程序不是手动运行的，而是作为服务运行：

- 摄像头采集服务。
- 推流服务。
- AI 推理服务。
- ROS2 节点服务。
- 设备监控服务。

你需要知道服务如何启动、如何崩溃恢复、如何看日志、如何配置参数。

### 11.3 需要练习的 Demo

#### Demo：把推流程序做成 systemd 服务

要求：

- 写一个简单推流或模拟程序。
- 配置成 systemd service。
- 支持开机自启动。
- 程序崩溃后自动重启。
- 使用 `journalctl` 查看日志。

重点：

- 工程程序要能长期稳定运行。
- 日志和自动恢复是部署能力的一部分。

---

## 12. 实时性基础

### 12.1 必须理解

- Linux 不是强实时系统。
- 调度延迟。
- 上下文切换。
- 线程优先级。
- CPU 亲和性。
- 实时调度策略。
- `SCHED_FIFO`。
- `SCHED_RR`。
- PREEMPT_RT。
- 优先级反转。

### 12.2 和音视频关系

音视频是软实时系统：

- 偶尔丢一帧可以接受。
- 但长期延迟增大不可接受。
- 要控制队列长度。
- 要避免阻塞关键线程。
- 要减少内存分配和锁竞争。

### 12.3 和机器人关系

机器人控制更接近硬实时：

- 电机控制周期要求稳定。
- IMU 数据频率要求稳定。
- 控制延迟可能影响安全。

所以常见分工是：

```text
Linux：感知、规划、AI 推理、日志、通信
MCU/RTOS：电机控制、传感器实时采集、安全保护
```

### 12.4 需要练习的 Demo

#### Demo：定时任务抖动测试

要求：

- 创建一个 1ms 或 10ms 周期任务。
- 记录实际唤醒时间。
- 统计最大延迟、平均延迟、抖动分布。
- 对比普通线程和实时优先级线程。

重点：

- 认识普通 Linux 的调度不确定性。
- 理解为什么强实时控制不直接放普通 Linux。

---

## 13. Linux 音视频专项知识

### 13.1 FFmpeg

需要掌握：

- 解封装。
- 解码。
- 编码。
- 封装。
- filter。
- swscale。
- swresample。
- time_base。
- PTS/DTS。
- AVFrame。
- AVPacket。

### 13.2 GStreamer

需要掌握：

- pipeline。
- element。
- pad。
- caps。
- bus。
- appsrc。
- appsink。
- 硬件编解码插件。

### 13.3 V4L2

需要掌握：

- 查询设备能力。
- 设置格式。
- 申请 buffer。
- mmap。
- 入队和出队。
- 开始和停止采集。

### 13.4 硬件编解码

需要了解：

- VAAPI。
- NVENC/NVDEC。
- V4L2 M2M。
- MPP。
- VideoToolbox 和 MediaCodec 的类比。
- 硬件帧和软件帧。
- 零拷贝。

### 13.5 推荐专项项目

#### 项目：Linux 低延迟摄像头推流系统

功能：

- V4L2 采集。
- GStreamer 或 FFmpeg 编码。
- RTSP/WebRTC 推流。
- 支持分辨率、帧率、码率配置。
- 统计采集、编码、发送各阶段耗时。

重点：

- 低延迟链路设计。
- 线程队列设计。
- 时间戳处理。
- 丢帧策略。
- 性能分析。

---

## 14. Linux 机器人专项知识

### 14.1 ROS2 相关

需要掌握：

- node。
- topic。
- service。
- action。
- parameter。
- launch。
- rosbag。
- rviz。
- tf2。
- DDS。

### 14.2 传感器接入

需要了解：

- USB 摄像头。
- MIPI 摄像头。
- 深度相机。
- IMU。
- 雷达。
- CAN 电机。

### 14.3 数据同步

需要重点理解：

- 系统时间。
- 硬件时间戳。
- 软件时间戳。
- 相机和 IMU 同步。
- 多路相机同步。
- rosbag 回放。

### 14.4 推荐专项项目

#### 项目：ROS2 摄像头 + AI 推理节点

功能：

- Linux 摄像头采集。
- 发布 ROS2 Image topic。
- 订阅图像做目标检测。
- 发布 Detection topic。
- 使用 rviz 可视化。
- 使用 rosbag 录制。

重点：

- Linux 音视频和 ROS2 的结合。
- AI 推理如何接入机器人感知链路。
- 延迟和队列如何控制。

---

## 15. 学习顺序建议

### 第一阶段：Linux 基础和命令

目标：

- 熟练使用 Linux。
- 能看日志、查进程、查端口、查资源。
- 能写简单 shell 脚本。

建议时间：

- 2 到 4 周。

### 第二阶段：Linux C/C++ 系统编程

目标：

- 掌握文件 IO、进程、线程、socket、epoll、IPC。
- 能写多线程网络程序。
- 能做基本性能排查。

建议时间：

- 2 到 3 个月。

### 第三阶段：Linux 音视频

目标：

- 掌握 FFmpeg、GStreamer、V4L2。
- 能做摄像头采集、编码、推流。
- 能分析延迟和丢帧。

建议时间：

- 3 到 6 个月。

### 第四阶段：边缘设备和硬件加速

目标：

- 熟悉 Jetson/RK3588。
- 跑通硬件编解码。
- 跑通视频 + AI 推理链路。

建议时间：

- 3 到 6 个月。

### 第五阶段：机器人系统

目标：

- 掌握 ROS2。
- 接入摄像头、IMU。
- 做 AI 推理节点。
- 理解时间同步和坐标系。

建议时间：

- 3 到 6 个月。

### 第六阶段：嵌入式 Linux 和实时控制

目标：

- 了解设备树、驱动、UART/I2C/SPI/CAN。
- 理解 Linux 和 MCU/RTOS 的分工。
- 能做 Linux + MCU 通信 Demo。

建议时间：

- 长期补齐。

---

## 16. 最小项目路线

如果你想用项目驱动学习，可以按这个顺序：

### 项目 1：Linux 多线程文件处理工具

覆盖：

- 文件 IO。
- 多线程。
- 队列。
- 性能统计。

### 项目 2：epoll 网络服务器

覆盖：

- socket。
- 非阻塞 IO。
- epoll。
- 协议设计。

### 项目 3：V4L2 摄像头采集工具

覆盖：

- `/dev/video0`。
- ioctl。
- mmap。
- YUV 数据。

### 项目 4：Linux 摄像头编码推流

覆盖：

- V4L2。
- FFmpeg/GStreamer。
- H.264/H.265。
- RTSP/RTP/WebRTC。

### 项目 5：边缘设备视频 AI 推理

覆盖：

- Jetson/RK3588。
- TensorRT/ONNX Runtime。
- OpenCV。
- 硬件加速。

### 项目 6：ROS2 摄像头感知节点

覆盖：

- ROS2 topic。
- Image message。
- camera_info。
- rviz。
- rosbag。

### 项目 7：Linux + MCU 通信

覆盖：

- UART/CAN。
- 简单协议。
- 传感器数据上报。
- ROS2 topic 发布。

---

## 17. 求职导向的能力清单

### Linux 音视频岗位

重点准备：

- FFmpeg。
- GStreamer。
- V4L2。
- RTSP/RTP/WebRTC。
- C++ 多线程。
- Linux 网络编程。
- 性能优化。
- 音画同步。
- 低延迟链路。

### 边缘 AI 音视频岗位

重点准备：

- Jetson/RK3588。
- 硬件编解码。
- TensorRT/ONNX Runtime。
- OpenCV。
- DMA Buffer。
- 零拷贝。
- 视频帧预处理。
- 推理性能优化。

### 机器人感知岗位

重点准备：

- ROS2。
- camera driver。
- Image topic。
- camera_info。
- rosbag。
- rviz。
- TF。
- 相机标定。
- 时间同步。
- AI 推理部署。

### 嵌入式 Linux 岗位

重点准备：

- ARM Linux。
- 交叉编译。
- 设备树。
- 字符设备驱动。
- UART/I2C/SPI/CAN。
- systemd。
- boot 和 rootfs 基础。

---

## 18. 总结

你在 Linux 上需要补齐的不是单一知识点，而是一条系统能力链：

```text
Linux 基础使用
    -> Linux C/C++ 系统编程
    -> 多线程/网络/IPC/IO
    -> 调试和性能分析
    -> FFmpeg/GStreamer/V4L2
    -> 边缘设备和硬件加速
    -> ROS2 和机器人感知
    -> 嵌入式 Linux 与实时控制
```

对你的转型来说，优先级最高的是：

1. Linux C/C++ 系统编程。
2. 多线程、socket、epoll、IPC。
3. gdb、perf、strace、valgrind。
4. FFmpeg/GStreamer/V4L2。
5. Jetson/RK3588 边缘部署。
6. ROS2 摄像头和 AI 推理链路。
7. UART/CAN/设备树/驱动基础。

一句话：

> 先把 Linux 当成音视频工程平台学扎实，再把它扩展到边缘设备和机器人感知系统，这样最符合你当前背景，也最容易形成可迁移的竞争力。
