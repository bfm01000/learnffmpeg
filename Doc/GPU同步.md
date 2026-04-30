# GPU同步策略说明：`glFinish` vs `glFenceSync + glWaitSync`

本文面向当前预览渲染链路（`AndroidTextureRender::ReadFrame`）中的两种同步流程，解释它们的差异、适用场景和选型建议。

---

## 1. 两种流程分别做了什么

### 流程A：`glFinish`（全局阻塞）

典型形态：

1. 提交前序GPU命令
2. 调用 `glFinish()`
3. CPU阻塞，直到**当前上下文里所有已提交命令**都执行完成
4. 再继续后续渲染/交换

核心特点：

- 同步粒度粗（全队列）
- 最保守，最容易“看起来正确”
- 对实时预览不友好，容易产生长阻塞

---

### 流程B：`glFenceSync + glFlush + glWaitSync`（精确同步）

典型形态：

1. `GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);`
2. `glFlush()`，确保命令尽快送到GPU
3. 切换到消费者侧上下文（如 offscreen context）
4. 在确实需要消费该结果前执行 `glWaitSync(fence, ...)`
5. 删除 fence，继续后续渲染

核心特点：

- 同步粒度细（只针对该 fence 之前的命令）
- 比 `glFinish` 更可控，通常阻塞更小
- 需要正确管理 fence 生命周期与上下文关系

---

## 2. 关键差异（性能与行为）

### 同步范围

- `glFinish`：等待“全部已提交GPU命令”
- `fence+wait`：等待“某个里程碑之前的命令”

### 对CPU线程的影响

- `glFinish`：CPU更容易被长时间挂起
- `fence+wait`：通常只在真正需要数据可见性时等待

### 长尾风险

- `glFinish`：长尾更明显，容易把所有阶段拖慢
- `fence+wait`：更容易把等待局限在必要区间，便于定位问题

### 实现复杂度

- `glFinish`：低
- `fence+wait`：中（需处理失败回退、资源清理、统计口径）

---

## 3. 为什么“没有CPU拷贝”也会慢

即使链路是 AHardwareBuffer/纹理路径，没有显式 `memcpy`，仍会慢，常见原因：

1. GPU算法本身重（例如 stitch/blend/fusion）
2. GPU队列拥塞导致同步点等待
3. `eglSwapBuffers` 受 BufferQueue/VSYNC 反压影响
4. 上下文切换、FBO blit、颜色转换 pass 都是GPU工作（不是CPU memcpy）

所以“零拷贝”不等于“零等待”。

---

## 4. 什么场景用哪个

### 场景1：实时预览（低延迟、允许偶发跳帧）

推荐：`fence + wait`（必要时进一步做非阻塞/超时策略）

原因：

- 更利于控制平均时延和尾延迟
- 更适合30/60fps实时链路
- 可结合慢帧策略（降级/跳帧）维持流畅性

---

### 场景2：离线导出/高一致性任务（吞吐优先、延迟不敏感）

可选：`glFinish` 或更保守同步策略

原因：

- 正确性优先，性能波动可接受
- 调试和问题复现更直接

---

### 场景3：问题定位阶段（怀疑跨上下文可见性）

建议：

1. 先用 `glFinish` 验证问题是否消失（建立对照组）
2. 再切回 `fence+wait` 精确定位等待来源
3. 最终回到细粒度方案上线

---

## 5. 当前工程建议

结合当前日志特征（慢帧由算法+swap长尾共同驱动）：

1. 预览主路径优先使用 `fence+wait`，避免全局 `glFinish`
2. 保留“严格同步开关”用于A/B验证与线上应急
3. 统计上持续观察：
   - `totalP95/P99`
   - `swapP95/P99`
   - 超预算帧占比与预算债务
4. 若仍有抖动，再加“非阻塞wait + 超时跳帧”策略

---

## 6. 概念补充：何时需要 `glWaitSync`、与串行滤镜 / 推流的关系

本节把 fence / `glWaitSync` 的语义和「渲染 → 编码 → RTMP」的关系说清楚，避免与「串行滤镜」「解码器」等概念混在一起。

