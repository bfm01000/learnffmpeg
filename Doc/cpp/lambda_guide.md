# C++ Lambda 表达式：原理、实战与面试避坑指南

Lambda 表达式是 C++11 引入的最重要特性之一。它让 C++ 拥有了函数式编程的体验，极大地简化了回调函数、异步任务和 STL 算法的编写。

但在面试中，Lambda 也是一个“重灾区”。面试官非常喜欢通过 Lambda 来考察候选人对**内存管理、生命周期和底层编译原理**的理解。

---

## 一、 Lambda 的底层原理（本质是什么？）

很多初学者觉得 Lambda 是一种“魔法函数”。但在 C++ 编译器的眼里，**Lambda 根本不是函数，而是一个匿名的类（重载了 `operator()` 的仿函数 Functor）。**

当你写下这段代码：

```cpp
// 最常见的写法：捕获外部变量 x，接收参数 y
int x = 10;
auto myLambda = `[x]`(int y) { return x + y; };

// 完整语法结构：
// [捕获列表](参数列表) mutable(可选) exception_attribute(可选) -> 返回类型 { 函数体 }

// 写法 2：显式指定返回类型（通常编译器会自动推导，可以省略）
auto lambda2 = [](double a, double b) -> double { return a + b; };

// 写法 3：无参数，无捕获（最简形式）
auto lambda3 = []() { cout << "Hello"; };
// 甚至可以省略空括号（注意：方括号 [] 绝对不能省！）
auto lambda4 = [] { cout << "World"; };

// 写法 4：C++14 引入的泛型 Lambda（参数使用 auto）
auto lambda5 = [](auto a, auto b) { return a + b; };
```

编译器在底层会默默地把它翻译成类似这样的代码：

```cpp
// 第一步：编译器自动生成一个匿名类
class __Lambda_Anonymous_123 {
private:
    int x; // 捕获的变量变成了成员变量

public:
    // 构造函数，用于初始化捕获的变量
    __Lambda_Anonymous_123(int _x) : x(_x) {}

    // 第二步：重载 () 运算符，把你的函数体塞进去（默认是 const 的！）
    int operator()(int y) const {
        return x + y;
    }
};

// 第三步：实例化这个匿名类
int x = 10;
// 这就是 auto myLambda = [x](int y) { return x + y; }; 的真实面目！
auto myLambda = __Lambda_Anonymous_123(x);
```

**核心结论：**

1. **捕获列表 `[]`**：决定了这个匿名类有哪些成员变量，以及构造函数怎么传参。
2. **参数列表 `()`**：决定了 `operator()` 的参数。
3. **函数体 `{}`**：就是 `operator()` 的具体实现。
4. **默认 `const`**：Lambda 的 `operator()` 默认是 `const` 的，这意味着你不能在 Lambda 内部修改按值捕获的变量（除非加 `mutable` 关键字）。

---

## 二、 捕获列表的使用与陷阱

捕获列表是 Lambda 最强大的地方，也是最容易写出 Bug 的地方。

### 1. 按值捕获 `[=]` 或 `[x]`

- **行为**：将外部变量**拷贝**一份到 Lambda 的匿名类中。
- **特点**：安全，不用担心外部变量被销毁。但如果捕获的是大对象，会有拷贝开销。
- **注意**：捕获的值在 Lambda 创建的那一刻就固定了。
  ```cpp
  int a = 1;
  auto f = [a]() { cout << a; };
  a = 2;
  f(); // 输出 1，不是 2！
  ```

### 2. 按引用捕获 `[&]` 或 `[&x]`

- **行为**：将外部变量的**引用（指针）**存到 Lambda 中。
- **特点**：没有拷贝开销，且能实时读取外部变量的最新值，也能在 Lambda 内修改它。
- **致命陷阱：悬垂引用（Dangling Reference）**
如果 Lambda 的生命周期比被捕获的局部变量长（比如把 Lambda 扔进了异步线程池），当 Lambda 执行时，局部变量早就销毁了，此时访问引用会导致 Crash。
  ```cpp
  std::function<void()> getLambda() {
      int temp = 100;
      return [&temp]() { cout << temp; }; // ❌ 极其危险！temp 离开函数就销毁了
  }
  ```

