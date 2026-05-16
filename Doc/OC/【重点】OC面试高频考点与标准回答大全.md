# OC (Objective-C) 高频面试考点与标准回答（口语化版）

在 iOS/macOS 音视频开发及底层 SDK 开发中，虽然 Swift 越来越流行，但很多底层框架（如 WebRTC 源码、FFmpeg 的封装）以及大厂的核心老业务，依然深度依赖 Objective-C (OC)。面试官考察 OC，核心是看你对 **Runtime（运行时）** 和 **内存管理** 的理解有多深。

以下是面试中最常问的 6 个 OC 考点，附带底层原理和可以直接在面试中“背诵”的口语化标准回答。

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
> **至于为什么不能直接添加成员变量**，是因为一个类的内存布局在编译期就已经死死定好了。对象的实例变量在底层其实就是一段连续的内存偏移量。运行的时候，这块内存早就分配完了，你不可能再硬生生地改变它的大小。
> 如果非要在分类里加变量，我们通常的绕过方案是利用 Runtime 的 **关联对象（Associated Object）** 技术，本质上是系统在底层维护了一个全局的 Hash 表来存这些额外的数据。”

---

## 考点 4：Block 的底层是什么？`__block` 解决了什么问题？

**底层原理：**
Block 本质上也是一个 OC 对象（底层结构体里有 `isa` 指针）。它可以捕获外部变量。
*   默认情况下，捕获的是外部局部变量的**值拷贝**（相当于 const 传递，内部无法修改）。
*   加上 `__block` 后，编译器会把这个基本数据类型包装成一个**专门的结构体对象**，把局部变量从栈区搬到了堆区。Block 内部拿到的是指向这个结构体的指针，所以可以修改原变量。

**🗣️ 面试标准回答：**
> “Block 在底层其实就是一个封装了函数及其执行上下文的 **OC 对象**。
> 默认情况下，我们在 Block 里用外部的局部变量，只是拿到了它的一个**值拷贝**，是改不了原本的变量的。
> 加了 `__block` 修饰符之后，编译器在底层会玩一个魔术：它会把这个原本在栈上的变量，包装成一个**对象类型的结构体**，并把这块内存转移到堆上。我们在 Block 内部修改这个变量时，其实是通过指针去修改堆上的那个结构体里的值，这就实现了在 Block 内部修改外部变量。”

**⚠️ 高级追问与坑点：`__block` 会导致野指针吗？**

