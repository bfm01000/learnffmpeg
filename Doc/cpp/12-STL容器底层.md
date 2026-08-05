# C++ STL 容器底层与选型

## 0. 本篇定位

- 面试复习：先掌握 vector 扩容、迭代器失效、deque 分段、map/unordered_map 底层和 emplace 语义。
- 深入学习：重点看容器选型如何影响内存局部性、实时稳定性和引用失效。
- 音视频落点：帧队列、缓存表、时间戳索引和任务容器不能只按“会用 STL”选择，要看访问模式和生命周期。
> 面试高频：“vector 怎么扩容？迭代器什么时候失效？map 和 unordered_map 区别？emplace 和 push 区别？”本篇讲常用容器的底层结构、复杂度与选型。
>
> 相关：扩容触发的拷贝/移动见 [07-移动语义与右值引用](07-移动语义与右值引用.md)；内存分配与对齐见 [05-内存管理与对象生命周期](05-内存管理与对象生命周期.md)。

---

## 面试速记

| 容器 | 底层结构 | 随机访问 | 插入/删除 | 关键陷阱 |
| :--- | :--- | :--- | :--- | :--- |
| `vector` | 连续动态数组 | O(1) | 尾部均摊 O(1)，中间 O(n) | 扩容使**所有**迭代器失效 |
| `deque` | 分段连续（多块） | O(1) | 两端 O(1) | 中间插入慢，指针稳定性弱 |
| `list` | 双向链表 | O(n) | 任意位置 O(1) | 缓存不友好 |
| `map`/`set` | 红黑树 | — | O(log n) | 有序；节点稳定，删除只失效被删节点 |
| `unordered_map`/`set` | 哈希表（桶+链/开放） | — | 均摊 O(1) | rehash 使迭代器失效；无序 |

一句话选型：**默认 `vector`；要按 key 查且需有序遍历用 `map`，只查不要序用 `unordered_map`；频繁两端进出用 `deque`；几乎不用 `list`（缓存不友好）。**

---

## 一、vector：连续数组与扩容

### 1. 三个指针 + 容量

`vector` 内部维护一段连续内存，概念上是三个指针：`begin`（起始）、`end`（已用末尾）、`capacity_end`（已分配末尾）。`size()` = 已用元素数，`capacity()` = 已分配能放下的元素数。

### 2. 扩容机制（必考）

当 `push_back` 时 `size == capacity`，触发扩容：

1. 申请一块**更大的新内存**（GCC libstdc++ 按 **2 倍**增长，MSVC 按 **1.5 倍**）。
2. 把旧元素**逐个搬到新内存**（能移动则移动，否则拷贝——见下文 noexcept）。
3. 析构旧元素、释放旧内存。

所以单次 `push_back` 最坏 O(n)，但因为容量翻倍，n 次 push_back 总搬运次数是等比级数，**均摊 O(1)**。

```cpp
std::vector<int> values;
values.reserve(1000);   // ✅ 已知规模时预分配，避免多次扩容搬运
for (int i = 0; i < 1000; ++i) values.push_back(i); // 不再触发扩容
```

> **为什么移动构造要写 `noexcept`？** 扩容搬元素时，若元素的移动构造**不保证 noexcept**，`vector` 为了异常安全（搬到一半抛异常无法回滚）会**退化成拷贝**。给移动构造/移动赋值加 `noexcept` 才能让扩容走移动、避免昂贵拷贝。详见 [07-移动语义与右值引用](07-移动语义与右值引用.md)。

### 3. 迭代器失效规则（必考）

- **扩容（重新分配）**：旧内存被释放，**所有**迭代器、指针、引用全部失效。
- **未扩容的尾部 push_back**：`end()` 失效，其余有效。
- **中间 erase/insert**：操作点及其之后的迭代器失效。

```cpp
std::vector<int> nums{1, 2, 3, 4};
for (auto it = nums.begin(); it != nums.end(); ) {
    if (*it % 2 == 0) it = nums.erase(it); // ✅ erase 返回下一个有效迭代器
    else ++it;
}
```

### 4. size vs capacity，shrink

