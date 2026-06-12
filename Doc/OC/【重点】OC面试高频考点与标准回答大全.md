# OC (Objective-C) 高频面试考点与标准回答（口语化版）

在 iOS/macOS 音视频开发及底层 SDK 开发中，虽然 Swift 越来越流行，但很多底层框架（如 WebRTC 源码、FFmpeg 的封装）以及大厂的核心老业务，依然深度依赖 Objective-C (OC)。面试官考察 OC，核心是看你对 **Runtime（运行时）** 和 **内存管理** 的理解有多深。

## 以下是面试中最常问的 11 个 OC 考点，附带底层原理和可以直接在面试中“背诵”的口语化标准回答。

---

## 考点 1：OC 的方法调用到底是怎么回事？（消息传递机制 / objc_msgSend）

**底层原理：**
OC 是一门动态语言。你在代码里写 `[obj doSomething]`，在编译期并不会像 C++ 那样直接确定函数的内存地址，而是会被编译器转化为一个 C 语言函数调用：`objc_msgSend(obj, @selector(doSomething))`。
运行时，系统会先在对象的类（Class）的方法缓存（cache）里找，找不到就去方法列表（method_list）里找，再找不到就顺着 `superclass` 指针去父类里找。

**🗣️ 面试标准回答：**

> “OC 的方法调用本质上是**消息发送**机制。
> 当我们调用一个方法时，底层其实是调用了 C 语言的 `objc_msgSend` 函数。它接收两个核心参数：消息的接收者（id）和方法名（SEL）。
> 执行的时候，系统会先去这个对象的类里面的 `cache`（缓存）查找，如果命中了就直接执行，性能很高；如果没命中，就去这个类的方法列表（method list）里找；如果还是没找到，就会顺着 `superclass` 指针一直向上找父类，直到找到 `NSObject`。如果一路找到顶都没找到，就会触发**消息转发**机制。”

---

## 考点 2：如果找不到方法，程序一定会崩溃吗？（消息转发机制）

**底层原理：**
如果 `objc_msgSend` 走完了上面的查找流程依然没找到方法，并不会立刻报 `unrecognized selector sent to instance` 崩溃，而是会给你三次“抢救”的机会（消息转发的三大阶段）：

1. **动态方法解析**：`resolveInstanceMethod`，你可以临时动态添加一个方法。
2. **快速转发**：`forwardingTargetForSelector`，你可以把这个消息甩锅给别的对象处理。
3. **慢速转发 / 完整消息转发**：`methodSignatureForSelector` 和 `forwardInvocation`，你可以把这个消息打包成一个 Invocation，做最后的灵活处理。

**🗣️ 面试标准回答：**

> “不一定会立刻崩溃，系统会给我们三次挽救的机会，也就是大名鼎鼎的**消息转发机制**。
> **第一步是动态解析**：系统会调用 `resolveInstanceMethod`，我们可以在这里利用 Runtime 的 `class_addMethod` 临时给它塞一个方法进去。
> **第二步叫快速转发**：如果我们啥也没加，系统会调 `forwardingTargetForSelector`，问我们要不要把这个消息‘甩锅’给其他能处理的对象，这就是典型的备援接收者。
> **第三步叫完整转发**：如果连备胎也没有，系统会要求我们返回一个方法签名，然后把整个消息打包成一个 `NSInvocation` 对象交给我们处理。如果我们连这最后一步都不管，程序才会抛出那个经典的 `unrecognized selector` 异常然后崩溃。”

---

## 考点 3：Category（分类）的底层原理是什么？为什么不能添加成员变量？

**底层原理：**
Category 在底层是一个叫 `category_t` 的结构体。在程序运行时（App 启动加载 image 时），Runtime 会将 Category 中定义的方法、属性和协议，动态地附加到**宿主类的结构体**中（通常是插到方法列表的最前面）。
为什么不能加成员变量？因为宿主类（Class）的内存布局在编译期就已经固定了（实例变量在内存中是连续分配的），运行时无法凭空在中间硬塞一段内存进去。

**🗣️ 面试标准回答：**

> “Category 的底层其实是在程序运行的时候（Runtime 阶段），通过动态把分类里面的方法列表拼接到原来类的方法列表的最前面，这也是为什么分类方法会‘覆盖’原类方法的原因。
> **这里有一个非常经典的面试陷阱：分类其实是可以添加【属性】的，但绝对不能添加【成员变量】。**
> 在分类里写 `@property`，底层确实会把这个属性添加到类的 `property_list` 中。但是，因为类的内存布局在编译期就已经死死定好了，运行的时候不可能再硬生生地改变它的大小，所以编译器**不会**为这个属性自动生成带下划线的成员变量（`_ivar`），也**不会**自动生成 setter 和 getter 方法。
> 为了让这个属性真正能存取数据，我们的标准解法是利用 Runtime 的 **关联对象（Associated Object）** 技术，手动重写 setter 和 getter，把数据存在系统底层维护的一个全局 Hash 表里。”

**🔥 极限深挖拷问：属性（Property）和成员变量（Ivar）的本质区别到底是什么？**

> 面试绝杀回答：“它们的本质区别在于：**成员变量是‘物理存储’，而属性是‘逻辑封装’（语法糖）。**
> 
> 1. **成员变量（Ivar）**：它是实实在在的内存空间（比如 `_name`）。在编译期，它就被硬编码成了对象内存布局中的一段偏移量。对象一旦创建，这块内存就固定了。
> 2. **属性（Property）**：它是编译器提供的一个‘套餐’。在正常的类中，写一个 `@property`，编译器会自动帮你做三件事：**生成一个带下划线的成员变量 + 生成 setter 方法 + 生成 getter 方法**。
> 
> **结合 Category 来看**：Category 的底层结构体里有 `property_list`（属性列表），所以我们可以声明属性；但它没有权利去改变宿主类的物理内存大小，所以无法生成成员变量。既然没有了底层的物理存储，编译器自然也就‘罢工’，不再帮你自动生成 setter 和 getter 方法了。这就是为什么我们在分类里写了 `@property`，还必须手动用关联对象去实现存取方法的原因。”

