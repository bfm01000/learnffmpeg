# C++ STL 常用操作速查

## 0. 本篇定位

- 面试复习：这是 STL 操作速查，不建议从这里建立体系；遇到现场写代码或刷题卡壳时快速查用法。
- 深入学习：容器底层和选型回到 `12-STL容器底层.md`，算法思想回到 `19-STL算法与迭代器.md`。
- 音视频落点：工程热路径更关心复杂度、分配和失效规则，速查语法只是最后一步。
> 本篇是日常开发中最高频的 STL 容器/工具类操作速查，覆盖初始化、插入、查找、删除、遍历。
> 底层原理与选型见 [12-STL容器底层](12-STL容器底层.md)，算法与迭代器详见 [19-STL算法与迭代器](19-STL算法与迭代器.md)，智能指针见 [06-智能指针与资源管理](06-智能指针与资源管理.md)。

---

## 面试速记

| 操作 | vector | map/unordered_map | set | string |
| :--- | :--- | :--- | :--- | :--- |
| 初始化 | `{1,2,3}` / `(n, val)` | `{{k,v},{k2,v2}}` | `{1,2,3}` | `"hello"` / `(n, 'c')` |
| 尾部插入 | `push_back` / `emplace_back` | — | — | `push_back` / `+=` |
| 通用插入 | `insert(pos, val)` | `insert({k,v})` / `emplace(k,v)` / `[k]=v` | `insert(val)` | `insert(pos, str)` |
| 查找 | `find(b,e,v)` 算法 | `find(k)` 成员 | `find(v)` 成员 | `find(str)` 成员 |
| 删除 | `erase(pos)` / `erase(b,e)` | `erase(k)` / `erase(pos)` | `erase(v)` / `erase(pos)` | `erase(pos, n)` |
| 判断存在 | `find(b,e,v) != end()` | `contains(k)` (C++20) / `find(k)!=end()` / `count(k)` | `contains(v)` (C++20) / `find(v)!=end()` | `find(str) != npos` |

> **核心原则**：能用 `emplace` 不用 `push/insert`（原位构造少一次拷贝）；能用 `contains` (C++20) 不用 `find != end`（表意更清晰）；`operator[]` 在 map 中**不存在时会默认插入**，只读请用 `find`。

---

## 一、std::vector

### 1. 初始化

```cpp
#include <vector>

// 默认空
std::vector<int> v1;

// 列表初始化
std::vector<int> v2 = {1, 2, 3, 4, 5};
std::vector<int> v3{1, 2, 3};

// 指定大小 + 默认值
std::vector<int> v4(10);          // 10 个 0
std::vector<int> v5(10, -1);      // 10 个 -1

// 从数组/其他容器拷贝
int arr[] = {1, 2, 3, 4};
std::vector<int> v6(arr, arr + 4);
std::vector<int> v7(std::begin(arr), std::end(arr));

// 从另一个 vector 拷贝/移动
std::vector<int> v8(v2);              // 拷贝
std::vector<int> v9(std::move(v2));   // 移动，v2 变为空

// 预分配（推荐：已知大小时避免扩容）
std::vector<int> v10;
v10.reserve(1000);  // 容量 1000，size 仍为 0
```

### 2. 插入

```cpp
std::vector<int> v;

// 尾部插入（高频，均摊 O(1)）
v.push_back(1);
v.push_back(2);
v.emplace_back(3);   // 原位构造，比 push_back 少一次拷贝/移动

// 指定位置插入（O(n)）
auto it = v.begin() + 1;
v.insert(it, 10);                 // 在 it 前插入 10
v.insert(it, 3, 99);              // 在 it 前插入 3 个 99
v.insert(it, {4, 5, 6});           // 在 it 前插入列表
v.insert(v.end(), other.begin(), other.end());  // 末尾拼接另一个容器

// emplace 原位构造（传构造参数，不传对象）
struct Point { int x, y; Point(int x_, int y_) : x(x_), y(y_) {} };
std::vector<Point> pts;
pts.emplace_back(1, 2);            // ✅ 直接传 Point 构造参数
pts.push_back(Point(1, 2));        // ❌ 先构造临时对象再移动
pts.emplace(pts.begin(), 3, 4);    // 指定位置 emplace
```