### 6.1 `glWaitSync` 立刻返回，为什么还要调？

- **`glWaitSync` 基本上不阻塞 CPU**：调用后 CPU 马上可以继续往下执行。
- **阻塞发生在 GPU 侧**：在当前（消费者）上下文的 **GPU 命令队列**里插入一条「必须等 fence signaled 之后，后面的绘制 / blit / 采样才能执行」的依赖。
- **直观理解**：CPU 像主管只负责把工单递进去；**真正干活的是 GPU**，`glWaitSync` 保证的是 **「工人 B 在用这块纹理之前，工人 A 那一批写入在 GPU 上已经完成了」**，而不是保证「你的 C++ 函数返回时整张图已经画完」。

若既不 `glWaitSync` / `glClientWaitSync`，也不 `glFinish`，又没有其它等价同步，**跨上下文复用同一张纹理**时，消费者可能在 GPU 上 **采样到尚未写完的数据**（花屏、闪屏、脏纹理等）。

### 6.2 Fence 是不是「一个状态」？

可以粗略理解：**同步对象从未完成 → 已完成（signaled）**，这个拐点由 **GPU 在执行完 fence 之前的命令后** 推动，而不是应用随手设的布尔变量。

它和「编码器内部盯着你这个 `GLsync` 句柄」**不是一回事**：编码走 Surface / BufferQueue / release fence 时，往往是 **另一套**「buffer 上的写入是否结束」的约定；思想类似「等画完再用」，**不一定等于你在 GL 里创建的那一个 fence 对象**。

### 6.3 是不是每一个 GPU 操作都必须等上一个「全部」做完？

**不是。** GPU 会重叠执行大量无关工作以提高吞吐；API 要保证的是 **数据依赖**（例如先写到纹理再采样），而不是把整个 GPU 串成单线程。

- **同一 EGL/OpenGL 上下文、顺序提交**：滤镜 A 写到 FBO，滤镜 B 再采这张纹理，属于驱动能处理的依赖链，**一般不需要**在 A、B 之间再插 `glWaitSync`。
- **典型必须格外小心**：换了一个 context 还去动 **同一块共享纹理**（例如 `AndroidTextureRender::ReadFrame`：生产者 context 写完 → `makeCurrent` 到离屏 context 再 blit），此时 **显式 `glFenceSync + glWaitSync`（或失败时 `glFinish`）** 才有针对性。

### 6.4 本质结论：为什么我们会用到 `glWaitSync`？

**本质往往是「两条 GPU 命令流 + 共享纹理（或等价的 GPU 资源）」**，而不是「RTMP 协议要求」。

| 场景 | 是否需要应用层 `glFenceSync` / `glWaitSync`（典型） |
|------|------------------------------------------------------|
| 同上下文串行滤镜链 A → B | **一般不需要** |
| 跨上下文：context A 写纹理 → context B 读/ blit（如 `ReadFrame`） | **需要**（或 `glFinish` 兜底） |
| 同上下文直接渲染到 **MediaCodec Input Surface** | **一般不需要再套一层**；「画完再给编码读」多由 **Surface / BufferQueue / fence fd** 等在系统侧对齐 |
| CPU 读 GPU 结果（`glReadPixels`、map 等） | 需要 **`glClientWaitSync` / `glFinish`** 等 **CPU 侧**可见性，单靠 `glWaitSync` 不够 |

### 6.5 渲染 → 编码 → RTMP 要不要 `glWaitSync`？

- **RTMP**：只处理编码后的字节流，**与 OpenGL fence 无关**。
- **编码环节**：需要的是 **「编码器消费的那一帧，对应 GPU 写入已就绪」**；若路径是 **单 context 直投 Surface**，通常 **不必**在业务里再对每个 pass 写 `glWaitSync`，同步多在缓冲队列层完成。
- **工程里 `ReadFrame` 的 `glWaitSync`**：针对的是 **跨 context 消费渲染纹理**，与「码流送进硬解码器」不是同一条链路；不要混成「送给解码器前必须 `glWaitSync`」。

