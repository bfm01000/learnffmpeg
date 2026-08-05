# C++ 可调用对象：Lambda 与 std::function

## 0. 本篇定位

- 面试复习：先掌握 lambda 的匿名类本质、捕获方式、生命周期，以及 `std::function` 的类型擦除和潜在分配。
- 深入学习：重点看回调、任务队列、线程池 submit、C API 回调桥接时如何选择可调用对象。
- 音视频落点：采集回调、编码回调、UI 事件和异步任务都离不开 callable，但要警惕悬空引用和热路径额外分配。
> 本篇把 `lambda` 与 `std::function` 放在一起讲，因为面试里它们几乎总是成对出现：先用 Lambda 写出可调用对象，再用 `std::function` 把它存起来。理解两者的**底层差异**和**选型边界**，是这一块的核心。
>
> 相关：函数调用开销与内联见 [13-语言基础关键字](13-语言基础关键字.md)；`std::function` 的虚表机制见 [08-继承、多态、虚函数与对象模型](08-继承、多态、虚函数与对象模型.md)；按引用捕获导致的悬垂问题与生命周期管理见 [06-智能指针与资源管理](06-智能指针与资源管理.md)。

---

## 面试速记（考前 5 分钟扫一遍）

- **Lambda 的本质**：编译器自动生成的一个**匿名类**，重载了 `operator()`，捕获的变量变成它的成员变量。不是“函数”，是“仿函数对象”。
- **捕获方式**：`[=]` 按值拷贝、`[&]` 按引用、`[this]` 捕获对象指针、`[x = expr]` 初始化捕获（C++14）。延后执行的 Lambda **绝不能按引用捕获局部变量**。
- **`std::function` 的本质**：类型擦除的可调用对象包装器，底层靠**继承 + 虚函数 + （可能的）堆分配**。能装下函数指针、Lambda、仿函数、`std::bind` 结果。
- **黄金法则**：能用 `auto` 接收 Lambda 的地方，绝不用 `std::function`——后者有虚调用开销、可能堆分配、无法内联。只有需要**统一类型存储**（成员变量、容器、跨边界接口）时才用 `std::function`。
- **空 `std::function` 调用**会抛 `std::bad_function_call`，调用前要判空。

---

## 一、Lambda 的底层原理（本质是什么？）

很多人觉得 Lambda 是“魔法函数”。但在编译器眼里，**Lambda 不是函数，而是一个匿名的类（重载了 `operator()` 的仿函数 Functor）**。

```cpp
// 完整语法：[捕获列表](参数列表) mutable(可选) 异常说明(可选) -> 返回类型 { 函数体 }
int base = 10;
auto adder = [base](int delta) { return base + delta; };

auto explicitReturn = [](double left, double right) -> double { return left + right; };
auto noArg          = [] { std::cout << "World"; };   // 空括号可省，但 [] 绝不能省
auto generic        = [](auto left, auto right) { return left + right; }; // C++14 泛型 Lambda
```

编译器把 `adder` 翻译成类似这样的代码：

```cpp
class __Lambda_Anonymous {
private:
    int base;                                  // 捕获的变量变成成员变量
public:
    explicit __Lambda_Anonymous(int capturedBase) : base(capturedBase) {}
    int operator()(int delta) const {          // 函数体塞进 operator()，默认 const
        return base + delta;
    }
};
auto adder = __Lambda_Anonymous(base);         // 实例化匿名类
```

**核心结论：**

1. **捕获列表 `[]`** → 决定匿名类有哪些成员变量、构造函数怎么传参。
2. **参数列表 `()`** → 决定 `operator()` 的参数。
3. **函数体 `{}`** → 就是 `operator()` 的实现。
4. **默认 `const`** → 不能在 Lambda 内修改按值捕获的变量，除非加 `mutable`。

---

## 二、捕获列表的使用与陷阱

捕获列表是 Lambda 最强大的地方，也是最容易写出 Bug 的地方。

### 1. 按值捕获 `[=]` 或 `[x]`

- **行为**：把外部变量**拷贝**一份进匿名类。
- **特点**：安全，不担心外部变量销毁；但大对象有拷贝开销。
- **关键**：捕获值在 Lambda **创建那一刻**就固定了。

```cpp
int value = 1;
auto printer = [value]() { std::cout << value; };
value = 2;
printer(); // 输出 1，不是 2
```