### 3. 查找

```cpp
std::vector<int> v = {1, 3, 5, 7, 9};

// 算法查找（O(n)），返回迭代器
auto it = std::find(v.begin(), v.end(), 5);
if (it != v.end()) { /* 找到了，*it == 5 */ }

// 条件查找
auto it2 = std::find_if(v.begin(), v.end(),
                        [](int n) { return n > 6; });  // 第一个 >6 的

// 计数
int cnt = std::count(v.begin(), v.end(), 3);
int cnt2 = std::count_if(v.begin(), v.end(),
                         [](int n) { return n % 2 == 0; });

// 二分查找（需已排序，O(log n)）
bool found = std::binary_search(v.begin(), v.end(), 5);
auto lb = std::lower_bound(v.begin(), v.end(), 5);  // 第一个 >=5 的位置
auto ub = std::upper_bound(v.begin(), v.end(), 5);  // 第一个 >5 的位置
```

### 4. 删除

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 3, 6};

// 按位置删（O(n)）
v.erase(v.begin() + 2);               // 删除下标 2 的元素（值 3）

// 按区间删
v.erase(v.begin() + 1, v.begin() + 3); // 删除 [1, 3) 区间

// 删除尾部（O(1)）
v.pop_back();

// 按值删：erase-remove 惯用法
v.erase(std::remove(v.begin(), v.end(), 3), v.end());  // 删除所有值为 3 的

// 按条件删
v.erase(std::remove_if(v.begin(), v.end(),
                        [](int n) { return n % 2 == 0; }),
        v.end());  // 删除所有偶数

// C++20 简化写法
std::erase(v, 3);                            // 删所有等于 3 的
std::erase_if(v, [](int n) { return n % 2 == 0; });  // 删所有偶数

// 清空
v.clear();       // size = 0, capacity 不变
v.shrink_to_fit(); // 请求归还多余内存（非强制）
```

### 5. 排序与去重

```cpp
std::vector<int> v = {5, 3, 1, 4, 2, 3, 1};

// 升序
std::sort(v.begin(), v.end());

// 降序
std::sort(v.begin(), v.end(), std::greater<int>());
// 或
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });

// 部分排序：前 3 个最小的排到前面
std::partial_sort(v.begin(), v.begin() + 3, v.end());

// 去重（需先排序）
std::sort(v.begin(), v.end());
auto last = std::unique(v.begin(), v.end());  // 把重复的挪到末尾
v.erase(last, v.end());                       // 真正删除

// C++20 一行去重（需先排序）
std::sort(v.begin(), v.end());
auto [first, last2] = std::ranges::unique(v);
v.erase(first, last2);
```

### 6. 遍历

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

// 索引遍历
for (size_t i = 0; i < v.size(); ++i) { /* v[i] */ }

// 迭代器遍历
for (auto it = v.begin(); it != v.end(); ++it) { /* *it */ }

// 范围 for（最常用）
for (int n : v)        { /* 拷贝，只读 OK */ }
for (int& n : v)       { /* 引用，可修改 */ }
for (const int& n : v) { /* const 引用，只读且避免拷贝 */ }

// 遍历中安全删除
for (auto it = v.begin(); it != v.end(); ) {
    if (*it % 2 == 0)
        it = v.erase(it);  // ✅ erase 返回下一个有效迭代器
    else
        ++it;
}
```

### 7. 其他常用

```cpp
v.front();          // 首元素引用
v.back();           // 尾元素引用
v[2];               // 随机访问（不检查越界）
v.at(2);            // 随机访问（越界抛 std::out_of_range）
v.size();           // 元素数
v.capacity();       // 容量
v.empty();          // 是否为空
v.data();           // 底层数组指针（用于传 C 接口）
std::swap(v1, v2);  // O(1) 交换
```

---

## 二、std::map（有序） / std::unordered_map（无序）

### 1. 初始化

