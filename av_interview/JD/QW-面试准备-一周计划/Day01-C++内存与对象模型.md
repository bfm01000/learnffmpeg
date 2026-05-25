# Day 1 · C++ 内存与对象模型

**目标**：夯实内推强调的「C++ 特别扎实」基础层，并能映射到 4K 直播里的 `shared_ptr` / 跨线程释放。  
**建议时长**：4～5 小时

---

## 上午块（2h）· 理论精读

### 1. 必会知识点清单

- [ ] **RAII**：资源获取即初始化；析构自动释放；对比 Java try-finally
- [ ] **三/五/零法则**：何时需要自定义析构、拷贝构造、拷贝赋值、`=default`、`=delete`
- [ ] **智能指针**：`unique_ptr` 独占、`shared_ptr` 引用计数、`weak_ptr` 打破循环引用
- [ ] **移动语义**：左值/右值、`std::move`、移动构造减少拷贝；`std::forward` 了解即可
- [ ] **虚函数**：vtable 概念、**虚析构**为何必须、多态下的对象切片
- [ ] **对象生命周期**：栈 vs 堆；悬空指针、use-after-free

### 2. 推荐阅读（选 1～2 即可）

- 《Effective Modern C++》条款 13～22（智能指针与移动）
- 或 cppreference：`std::shared_ptr` / `std::unique_ptr` / 虚析构条目

### 3. 自检题（闭卷能答）

1. `shared_ptr` 线程安全吗？引用计数安全，**所指对象不自动线程安全**。
2. 异步发送队列里 `Lambda` 捕获 `shared_ptr<Muxer>` 解决了什么问题？
3. 为什么基类析构函数应是 virtual？
4. `std::move` 后对象还能用吗？处于 valid but unspecified 状态。

---

## 下午块（2h）· 结合项目

### 任务 A：重写「4K 直播 · 异步发送」的 C++ 叙事（45min）

用下面结构写 bullet（可直接贴简历/口述）：

```
问题：编码线程被 RTMP send 同步阻塞 → 雪崩掉帧
埋点：SFT_SCOPE 四段耗时，定位网络 I/O
方案：DispatchQueue2 生产者-消费者 + shared_ptr 保活 Muxer + 队列水位 ABR
结果：长时推流 5fps → 29-30fps
```

重点准备追问：**为何用 `shared_ptr` 而不是裸指针或 `unique_ptr`？**

### 任务 B：手写 mini 代码（45min）

在本地或 LeetCode Playground 写一段（不要求编译进工程）：

```cpp
// 1. 线程 A 往队列丢任务，任务里捕获 shared_ptr<Worker>
// 2. 主线程销毁 Worker 前，队列里任务仍安全
// 3. 注释说明引用计数何时 +1/-1
```

### 任务 C：刷题 1 题（30min）

- [LeetCode 146. LRU Cache](https://leetcode.cn/problems/lru-cache/)（哈希 + 链表，考数据结构基本功）
- 或 剑指 Offer 09「用两个栈实现队列」

---

## 晚间（30min）· 每日固定

- [ ] 录音：3 分钟讲「4K 直播」C++ 向版本（不看稿第二遍）
- [ ] 在 `周复盘清单.md` 记 1～3 个盲点

---

## 今日产出物

- [ ] `Day01-笔记.md`（自建，可选）或直接在复盘清单打勾
- [ ] 4K 项目 C++ 叙事 bullet 一版定稿
- [ ] LRU 或队列题 AC 1 道
