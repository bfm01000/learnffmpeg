# C++ 基础知识面试指南

## 一、 重写（Override）与重载（Overload）的区别

这是 C++ 面试中最基础、也是被问频率最高的问题之一。虽然它们的名字只有一字之差，但在 C++ 中代表着完全不同的多态机制。

我们可以从四个维度来彻底区分它们：**发生的位置、函数签名、关键字、以及多态的类型**。

### 1. 核心对比速查表

| 维度 | 重载 (Overload) | 重写 (Override) |
| :--- | :--- | :--- |
| **发生位置** | **同一个类**（或同一个作用域）中 | **父类与子类**之间 |
| **函数名字** | 必须**相同** | 必须**相同** |
| **参数列表** | 必须**不同**（个数、类型或顺序不同） | 必须**完全相同** |
| **返回类型** | 可以相同也可以不同 | 必须**相同**（或协变返回类型） |
| **多态类型** | **静态多态**（编译期决议，早绑定） | **动态多态**（运行期决议，晚绑定） |
| **关键字** | 不需要特殊关键字 | 父类必须有 `virtual`，子类建议加 `override` |

---

### 2. 详细解析：重载 (Overload)

**本质**：在同一个范围内，为了方便调用，给功能相似但参数不同的函数起**同一个名字**。
**机制**：编译器在**编译阶段**，根据你传入的参数类型和个数，自动帮你匹配到对应的函数（这叫静态绑定/早绑定）。

```cpp
class Printer {
public:
    // 1. 打印整数
    void print(int i) {
        cout << "Printing int: " << i << endl;
    }

    // 2. 打印字符串（参数类型不同，构成重载）
    void print(string s) {
        cout << "Printing string: " << s << endl;
    }

    // 3. 打印两个整数（参数个数不同，构成重载）
    void print(int a, int b) {
        cout << "Printing two ints: " << a << ", " << b << endl;
    }

    // ❌ 错误示范：仅仅返回类型不同，不能构成重载！
    // int print(int i) { return i; } // 编译报错
};
```

---

### 3. 详细解析：重写 (Override)

**本质**：子类对父类的**虚函数**（`virtual`）进行重新实现，以表现出子类特有的行为。
**机制**：通过**虚函数表（vtable）**实现。在**程序运行阶段**，根据指针或引用实际指向的对象类型，动态决定调用哪个类的函数（这叫动态绑定/晚绑定）。

```cpp
class Animal {
public:
    // 父类必须声明为 virtual
    virtual void speak() {
        cout << "Animal makes a sound" << endl;
    }
};

class Dog : public Animal {
public:
    // 子类重写父类的 speak 函数
    // 建议加上 override 关键字，让编译器帮你检查函数签名是否完全一致
    void speak() override {
        cout << "Dog barks: Woof!" << endl;
    }
};

void test() {
    Animal* myPet = new Dog();
    // 运行期决议：虽然指针类型是 Animal*，但实际指向的是 Dog 对象
    // 所以会查虚函数表，最终调用 Dog 的 speak()
    myPet->speak(); // 输出: Dog barks: Woof!
}
```

---

### 4. 面试防坑指南：隐藏（Hide / Redefine）

面试官经常会抛出第三个概念来迷惑你：**隐藏（Hide）**。

**什么是隐藏？**
如果子类写了一个和父类**同名**的函数，但父类的函数**没有加 `virtual`**，或者子类的**参数列表和父类不一样**。
这时候，子类的函数会把父类的同名函数**全部隐藏掉**（即使参数不同，也不会构成重载，而是直接隐藏）。

```cpp
class Base {
public:
    void show(int x) { cout << "Base int" << endl; }
};

class Derived : public Base {
public:
    // 父类没有 virtual，这里发生了【隐藏】，而不是重写！
    // 并且它隐藏了父类所有名为 show 的函数
    void show(string s) { cout << "Derived string" << endl; }
};

void testHide() {
    Derived d;
    d.show("hello"); // 正常调用子类
    // d.show(10);   // ❌ 编译报错！父类的 show(int) 被隐藏了，子类看不见它
}
```

**总结一句话背诵：**
“**重载**是同类中同名不同参，编译期决定；**重写**是子类覆盖父类的虚函数，同名同参，运行期决定；如果没有 `virtual` 却同名了，那叫**隐藏**。”

---

## 二、 `volatile` 关键字：是什么？为什么不能替代锁？

`volatile` 是 C++ 里一个很容易被误解的关键字。很多人会把它和 Java 的 `volatile` 混在一起，以为它可以解决多线程可见性问题，甚至可以替代锁。这个理解在 C++ 里是错误的。

### 1. `volatile` 的真正含义

在 C++ 中，`volatile` 的核心含义是：

> 告诉编译器：这个变量的值可能会被“程序正常控制流之外”的因素改变，所以每次访问它时都要真的去内存读写，不要随便优化掉。

典型场景包括：

*   **内存映射硬件寄存器**：比如嵌入式开发中读取某个设备状态寄存器。
*   **信号处理相关变量**：变量可能被异步信号处理函数修改。
*   **特殊底层 I/O 场景**：变量背后不是普通内存，而是外部设备。

