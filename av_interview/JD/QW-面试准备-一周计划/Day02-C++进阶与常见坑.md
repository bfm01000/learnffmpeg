# Day 2 · C++ 进阶与常见坑

**目标**：覆盖内核面试常问的内存序、对齐、UB；对齐本地已有 CPP 面试文档。  
**建议时长**：4～5 小时

---

## 上午块（2h）· 理论 + 本地文档

### 1. 必会知识点

- [ ] **内存对齐**：`alignof` / `sizeof`、结构体 padding、为何缓存行对齐影响性能
- [ ] **内存序（了解）**：`memory_order_relaxed/acquire/release/seq_cst`；`std::atomic` 与 `volatile` 区别
- [ ] **常见 UB**：越界、有符号溢出、未初始化变量、双重释放、data race
- [ ] **模板基础**：函数模板、类模板、`typename`；SFINAE 知道名词即可
- [ ] **STL 容器选型**：`vector` vs `deque`；`unordered_map` 平均 O(1)；迭代器失效

### 2. 必读本地文档（约 1h）

| 文档 | 路径 | 重点章节 |
|------|------|----------|
| CPP 锁与音视频实战 | `av_interview/interview/桌面端面试文档/CPP锁机制与音视频实战.md` | 锁粒度、死锁四条件 |
| 中高级 CPP 模拟面试 | `av_interview/interview/中高级CPP开发工程师模拟面试.md` | 通读，标出不熟悉的题 |

### 3. 自检题

1. `std::atomic<int>` 能否替代所有互斥锁？不能，复合操作仍需锁。
2. 零拷贝里「刻意不加 CPU_READ」避免 Gralloc 退化——用**性能直觉**向面试官解释。
3. `vector` 扩容时迭代器为何失效？

---

## 下午块（2h）· 对象池 + 算法

### 任务 A：对象池 / 内存池话术（45min）

结合**自动剪辑项目**：

- GPU 渲染结果 → 算法前用**对象池**接收，避免碎片
- 对比 `AVBufferPool` / FFmpeg 引用计数（`AVBufferRef`）
- 一句话：**减少热路径 alloc/free，降低 cache miss 与碎片**

阅读（如有）：`桌面端面试文档/simple_frame_pool.cpp`（15min 扫实现思路）

### 任务 B：时间线算法（30min，了解）

浏览 `桌面端面试文档/时间线区间合并算法.md`——剪辑/编辑器岗常考，内核岗可能作逻辑思维题。

### 任务 C：刷题 2 题（1h）

| 题号 | 题目 | 考点 |
|------|------|------|
| 1 | [206. 反转链表](https://leetcode.cn/problems/reverse-linked-list/) | 指针基本功 |
| 2 | [215. 数组中的第K个最大元素](https://leetcode.cn/problems/kth-largest-element-in-an-array/) | 快选 / 堆 |

---

## 晚间（30min）

- [ ] 录音：3 分钟讲「自动剪辑 · Smart Seek + 对象池」（C++ 向）
- [ ] 复盘清单更新

---

## 今日产出物

- [ ] 能解释「为何对象池比频繁 new/delete 更适合热路径」
- [ ] 链表 + 堆/快选各 AC 1 道
- [ ] 标记 CPP 模拟面试 doc 里待第二轮复习的题号
