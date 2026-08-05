# C++ STL 算法与迭代器（选学 · 非必须）

## 0. 本篇定位

- 面试复习：先掌握迭代器分类、常用算法、erase-remove 惯用法和谓词/lambda 配合。
- 深入学习：重点看算法复杂度、区间边界和容器失效规则。
- 音视频落点：帧索引、时间线片段、缓存清理和统计汇总都适合用 STL 算法表达，但热路径要注意分配和拷贝。
> ⭐ **选学**：容器是高频（见 [12-STL容器底层](12-STL容器底层.md)），算法与迭代器属“会用就好”，面试偶尔考 `erase-remove`、迭代器分类、谓词。补在这里求完整。
>
> 相关：容器底层与迭代器失效见 [12-STL容器底层](12-STL容器底层.md)；谓词常用 Lambda 见 [10-可调用对象-Lambda与std-function](10-可调用对象-Lambda与std-function.md)；C++20 ranges 见 [17-C++20与协程](17-C++20与协程.md)。

---

## 面试速记

- **迭代器是算法与容器的“胶水”**：算法只认迭代器，不认具体容器，从而一套算法适配所有容器。
- **五类迭代器**（能力递增）：输入 / 输出 → 前向 → 双向 → 随机访问。不同算法要求不同的最低迭代器能力（如 `sort` 要随机访问，所以 `list` 不能用 `std::sort`，要用 `list::sort`）。
- **`erase-remove` 惯用法**：`remove` 只是把要删的元素移到末尾、返回新逻辑尾，**不真正删**；要配合 `erase` 才删除。C++20 起可直接用 `std::erase`/`std::erase_if`。
- **算法 + Lambda 谓词**：`find_if`/`count_if`/`sort`/`transform` 配 Lambda 是日常主力。

---

## 一、迭代器分类

| 类别 | 能力 | 代表容器 | 典型算法要求 |
| :--- | :--- | :--- | :--- |
| 输入 InputIterator | 单遍只读前进 | istream | `find` |
| 输出 OutputIterator | 单遍只写前进 | ostream、`back_inserter` | `copy` 的目标 |
| 前向 ForwardIterator | 多遍读写前进 | `forward_list` | `replace` |
| 双向 BidirectionalIterator | 可 `++`/`--` | `list`、`map`、`set` | `reverse` |
| 随机访问 RandomAccessIterator | `it + n`、`it[n]`、比较 | `vector`、`deque`、`array` | `sort`、二分 |

> 经典题：**为什么 `std::sort` 不能用于 `std::list`？** 因为 `sort` 需要随机访问迭代器（要 `it+n` 做分区），而 `list` 只有双向迭代器，所以 `list` 提供成员函数 `list::sort`（基于链表归并）。

---

## 二、常用算法速览

```cpp
#include <algorithm>
#include <numeric>
std::vector<int> nums{5, 3, 8, 1, 9, 2};

// 查找
auto it = std::find_if(nums.begin(), nums.end(), [](int n){ return n > 5; });
int big = std::count_if(nums.begin(), nums.end(), [](int n){ return n % 2; });

// 排序（随机访问迭代器）
std::sort(nums.begin(), nums.end(), [](int a, int b){ return a > b; }); // 降序
bool found = std::binary_search(nums.begin(), nums.end(), 8);           // 需已排序

// 变换 / 累加
std::vector<int> squared(nums.size());
std::transform(nums.begin(), nums.end(), squared.begin(), [](int n){ return n*n; });
int total = std::accumulate(nums.begin(), nums.end(), 0);

// 最值
auto [minIt, maxIt] = std::minmax_element(nums.begin(), nums.end());
```

`back_inserter` 等插入迭代器把“写”变成“push_back”：

```cpp
std::vector<int> dst;
std::copy_if(nums.begin(), nums.end(), std::back_inserter(dst),
             [](int n){ return n > 3; });  // 满足条件的 push_back 进 dst
```

---

## 三、erase-remove 惯用法（高频坑）

`std::remove` / `remove_if` **并不真正删除元素**——它把不删的元素往前搬，把要删的“逻辑上”挪到末尾，返回新的逻辑尾迭代器；容器大小不变。必须再调 `erase` 把尾巴真正删掉：

```cpp
std::vector<int> data{1, 2, 3, 4, 3, 5};
// 删除所有值为 3 的元素
data.erase(std::remove(data.begin(), data.end(), 3), data.end());  // 经典两段式

// 按条件删除
data.erase(std::remove_if(data.begin(), data.end(),
                          [](int n){ return n % 2 == 0; }),
           data.end());

// C++20 起一行搞定：
std::erase(data, 3);
std::erase_if(data, [](int n){ return n % 2 == 0; });
```

> 关联容器（`map`/`set`）没有这个问题，直接 `erase(key)` 或 `erase(it)`，且 C++11 起 `erase` 返回下一个有效迭代器，便于边遍历边删（见 [12-STL容器底层](12-STL容器底层.md)）。

---

## 四、自定义迭代器（了解即可）

让自己的容器能被 range-for 和 STL 算法使用，需要提供迭代器类型并实现相应操作符。最小一个前向迭代器示例：

```cpp
class IntRange {
    int begin_, end_;
public:
    IntRange(int b, int e) : begin_(b), end_(e) {}
    struct Iterator {
        int value;
        int operator*() const { return value; }
        Iterator& operator++() { ++value; return *this; }
        bool operator!=(const Iterator& other) const { return value != other.value; }
    };
    Iterator begin() const { return {begin_}; }
    Iterator end()   const { return {end_}; }
};

for (int v : IntRange(0, 5)) { /* 0,1,2,3,4 */ }
```

> C++20 起更推荐用 ranges/views 组合，少手写迭代器（见 [17-C++20与协程](17-C++20与协程.md)）。

> 面试一句话：“STL 用迭代器解耦算法和容器，迭代器分输入/输出/前向/双向/随机访问五类，算法按最低能力要求迭代器——`sort` 要随机访问所以 `list` 用成员 `sort`。最常被问的坑是 `remove` 不真删，要配 `erase` 组成 erase-remove 惯用法，C++20 用 `std::erase`/`erase_if` 一行解决。”