**💻 代码实战：如何利用关联对象（Associated Object）在分类中添加属性？**

在实际开发中，我们经常需要在分类中添加属性（例如给 `UIView` 加一个 `clickBlock`）。虽然不能添加成员变量（`_ivar`），但我们可以通过 `@property` 配合 Runtime 的关联对象来实现。

```objc
#import <objc/runtime.h>

@interface UIView (ClickBlock)
// 声明属性，但分类不会自动生成 _clickBlock 成员变量和 setter/getter
@property (nonatomic, copy) void(^clickBlock)(void);
@end

@implementation UIView (ClickBlock)

// 1. 定义一个唯一的静态变量地址作为 Key
static const void *kClickBlockKey = &kClickBlockKey;

// 2. 手动实现 setter 方法
- (void)setClickBlock:(void (^)(void))clickBlock {
    // objc_setAssociatedObject 的作用：把一个值，通过一个唯一的 Key，挂载到一个宿主对象身上。
    // 参数 1 (object): 宿主对象，这里就是 self (当前的 UIView)
    // 参数 2 (key): 唯一的 Key，用来在 Hash 表中定位这个值
    // 参数 3 (value): 你要存进去的具体数据，这里就是传进来的 clickBlock
    // 参数 4 (policy): 内存管理策略。OBJC_ASSOCIATION_COPY_NONATOMIC 完美对应了我们在 @property 里写的 (nonatomic, copy)
    objc_setAssociatedObject(self, kClickBlockKey, clickBlock, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

// 3. 手动实现 getter 方法
- (void (^)(void))clickBlock {
    // objc_getAssociatedObject 的作用：拿着宿主对象和唯一的 Key，去系统的全局 Hash 表里把之前存的值取出来。
    return objc_getAssociatedObject(self, kClickBlockKey);
}

@end
```

**🔥 极限深挖拷问：关联对象存在哪里？对象销毁时，关联对象会泄漏吗？**

> 面试绝杀回答：“关联对象**并没有存在宿主对象本身的内存里**。系统在底层维护了一个全局的 `AssociationsManager`，里面有一个 `AssociationsHashMap`（哈希表）。这个哈希表的 Key 是宿主对象的内存地址，Value 是另一个哈希表（存着这个对象所有的关联数据）。
>
> **至于内存泄漏问题，完全不用担心。** 当宿主对象调用 `dealloc` 准备销毁时，Runtime 的底层源码在 `dealloc` 的流程中，会调用一个 `_object_remove_assocations()` 函数。这个函数会拿着当前对象的地址去全局哈希表里查，如果发现有挂载的关联对象，就会自动把它们全部清理掉。所以只要你的内存管理策略（Policy）写对了，关联对象是绝对安全的。”

**🔥 极限深挖拷问：多个不同的对象（比如 viewA 和 viewB）都用同一个静态变量 `kClickBlockKey` 作为 Key，会互相覆盖冲突吗？**

> 面试绝杀回答：“绝对不会！这是一个非常经典的底层数据结构问题。
> 很多人担心 `static` 变量全局只有一份地址，会导致 viewA 的 block 覆盖掉 viewB 的 block。但实际上，Runtime 底层存储关联对象使用的是一个**双层哈希表（两级 Hash Map）**：
> - **第一层 Hash 表的 Key 是【宿主对象的内存地址】（即 `self`）**。这意味着 viewA 和 viewB 在第一层就被完全隔离开了，它们各自拥有一个独立的第二层 Hash 表。
> - **第二层 Hash 表的 Key 才是我们传进去的【静态变量地址】（即 `kClickBlockKey`）**。
> 
> 所以，`kClickBlockKey` 的作用仅仅是为了区分**同一个对象身上的不同属性**（比如区分 `clickBlock` 和 `longPressBlock`）。不同对象之间因为第一层 Key（`self`）不同，哪怕用同一个静态变量做第二层 Key，也绝对不可能发生冲突。”

---

## 考点 4：Block 的底层是什么？`__block` 解决了什么问题？

**底层原理：**
Block 本质上也是一个 OC 对象（底层结构体里有 `isa` 指针）。它可以捕获外部变量。

- 默认情况下，捕获的是外部局部变量的**值拷贝**（相当于 const 传递，内部无法修改）。
- 加上 `__block` 后，编译器会把这个基本数据类型包装成一个**专门的结构体对象**，把局部变量从栈区搬到了堆区。Block 内部拿到的是指向这个结构体的指针，所以可以修改原变量。

**🗣️ 面试标准回答：**

> “Block 在底层其实就是一个封装了函数及其执行上下文的 **OC 对象**。
> 默认情况下，我们在 Block 里用外部的局部变量，只是拿到了它的一个**值拷贝**，是改不了原本的变量的。
> 加了 `__block` 修饰符之后，编译器在底层会玩一个魔术：它会把这个原本在栈上的变量，包装成一个**对象类型的结构体**，并把这块内存转移到堆上。我们在 Block 内部修改这个变量时，其实是通过指针去修改堆上的那个结构体里的值，这就实现了在 Block 内部修改外部变量。”

---

### 💻 结合源码实例深度拆解

为了能和面试官聊得更深，我们需要通过 Clang 编译（`clang -rewrite-objc`）后的 C++ 底层实现来对比这两个场景：

#### 场景一：默认情况（不加 `__block`）—— 值拷贝

**1. OC 示例代码：**

```objc
int age = 18;
void (^myBlock)(void) = ^{
    NSLog(@"age is %d", age);
};
age = 20;
myBlock(); // 输出：age is 18
```

**2. 编译后的 C++ 底层结构体还原：**