```cpp
#include <map>
#include <unordered_map>

// 默认空
std::map<std::string, int> m1;

// 列表初始化
std::map<std::string, int> m2 = {
    {"apple", 3},
    {"banana", 5},
    {"cherry", 2},
};

// 从另一个 map 拷贝/移动
std::map<std::string, int> m3(m2);
std::map<std::string, int> m4(std::move(m2));

// unordered_map 同理
std::unordered_map<std::string, int> um1 = {
    {"foo", 1}, {"bar", 2}, {"baz", 3},
};
```

### 2. 插入

```cpp
std::map<std::string, int> m;

// insert（key 已存在时不覆盖）
auto [it, ok] = m.insert({"key", 100});           // C++17 结构化绑定
// ok == true  表示插入成功（key 原来不存在）
// ok == false 表示 key 已存在，未覆盖，it 指向已有元素
m.insert(std::make_pair("key2", 200));

// emplace（推荐：原位构造，少一次拷贝）
auto [it2, ok2] = m.emplace("key3", 300);

// operator[]（key 存在则修改，不存在则默认构造后赋值）
m["key4"] = 400;              // 插入
m["key"] = 999;               // 覆盖（上面 insert 的 "key" 变为 999）

// insert_or_assign（C++17：存在就更新，不存在就插入）
auto [it3, ok3] = m.insert_or_assign("key", 888);  // key 存在，更新为 888
auto [it4, ok4] = m.insert_or_assign("new_key", 42); // 不存在，插入

// try_emplace（C++17：key 不存在时才构造 value，存在时啥也不做——比 operator[] 高效）
auto [it5, ok5] = m.try_emplace("expensive", 42);  // key 不存在才用 42 构造

// 批量插入
std::map<std::string, int> other = {{"x", 1}, {"y", 2}};
m.insert(other.begin(), other.end());
m.merge(other);  // C++17：把 other 的节点移入 m，不拷贝；重复 key 留在 other
```

> **面试重点**：`operator[]` vs `insert` vs `try_emplace`
> - `m[k] = v`：k 不存在时**默认构造 value 再赋值**，k 存在时覆盖。**只读查找不要用 []**，会意外插入。
> - `m.insert({k, v})`：k 存在时不覆盖。
> - `m.try_emplace(k, args...)`：k 不存在时才构造，**不移动不拷贝 key**（C++17 推荐）。

### 3. 查找

```cpp
std::map<std::string, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};

// find：返回迭代器，找不到返回 end()
auto it = m.find("b");
if (it != m.end()) {
    std::cout << it->first << " -> " << it->second << std::endl;
}

// contains（C++20，最推荐：表意最清晰）
if (m.contains("a")) { /* 存在 */ }

// count：返回 0 或 1（map 中 key 唯一）
if (m.count("a") > 0) { /* 存在 */ }

// at：返回 value 引用，不存在抛 std::out_of_range
try {
    int val = m.at("d");  // 抛异常
} catch (const std::out_of_range& e) { }

// operator[]（⚠️ key 不存在时插入默认值！只用于"读取+写入"场景）
int v = m["a"];       // key 存在，OK
int v2 = m["nonexistent"];  // ⚠️ 插入了 {"nonexistent", 0}！

// 范围查找（仅有序容器 map/set）
auto lb = m.lower_bound("b");   // 第一个 key >= "b" 的迭代器
auto ub = m.upper_bound("b");   // 第一个 key > "b" 的迭代器
auto [lo, hi] = m.equal_range("b");  // [lower_bound, upper_bound) 区间
```

### 4. 删除

```cpp
std::map<std::string, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};

// 按 key 删
size_t n = m.erase("b");  // 返回删除数量（0 或 1）

// 按迭代器删
auto it = m.find("c");
if (it != m.end()) m.erase(it);

// 按区间删
m.erase(m.begin(), m.end());

// 遍历中安全删除
for (auto it = m.begin(); it != m.end(); ) {
    if (it->second % 2 == 0)
        it = m.erase(it);  // ✅ C++11 起 erase 返回下一个迭代器
    else
        ++it;
}

// 清空
m.clear();
```

### 5. 遍历