示例：

```cpp
volatile int* statusRegister = reinterpret_cast<volatile int*>(0x1000);

while ((*statusRegister & 0x1) == 0) {
    // 每次循环都必须重新读取硬件寄存器
}
```

如果没有 `volatile`，编译器可能认为 `*statusRegister` 在循环里没有被当前程序修改，于是把它缓存起来，导致循环永远看不到硬件状态变化。

### 2. `volatile` 能做什么？

它主要限制的是**编译器优化**：

```cpp
volatile int flag = 0;

void waitFlag() {
    while (flag == 0) {
        // 编译器不能把 flag 的读取优化成只读一次
    }
}
```

因为 `flag` 是 `volatile`，编译器必须保留每次读取，不能假设它不变。

但这只说明一件事：**编译器每次都会发起读写指令**。它并不代表多线程下这个读写就是安全的。

### 3. 为什么 `volatile` 不能替代锁？

多线程安全至少要解决三类问题：

1.  **原子性**：一个操作能不能被线程切到一半。
2.  **可见性**：一个线程写入后，其他线程能不能可靠看到。
3.  **有序性**：读写操作的先后顺序会不会被 CPU 或编译器重排。

C++ 的 `volatile` 不能完整解决这三件事。

### 4. 不能保证原子性

看这段代码：

```cpp
volatile int counter = 0;

void increase() {
    ++counter;
}
```

`++counter` 看起来是一行，底层大致是三步：

```text
1. 从内存读取 counter
2. 对读取到的值 +1
3. 把新值写回 counter
```

如果两个线程同时执行：

```text
counter 初始值 = 0

线程 A 读到 0
线程 B 读到 0
线程 A 写回 1
线程 B 写回 1
```

执行两次 `++counter` 后，结果可能还是 `1`，而不是期望的 `2`。这就是典型的数据竞争。`volatile` 只能让读写不被编译器省略，不能把 `++counter` 变成不可分割的原子操作。

正确写法应该用 `std::atomic`：

```cpp
#include <atomic>

std::atomic<int> counter{0};

void increase() {
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

如果操作的是复杂临界区，而不是一个简单计数器，就应该用锁：

```cpp
#include <mutex>
#include <vector>

std::mutex mutex;
std::vector<int> frames;

void pushFrame(int frame) {
    std::lock_guard<std::mutex> lock(mutex);
    frames.push_back(frame);
}
```

### 5. 不能保证线程间同步语义

再看一个很常见的错误写法：

```cpp
volatile bool ready = false;
int data = 0;

void producer() {
    data = 42;
    ready = true;
}

void consumer() {
    while (!ready) {
        // 等待生产者写入数据
    }

    // 期望这里一定看到 data == 42，但 volatile 不能提供这种跨线程同步保证
    use(data);
}
```

这段代码的问题是：`volatile` 不能建立标准意义上的 **happens-before** 关系。也就是说，C++ 标准并不保证 `consumer` 看到 `ready == true` 时，就一定能可靠看到 `producer` 之前写入的 `data = 42`。

正确写法应该使用 `std::atomic` 的 acquire/release 语义：

```cpp
#include <atomic>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;
    ready.store(true, std::memory_order_release);
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) {
        // 等待 producer 发布数据
    }

    use(data); // 这里可以看到 producer 在 release 前写入的 data
}
```

或者直接用 `std::mutex` + `std::condition_variable`，让语义更清楚：

```cpp
#include <condition_variable>
#include <mutex>

std::mutex mutex;
std::condition_variable cv;
bool ready = false;
int data = 0;

void producer() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        data = 42;
        ready = true;
    }
    cv.notify_one();
}

void consumer() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [] { return ready; });
    use(data);
}
```

### 6. `volatile` vs `std::atomic` vs `std::mutex`

| 工具 | 解决什么问题 | 典型用途 |
| :--- | :--- | :--- |
| `volatile` | 防止编译器优化掉对特殊内存的访问 | 硬件寄存器、内存映射 I/O、信号相关变量 |
| `std::atomic` | 提供原子操作和线程间内存序 | 计数器、状态标志、无锁同步 |
| `std::mutex` | 保护一段临界区，保证同一时间只有一个线程进入 | 修改容器、复杂对象、多步业务逻辑 |

一句话判断：

*   **和硬件/特殊内存打交道**：考虑 `volatile`。
*   **多线程共享一个简单变量**：优先考虑 `std::atomic`。
*   **多线程共享复杂对象或多步逻辑**：用 `std::mutex`。

### 7. 面试标准回答

> “C++ 里的 `volatile` 不是线程同步工具，它只是告诉编译器这个变量可能被外部因素修改，所以不要优化掉它的读写。它常用于硬件寄存器、内存映射 I/O 这类底层场景。
>
> 但是它不能替代锁，因为它不保证复合操作的原子性，也不提供完整的线程间同步和 happens-before 关系。比如 `volatile int counter++` 仍然会有数据竞争。
>
> 多线程里如果只是共享简单计数器或状态标志，应该用 `std::atomic`；如果要保护容器或一段复杂临界区，应该用 `std::mutex`。一句话：`volatile` 管的是编译器优化，不是线程安全。”

---

## 三、 `explicit` 关键字：防止隐式构造和隐式类型转换

`explicit` 是 C++ 中用来修饰构造函数或转换函数的关键字。它最常见的作用是：**禁止编译器把某个构造函数用于隐式类型转换**。

### 1. 为什么需要 `explicit`？

先看一个没有 `explicit` 的例子：

```cpp
#include <string>