```cpp
// 1. Block 本质上是一个结构体
struct __main_block_impl_0 {
    struct __block_impl impl;
    struct __main_block_desc_0* Desc;
    
    // 关键点：Block 内部直接定义了一个同名的 int 变量，用于存储捕获的值
    int age; 

    // 构造函数
    __main_block_impl_0(void *fp, struct __main_block_desc_0 *desc, int _age, int flags=0) 
        : age(_age) { // 核心：这里进行了【值拷贝】，把外部的 18 传进来，赋值给结构体内部的 age
        impl.isa = &_NSConcreteStackBlock; // 此时在栈上
        impl.FuncPtr = fp; // 指向 Block 的执行函数
        Desc = desc;
    }
};

// 2. Block 内部要执行的代码段（函数指针指向的地方）
static void __main_block_func_0(struct __main_block_impl_0 *__cself) {
    // 核心：这里访问的 age，其实是结构体内部的 age（值是 18），而不是外部的那个 age
    int age = __cself->age; 
    NSLog(@"age is %d", age);
}
```

* **为什么输出是 18？**：在创建 `myBlock` 的瞬间，外部的 `age` (18) 已经通过构造函数**值拷贝**（Copy By Value）到了结构体内部的 `age` 成员变量中。此后外部修改 `age = 20`，跟 Block 内部保存的 `age` 毫无关系。
* **为什么内部不能修改？**：因为在 C++ 层中，`__main_block_func_0` 里的 `age` 属性是只读的，在内部写 `age = 30;` 相当于试图修改传入的 `const` 参数，编译器会直接报错。

#### 场景二：加了 `__block` 修饰符 —— 指针拷贝（包装为结构体对象）

**1. OC 示例代码：**

```objc
__block int age = 18;
void (^myBlock)(void) = ^{
    age = 20; // 内部可以修改
    NSLog(@"age is %d", age);
};
myBlock(); // 输出：age is 20
NSLog(@"outside age is %d", age); // 外部也变为了 20
```

**2. 编译后的 C++ 底层结构体还原：**

```cpp
// 1. __block 变量被包装成一个专门的结构体对象
struct __Block_byref_age_0 {
    void *__isa; // 它也是个对象
    struct __Block_byref_age_0 *__forwarding; // 关键：指向自身的指针
    int __flags;
    int __size;
    int age; // 真正的那个 age 值保存在这里
};

// 2. Block 的底层结构体
struct __main_block_impl_0 {
    struct __block_impl impl;
    struct __main_block_desc_0* Desc;
    
    // 关键点：Block 内部捕获的不再是简单的 int，而是指向包装后结构体对象的指针！
    struct __Block_byref_age_0 *age; 

    __main_block_impl_0(void *fp, struct __main_block_desc_0 *desc, struct __Block_byref_age_0 *_age, int flags=0) 
        : age(_age) { // 指针拷贝，让 Block 内部持有该结构体的指针
        impl.isa = &_NSConcreteStackBlock;
        impl.FuncPtr = fp;
        Desc = desc;
    }
};

// 3. Block 内部执行的代码
static void __main_block_func_0(struct __main_block_impl_0 *__cself) {
    struct __Block_byref_age_0 *age = __cself->age; // 拿到保存的结构体指针
    
    // 核心：通过 __forwarding 指针找到并修改包装结构体内部的 age 变量
    (age->__forwarding->age) = 20; 
    
    NSLog(@"age is %d", (age->__forwarding->age));
}
```

---

### 🌟 面试满分加分项：为什么要通过 `__forwarding` 指针？（从栈到堆的转移）

面试官通常会在你讲完上述底层后顺藤摸瓜：**“既然能够拿到指针，为什么不直接修改 `age->age`，而要多此一举地绕一个圈子，通过 `age->__forwarding->age` 来修改？”**

这涉及 Block 的 **Copy（拷贝）机制**：

1. **栈状态**：当 Block 创建在**栈**上时，外部的 `__block` 变量结构体也在**栈**上。此时，栈上结构体的 `__forwarding` 指针指向的就是**它自己**（即栈上的结构体）。
2. **堆状态**：当 Block 被 copy 到**堆**上时，运行时系统（Runtime）会把栈上的 `__block` 变量结构体也**拷贝一份到堆上**。
   * 此时，**栈上**结构体的 `__forwarding` 指针会指向**堆上的新结构体**。
   * **堆上**结构体的 `__forwarding` 指针会指向**它自己**。

```text
【栈上的 Block】访问 age ──> 【栈上的 __Block_byref_age_0】
                                            │
                                      __forwarding 指针 ────────┐
                                                                 ▼
【堆上的 Block】访问 age ──> 【堆上的 __Block_byref_age_0】 <─────┘
                                            │
                                      __forwarding 指针 ──── 指向自己
```

**🗣️ 终极满分回答：**
> “通过 `__forwarding` 指针，可以完美保证**无论是在栈上还是在堆上修改这个 `__block` 变量，改到的都是同一份数据**！
> 
> 只要 Block 被 copy 到堆上，栈上结构体的 `__forwarding` 就会自动指向堆上的结构体。此时即使我们在栈上操作变量，通过 `age->__forwarding->age` 的调用链，实际上修改的依然是堆上的那份真实数据。这就解决了栈堆数据同步的问题。”

---

**⚠️ 高级追问与坑点：`__block` 会导致野指针吗？**

**答案：一般不会导致野指针，但会极其容易导致**循环引用（内存泄漏）*。如果是特殊场景下的裸指针（如 C 数组或 char），则可能产生野指针。**

**深层拷问：如果对象本来就在堆上，`__block` 搬运了什么？是怎么导致循环引用的？**

> 面试绝杀回答：“在 OC 中，真正的对象一直都在堆上，而我们用 `__block` 修饰的，其实是**指向那个堆对象的、位于栈上的局部指针变量**。`__block` 的本质并不是去搬运堆上的对象，而是把**『栈上的指针变量』**包装成了一个底层的结构体，并搬运到了堆上。
>
> 在 ARC 时代，当这个底层的结构体被拷贝到堆上时，它内部维护的那个强指针，会顺理成章地对真正的堆内存对象发起一次强引用（Retain）。如果这个真正的对象刚好又是持有该 Block 的 `self`，闭环形成，这就导致了经典的循环引用死锁。”