```cpp
std::map<std::string, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};

// 范围 for（最常用）—— map 按 key 有序，unordered_map 无序
for (const auto& [key, value] : m) {  // C++17 结构化绑定
    std::cout << key << " -> " << value << std::endl;
}

// 仅遍历 key
for (const auto& key : std::views::keys(m)) { /* C++20 */ }

// 仅遍历 value
for (auto& value : std::views::values(m)) { /* C++20 */ }

// 迭代器
for (auto it = m.begin(); it != m.end(); ++it) {
    std::cout << it->first << " -> " << it->second << std::endl;
}

// 反向遍历（仅有序容器）
for (auto it = m.rbegin(); it != m.rend(); ++it) { /* 从大到小 */ }
```

### 6. unordered_map 自定义哈希

```cpp
struct MyKey { int id; std::string name; };
bool operator==(const MyKey& a, const MyKey& b) {
    return a.id == b.id && a.name == b.name;
}

// 自定义哈希（必须提供 operator== 和 hash）
struct MyHash {
    size_t operator()(const MyKey& k) const {
        return std::hash<int>{}(k.id) ^
               (std::hash<std::string>{}(k.name) << 1);
    }
};

std::unordered_map<MyKey, int, MyHash> customMap;
customMap[{1, "alice"}] = 100;
```

---

## 三、std::set（有序）/ std::unordered_set（无序）

```cpp
#include <set>
#include <unordered_set>

// 初始化
std::set<int> s1 = {3, 1, 4, 1, 5};  // {1, 3, 4, 5} 自动排序去重
std::unordered_set<int> us1 = {3, 1, 4, 1, 5};  // 无序，去重

// 插入
auto [it, ok] = s1.insert(2);   // ok: 是否插入成功（元素是否已存在）
auto [it2, ok2] = s1.emplace(6); // 原位构造

// 查找
if (s1.contains(3))     { /* C++20，最推荐 */ }
if (s1.find(3) != s1.end()) { /* 找到了 */ }
if (s1.count(3) > 0)    { /* 存在（set 中 count 只会是 0 或 1）*/ }

// 范围查找（仅有序 set）
auto lb = s1.lower_bound(3);  // 第一个 >= 3
auto ub = s1.upper_bound(3);  // 第一个 > 3

// 删除
s1.erase(3);               // 按值删
s1.erase(it);               // 按迭代器删
size_t n = us1.erase(42);   // 返回删除数量（0 或 1）

// 遍历
for (const auto& val : s1) { /* val 是 const，set 中元素不可修改 */ }

// unordered_set 自定义哈希：与 unordered_map 同理，只需 hash<Key> + operator==
```

---

## 四、std::deque（双端队列）

```cpp
#include <deque>

std::deque<int> dq;

// 两端插入/删除（O(1)）
dq.push_back(1);
dq.push_front(2);
dq.emplace_back(3);
dq.emplace_front(4);
dq.pop_back();
dq.pop_front();

// 随机访问（O(1)）
dq[0] = 10;
dq.at(0);

// 中间插入/删除（O(n)）
auto it = dq.begin() + 2;
dq.insert(it, 99);
dq.erase(it);

// 遍历：同 vector
for (const auto& v : dq) { }
```

> deque vs vector：deque 两端操作 O(1) 且不搬迁已有元素，但随机访问比 vector 多一次间接寻址。默认用 vector，需要频繁头端操作时才选 deque。

---

## 五、std::list（双向链表）

```cpp
#include <list>
// 适用场景极少（缓存不友好），但在"频繁中间插入/删除 + 迭代器不失效"场景有用

std::list<int> lst = {1, 2, 3, 4};

// 任意位置插入/删除（O(1)，有迭代器的前提下）
lst.push_back(5);
lst.push_front(0);
auto it = lst.begin();
std::advance(it, 2);   // list 迭代器不能 it+2
lst.insert(it, 99);
lst.erase(it);

// 自带的 sort/unique/merge（不能用 std::sort）
lst.sort();
lst.unique();  // 删除连续重复（需先排序）
lst.reverse();
lst.remove(3);           // 删除所有等于 3 的
lst.remove_if([](int n) { return n % 2 == 0; });

// splice：O(1) 转移节点（拼接链表，不拷贝）
std::list<int> other = {10, 20};
lst.splice(lst.end(), other);  // other 所有节点移到 lst 末尾，other 变空
```

