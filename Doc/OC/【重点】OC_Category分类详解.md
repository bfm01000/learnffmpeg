# Objective-C Category (分类) 从零到精通：原理与面试速记

> **适用方向**：iOS Native 开发、音视频客户端开发（涉及 OC 交互）
> **前置知识**：了解基本的面向对象概念即可。
> **核心心法**：Category 的本质是**“在不改变原类、不使用继承的情况下，在运行期动态地给类打补丁（加方法）”**。

---

## 第一部分：初识 Category（它到底是个啥？）

### 1. 痛点场景：为什么要用 Category？
假设你现在正在用苹果系统提供的 `NSString`（字符串类）。你发现系统提供的方法不够用，你想加一个方法：`md5Hash`（计算字符串的 MD5 值）。

在 C++ 或者 Java 里，你通常只有两种办法：
1. **写一个工具类**：`StringUtils::md5Hash(str)`。
2. **继承**：写一个 `MyString` 继承自 `NSString`，然后加方法。但这就意味着你以后到处都要用 `MyString`，非常麻烦。

**Objective-C 提供了一个极其优雅的终极方案：Category（分类）。**
它可以让你**直接把 `md5Hash` 这个方法“注入”到系统的 `NSString` 里面去**。注入之后，整个项目里所有的 `NSString` 对象，都可以直接调用 `[myStr md5Hash]`，就好像这个方法是苹果官方写的一样！

### 2. 基础语法怎么写？
Category 的命名规范通常是 `原类名+分类名`。

**声明文件 (NSString+Hash.h)**
```objc
#import <Foundation/Foundation.h>

// 语法：@interface 原类名 (分类名)
@interface NSString (Hash)

// 声明你想加的方法
- (NSString *)md5Hash;

@end
```

**实现文件 (NSString+Hash.m)**
```objc
#import "NSString+Hash.h"

@implementation NSString (Hash)

- (NSString *)md5Hash {
    // 实现 MD5 的逻辑...
    return @"hashed_string";
}

@end
```

就这么简单！以后在任何地方只要 `#import "NSString+Hash.h"`，你就可以直接 `[@"hello" md5Hash]` 了。

---

## 第二部分：Category 的能力边界（能干啥，不能干啥？）

这是面试**最常考**的地方。

### ✅ Category 可以做的事：
1. **添加实例方法**（如上面的 `- (NSString *)md5Hash`）。
2. **添加类方法**（以 `+` 开头的方法）。
3. **实现协议（Protocol）**。
4. **添加属性（Property）** —— **⚠️ 注意：这里有大坑！**

### ❌ Category 绝对不能做的事：
**不能直接添加实例变量（Instance Variables，简称 Ivars）。**

#### 🚨 面试必考：为什么 Category 不能添加成员变量？
**答：因为 Objective-C 的内存布局在编译期就已经固定了。**
当一个类被编译时，它占多大内存、里面有几个成员变量，都已经定死了。Category 是在**运行期**才把方法合并到类里面的。如果允许在运行期动态加成员变量，那以前已经创建好的对象内存大小就全乱套了，会导致严重的内存越界崩溃。

#### 🚨 面试必考：那如果我非要在 Category 里加属性怎么办？
虽然不能加成员变量，但我们可以通过 **关联对象（Associated Objects）** 来模拟添加属性。
（关联对象的本质是：系统在底层维护了一个全局的 Hash 表，把你的对象当成 Key，把你要存的值当成 Value 存起来，并没有改变对象本身的内存结构）。

**代码演示（极其高频的手写题）：**
```objc
#import <objc/runtime.h> // 必须引入 runtime

@interface UIView (MyTag)
@property (nonatomic, copy) NSString *myCustomTag;
@end

@implementation UIView (MyTag)

// 1. 定义一个唯一的 Key (通常用静态变量的地址)
static const void *MyCustomTagKey = &MyCustomTagKey;

// 2. 自己实现 getter
- (NSString *)myCustomTag {
    // objc_getAssociatedObject(对象, Key)
    return objc_getAssociatedObject(self, MyCustomTagKey);
}

// 3. 自己实现 setter
- (void)setMyCustomTag:(NSString *)myCustomTag {
    // objc_setAssociatedObject(对象, Key, Value, 内存管理策略)
    objc_setAssociatedObject(self, MyCustomTagKey, myCustomTag, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

@end
```

