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