---

## 六、std::array（定长数组）

```cpp
#include <array>

// 大小编译期确定，零开销封装 C 数组
std::array<int, 5> arr = {1, 2, 3, 4, 5};

arr[0] = 10;
arr.at(0);           // 带边界检查
arr.front();
arr.back();
arr.size();          // 编译期常量
arr.empty();         // 始终 false（size > 0）
arr.data();          // 底层 T*，传 C 接口
arr.fill(42);        // 全部填 42

// 支持迭代器、范围 for，可参与 std::sort 等算法
std::sort(arr.begin(), arr.end());

// 与 C 数组比较：array 不退化指针、知道大小、支持赋值（元素级拷贝）
int cArr[5] = {1, 2, 3, 4, 5};
// cArr = otherArr;  // ❌ C 数组不能赋值
std::array<int, 5> a1, a2;
a1 = a2;             // ✅ 元素级拷贝
```

---

## 七、std::string

```cpp
#include <string>

// 初始化
std::string s1;                    // 空串
std::string s2 = "hello";
std::string s3("world");
std::string s4(5, 'x');           // "xxxxx"
std::string s5(s2, 1, 3);         // "ell"（从 s2[1] 起取 3 个字符）

// 拼接
std::string s = "hello";
s += " world";                     // "hello world"
s.append("!!!");                   // "hello world!!!"
s.push_back('!');                  // 尾部追加单字符
s.insert(0, "prefix ");           // 指定位置插入
s.insert(s.end(), {'!', '!'});    // 末尾插入字符列表

// 数字 ↔ 字符串
std::string numStr = std::to_string(42);        // "42"
std::string piStr = std::to_string(3.14159);    // "3.141590"
int n = std::stoi("42");                         // 42
double d = std::stod("3.14");                    // 3.14
long long ll = std::stoll("9223372036854775807");

// C++17：高效数字转字符串
std::string s = std::to_chars(buf, buf + sizeof(buf), 42).ptr;  // 不分配内存

// 查找
size_t pos = s.find("world");          // 子串查找，找不到返回 npos
if (pos != std::string::npos) { /* 找到了 */ }
size_t pos2 = s.rfind("l");            // 反向查找
size_t pos3 = s.find_first_of("aeiou"); // 查找第一个元音
size_t pos4 = s.find_last_of("aeiou");
s.starts_with("hello");                // C++20
s.ends_with("!!");                     // C++20
s.contains("world");                   // C++23

// 提取子串
std::string sub = s.substr(0, 5);      // 前 5 个字符
std::string sub2 = s.substr(6);        // 从位置 6 到末尾

// 删除/替换
s.erase(0, 3);                         // 删除前 3 个字符
s.erase(std::remove(s.begin(), s.end(), ' '), s.end());  // 删除所有空格
s.pop_back();                          // 删除最后一个字符
s.replace(0, 5, "hi");                 // 把 [0,5) 替换为 "hi"

// C 接口互操作
const char* cstr = s.c_str();          // 返回 null-terminated C 字符串
const char* data = s.data();           // 同 c_str()（C++11 起保证 null-terminated）
std::string_view sv = s;               // C++17：不持有的字符串视图
```

---

## 八、std::pair / std::tuple

```cpp
#include <utility>
#include <tuple>

// --- pair ---
auto p1 = std::make_pair(1, "hello");
std::pair<int, std::string> p2 = {2, "world"};
std::pair p3{3, "c++17"};              // C++17 CTAD 推导

// 访问
std::cout << p1.first << " " << p1.second << std::endl;
auto [num, str] = p1;                   // C++17 结构化绑定

// --- tuple ---
auto t1 = std::make_tuple(1, 3.14, std::string("hello"));
std::tuple<int, double, std::string> t2 = {2, 2.718, "world"};
std::tuple t3{3, 1.414, "pi"};         // C++17 CTAD

// 按类型访问
std::cout << std::get<0>(t1) << std::endl;      // 按索引
std::cout << std::get<int>(t1) << std::endl;    // 按类型（类型唯一时可用）
std::cout << std::get<std::string>(t1) << std::endl;

// 结构化绑定
auto [i, d, s] = t1;

// 查询
size_t sz = std::tuple_size_v<decltype(t1)>;    // 元素个数

// 拼接
auto t4 = std::tuple_cat(t1, t2);

// tie：把已有变量绑定到 tuple 元素
int a; double b; std::string c;
std::tie(a, b, c) = t1;              // 解包到已有变量
// std::ignore 忽略某些元素
std::tie(a, std::ignore, c) = t1;
```