**坑点 1：最经典的循环引用（Retain Cycle）**
如果用 `__block` 修饰 `self`（或者一个持有该 Block 的对象）：

```objc
// ❌ 错误示范：致命的循环引用
__block typeof(self) blockSelf = self;
self.myBlock = ^{
    // self 持有了 myBlock，myBlock 持有了包装成结构体的 blockSelf，blockSelf 又强引用了 self。
    // 闭环形成，内存永远泄漏！
    [blockSelf doSomething]; 
};
```

**正确解法**：在 ARC 下，打破循环引用必须用 `__weak`，而不是 `__block`！

```objc
// ✅ 正确示范：Weak-Strong Dance
__weak typeof(self) weakSelf = self;
self.myBlock = ^{
    __strong typeof(weakSelf) strongSelf = weakSelf; // 防止执行一半 self 被释放
    if (strongSelf) {
        [strongSelf doSomething];
    }
};
```

**坑点 2：隐藏的野指针陷阱（捕获 C 语言指针）**
如果外部变量是一个 C 语言的裸指针（比如动态分配的内存、局部 C 数组等），Block 只是简单地把这个**指针地址拷贝了进去**，它**不会、也无法去管理这块 C 语言内存的生命周期**。

```objc
- (void)testPointerTrap {
    char *text = malloc(100);
    strcpy(text, "Hello World");
    
    // Block 捕获了这个指针的地址
    self.delayBlock = ^{
        // ❌ 这里极大概率发生野指针崩溃！
        NSLog(@"打印内容：%s", text); 
    };
    
    // 函数结束前，C 语言内存被手动释放了
    free(text);
}
// 等到 delayBlock 真正执行时，text 指向的内存已经是垃圾数据，触发野指针！
```

**防坑策略**：对于 C 语言指针或局部栈数组，如果要在 Block 内异步使用，必须先将其转换为 OC 对象（如 `NSString` 或 `NSData`），让 ARC 去接管生命周期。

---

## 考点 5：ARC 是怎么管理内存的？AutoReleasePool 是怎么工作的？

**底层原理：**
ARC（自动引用计数）是 LLVM 编译器的一个特性，编译时在合适的位置自动插入 `retain` 和 `release` 代码。
`AutoreleasePool`（自动释放池）在底层是一个以**双向链表**形式组合而成的栈结构（以 `AutoreleasePoolPage` 为节点）。调用 `autorelease` 的对象会被推入这个栈中。当池子销毁（如 RunLoop 迭代结束）时，系统会对栈里的所有对象统一发送一次 `release` 消息。

**🗣️ 面试标准回答：**

> “ARC 其实不是什么运行时的垃圾回收机制，而是**编译器特性**。是苹果的 LLVM 编译器在编译代码的时候，偷偷帮我们在合适的地方插好了 `retain` 和 `release` 代码。
> 关于 **AutoreleasePool**，它的底层其实是一个用双向链表拼起来的栈结构，叫做 `AutoreleasePoolPage`。
> 当我们把一个对象标记为 `autorelease` 时，它就会被压入这个栈里。当当前的 RunLoop 循环结束，或者代码走出了 `@autoreleasepool {}` 大括号时，这个池子就会被清空，同时会对里面装的所有对象统一发送一次 `release` 消息。”

**🔥 极限深挖拷问：RunLoop 和 AutoreleasePool 有什么联系和区别？**

> 面试绝杀回答：“`RunLoop` 和 `AutoreleasePool` 不是同一个东西，但在主线程上它们经常一起出现。
>
> `RunLoop` 负责线程的事件循环，解决的是**线程如何保持活着并处理事件**的问题；`AutoreleasePool` 负责延迟释放对象，解决的是 **autorelease 对象什么时候 release** 的问题。
>
> 在 iOS 主线程中，系统会给 `RunLoop` 注册 Observer，用来自动管理释放池。一次 RunLoop 循环大致可以理解为：
>
> ```objc
> // 伪代码理解
> RunLoop 即将进入一次循环:
>     push AutoreleasePool
>
> RunLoop 开始处理事件:
>     // 点击事件、Timer、Source、网络回调、布局刷新等
>     // 期间产生的 autorelease 对象进入当前 Pool
>
> RunLoop 即将休眠 / 本轮循环结束:
>     pop AutoreleasePool
>     // Pool 里的对象统一收到 release
>     push 一个新的 AutoreleasePool
> ```
>
> 所以主线程上的很多 `autorelease` 对象，并不是在当前方法结束或 `for` 循环结束时马上释放，而是等当前这轮 RunLoop 处理完事件、准备休眠时才统一释放。
>
> 一句话总结：**RunLoop 管线程调度和事件循环，AutoreleasePool 管对象延迟释放；主线程 RunLoop 每一轮循环都会自动帮我们维护一层 AutoreleasePool。**”

**⚠️ 面试必考实战：为什么在大的 for 循环里必须手动加 `@autoreleasepool`？**

如果我们在一个大的 `for` 循环里创建了大量的临时对象（比如读取、处理上万张图片），在默认情况下，这些临时对象都会被挂在主线程最外层的 AutoreleasePool 中。而主线程的 Pool 要等到**这一次 RunLoop 循环完全结束（也就是当前线程闲下来）**时才会统一清空。
这就导致在巨大的 `for` 循环跑完之前，临时对象的内存根本得不到释放，内存峰值会瞬间疯狂暴涨，直接被系统当场杀死（OOM 崩溃）。

**结合代码说明哪些会爆内存，哪些不会爆：**