### 2. 按引用捕获 `[&]` 或 `[&x]`

- **行为**：把外部变量的**引用**存进 Lambda。
- **特点**：无拷贝开销，能读到最新值、也能改外部变量。
- **致命陷阱——悬垂引用**：若 Lambda 活得比被捕获的局部变量久（如扔进线程池/异步任务），执行时局部变量早已销毁，访问引用必然 Crash。

```cpp
std::function<void()> makeLambda() {
    int temp = 100;
    return [&temp]() { std::cout << temp; }; // ❌ temp 离开函数即销毁，悬垂
}
```

### 3. 捕获 `this` 指针（最隐蔽的坑）

```cpp
class Worker {
    int data = 0;
    void doAsync() {
        // ❌ [=] 看似按值，但成员变量 data 实际是通过捕获的 this 裸指针访问
        // 等价于 [this]() { std::cout << this->data; }
        threadPool.push([=]() { std::cout << data; });
    }
};
```

**为什么危险**：若 `Worker` 实例被销毁，线程池里的 Lambda 仍拿着失效的 `this` 访问 `data`，必然 Crash。

**正确解法（C++14 初始化捕获 + 智能指针）：**

```cpp
class Worker : public std::enable_shared_from_this<Worker> {
    std::shared_ptr<int> dataPtr;
    void doAsync() {
        // ✅ 捕获 shared_ptr 副本，引用计数 +1，保证 Lambda 执行期间对象存活
        threadPool.push([keepAlive = shared_from_this(), data = dataPtr]() {
            std::cout << *data;
        });
    }
};
```

> `enable_shared_from_this` 的原理见 [06-智能指针与资源管理](06-智能指针与资源管理.md)。

---

## 三、Lambda 面试高频问答

### Q1：两个 Lambda 可以相互赋值吗？

不可以。即使签名和捕获完全一样，编译器也为它们各生成一个**不同的匿名类**，不同类的对象不能赋值。

```cpp
auto first = [](){};
auto second = [](){};
// first = second; // ❌ 编译报错
```

### Q2：`mutable` 是什么？什么时候用？

`operator()` 默认 `const`，不能改按值捕获的变量。要改就加 `mutable`（改的是 Lambda 内部那份拷贝，不影响外部）。

```cpp
int count = 0;
auto counter = [count]() mutable { return ++count; }; // 改的是内部副本
```

### Q3：Lambda 有多大（`sizeof`）？

取决于**捕获了什么**：

- 不捕获（`[]`）：通常 1 字节（空类大小为 1）。
- 有捕获：所有捕获变量之和（考虑内存对齐，见 [05-内存管理与对象生命周期](05-内存管理与对象生命周期.md)）。

```cpp
int countA; double ratioB;
auto fn = [countA, ratioB](){};
// sizeof(fn) 通常 16 字节：4(int) + 4(padding) + 8(double)
```

### Q4：无捕获 Lambda 和函数指针什么关系？

**无捕获**的 Lambda（`[]`）标准保证可隐式转换为 C 风格函数指针，方便对接遗留 C API；**有捕获**就不能转。

```cpp
void registerCallback(void (*func)(int));
registerCallback([](int value) { std::cout << value; }); // ✅ 无捕获可转
int extra = 10;
// registerCallback([extra](int value){ std::cout << value + extra; }); // ❌ 有捕获不可转
```

---

## 四、`std::function`：类型擦除的万能包装器

### 1. 痛点：如何统一保存一个“可调用对象”？

C++ 里可被调用的东西（统称 Callable）有很多：① 函数指针 ② 静态成员函数 ③ 仿函数 ④ Lambda ⑤ `std::bind` 结果。

假设写线程池，任务队列要保存用户提交的任意一种。元素类型该写成什么？

- 函数指针 `void(*)()`？存不了有捕获的 Lambda、仿函数。
- 模板？队列元素类型必须统一，模板做不到。

`std::function<int(int,int)>` 就是答案：只要**调用签名**匹配，它就能装。

```cpp
int add(int a, int b) { return a + b; }
struct Multiply { int operator()(int a, int b) const { return a * b; } };

std::function<int(int, int)> op;
op = add;                                  // 装普通函数
op = Multiply();                           // 装仿函数
op = [](int a, int b) { return a - b; };   // 装 Lambda
```