---

## 第三部分：底层原理（它是怎么把方法塞进去的？）

面试官一定会问：“Category 的方法是在什么时候、怎么加到原类里的？”

### 1. 编译期：变成一个结构体
你在代码里写的 Category，在编译后，并不会立刻合并到原类里，而是被编译器转化成了一个叫 `category_t` 的结构体。
这个结构体里存了：分类的名字、原类的指针、**实例方法列表**、**类方法列表**、协议列表、属性列表。

### 2. 运行期：动态合并（Attach）
当 App 启动时，OC 的 Runtime 系统会去解析这些 `category_t` 结构体。
它会找到原类的方法列表，然后把 Category 里的方法列表，**拼接到原类方法列表的最前面！**

#### 🚨 面试连环坑：
**问：如果 Category 里的方法和原类里的方法同名，会发生什么？**
**答**：Category 的方法会**“覆盖”**原类的方法。
**追问：是真的覆盖（抹掉）了吗？**
**答**：不是真的抹掉。因为 Runtime 在合并方法列表时，是把 Category 的方法放在了数组的**最前面**。OC 调用方法是顺着数组从前往后找的，找到了就立刻调用并返回。所以 Category 的方法被先找到了，原类的方法依然在数组的后面，只是“被挡住”了（Shadowed）。

**问：如果有两个 Category（比如 A 和 B）都重写了原类的同一个方法，最终调用谁的？**
**答**：取决于**编译顺序**。最后参与编译的 Category，它的方法会被放在列表的最最前面，所以最终调用的是**最后编译**的那个 Category 的方法。

---

## 第四部分：Category vs Extension（分类 vs 扩展）

这是 iOS 面试的“送分题”，但也极容易搞混。
**Extension（扩展）** 看起来很像 Category，但它的括号里是没有名字的，所以也叫“匿名分类”。

```objc
// 这是 Extension 的语法，写在 .m 文件里
@interface MyClass () 
@property (nonatomic, strong) NSString *privateString; // 可以加属性！
- (void)privateMethod;
@end
```

### 核心对比表（死记硬背）

| 维度 | Category (分类) | Extension (扩展) |
| :--- | :--- | :--- |
| **括号里有无名字** | 有（如 `(Hash)`） | 无（就是 `()`） |
| **决议时机** | **运行期**（Runtime） | **编译期**（Compile time） |
| **能否加成员变量** | **不能**（只能用关联对象模拟） | **能**（直接加，通常用于私有变量） |
| **主要作用** | 给**已经存在的类**（哪怕没源码）加方法 | 给**自己的类**声明私有属性和私有方法 |
| **必须有源码吗** | 不需要原类源码（系统类也能加） | **必须有原类源码**（编译时要合并进去） |

---

## 第五部分：高阶面试题（+load 方法）

**问：Category 里面可以写 `+load` 方法吗？如果原类和 Category 都写了 `+load`，执行顺序是什么？**

**答：**
可以写。`+load` 方法是一个极其特殊的特例。
前面说过，同名方法 Category 会“覆盖”原类。但是 `+load` 方法**不会被覆盖**！

在 App 启动加载类的时候，Runtime 会**直接拿到函数指针去调用所有的 `+load` 方法**，而不是走常规的消息转发机制（`objc_msgSend`）。

**执行顺序是严格规定的：**
1. 先调用**原类**的 `+load`。
2. 再调用**Category**的 `+load`。
3. （如果有多个 Category，按照编译顺序，先编译的先调用）。

---

## 总结：面试如何一句话向 C++ 面试官解释 Category？

> “OC 的 Category 就像是运行时的动态补丁。它允许我们在不修改原类代码、不使用继承的情况下，在 App 启动时（Runtime），把新的方法列表动态拼接到原类的方法列表前面。它能加方法、加协议，但因为对象内存布局在编译期已固定，所以它不能直接加成员变量，如果非要存数据，只能通过 Runtime 的关联对象（Hash 表）来旁路存储。它和 C++ 的继承不同，它是扁平化地直接注入到原类当中的。”