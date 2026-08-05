# C++ const 正确性与编译期计算（选学 · 非必须）

## 0. 本篇定位

- 面试复习：先掌握 const 成员函数、顶层/底层 const、mutable、`constexpr`、`consteval`、`constinit`。
- 深入学习：重点看编译期计算、不可变接口和线程安全之间的边界。
- 音视频落点：配置对象、格式描述、查表和固定参数适合做 const/constexpr 化，能减少状态混乱。
> ⭐ **选学**：`const` 正确性是日常工程的基本功（中频考），`constexpr`/编译期计算是进阶加分项（低中频）。补在这里求完整。
>
> 相关：`const` 全局变量的存储位置见 [05-内存管理与对象生命周期](05-内存管理与对象生命周期.md)；模板元编程见 [14-模板与泛型编程](14-模板与泛型编程.md)；`consteval`/`constinit` 也在 [17-C++20与协程](17-C++20与协程.md) 提到。

---

## 面试速记

- **const 正确性**：能加 `const` 就加——参数、成员函数、返回值。它既是契约（“我不改你”），也利于编译器优化和接口清晰。
- **const 成员函数**：承诺不修改对象逻辑状态；`const` 对象只能调 const 成员函数。可与非 const 版本**重载**。
- **`mutable`**：允许在 const 成员函数里修改的成员（缓存、互斥量、统计计数器）。
- **`const` vs `constexpr`**：`const` 是“运行期只读”；`constexpr` 是“**编译期可求值**”——更强，能用于数组大小、模板参数、`switch` 标签等需要编译期常量的地方。
- **C++20**：`consteval`（强制编译期求值）、`constinit`（保证编译期初始化，避免静态初始化顺序问题）。

---

## 一、const 正确性

### 1. 指针的 const：从右往左读

```cpp
const int* p1;        // 指向 const int：不能改 *p1，能改 p1
int* const p2 = &x;   // const 指针：能改 *p2，不能改 p2
const int* const p3 = &x; // 都不能改
```

### 2. const 成员函数与重载

```cpp
class FrameBuffer {
    std::vector<uint8_t> data_;
public:
    // const 版本：const 对象/常引用调用，返回只读
    const uint8_t& at(size_t i) const { return data_[i]; }
    // 非 const 版本：可写
    uint8_t& at(size_t i) { return data_[i]; }
};
```

`const` 对象、`const FrameBuffer&` 形参只能调用 const 成员函数——所以**接口设计时该 const 的成员函数一定要标 const**，否则常引用传参时无法调用。

### 3. mutable：const 成员函数里的“例外”

```cpp
class Decoder {
    mutable std::mutex mtx_;          // 加锁不算修改逻辑状态
    mutable size_t queryCount_ = 0;   // 统计计数器
    std::string name_;
public:
    std::string name() const {        // 逻辑上只读
        std::lock_guard<std::mutex> lock(mtx_); // 能锁 mutable 成员
        ++queryCount_;                          // 能改 mutable 成员
        return name_;
    }
};
```

`mutable` 适合“不影响对象对外逻辑状态”的内部细节：互斥量、缓存、惰性计算结果、统计量。

---

## 二、const vs constexpr

```cpp
const int runtimeSize = getSize();    // 运行期才知道值，只读
constexpr int compileSize = 1024;     // 编译期常量

int arrayA[runtimeSize];   // ❌（变长数组非标准）：大小要编译期常量
int arrayB[compileSize];   // ✅ constexpr 可作数组大小
std::array<int, compileSize> arr;     // ✅ 模板参数要求编译期常量
```

**要点**：所有 `constexpr` 变量都是 `const`，但反之不然。需要“编译期常量”的语境（数组维度、模板非类型参数、`case` 标签、`static_assert`）必须用 `constexpr`。

---

## 三、constexpr 函数：把计算挪到编译期

```cpp
constexpr int factorial(int n) {            // 可在编译期求值
    return n <= 1 ? 1 : n * factorial(n - 1);
}
constexpr int f5 = factorial(5);            // 编译期算出 120，零运行期开销
int runtimeN = readInput();
int fx = factorial(runtimeN);               // 也能在运行期调用（参数非常量时）
```

`constexpr` 函数“能编译期就编译期，不能就退化到运行期”。C++14 起 `constexpr` 函数体可包含循环、局部变量、分支，能力大幅增强。

```cpp
// 编译期生成查找表（音视频里如 gamma 表、量化表）
constexpr std::array<int, 256> makeSquareTable() {
    std::array<int, 256> table{};
    for (int i = 0; i < 256; ++i) table[i] = i * i;
    return table;
}
constexpr auto kSquareTable = makeSquareTable(); // 编译期算好，运行期直接查
```

---

## 四、C++20：consteval 与 constinit

- **`consteval`**（immediate function）：**必须**在编译期求值，否则编译错误（比 `constexpr` 更严格，杜绝意外的运行期调用）。

```cpp
consteval int squared(int n) { return n * n; }
constexpr int a = squared(5);   // ✅
// int b = squared(readInput()); // ❌ 参数非编译期常量，编译报错
```

- **`constinit`**：保证变量在**编译期完成初始化**，用于带静态存储期的变量，规避“静态初始化顺序灾难”（不同编译单元的全局对象初始化顺序未定义）。它不要求 const，只要求初始化在编译期完成。

---

## 五、模板元编程（进阶了解）

编译期计算的“老式”形态是模板递归（C++11 前的主力），现在多被 `constexpr` 函数取代，但面试可能问到：

```cpp
// 模板递归求阶乘（编译期）
template <int N> struct Factorial { static constexpr int value = N * Factorial<N-1>::value; };
template <>      struct Factorial<0> { static constexpr int value = 1; };
constexpr int f = Factorial<5>::value;  // 120，编译期算出

// 现代等价写法：直接用 constexpr 函数（上文 factorial），更易读
```

> 现代建议：**优先 `constexpr`/`consteval` 函数**做编译期计算，可读性远好于模板递归；模板元编程主要用于类型层面的操作（见 [14-模板与泛型编程](14-模板与泛型编程.md) 的 type_traits / CRTP）。

> 面试一句话：“`const` 表达运行期只读和接口契约，该 const 的成员函数一定要标 const，否则常引用传参调不了；`mutable` 给互斥量、缓存这类不影响逻辑状态的成员开后门。`constexpr` 更强，是编译期可求值，能用于数组大小、模板参数；C++14 起 constexpr 函数能写循环，可在编译期生成查找表。C++20 的 `consteval` 强制编译期、`constinit` 保证编译期初始化解决静态初始化顺序问题。”