**答案：一般不会导致野指针，但会极其容易导致**循环引用（内存泄漏）**。如果是特殊场景下的裸指针（如 C 数组或 char*），则可能产生野指针。**

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
>    如果你在循环里是用 `[[NSString alloc] initWithFormat:]` 生成的对象，底层是直接 +1 返回。ARC 在局部变量离开作用域 `}` 处插入 release 后，对象引用计数直接减到 0 当场销毁。这种情况下，即使不加 `@autoreleasepool` 内存也不会爆。
> 
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
> 
> 2. **哪些必须加 Pool？（非 alloc 开头的类工厂方法）**
>    但是像 `stringWithFormat:` 这类**类工厂方法**，其底层为了保证对象在返回给调用者时不被立刻销毁，会主动对这个新对象调用一次 `autorelease`。
> 
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
> 
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
>    很多人以为操作全局 `weak_table` 会引发多线程数据竞争，或者为了安全必须加一把全局大锁导致性能极差。其实苹果在底层并没有用全局大锁，而是采用了**分离锁（Striped Locks）**机制。底层的弱引用表实际上是由 64 个 `SideTable` 组成的一个数组，每个 `SideTable` 内部都有一把独立的自旋锁（现为 `os_unfair_lock`）。
>    当多线程同时对不同的对象清空 weak 指针时，Runtime 会通过对象地址 Hash 取模，找到它专属的那一个 `SideTable` 并加锁。这种设计既保证了多线程下置空 `nil` 的绝对安全，又完美避免了全局锁造成的性能阻塞。
> 
> 2. **业务代码层面：存在严重的“时序”多线程隐患（必须用强弱共舞解决）。**
>    虽然底层安全，但在我们的业务 Block 中直接使用 `weakSelf` 会有经典的多线程问题。假设我们在异步网络回调里写了 `[weakSelf doStepOne];` 和 `[weakSelf doStepTwo];`。因为是异步，可能在两句代码执行的间隙，主线程触发了页面退出，把对象给 `dealloc` 了。底层的安全机制会瞬间把 `weakSelf` 置为 `nil`，导致 `doStepTwo` 直接失效不执行（向 nil 发消息无效）。这会让你的业务逻辑被硬生生从中间截断，产生极其诡异的 Bug。
>
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
> 
>    **标准解法就是 Weak-Strong Dance（强弱共舞）：**在 Block 内部的第一行，立刻加上 `__strong typeof(weakSelf) strongSelf = weakSelf;`。这会在局部给对象 `+1` 引用计数，哪怕此时外部页面退出，子线程的 `strongSelf` 强指针也能临时把对象“拉住”，直到当前 Block 的业务代码全部安全执行完毕，才随着局部变量出作用域而彻底释放。”
>
>    **强弱共舞（安全代码）示例：**
>    ```objc
>    __weak typeof(self) weakSelf = self;
>    [Network requestData:^{
>        // 进来第一件事，用局部的强指针【临时保住】它的命
>        __strong typeof(weakSelf) strongSelf = weakSelf; 
>        
>        if (strongSelf) { // 判空，确保刚进 Block 时对象还没死
>            [strongSelf doStepOne];
>            // 就算此时外部主线程退出了页面，由于 strongSelf 强引用着它（引用计数>0）
>            // 对象绝对死不掉，保证了业务逻辑的完整性
>            [strongSelf doStepTwo]; 
>            strongSelf.data = data;
>        }
>        // Block 彻底执行完出 `}` 作用域，局部变量 strongSelf 释放，对象安详离世。
>    }];
>    ```

---

## 考点 7：`alloc`, `retain`, `release` 的底层含义与运作机制

在 Objective-C 中，内存管理的核心围绕着“引用计数（Reference Counting）”展开。这三个方法是操控对象生死的最基础操作。

**底层原理与含义：**

1. **`alloc`（分配内存）**
   * **含义**：全称是 Allocate，意思是向系统申请一块内存，用来存放一个全新的对象。
   * **底层行为**：底层调用 `calloc` 函数在堆区开辟内存，并将这块内存清零（所以新对象的属性默认都是 `nil` 或 `0`）。同时，**将这个新诞生的对象的引用计数（Retain Count）初始化为 1**。
   * **白话比喻**：“造一个新气球，并在气球上挂一块牌子写着数字 1。”

2. **`retain`（强引用/持有）**
   * **含义**：表示“我要用这个对象，你不能把它销毁了”。它用来声明对一个已有对象的所有权。
   * **底层行为**：底层通过原子操作（或者操作 SideTable），将该对象的**引用计数 +1**。
   * **白话比喻**：“往那个气球的牌子上，把数字加 1。”

3. **`release`（放弃持有）**
   * **含义**：表示“我用完这个对象了，我不再管它的死活了”。
   * **底层行为**：底层将该对象的**引用计数 -1**。随后，它会立刻检查减完之后的数字：**如果引用计数变成了 0**，说明全天下已经没有任何人需要这个对象了，系统会立刻调用该对象的 `dealloc` 方法，调用底层的 `free` 函数把内存彻底还给操作系统。
   * **白话比喻**：“把气球牌子上的数字减 1。如果减到了 0，就当场把气球戳破扔进垃圾桶。”

**🗣️ 面试标准回答：**
> “这三个操作是 OC 基于引用计数内存管理的最核心基石。
> `alloc` 负责在堆上分配清零的内存，诞生新对象，并把引用计数设为 1。
> `retain` 表示接手所有权，会让引用计数 +1，确保对象不死。
> `release` 表示放弃所有权，会让引用计数 -1。如果系统发现某次 release 后对象的计数清零了，就会立刻触发 dealloc 彻底销毁它。在现在的 ARC 时代，这三个方法的调用已经完全被编译器接管，严禁我们手动调用了。”