---

## 7. 一句话决策规则

- **实时预览优先流畅**：用 `glFenceSync + glWaitSync`（必要时非阻塞化）
- **离线/保守正确性优先**：可用 `glFinish`
- **不建议长期在实时链路使用全局 `glFinish`**
- **跨上下文共享纹理**：需要 fence / wait（或 `glFinish`）；**同上下文串行滤镜 / 直投编码 Surface**：一般不必再叠一层 `glWaitSync`

---

## 8. 架构级终极优化方案：双线程+共享上下文 (Triple Buffering)

当我们面临的问题不再是单纯的等待算法执行（CPU/GPU 指令堆叠），而是**不可控的系统级 `eglSwapBuffers` VSync 阻塞（长达 30ms+）**时，无论在单线程里使用 `glFinish` 还是 `glFenceSync` 都无法解决根本的掉帧问题。

这时需要从“单线程同步等待”升级为“**双线程异步解耦**”架构。

### 8.1 为什么不能用现成的 `DispatchQueue2` 直接包装 `eglSwapBuffers`？

在 Android/OpenGL 开发中，**绝不能把 `eglSwapBuffers` 直接扔进一个普通的后台线程队列（如 `DispatchQueue2`）去执行。**

* **原因：** OpenGL 具有严格的**上下文线程绑定机制（EGL Context Binding）**。
* **现象：** `eglSwapBuffers` 必须在“已经绑定了主屏幕 Window Surface 且拥有当前 EGL Context”的线程中被调用。如果 `DispatchQueue2` 的后台线程没有绑定这个 Context，调用会直接引发 `EGL_BAD_CONTEXT` 崩溃或黑屏。

### 8.2 正确的改造思路：拆分“渲染器”与“上屏员”

核心思想是让繁重的算法脱离主屏幕的 VSync 绑定，使用**共享上下文（Shared Context）**技术在两个真实线程间传递纹理。

#### 步骤一：创建跨线程的“纹理同步队列”
- 实现一个线程安全的队列（大小为 2 到 3，实现 Triple Buffering）。
- 队列里存放的不是画面数据（不拷贝），而是**离屏渲染好的 Texture ID**（或封装了 Texture 的 `MediaTextureGroup`）。

#### 步骤二：EGL 上下文改造（核心难点）
需要两套 EGL Context，它们通过 `share_context` 参数互通：
1. **渲染环境（Render Context）：** 运行在当前的算法主线程。它不再绑定手机的 Window Surface，而是绑定一个 1x1 的 Pbuffer Surface（Dummy Surface）。它专心跑算法，把结果画到 FBO（离屏纹理）上。
2. **显示环境（Display Context）：** 运行在一个**新建的专属上屏线程**。在创建这个 Context 时，必须把“渲染环境的 Context”作为 `share_context` 传入。并把这个环境绑定到手机真实的 Window Surface 上。

#### 步骤三：流水线运转机制
1. **渲染主线程（原流程）：**
   - 取帧 -> 执行拼接算法 -> 渲染到 FBO 的 Texture 上。
   - 执行 `glFlush()` 确保指令发送。
   - 把 Texture ID 放入“纹理同步队列”。
   - **立刻返回**处理下一帧，**绝不调用 `eglSwapBuffers`**。
2. **专属上屏线程（新流程）：**
   - 死循环：从“纹理同步队列”阻塞获取一个就绪的 Texture ID。
   - 在 Display Context 下，执行极简的 OES/2D 纹理贴图（画一个全屏矩形）。
   - 调用 `eglSwapBuffers` 将画面上屏。
   - **所有的 VSync 卡顿都被吸收到这个线程，它卡死也不会阻塞主线程的算法进度。**
   - 释放 Texture ID 归还给渲染线程复用。

### 8.3 评估

这种双线程异步队列的改造需要投入较高的开发成本（处理 EGL 生命周期的各种 Edge Case），但它是**彻底根治 VSync 卡顿、实现稳定 30/60 FPS 实时渲染的唯一行业标准解法**。