---

## 九、std::optional（C++17）

```cpp
#include <optional>

// 表示"可能没有值"的语义，替代返回 nullptr / -1 / 特殊值

std::optional<int> maybeGet(bool ok) {
    if (ok) return 42;
    return std::nullopt;               // 表示"没有值"
}

auto result = maybeGet(true);

// 判断是否有值
if (result)                 { /* 有值 */ }
if (result.has_value())     { /* 有值 */ }

// 取值（⚠️ 无值时 value() 抛异常，* 不检查）
int v1 = result.value();              // 无值时抛 std::bad_optional_access
int v2 = result.value_or(-1);         // 有值给值，无值给默认值 -1（推荐）
int v3 = *result;                     // 无值时 UB，仅在你确定有值时用

// 修改
result = 100;
result.reset();                        // 清空，回到无值状态
result.emplace(200);                   // 原位构造新值

// 单子操作
std::optional<int> opt = maybeGet(true);
auto doubled = opt.transform([](int n) { return n * 2; });     // C++23：有值则映射
auto filtered = opt.and_then([](int n) -> std::optional<int> {  // C++23：链式
    return n > 10 ? std::optional{n * 2} : std::nullopt;
});
```

---

## 十、std::variant（C++17）

```cpp
#include <variant>

// 类型安全的 union，存储多个备选类型之一

std::variant<int, double, std::string> var;

var = 42;                            // 存 int
var = 3.14;                          // 存 double（替代原来的 int）
var = "hello";                       // 存 string

// 访问方式一：std::get（不匹配则抛异常）
try {
    std::cout << std::get<std::string>(var) << std::endl;
    std::cout << std::get<int>(var) << std::endl;  // 抛 std::bad_variant_access
} catch (const std::bad_variant_access&) { }

// 访问方式二：std::get_if（返回指针，不匹配返回 nullptr）
if (auto* p = std::get_if<int>(&var)) {
    std::cout << *p << std::endl;
}

// 访问方式三：std::visit + Lambda（最推荐，编译器确保穷举）
std::visit([](const auto& val) {
    std::cout << val << std::endl;
}, var);

// 按类型处理
using T = std::variant<int, std::string>;
T v = "world";
std::visit([](const auto& val) {
    using ValueType = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<ValueType, int>)
        std::cout << "int: " << val << std::endl;
    else if constexpr (std::is_same_v<ValueType, std::string>)
        std::cout << "string: " << val << std::endl;
}, v);

// 查询
size_t idx = var.index();            // 当前是第几个备选类型
size_t cnt = std::variant_size_v<decltype(var)>;  // 备选类型数量
```

---

## 十一、std::stack / std::queue / std::priority_queue（容器适配器）

```cpp
#include <stack>
#include <queue>

// --- stack（后进先出，默认底层 deque）---
std::stack<int> stk;
stk.push(1);
stk.push(2);
stk.emplace(3);     // 原位构造
stk.top();          // 访问栈顶（不弹出）
stk.pop();          // 弹出栈顶（不返回值）
stk.size();
stk.empty();

// --- queue（先进先出，默认底层 deque）---
std::queue<int> q;
q.push(1);
q.push(2);
q.emplace(3);
q.front();          // 访问队首
q.back();           // 访问队尾
q.pop();            // 弹出队首
q.size();
q.empty();

// --- priority_queue（大顶堆，默认底层 vector，默认 std::less = 大顶）---
std::priority_queue<int> pq;
pq.push(3);
pq.push(1);
pq.push(5);
pq.push(2);
int top = pq.top();  // 5（最大值在堆顶）
pq.pop();            // 弹出堆顶（5）

// 小顶堆
std::priority_queue<int, std::vector<int>, std::greater<int>> minPq;
minPq.push(3);
minPq.push(1);
minPq.push(5);
minPq.top();  // 1（最小值在堆顶）

// 自定义比较（Lambda）
auto cmp = [](const auto& a, const auto& b) { return a.second < b.second; };
std::priority_queue<std::pair<int,int>, std::vector<std::pair<int,int>>, decltype(cmp)> pq2(cmp);
```