### 3. 捕获 `this` 指针（最隐蔽的坑）

在类的成员函数中使用 Lambda 时，经常需要访问类的成员变量。

```cpp
class MyClass {
    int data = 0;
    void doAsync() {
        // ❌ 陷阱 1：隐式捕获 this
        // [=] 看起来是按值捕获，但对于成员变量 data，它实际上捕获的是 this 裸指针！
        // 相当于 [this]() { cout << this->data; }
        threadPool.push([=]() { cout << data; }); 
    }
};
```

**为什么危险？**
如果 `MyClass` 的实例被销毁了，异步线程池里的 Lambda 还在运行，它拿着失效的 `this` 指针去访问 `data`，必然 Crash。

**正确解法（C++14 引入的初始化捕获）：**

```cpp
class MyClass {
    shared_ptr<int> dataPtr;
    void doAsync() {
        // ✅ 安全做法：在捕获列表中显式拷贝 shared_ptr
        // 这会强制引用计数 +1，保证 Lambda 执行期间对象存活
        threadPool.push([data = this->dataPtr]() { cout << *data; }); 
    }
};
```

---

## 三、 面试高频问题 Q&A

### Q1: Lambda 表达式可以相互赋值吗？

**答**：不可以。即使两个 Lambda 的签名和捕获列表完全一样，编译器也会为它们生成**两个完全不同的匿名类**。不同类的对象之间是不能赋值的。

```cpp
auto f1 = [](){};
auto f2 = [](){};
// f1 = f2; // ❌ 编译报错
```

### Q2: 什么是 `mutable` 关键字？什么时候用？

**答**：Lambda 的 `operator()` 默认是 `const` 的，所以你不能修改按值捕获的变量。如果非要修改，必须加上 `mutable` 关键字。

```cpp
int count = 0;
// auto f = [count]() { count++; }; // ❌ 编译报错：count 是只读的
auto f = [count]() mutable { count++; }; // ✅ 编译通过，修改的是 Lambda 内部的拷贝
```

### Q3: Lambda 表达式有多大（`sizeof`）？

**答**：Lambda 的大小取决于它**捕获了什么**。

- 如果不捕获任何变量（`[]`），它的大小通常是 1 字节（C++ 规定空类大小为 1）。
- 如果捕获了变量，它的大小就是所有捕获变量占用内存的总和（考虑内存对齐）。
  ```cpp
  int a; double b;
  auto f = [a, b](){}; 
  // sizeof(f) 通常是 16 字节（4字节 int + 4字节 padding + 8字节 double）
  ```

### Q4: 无捕获的 Lambda 和普通函数指针有什么关系？

**答**：如果一个 Lambda **没有捕获任何变量**（即 `[]`），C++ 标准保证它可以隐式转换为一个普通的 C 风格函数指针。这在调用遗留的 C 语言 API 时非常有用。

```cpp
void callCFunction(void (*func)(int)) { ... }

// ✅ 可以直接传无捕获的 Lambda
callCFunction([](int x) { cout << x; }); 

// ❌ 如果有捕获，就不能转成函数指针了！
int y = 10;
// callCFunction([y](int x) { cout << x + y; }); // 编译报错
```

### Q5: `std::function` 和 Lambda 有什么区别？

**答**：

- **Lambda** 是一个具体的匿名类对象，它在编译期就确定了类型，调用它通常可以被编译器**内联优化（Inline）**，性能极高。
- `**std::function`** 是一个类型擦除的多态包装器（可以装入普通函数、Lambda、仿函数等）。它在底层使用了虚函数表或动态内存分配，调用时有间接寻址的开销，**无法被内联**。
- **总结**：在能用 `auto` 接收 Lambda 的地方，绝不用 `std::function`。只有在需要把 Lambda 存入容器（如 `std::vector<std::function<...>>`）或作为类成员变量时，才使用 `std::function`。