### 2. 底层原理（面试核心）：类型擦除 + 虚函数

`std::function` 能装下千奇百怪的类型，靠的是**类型擦除（Type Erasure）**——用一个纯虚基类抹掉具体类型，再用模板子类保存真身：

```cpp
template<typename Ret, typename... Args>
class MyFunction {
    struct CallableBase {                          // 抹掉类型的基类
        virtual Ret invoke(Args...) = 0;
        virtual ~CallableBase() = default;
    };
    template<typename T>
    struct CallableImpl : CallableBase {           // 模板子类保存真身
        T functor;
        explicit CallableImpl(T f) : functor(std::move(f)) {}
        Ret invoke(Args... args) override { return functor(args...); }
    };
    CallableBase* impl = nullptr;                  // 多态指针
public:
    template<typename T>
    MyFunction(T f) { impl = new CallableImpl<T>(std::move(f)); }
    Ret operator()(Args... args) { return impl->invoke(args...); } // 触发虚调用
    ~MyFunction() { delete impl; }
};
```

### 3. 三大性能开销（致命弱点）

1. **堆内存分配**：包装体积较大的 Lambda（捕获很多变量）时，要在堆上 `new` 一块内存保存它。
2. **虚函数调用**：每次调用都要查虚表间接寻址，增加 CPU 流水线预测失败概率。
3. **阻碍内联**：运行期动态决议，编译器**绝对无法**内联 `std::function` 的调用；而直接调用 Lambda 通常能完美内联。

> **小对象优化（SSO）**：标准库实现通常带 SSO——若 Lambda 很小（一般 ≤16/24 字节），直接存在 `std::function` 自身的栈内缓冲里，省掉 `new`。但虚调用开销依然存在。

---

## 五、Lambda vs std::function：选型与避坑

### 1. 黄金法则：能用 `auto` 就别用 `std::function`

```cpp
// ❌ 常见的无谓性能浪费
std::function<void()> bad = []() { std::cout << "hi"; }; // 虚调用 + 无法内联

// ✅ 用 auto 接收 Lambda：零开销、完美内联
auto good = []() { std::cout << "hi"; };
```

**必须用 `std::function` 的场景：**

1. **作为类成员变量**：把回调存起来以后再调。
2. **放入容器**：`std::vector<std::function<void()>> taskQueue;`。
3. **跨动态库边界 / 头文件接口**：隐藏实现、统一类型。

### 2. 悬垂引用陷阱（与延后执行结合时最危险）

```cpp
class TaskManager {
    std::function<void()> task;
public:
    void setTask(int& value) {
        task = [&value]() { std::cout << value; }; // ❌ 按引用捕获局部变量
    }
    void runTask() { task(); } // 执行时 value 早已销毁，Crash
};
```

**解法**：延后执行的 Lambda 必须**按值捕获**，或捕获智能指针（`[ptr = shared_from_this()]`）。

### 3. 空 `std::function` 调用会崩

```cpp
std::function<void()> callback;
// callback(); // ❌ 抛 std::bad_function_call
if (callback) { callback(); } // ✅ 调用前判空
```

---

## 六、音视频场景里的可调用对象

- **解码完成回调 / 渲染回调**：用 `std::function<void(AVFrame*)>` 作为成员存起来，解码线程产帧后回调通知渲染线程。注意回调里若访问 `this` 成员，要么保证对象生命周期长于回调，要么捕获 `shared_from_this`。
- **线程池任务队列**：`std::queue<std::function<void()>>` 配合互斥锁/条件变量（见 [01-多线程与锁](01-多线程与锁.md)），把解封装、解码、缩放等任务投递进去。
- **热路径避免 `std::function`**：每帧都要调用的极热回调（如音频回调），若类型固定，用模板/`auto` 传 Lambda，避免虚调用与堆分配。

---

## 七、面试总结一句话

> “Lambda 本质是编译器生成的匿名仿函数类，捕获的变量是它的成员；它能内联、零开销，所以能用 `auto` 接就用 `auto`。`std::function` 是类型擦除包装器，底层靠继承+虚函数+可能的堆分配，有调用开销但能统一存储不同的可调用对象，只在成员变量、容器、跨边界接口这种**需要统一类型**的场景才用。延后执行时一律按值或按智能指针捕获，杜绝悬垂引用。”