```objc
// ❌ 错误示范：产生 autorelease 对象，死等主线程 Pool，内存暴涨 OOM
for (int i = 0; i < 100000; i++) {
    // 像 [NSString stringWithFormat] 和 [UIImage imageNamed:] 这类【非 alloc】开头的工厂方法，
    // 产生的都是 autorelease 对象。它们会越过当前的 `}` 作用域，全部堆积在主线程的 Pool 里。
    NSString *fileName = [NSString stringWithFormat:@"image_%d.png", i];
    UIImage *image = [UIImage imageNamed:fileName];
    // ... 处理图片的复杂逻辑 ...
}

// ✅ 正确示范 1：加 @autoreleasepool 就近拦截，内存稳如老狗
for (int i = 0; i < 100000; i++) {
    @autoreleasepool {
        // 加上大括号后，为【每一次】循环单独创建了临时释放池。
        // 这一轮产生的 autorelease 临时对象，在遇到 `}` 出括号时，局部池子销毁，对象立刻被释放。
        NSString *fileName = [NSString stringWithFormat:@"image_%d.png", i];
        UIImage *image = [UIImage imageNamed:fileName];
    }
}

// ✅ 正确示范 2：全部使用 alloc/new 族方法（根本不产生 autorelease 对象），不加 Pool 也不会爆！
for (int i = 0; i < 100000; i++) {
    // 根据 OC 命名公约，alloc 创建的对象调用者拥有绝对所有权，底层不会调 autorelease。
    // 当代码执行到当前循环的 `}` 时，ARC 自动插入 release，引用计数清零，对象当场销毁！
    NSString *fileName = [[NSString alloc] initWithFormat:@"image_%d.png", i];
    // 注意：为了彻底不产生 autorelease 对象，图片也不能用 imageNamed (内部有缓存和autorelease)，
    // 必须用 alloc 搭配 initWithContentsOfFile 来初始化。
    NSString *path = [[NSBundle mainBundle] pathForResource:fileName ofType:nil];
    UIImage *image = [[UIImage alloc] initWithContentsOfFile:path];
}
```

**🔥 极限深挖拷问：我们在写代码时，如果返回一个对象，需要手动加 `[obj autorelease]` 吗？**

> 面试绝杀回答：“在目前的 ARC 时代，**绝对不需要，也不允许手动写 `autorelease`**。编译器（LLVM）会彻底接管这件事。
>
> 当我们写一个普通工厂方法（比如 `+ createMyString`）直接 `return str;` 时，ARC 会在底层自动帮我们插入 `objc_autoreleaseReturnValue` C 函数，完成延迟释放的挂载。
> 更厉害的是，现代 ARC 在这里做了极大的性能优化（**TLS 线程局部存储优化**）：它会偷偷去检查调用这个方法的外层代码，如果外层马上用强指针接手了这个对象，ARC 就会发现压入 AutoreleasePool 纯属多此一举，它会直接做个标记，跳过入池操作，完成内外无缝交接，榨干了最后一滴性能。”

**🔥 极限深挖拷问：ARC 不是会在作用域结束时自动插入 release 吗？为什么这里 ARC 没把对象杀掉？**

> 面试绝杀回答：“ARC 确实在每次 for 循环 `}` 结束时插入了 `release`，但这只能抵消掉局部变量的强引用，**无法让对象引用计数归零**。
>
> 核心在于：OC 遵循严格的方法命名约定。
>
> 1. **哪些不需要加 Pool？（`alloc/new/copy/mutableCopy` 开头的方法）**
>   如果你在循环里是用 `[[NSString alloc] initWithFormat:]` 生成的对象，底层是直接 +1 返回。ARC 在局部变量离开作用域 `}` 处插入 release 后，对象引用计数直接减到 0 当场销毁。这种情况下，即使不加 `@autoreleasepool` 内存也不会爆。
>    **代码示例：**

---

```objc
for (int i = 0; i < 100000; i++) {
    // alloc 创建，初始引用计数为 1
    NSString *str = [[NSString alloc] initWithFormat:@"Hello %d", i]; 
    // do something with str
    
    // 循环结束 \} 前，ARC 自动插入 [str release]
    // 此时引用计数 1 - 1 = 0，str 被立即释放销毁，内存不会堆积！
}
```

---

> 1. **哪些必须加 Pool？（非 alloc 开头的类工厂方法）**
>   但是像 `stringWithFormat:` 这类**类工厂方法**，其底层为了保证对象在返回给调用者时不被立刻销毁，会主动对这个新对象调用一次 `autorelease`。

**代码示例：**

```objc
for (int i = 0; i < 100000; i++) {
    // 类工厂方法创建，内部相当于：
    // NSString *str = [[[NSString alloc] initWithFormat:@"..."] autorelease];
    NSString *str = [NSString stringWithFormat:@"Hello %d", i];
    // do something with str
    
    // 循环结束 } 前，ARC 同样自动插入 [str release]
    // 虽然抵消了局部变量的强引用，但对象身上还有一层 autorelease "锁"
    // 由于当前循环没有手写 @autoreleasepool，对象被挂载到了距离当前最近的 Pool 中
    // 
    // 【池子到底是谁的？什么时候销毁？】
    // 场景 A（最常见：主线程有 UI 交互 / 定时器 / 网络回调触发）：
    // 系统会在整个事件（比如整个 buttonClicked 方法，包括它调用的所有子方法）开始前 Push 一个 Pool，
    // 并在整个事件彻底处理完、RunLoop 准备休眠时才 Pop 销毁。
    // 在事件完全跑完之前，即使你把这 10 万次循环封装进了 10 层深的方法里，返回时池子也依然没销毁，内存照样暴涨！
    //
    // 场景 B（子线程没有开启 RunLoop）：
    // 如果是你自己创建的子线程（比如 pthread 或者直接 [NSThread detach]），且没有主动跑 RunLoop。
    // 这种情况下，根本没有系统帮你创建最外层的 Pool！
    // 这些 autorelease 对象会因为找不到池子而发生内存泄漏，控制台还会打印 "just leaking" 警告。
    // 因此在这种子线程里，你必须自己在最外层（或者循环里）手写 @autoreleasepool。
}
```