class LiveConfig {
public:
    LiveConfig(int bitrate) : bitrate_(bitrate) {}

private:
    int bitrate_ = 0;
};
```

因为 `LiveConfig(int)` 是一个单参数构造函数，所以编译器可以把 `int` 隐式转换成 `LiveConfig`：

```cpp
LiveConfig config = 8000000; // 编译器偷偷调用 LiveConfig(8000000)
```

这行代码看起来像是在赋值，实际上是在构造对象。问题是：**调用方可能并没有意识到这里发生了一次对象构造**。

再看一个更危险的例子：

```cpp
void startLive(const LiveConfig& config) {
    // 启动推流
}

startLive(8000000); // 如果没有 explicit，这也可能被允许
```

`startLive()` 明明需要的是 `LiveConfig`，但调用方传了一个整数，编译器却自动帮你构造成配置对象。这种代码可读性很差，也容易隐藏 Bug。

### 2. 加上 `explicit` 后会怎样？

```cpp
class LiveConfig {
public:
    explicit LiveConfig(int bitrate) : bitrate_(bitrate) {}

private:
    int bitrate_ = 0;
};
```

加上 `explicit` 后，下面这种隐式转换就不允许了：

```cpp
LiveConfig config = 8000000; // 编译失败：禁止隐式构造
startLive(8000000);          // 编译失败：不能把 int 偷偷转成 LiveConfig
```

必须显式写清楚：

```cpp
LiveConfig config(8000000);
startLive(LiveConfig(8000000));
```

这样代码意图更明确：**我就是要用这个整数构造一个 `LiveConfig` 对象**。

### 3. `explicit` 主要防什么坑？

它主要防止下面这种“看起来不是构造对象，但编译器偷偷构造了对象”的情况：

```cpp
class FrameId {
public:
    FrameId(int value) : value_(value) {}

private:
    int value_ = 0;
};

void seekToFrame(FrameId frameId) {
    // 跳转到指定帧
}

seekToFrame(100); // 没有 explicit 时，int 会被隐式转换成 FrameId
```

如果 `FrameId` 是一个强语义类型，我们其实希望调用方明确写：

```cpp
class FrameId {
public:
    explicit FrameId(int value) : value_(value) {}

private:
    int value_ = 0;
};

seekToFrame(FrameId(100));
```

这样能避免把普通整数误传到有业务语义的参数里。

### 4. 哪些构造函数建议加 `explicit`？

一般建议：**单参数构造函数尽量加 `explicit`**。

例如：

```cpp
class Bitrate {
public:
    explicit Bitrate(int bps) : bps_(bps) {}

private:
    int bps_ = 0;
};

class Url {
public:
    explicit Url(std::string value) : value_(std::move(value)) {}

private:
    std::string value_;
};
```

这些类型都有明确业务语义，不希望编译器随便把 `int` 或 `std::string` 偷偷转进去。

### 5. 什么时候可以不加？

如果你就是希望类型之间可以自然转换，可以不加 `explicit`。

例如标准库里的某些轻量包装类型或数学类型，可能允许自然转换。但在业务代码、SDK 配置、强语义参数里，通常更推荐加 `explicit`，让调用方表达清楚意图。

### 6. C++11 之后：多参数构造也可能需要注意

C++11 引入列表初始化后，即使构造函数有多个参数，也可能通过花括号触发某些隐式构造场景。因此现代 C++ 中，`explicit` 不只适用于传统意义上的“一个参数构造函数”，但面试里重点掌握单参数构造函数即可。

```cpp
class Resolution {
public:
    explicit Resolution(int width, int height)
        : width_(width), height_(height) {}

private:
    int width_ = 0;
    int height_ = 0;
};
```

### 7. 面试标准回答

> “`explicit` 是用来禁止构造函数参与隐式类型转换的。比如一个类有 `LiveConfig(int)` 这种单参数构造函数，如果不加 `explicit`，编译器可能允许 `LiveConfig config = 8000000`，甚至允许把 `int` 传给需要 `LiveConfig` 的函数。
>
> 加上 `explicit` 后，调用方必须明确写 `LiveConfig(8000000)`，代码意图更清楚，也能避免很多隐式转换带来的 Bug。
>
> 工程里我一般会给单参数构造函数加 `explicit`，尤其是 `FrameId`、`Bitrate`、`Url`、`LiveConfig` 这类有业务语义的类型，除非我明确希望它支持隐式转换。”