---

## 十二、常用快捷写法速查

```cpp
// ===== 判断容器是否包含某元素 =====
// vector / deque / list（无成员 find，用算法）
bool exists = std::find(v.begin(), v.end(), val) != v.end();
// map / set（用成员 find 或 contains）
bool exists = m.contains(key);          // C++20 最推荐
bool exists = m.find(key) != m.end();   // C++11~
bool exists = m.count(key) > 0;         // 经典但不够表意

// ===== 遍历并删除符合条件的元素 =====
// vector
v.erase(std::remove_if(v.begin(), v.end(),
         [](int n) { return n < 0; }), v.end());
// C++20
std::erase_if(v, [](int n) { return n < 0; });

// map / set（迭代器安全删除）
for (auto it = m.begin(); it != m.end(); ) {
    if (shouldRemove(it->second))
        it = m.erase(it);
    else
        ++it;
}

// ===== 一次性取出 map 所有 key / value =====
std::vector<std::string> keys;
for (const auto& [k, v] : m) keys.push_back(k);
// 或
#include <ranges>  // C++20
auto keys = m | std::views::keys | std::ranges::to<std::vector>();  // C++23
auto vals = m | std::views::values | std::ranges::to<std::vector>();

// ===== 排序 map 的 value（降序取 top N）=====
std::vector<std::pair<std::string, int>> sorted(m.begin(), m.end());
std::sort(sorted.begin(), sorted.end(),
          [](const auto& a, const auto& b) { return a.second > b.second; });
for (size_t i = 0; i < std::min(sorted.size(), 5UL); ++i) {
    std::cout << sorted[i].first << " -> " << sorted[i].second << std::endl;
}

// ===== 拼接两个 vector =====
std::vector<int> a = {1, 2, 3}, b = {4, 5, 6};
a.insert(a.end(), b.begin(), b.end());

// ===== vector 二分查找 =====
std::sort(v.begin(), v.end());                          // 先排序
bool found = std::binary_search(v.begin(), v.end(), x);
auto it = std::lower_bound(v.begin(), v.end(), x);      // 第一个 >=x
int idx = std::distance(v.begin(), it);                 // 获取下标
```

---

## 十三、迭代器辅助

```cpp
// std::advance：通用移动迭代器（list/map 等不支持 it+n 的也能用）
auto it = lst.begin();
std::advance(it, 3);  // 相当于 ++it 三次

// std::distance：迭代器间距（返回元素个数差）
int d = std::distance(v.begin(), v.end());  // 即 v.size()

// std::next / std::prev
auto it2 = std::next(v.begin(), 2);   // begin 后 2 个
auto it3 = std::prev(v.end(), 1);     // end 前 1 个（即最后一个元素）

// 插入迭代器
std::vector<int> src = {1, 2, 3}, dst;
std::copy(src.begin(), src.end(), std::back_inserter(dst));      // push_back
std::copy(src.begin(), src.end(), std::front_inserter(dst));     // push_front（仅 deque/list）
std::copy(src.begin(), src.end(), std::inserter(dst, dst.begin() + 1));  // insert 到指定位置
```

---

## 相关文档

- [12-STL容器底层](12-STL容器底层.md)：容器底层结构、扩容机制、迭代器失效、emplace vs push 的原理
- [19-STL算法与迭代器](19-STL算法与迭代器.md)：常用算法、erase-remove 惯用法、迭代器分类
- [06-智能指针与资源管理](06-智能指针与资源管理.md)：unique_ptr / shared_ptr 管理资源
- [10-可调用对象-Lambda与std-function](10-可调用对象-Lambda与std-function.md)：Lambda 作为谓词传递给算法
- [09-现代C++特性（C++11到17）](09-现代C++特性（C++11到17）.md)：结构化绑定、CTAD 等特性在本篇大量使用