> `[obj autorelease]` 的本质是：**它并没有释放对象，而是把该对象挂载（注册）到了距离当前代码最近的一个 AutoreleasePool 中。** 这就相当于给这个对象上了一个『延迟执行的 release 锁』。
>
> 在大循环中，如果没有手动加 `@autoreleasepool`，距离它最近的 Pool 就是主线程 RunLoop 自动创建的那个巨大的系统 Pool。在循环大括号 `}` 处，ARC 虽然释放了局部变量的强引用，但那个『延迟的 release 锁』依然被系统的主线程 Pool 捏在手里。这就导致这 10 万个对象处于『暂时死不掉』的僵尸状态。必须等到主线程当前 RunLoop 循环结束、清理系统默认 Pool 时，它们才会统一收到最后的 release 而销毁。这也是我们必须手动包一层 `@autoreleasepool` 来就近拦截并提前清算它们的原因。”

---

## 考点 6：循环引用（Retain Cycle）怎么解决？`__weak` 是如何置空的？

**底层原理：**
当两个对象相互强引用（比如 Block 强持有 self，self 又强持有 Block）时，引用计数永远降不到 0，导致内存泄漏。
打断循环引用常用 `__weak`。`__weak` 修饰的指针底层是被 Runtime 的一个全局 `weak_table`（弱引用表，一个 Hash 表）管理的。Key 是对象的内存地址，Value 是所有指向它的弱引用指针数组。当对象被释放（`dealloc`）时，Runtime 会去这张表里查找到所有的弱引用指针，并将它们全部置为 `nil`，从而避免野指针。

**🗣️ 面试标准回答：**

> “循环引用最常见的场景就是 Block 或者 Delegate 互相持有着不放手，导致两边谁也死不掉。我们的标准解法是用 `__weak` 打破这个闭环，比如常用的 `__weak typeof(self) weakSelf = self`。
> **至于 `__weak` 为什么在对象死后能自动变成 `nil`**，这是因为 Runtime 在底层维护了一张全局的**弱引用 Hash 表（weak_table）**。
> 对象的内存地址是 Key，我们的 weak 指针地址是 Value。当这个对象走到生命的尽头、调用 `dealloc` 的时候，Runtime 就会拿着对象的地址去这个 Hash 表里查，把所有指向它的 weak 指针统统清空设为 `nil`，这套机制极其优雅地防止了野指针 Crash 问题的发生。”

**🔥 极限深挖拷问：`__weak` 置空的过程有多线程安全问题吗？如果在异步 Block 里连续使用 `weakSelf` 会有什么隐患？**

> 面试绝杀回答：“这个问题要分**底层 Runtime** 和**业务代码**两个层面来看。
>
> 1. **底层 Runtime 层面没有多线程安全问题（分离锁机制）。**
>   很多人以为操作全局 `weak_table` 会引发多线程数据竞争，或者为了安全必须加一把全局大锁导致性能极差。其实苹果在底层并没有用全局大锁，而是采用了**分离锁（Striped Locks）**机制。底层的弱引用表实际上是由 64 个 `SideTable` 组成的一个数组，每个 `SideTable` 内部都有一把独立的自旋锁（现为 `os_unfair_lock`）。
>    当多线程同时对不同的对象清空 weak 指针时，Runtime 会通过对象地址 Hash 取模，找到它专属的那一个 `SideTable` 并加锁。这种设计既保证了多线程下置空 `nil` 的绝对安全，又完美避免了全局锁造成的性能阻塞。
> 2. **业务代码层面：存在严重的“时序”多线程隐患（必须用强弱共舞解决）。**
>   虽然底层安全，但在我们的业务 Block 中直接使用 `weakSelf` 会有经典的多线程问题。假设我们在异步网络回调里写了 `[weakSelf doStepOne];` 和 `[weakSelf doStepTwo];`。因为是异步，可能在两句代码执行的间隙，主线程触发了页面退出，把对象给 `dealloc` 了。底层的安全机制会瞬间把 `weakSelf` 置为 `nil`，导致 `doStepTwo` 直接失效不执行（向 nil 发消息无效）。这会让你的业务逻辑被硬生生从中间截断，产生极其诡异的 Bug。
>    **危险的时序 Bug 代码示例：**

```objc
__weak typeof(self) weakSelf = self;
[Network requestData:^{
    // 假设当前在子线程回调，此时 weakSelf 存活
    [weakSelf doStepOne];  // 执行成功
    
    // 🔴 极度危险的时刻！
    // 就在这一毫秒，用户退出了当前页面，主线程立刻把 self 释放了！
    // 底层 weak_table 瞬间把 weakSelf 变成了 nil
    
    [weakSelf doStepTwo];  // 此时 weakSelf 变成了 nil，这行代码无效
    weakSelf.data = data;  // 数据赋值失败，业务逻辑断裂！
}];
```

>    **标准解法就是 Weak-Strong Dance（强弱共舞）：**在 Block 内部的第一行，立刻加上 `__strong typeof(weakSelf) strongSelf = weakSelf;`。这会在局部给对象 `+1` 引用计数，哪怕此时外部页面退出，子线程的 `strongSelf` 强指针也能临时把对象“拉住”，直到当前 Block 的业务代码全部安全执行完毕，才随着局部变量出作用域而彻底释放。”
>
>    **强弱共舞（安全代码）示例：**

---

## 考点 7：`alloc`, `retain`, `release` 的底层含义与运作机制

在 Objective-C 中，内存管理的核心围绕着“引用计数（Reference Counting）”展开。这三个方法是操控对象生死的最基础操作。

**底层原理与含义：**

1. `**alloc`（分配内存）**
  - **含义**：全称是 Allocate，意思是向系统申请一块内存，用来存放一个全新的对象。
  - **底层行为**：底层调用 `calloc` 函数在堆区开辟内存，并将这块内存清零（所以新对象的属性默认都是 `nil` 或 `0`）。同时，**将这个新诞生的对象的引用计数（Retain Count）初始化为 1**。
  - **白话比喻**：“造一个新气球，并在气球上挂一块牌子写着数字 1。”
