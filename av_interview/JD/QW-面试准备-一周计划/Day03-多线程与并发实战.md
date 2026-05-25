# Day 3 · 多线程与并发实战

**目标**：生产者-消费者、条件变量、背压——能白板实现并映射三个项目。  
**建议时长**：4～5 小时

---

## 上午块（2h）· 并发核心

### 1. 必会知识点

- [ ] `std::mutex` + `std::lock_guard` / `unique_lock`
- [ ] `std::condition_variable`：`wait` 谓词、虚假唤醒、`notify_one/all`
- [ ] **生产者-消费者**：有界队列、满/空阻塞、优雅退出（`stop` 标志）
- [ ] **死锁**：四个必要条件；`std::lock` 同时锁多把锁
- [ ] **背压（Backpressure）**：队列水位 → ABR 降码率（你的 4K 项目）

### 2. 必读本地文档

| 文档 | 路径 |
|------|------|
| 并发控制与无锁队列 | `av_interview/interview/桌面端面试文档/并发控制与无锁队列.md` |
| JNI 回调（跨线程） | `learnffmpeg/Doc/JNI/SDK回调.md` 前 150 行 |

### 3. 补强.md 对齐

- [ ] 能白板画：**工厂模式**在 SDK 里「创建解码器/编码器实例」的角色（Day6 会深化）
- [ ] 能白板画：**生产者-消费者**三处映射表（见下）

---

## 三项目 × 并发映射表（必背）

| 项目 | 生产者 | 消费者 | 队列/同步 | 背压/流控 |
|------|--------|--------|-----------|-----------|
| 4K 直播 | 编码线程 | 发送线程 | DispatchQueue2，容量 90 | 水位 >60 降码率，<15 恢复 |
| 自动剪辑 | VPU 抽帧 | GPU 渲染 → NPU 推理 | 两阶段队列 | 并行隐藏抽帧/渲染耗时 |
| 车载预览 AJB | 解码线程 | 渲染线程 | YUV 有界队列 | EMA 调目标缓冲 + 过期丢帧 |

---

## 下午块（2h）· 手写 + 项目

### 任务 A：手写有界阻塞队列（60min）

要求（C++11，纸笔或 IDE 二选一）：

```cpp
template<typename T>
class BlockingQueue {
  // push: 满则 wait
  // pop:  空则 wait
  // stop(): 唤醒所有线程安全退出
};
```

对照 `simple_frame_pool` / 你熟悉的 `DispatchQueue2` 设计，**不要求一模一样**，逻辑正确即可。

### 任务 B：AJB 并发叙事（45min）

写清：

- 为何用 `steady_clock`（单调时钟 vs 系统时钟）
- 解码线程与渲染线程如何协作
- 「过期帧丢弃」与「背压追赶」如何不破坏线程安全

### 任务 C：刷题 1 题（30min）

- [111. 二叉树的最小深度](https://leetcode.cn/problems/minimum-depth-of-binary-tree/)（BFS/DFS 热身）
- 或 [剑指 Offer 31. 栈的压入弹出序列](https://leetcode.cn/problems/stack-push-pop-sequence-lcci/)

---

## 晚间（30min）

- [ ] 录音：3 分钟讲「车载 AJB」（强调时钟域 + 队列 + 丢帧策略）
- [ ] **简历标题**若未改，今晚改完

---

## 今日产出物

- [ ] 阻塞队列手写一版 + 能讲三个项目映射表
- [ ] AJB 并发追问答案提纲
- [ ] 算法 +1 题 AC