- `clear()` 只把 `size` 清零、**不释放内存**（capacity 不变）。
- 真要还内存：`std::vector<T>().swap(v)`（swap 惯用法）或 `v.shrink_to_fit()`（C++11，非强制）。

---

## 二、deque：分段连续

`deque`（双端队列）由**多个固定大小的连续块 + 一张管理这些块的中央映射表**组成。因此：

- 支持 O(1) 随机访问（先定位块、再块内偏移）。
- **两端** push/pop 都是 O(1)，且**两端插入不会使已有元素的指针/引用失效**（但会使迭代器失效）。
- 中间插入删除是 O(n)。

适合：需要频繁两端进出、又想要随机访问的场景（如滑动窗口缓冲、帧队列）。`std::queue` / `std::stack` 默认就用 `deque` 做底层。

---

## 三、map/set vs unordered_map/set

### 1. map / set —— 红黑树（有序关联容器）

底层是**自平衡二叉搜索树（红黑树）**：

- 元素**按 key 有序**，中序遍历即升序。
- 增删查都是 **O(log n)**。
- 节点是独立分配的，**插入不失效任何迭代器，删除只失效被删节点**——指针/引用稳定性好。

### 2. unordered_map / set —— 哈希表

底层是**哈希表**（桶数组 + 冲突链）：

- 均摊 **O(1)** 增删查，但**无序**。
- **rehash**：当负载因子（`size / bucket_count`）超过 `max_load_factor` 时重新分桶，**所有迭代器失效**（指针/引用仍有效，因为元素节点本身不搬）。
- 最坏情况（大量哈希冲突）退化到 O(n)；可用 `reserve` 预分桶减少 rehash。

### 3. 选型

| 需求 | 选 |
| :--- | :--- |
| 需要按 key 有序遍历 / 范围查询（`lower_bound`） | `map` / `set` |
| 只要快速按 key 查找，不在乎顺序 | `unordered_map` / `unordered_set` |
| key 是自定义类型 | `map` 需 `operator<`；`unordered_map` 需 `hash` + `operator==` |

> 音视频里：用 `unordered_map<int, Decoder*>` 按 stream_index 查解码器（只查不需序）；用 `map<int64_t, Frame>` 按 pts 排序缓存帧（需有序，便于按时间戳取最近帧）。

---

## 四、emplace vs push/insert

`push_back`/`insert` 接收**已构造好的对象**，再拷贝/移动进容器；`emplace_back`/`emplace` 接收**构造参数**，在容器内存上**原地构造**（完美转发，见 [07-移动语义与右值引用](07-移动语义与右值引用.md)），省一次临时对象的构造与移动。

```cpp
std::vector<std::pair<int, std::string>> items;
items.push_back(std::make_pair(1, "a"));   // 构造临时 pair 再移动进去
items.emplace_back(1, "a");                // 原地用 (1,"a") 构造，无临时对象
```

- 元素构造较重时 `emplace` 更优。
- 但 `emplace` 不是永远更快：若已有现成对象，`push_back(std::move(obj))` 同样只移动一次；且 `emplace` 因绕过隐式转换检查，偶尔会接受本不该接受的参数。

---

## 五、其它高频点

- **`vector<bool>` 是特例**：位压缩存储，`operator[]` 返回代理对象而非 `bool&`，不能取地址；需要真正 bool 数组用 `std::deque<bool>` 或 `std::vector<char>`。
- **`std::array`**：固定大小、栈上、零开销，替代 C 数组。
- **`std::string` 的 SSO**：短字符串优化，短串直接存在对象内部栈缓冲，不分配堆内存。
- **遍历删除**：关联容器用 `it = container.erase(it)`（C++11 起 `erase` 返回下一迭代器）。

> 面试一句话：“`vector` 是连续数组，扩容按倍增搬数据、均摊 O(1)，扩容会让所有迭代器失效，所以移动构造要 `noexcept`、已知规模要 `reserve`。`map` 是红黑树有序、O(log n)、节点稳定；`unordered_map` 是哈希表均摊 O(1) 但无序、rehash 失效迭代器。需要有序选 map，只查选 unordered_map。”