2. `**retain`（强引用/持有）**
  - **含义**：表示“我要用这个对象，你不能把它销毁了”。它用来声明对一个已有对象的所有权。
  - **底层行为**：底层通过原子操作（或者操作 SideTable），将该对象的**引用计数 +1**。
  - **白话比喻**：“往那个气球的牌子上，把数字加 1。”
3. `**release`（放弃持有）**
  - **含义**：表示“我用完这个对象了，我不再管它的死活了”。
  - **底层行为**：底层将该对象的**引用计数 -1**。随后，它会立刻检查减完之后的数字：**如果引用计数变成了 0**，说明全天下已经没有任何人需要这个对象了，系统会立刻调用该对象的 `dealloc` 方法，调用底层的 `free` 函数把内存彻底还给操作系统。
  - **白话比喻**：“把气球牌子上的数字减 1。如果减到了 0，就当场把气球戳破扔进垃圾桶。”

**🗣️ 面试标准回答：**

> “这三个操作是 OC 基于引用计数内存管理的最核心基石。
> `alloc` 负责在堆上分配清零的内存，诞生新对象，并把引用计数设为 1。
> `retain` 表示接手所有权，会让引用计数 +1，确保对象不死。
> `release` 表示放弃所有权，会让引用计数 -1。如果系统发现某次 release 后对象的计数清零了，就会立刻触发 dealloc 彻底销毁它。在现在的 ARC 时代，这三个方法的调用已经完全被编译器接管，严禁我们手动调用了。”

---

## 考点 8：简单介绍一下什么是 OC 的 Runtime？

**🗣️ 面试标准回答：**

> “Runtime 其实就是 OC 的运行时机制，它是 OC 能够成为动态语言的基石。底层是用 C、C++ 和汇编实现的一套 API。我们在写 OC 代码的时候，比如调用一个方法 `[obj doSomething]`，在编译阶段其实只是确定了要发送消息，真正找到这个方法并执行，是推迟到运行阶段通过 `objc_msgSend` 来完成的。正是因为有了 Runtime，我们才能在程序运行的时候动态地创建类、添加方法、替换方法实现（Method Swizzling）等等。”

---

## 考点 9：什么是 Method Swizzling（黑魔法）？你在项目里用它做过什么？

**🗣️ 面试标准回答：**

> “Method Swizzling 本质上就是利用 Runtime 动态交换两个方法的实现（IMP）。每个类都有一个方法列表，里面存着 SEL（方法名）和 IMP（函数指针）的映射关系。Swizzling 就是把这两个 SEL 指向的 IMP 交换一下。在项目里，我们通常用它来做 AOP（面向切面编程），比如无侵入地统计页面的 PV/UV（交换 `UIViewController` 的 `viewWillAppear`），或者做全局的防 Crash 处理（比如拦截 `NSArray` 的 `objectAtIndex:` 防止越界崩溃）。不过使用时一定要放在 `+load` 方法的 `dispatch_once` 里，保证只交换一次，否则容易出大问题。”

---

## 考点 10：讲一下对 isa 指针的理解？（含查找流程与底层深挖）

**🗣️ 面试标准回答：**

> “在 OC 里，`isa` 指针主要负责‘找类型’，而 `superclass` 指针负责‘找继承’，它们俩是配合工作的。
> 首先说 `isa`：实例对象的 `isa` 指向类对象，类对象的 `isa` 指向元类，元类的 `isa` 最终指向根元类形成闭环。`**isa` 的作用仅仅是决定了方法查找的【第一站（起点）】**（实例方法第一站去类对象找，类方法第一站去元类找）。
>
> **具体是怎么顺着找的呢？（结合 cache 和 method_list）**
> 假设 B 继承 A，我们用 B 的实例调用 A 的方法：
>
> 1. **第一站（靠 isa）**：Runtime 首先顺着 B 实例的 `isa` 来到 **B 的类对象**。
>   - 先在 B 类的 `cache`（哈希表，纯汇编实现，速度极快）里找。
>   - 没找到，再进入 C/C++ 层面，去 B 类的 `method_list` 里找（二分查找或遍历）。
>   - **⚠️ 极其容易混淆的坑点：** 如果在 B 的类对象里没找到实例方法，**绝对不会**顺着 isa 去 B 的元类里找！元类只存类方法，实例方法的查找路径永远只在类对象和父类对象之间穿梭。
> 2. **第二站及以后（靠 superclass）**：如果在 B 类里彻底没找到，`**isa` 的任务就结束了**。接下来 Runtime 会顺着 B 类对象内部的 `**superclass` 指针**，来到 **A 的类对象**。
>   - 在 A 类里，**重新执行一遍**先查 `cache`，再查 `method_list` 的流程。
>   - 如果找到了，不仅会执行，还会把这个方法塞进 **B 类（当前调用者）的 `cache` 里**，方便下次秒查。
>   - 如果 A 类还找不到，就继续顺着 A 的 `superclass` 找，直到根类（NSObject）。
>
> **如果是调用父类的【类方法】**，原理一模一样：第一站顺着 B 类的 `isa` 找到 **B 的元类**，找不到就顺着 `superclass` 找到 **A 的元类**，在里面查 cache 和 method_list。
>
> 一句话总结：`**isa` 只负责把你带到第一站，如果在第一站找不到，后面所有的向上查找全靠 `superclass` 指针；而每到一个站点，都是先查 `cache`，再查 `method_list`。**”

**🔥 极限深挖拷问：实例对象能直接调用类方法吗？（比如 `[person classMethod]`）**

> 面试绝杀回答：“在 OC 里绝对不能！这就好比你拿着一张去北京的高铁票，却想坐飞机去上海。
> 当你用实例对象调用方法时，Runtime 规定死了第一站必须顺着实例的 `isa` 去**类对象**里找。而类方法是存在**元类**里的。
> 在类对象这条线上，无论你怎么顺着 `superclass` 往上找，找破天也只能找到父类的类对象、根类的类对象，**永远不可能跨界跳到元类那条线上去**。所以实例对象调用类方法，最终一定会因为找不到方法而引发 `unrecognized selector sent to instance` 崩溃。
>
> **（加分项：对比 C++）** 顺便提一下，这和 C++是完全不同的。在 C++ 里，你是可以用实例对象去调用静态成员函数（类方法）的（比如 `obj.staticMethod()`）。因为 C++ 的方法绑定在编译期就决定了，编译器看到你调静态方法，会自动把它替换成 `Class::staticMethod()`。但 OC 是动态语言，完全依赖运行时的 `isa` 寻址，路线不通就是不通。”

**🔥 极限深挖拷问：OC 中有没有不依赖类的、类似 C 语言全局函数的方法？它们的调用是静态绑定还是动态绑定？**

> 面试绝杀回答：“有的！因为 Objective-C 是 C 语言的严格超集，所以我们完全可以在 OC 文件里直接写 C 语言的全局函数（比如普通的 C 函数，或者 `static inline` 内联函数）。
> 它们的调用是**绝对的静态绑定**！在编译阶段，编译器就已经确定了这些 C 函数的内存地址。运行时 CPU 直接 `call` 这个固定的内存地址，**完全不经过 `objc_msgSend` 消息发送机制**，根本不需要查 `isa` 指针，也不需要遍历什么 `cache` 和 `method_list`。
> **（结合业务场景加分）** 在实际开发中，苹果底层的 Core Graphics 框架（比如 `CGRectMake`）大量使用了 C 函数。另外，在音视频底层开发（比如 FFmpeg 视频帧处理）这种对性能要求极其苛刻、需要每秒高频调用成千上万次的场景下，我们通常会故意放弃 OC 的动态方法，直接使用 C 函数来实现。这样能彻底省去 Runtime 动态查找的开销，达到极致的性能。”

---

## 考点 11：@property 中常用修饰符（strong, weak, copy, assign, nonatomic）的底层含义与使用场景？

**底层原理与含义：**

`@property` 本质上是帮我们自动生成了实例变量（`_ivar`）以及对应的 `setter` 和 `getter` 方法。不同的修饰符，决定了底层 `setter` 方法内部是如何管理内存和线程安全的。

1. **`strong`（强引用）**
   - **底层行为**：在 `setter` 方法中，会对新传进来的对象调用 `retain`（引用计数 +1），对旧对象调用 `release`（引用计数 -1），然后进行赋值。
   - **使用场景**：绝大多数的 OC 对象（如 `UIView`, `NSArray`, 自定义对象等）。它保证了只要当前对象还活着，它强引用的属性就不会被销毁。

2. **`weak`（弱引用）**
   - **底层行为**：在 `setter` 方法中，仅仅是简单的指针赋值，**绝对不会**增加对象的引用计数。更强大的是，底层 Runtime 会把这个指针的地址注册到全局的 `weak_table`（弱引用哈希表）中。当对象被销毁时，Runtime 会自动把这个指针置为 `nil`，防止野指针崩溃。
   - **使用场景**：专门用来打破循环引用。最常见的是修饰 `delegate`（代理）和 XIB/Storyboard 拖出来的 UI 控件。

3. **`copy`（拷贝）**
   - **底层行为**：在 `setter` 方法中，会对新传进来的对象调用 `copy` 方法，克隆出一个全新的对象，然后强引用这个新对象。
   - **使用场景**：
     - **修饰 NSString、NSArray 等有可变子类（NSMutableString）的对象**：防止外部传进来一个可变对象，然后在外部被偷偷修改，导致内部状态错乱。
     - **修饰 Block**：在 ARC 时代，虽然编译器有时会自动把栈上的 Block 拷到堆上，但为了语义明确和绝对安全，Block 属性必须用 `copy` 修饰，确保它离开局部作用域后依然存活在堆区。

4. **`assign`（简单赋值）**
   - **底层行为**：最原始的直接赋值，没有任何内存管理操作（不 retain，也不注册 weak_table）。如果指向的对象被销毁了，指针依然指向那块内存，变成极其危险的**野指针**。
   - **使用场景**：只能用来修饰**基本数据类型**（如 `int`, `float`, `BOOL`, `CGFloat` 等），因为基本数据类型分配在栈区，由系统自动管理，不需要引用计数。**绝对不要用 assign 来修饰 OC 对象**。

5. **`nonatomic`（非原子性） vs `atomic`（原子性）**
   - **`atomic`**：底层会在 `setter` 和 `getter` 方法内部加一把自旋锁（现为 `os_unfair_lock`），保证同一时刻只有一个线程能进行读写操作。但它**只能保证读写安全，不能保证业务逻辑的安全**（比如多线程对数组进行 add/remove 依然会崩），而且加锁极其耗费性能。
   - **`nonatomic`**：底层不加任何锁，直接读写。
   - **使用场景**：在 iOS 开发中，为了极致的性能，**99.99% 的属性都必须使用 `nonatomic`**。如果真的需要线程安全，我们会在业务代码里自己加锁（如 GCD、NSLock），而不是依赖 `atomic`。

**🗣️ 面试标准回答：**

> “`@property` 的修饰符主要分为内存管理和线程安全两大类。
> 内存管理方面：
> - `strong` 用来修饰普通对象，底层会 retain 新值，保证对象存活。
> - `weak` 用来修饰代理或解决循环引用，它不增加引用计数，且对象销毁时底层 Runtime 会自动把指针置为 nil，非常安全。
> - `copy` 主要用来修饰 NSString 和 Block。修饰字符串是为了防止外部传入 NSMutableString 被意外篡改；修饰 Block 是为了把它从栈区拷贝到堆区，防止离开作用域被销毁。
> - `assign` 只能修饰基本数据类型，如果修饰对象会产生野指针。
> 
> 线程安全方面：
> - 我们几乎永远只用 `nonatomic`。因为 `atomic` 虽然会在 setter/getter 内部加锁，但它极其消耗性能，而且根本无法保证真正的多线程业务安全。真正的线程安全应该由我们在业务层手动加锁来控制。”

