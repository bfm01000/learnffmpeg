# Objective-C Runtime 核心原理、避坑与面试指南

> **💡 面试速记（可直接背诵的话术模板）**
>
> 1. **什么是 Runtime？** Runtime 是 Objective-C 面向对象和动态机制的基石。它是一个用 C 和汇编编写的运行时库，它把编译期的工作（如方法绑定）推迟到了运行期。
> 2. **消息机制（`objc_msgSend`）的流程是怎样的？** 调用方法本质是发消息。流程分三步：① 快速查找（汇编实现，在类的 cache 中找）；② 慢速查找（C 实现，在当前类和父类的 method_list 中找）；③ 动态方法解析与消息转发（如果没找到，给最后三次机会补救）。
> 3. **Category 为什么不能添加成员变量？** 因为类的内存布局在编译期就已经确定，isa 指针和 ivar_list 的大小是固定的。如果在运行期通过 Category 强行增加 ivar，会导致破坏原有的内存布局。但我们可以通过 Runtime 的**关联对象（Associated Objects）**来模拟添加属性。
> 4. **Method Swizzling（黑魔法）的作用和风险？** 它可以在运行时动态交换两个方法的实现（IMP），常用于无侵入式的 AOP 编程（如无痕打点、防 Crash 兜底）。但由于它的全局性，如果多个 Category 同时 Swizzle 同一个方法，或者在非 dispatch_once 中执行，极易引发死循环和不可预知的 Crash。

---

## 一、 Runtime 核心概念与底层结构

在 C 语言中，函数的调用在编译期就已经确定了地址（静态绑定）。但在 OC 中，`[obj doSomething]` 直到运行期才会去查找真正的函数地址（动态绑定）。

### 1. 对象的本质（`objc_object` & `isa`）

在 OC 中，每一个对象底层都是一个 C 结构体。

```c
struct objc_object {
    Class isa; // 每个对象都有一个 isa 指针
};
// id 其实就是 objc_object* 的别名
typedef struct objc_object *id;
```

`isa` 指针是 Runtime 的灵魂，它指向了**类对象（Class）**。当向实例对象发消息时，Runtime 就是顺着 `isa` 找到类对象，去类的方法列表里查找。

**面试高频追问：`NSObject` 和 `objc_object` 是什么关系？**

- **本质区别**：`objc_object` 是 Runtime 底层的 **C 语言结构体**，代表了 OC 对象的物理形态；而 `NSObject` 是 Foundation 框架提供的 **Objective-C 根类**，代表了逻辑封装。
- **内存重合**：`NSObject` 类的第一个成员变量就是 `Class isa`。这就保证了任何继承自 `NSObject` 的实例化对象，其内存布局在 C 语言视角下完美等价于一个 `objc_object` 结构体。
- **并非唯一**：万能指针 `id` 本质上是指向 `objc_object` 的指针。虽然我们打交道的类 99.9% 继承自 `NSObject`，但 OC 中还有其他根类（如 `NSProxy`）。无论是 `NSObject` 还是 `NSProxy`，在底层 Runtime 看来，它们统统都是 `objc_object`。

### 2. 类的本质（`objc_class`）

类在底层也是一个对象（类对象），它的结构大致如下：

```c
struct objc_class {
    Class isa;              // 指向元类（Meta Class）
    Class superclass;       // 指向父类
    cache_t cache;          // 方法缓存（散列表），为了极速查找
    class_data_bits_t bits; // 具体的类信息（rw_t，包含方法列表 method_list、属性列表、协议等）
};
```

- **实例方法** 存在**类对象**的方法列表里。
- **类方法** 存在**元类（Meta Class）**的方法列表里。

**面试高频考点：类、元类、根元类到底是什么？**

在 Objective-C 中，“万物皆对象”，连“类”本身也是一个对象（类对象）。为了让这套逻辑自洽，苹果设计了经典的元类体系：

1. **实例对象（Instance Object）**：我们平时 `[[Person alloc] init]` 创建出来的对象。它的内部存着具体的属性值，它的 `isa` 指针指向类对象。当你调用 `[person eat]`（实例方法）时，它会通过 `isa` 找到类对象，在类对象的方法列表里找 `eat` 的实现。
2. **类对象（Class Object）**：在内存中全局只有一份，存储着类的成员变量类型、实例方法列表、协议等信息。因为类本身也是对象，那调用 `[Person run]`（类方法）时去哪找这个方法呢？这就引出了元类。类对象的 `isa` 指针指向元类。
3. **元类（Meta Class）**：它是类对象的“类”。它专门用来存储**类方法**。当你调用类方法时，Runtime 会顺着类对象的 `isa` 找到元类，在元类的方法列表里查找。
4. **根元类（Root Meta Class）**：所有元类的基类（通常是 NSObject 的元类）。为了让所有的 `isa` 指针最终有个归宿形成闭环，根元类的 `isa` 指针指向它自己。

**💻 代码实战：如何获取与区分这四种对象？**

```objc
// 1. 实例对象：通过 alloc 创建，每次地址都不同
Person *p = [[Person alloc] init];

// 2. 类对象：通过 class 方法获取，全局单例
Class personClass = [Person class]; 
// 注意：[p class] 和 [Person class] 拿到的地址完全一样，都是那个唯一的类对象

// 3. 元类：必须通过 Runtime 的 object_getClass() 获取其 isa 指向
Class personMetaClass = object_getClass(personClass);
// ⚠️ 极其容易丢分的坑点：[personClass class] 返回的依然是 personClass 自己！不会返回元类！

// 4. 根元类：继续获取元类的 isa 指向
Class rootMetaClass = object_getClass(personMetaClass);

// 验证闭环：对根元类再调一次，发现返回的指针地址还是它自己
Class rootRoot = object_getClass(rootMetaClass);
// 此时 rootMetaClass == rootRoot
```

**经典 isa 与 superclass 走向图（必须刻在脑子里）：**

```text
                      isa 走 向 (找方法)
           +----------------------------------+
           |                                  |
           v                                  |
[ 实例对象 ] ---> [ 类对象 ] ---> [ 元类 ] ---> [ 根元类 ] --+
 (Instance)       (Class)      (Meta Class) (Root Meta)  | isa指向自己
                      |             |             |      |
                      v             v             v      |
superclass     [ 父类对象 ] ---> [ 父元类 ] ---> [ 根元类 ] <-+
走 向                 |             |             |
(找继承)              v             v             |
               [根类(NSObject)]-> [根元类]         |
                      |                           |
                      +---------------------------+
                      | 根元类的 superclass 指向根类对象！
                      v
                     nil
```

**看图口诀与必考防坑点：**

- **isa 路线（决定去哪找当前方法）**：实例对象 -> 类 -> 元类 -> 根元类 -> 自己。
- **superclass 路线（决定去哪找父类方法）**：子类 -> 父类 -> 根类(NSObject) -> `nil`。
- **终极坑点（图中右下角的虚线连接）**：根元类（NSObject 的元类）的 `superclass` 居然指向了根类（NSObject 类对象）！这就意味着：**如果你调用一个类方法，但在所有的元类里都没找到，Runtime 最后竟然会去 NSObject 的“实例方法”里找！** 如果 NSObject 有这个同名的实例方法，就不会崩溃！

---

### 3. 灵魂拷问：为什么需要元类？（代码实战验证）

总结：

- **实例方法存在“类对象”中。**
- **类方法存在“元类”中。**



要想彻底理解上面的图，我们必须回答一个问题：苹果为什么要费劲设计出一个“元类”？

假设我们定义了一个 `Person` 类：

```objc
@interface Person : NSObject
- (void)eat;    // 实例方法
+ (void)run;    // 类方法
@end
```

**场景 A：调用实例方法**

```objc
Person *p = [[Person alloc] init];
[p eat]; 
```

底层转化为发消息：`objc_msgSend(p, @selector(eat))`。
Runtime 会顺着 `p` 内部的 `isa` 指针，找到 `Person` 的**类对象**，然后在类对象的 `method_list` 里找到了 `eat`，执行成功。
**结论：实例方法存在“类对象”中。**

**场景 B：调用类方法**

```objc
[Person run]; 
```

底层转化为发消息：`objc_msgSend(Person_Class_Object, @selector(run))`。
注意！这里接收消息的 Target 变成了 `Person` 这个**类对象**。
既然类也是对象，那 Runtime 依然会顺着这个类对象的 `isa` 指针去找方法。那类对象的 `isa` 指向哪里呢？
**它指向的地方，就叫做“元类（Meta Class）”。**
Runtime 会去这个“元类”的 `method_list` 里找，找到了 `run`，执行成功。
**结论：类方法存在“元类”中。**

**我们直接用底层 C/OC 混合代码证明：**

```objc
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// 辅助函数：打印某个 Class 对象的方法列表
void printMethodList(Class cls) {
    unsigned int count;
    Method *methods = class_copyMethodList(cls, &count);
    NSMutableString *methodNames = [NSMutableString string];
    for (int i = 0; i < count; i++) {
        SEL name = method_getName(methods[i]);
        [methodNames appendFormat:@"%@, ", NSStringFromSelector(name)];
    }
    free(methods);
    NSLog(@"[%@] 的方法列表: %@", NSStringFromClass(cls), methodNames);
}

int main() {
    @autoreleasepool {
        Class personClass = objc_getClass("Person");      // 获取类对象
        Class personMetaClass = objc_getMetaClass("Person"); // 获取元类对象
        
        printMethodList(personClass);       // 预期输出：eat
        printMethodList(personMetaClass);   // 预期输出：run
    }
    return 0;
}
```

**终极总结：**
苹果之所以这样设计，是为了让“实例对象调用实例方法”和“类对象调用类方法”在底层 C 语言（`objc_msgSend`）的处理逻辑上**完全统一**。无论是谁发消息，底层都只做一件事：**无脑顺着它的 `isa` 去找方法就行了**。这就是面向对象底层的极致优雅。

---

## 二、 核心机制：消息发送（`objc_msgSend`）

当我们写下 `[person eat];` 时，编译器会把它转化为 C 函数调用：

```c
// 转化前
[person eat];
// 转化后
objc_msgSend(person, @selector(eat));
```

### 面试必考：消息查找的三大阶段

如果面试官问“请描述一下 objc_msgSend 的流程”，你必须答出这三步：

#### 阶段 1：快速查找（Cache Lookup）

为了性能，这部分是纯**汇编**实现的。Runtime 会顺着对象的 `isa` 找到类对象，直接在类的 `cache` 中查找对应 `SEL` 的 `IMP`（函数实现指针）。

**追问：`cache` 存在哪里？**
`cache` 就在类对象（`objc_class`）的内存结构里。回顾上面类的本质：
```c
struct objc_class {
    Class isa;              
    Class superclass;       
    cache_t cache;          // <- 就是这里！
    class_data_bits_t bits; 
};
```
底层通过对 `SEL` 进行哈希运算（类似取模），直接计算出在 `cache` 散列表中的索引，实现 O(1) 的极速查找。如果找到，直接调用并返回。

#### 阶段 2：慢速查找（Method List Lookup）

如果在 `cache` 里没找到（或者发生了哈希冲突且没找到），就会跳出汇编，进入 C/C++ 实现的慢速查找阶段。

**追问：`method_list` 存在哪里？**
它也存在类对象中，只不过隐藏得比较深，在 `class_data_bits_t bits` 里面。
Runtime 会把 `bits` 和一个掩码（Mask）进行按位与操作，提取出指向 `class_rw_t`（读写表）的指针，而在 `class_rw_t` 里，就包含了类的方法列表（`method_array_t methods`）。

**慢速查找的具体流程：**
1. 先在**当前类**的 `method_list` 中查找。
   * 如果方法列表是按地址排序的（通常是这样），使用**二分查找**；否则使用遍历查找。
2. 还没找到，顺着 `superclass` 去**父类**中找。
   * 注意：去父类找时，会先查父类的 `cache`，如果没命中，再查父类的 `method_list`。
   * 就这样一直顺着继承链找，直到找到根类 `NSObject`。
3. 如果找到了，会把这个方法塞进**当前类**（不是父类）的 `cache` 里（方便下次快速调用），并执行它。
4. 如果连 `NSObject` 都没有，进入第三阶段。

#### 阶段 3：动态解析与消息转发（Message Forwarding）

如果彻底找不到方法，程序正常情况下会报大名鼎鼎的 `unrecognized selector sent to instance` 错误然后直接崩溃。但在崩溃前，Runtime 还会给你 **最后三次“起死回生”的补救机会**！

**灵魂拷问：为什么要设计这套机制？只是为了炫技吗？**
绝对不是炫技！这套机制在底层基建、防 Crash 系统和知名框架中有着极其重要的实战价值。
**核心应用场景：**
1. **全局防 Crash 兜底（最常见）**：我们可以在基类（或通过 Category Hook `NSObject`）拦截最后一步的消息转发。遇到找不到的方法时，不让 App 崩溃，而是将消息转发给一个专门的“垃圾桶”空对象，同时向后台收集上报一条错误日志。
2. **模拟多重继承**：Objective-C 不支持多重继承。如果对象 A 想同时拥有对象 B 和对象 C 的能力，A 可以内部持有一个 C，然后把不属于自己的消息通过“备援接收者”**甩锅**给 C。对外部调用者来说，就好像 A 直接继承了 C 一样。
3. **`@dynamic` 属性（如 CoreData）**：CoreData 中的数据模型属性通常标记为 `@dynamic`，意味着编译期不生成 getter/setter。在运行时，CoreData 会在第一步“动态方法解析”阶段拦截，结合数据库字段动态生成存取方法。
4. **大名鼎鼎的 JSPatch (热修复)**：以前 iOS 圈最火的动态下发 JS 脚本修复线上 Bug 的技术，其底层核心原理就是把原本要执行的方法全部强行导向 `forwardInvocation:`，然后在里面执行下发的 JS 代码。

**三大补救机会详解：**

**机会 1：动态方法解析 (Dynamic Method Resolution) —— "自己修"**
Runtime 发现找不到方法，先问你：“你要不要现在临时写一个方法加上去？”
触发方法：`+resolveInstanceMethod:`（实例方法）或 `+resolveClassMethod:`（类方法）。
*用法*：你可以在这里用 `class_addMethod` 动态加一个方法上去，然后告诉 Runtime “我已经补救了，你再试一次”。

```objc
+ (BOOL)resolveInstanceMethod:(SEL)sel {
    if (sel == @selector(eat)) {
        // 动态添加一个 C 语言函数当做吃的方法
        class_addMethod(self, sel, (IMP)myEatFunction, "v@:");
        return YES; 
    }
    return [super resolveInstanceMethod:sel];
}
```

**机会 2：备援接收者 (Fast Forwarding) —— "找别人修"**
如果第一步你没处理，Runtime 接着问：“你自己不会，那你认识谁会处理这个方法吗？”
触发方法：`-forwardingTargetForSelector:`
*用法*：你可以把这个消息甩锅给另一个对象处理。这是**模拟多重继承**和**局部防 Crash** 的最佳阶段，因为它直接返回新对象，开销极小。

```objc
- (id)forwardingTargetForSelector:(SEL)aSelector {
    if (aSelector == @selector(eat)) {
        // 我不会吃，但我内部有个 myDog 属性，让 myDog 去处理吧
        return self.myDog; 
    }
    return [super forwardingTargetForSelector:aSelector];
}
```

**机会 3：完整消息转发 (Normal Forwarding) —— "最后通牒"**
如果第二步还是没处理，进入最后的完整转发。这是极其笨重、开销最大的一步。
Runtime 说：“既然你都不管，那我把这个调用的所有信息（调谁、传了什么参数、返回值是什么类型）全部打包成一个包裹给你，你自己看着办吧。”
触发流程：
1. Runtime 先调用 `-methodSignatureForSelector:` 要求你提供一个方法签名（参数和返回值类型）。如果这里返回 `nil`，直接崩溃。
2. 如果拿到了签名，Runtime 会生成一个沉甸甸的 `NSInvocation` 对象，并把它传给 `-forwardInvocation:`。

```objc
- (void)forwardInvocation:(NSInvocation *)anInvocation {
    if ([self.myDog respondsToSelector:anInvocation.selector]) {
        // 改变接收目标，再次甩锅执行
        [anInvocation invokeWithTarget:self.myDog]; 
    } else {
        // 连垃圾桶都没有，只能调用父类，最终必定抛出 unrecognized selector 崩溃
        [super forwardInvocation:anInvocation]; 
    }
}
```

---

## 三、 实战大杀器：Method Swizzling（方法交换）

**原理**：在运行期修改 `method_list` 中 `SEL` 与 `IMP` 的映射关系，让原本指向 A 实现的指针指向 B，实现“偷天换日”。

**常见场景**：无埋点统计（Hook `viewWillAppear`）、防 Crash 兜底（Hook 数组越界）。

**标准代码模板（必须背熟，面试可能会让手写）**：

```objc
#import <objc/runtime.h>

@implementation UIViewController (Tracking)

+ (void)load {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        Class class = [self class];

        SEL originalSelector = @selector(viewWillAppear:);
        SEL swizzledSelector = @selector(my_viewWillAppear:);

        Method originalMethod = class_getInstanceMethod(class, originalSelector);
        Method swizzledMethod = class_getInstanceMethod(class, swizzledSelector);

        // 先尝试添加方法，防止原方法是在父类中实现的（直接交换会污染父类）
        BOOL didAddMethod = class_addMethod(class, originalSelector,
                                            method_getImplementation(swizzledMethod),
                                            method_getTypeEncoding(swizzledMethod));

        if (didAddMethod) {
            // 如果添加成功，说明原方法是父类的，将我们自己的方法替换过去
            class_replaceMethod(class, swizzledSelector,
                                method_getImplementation(originalMethod),
                                method_getTypeEncoding(originalMethod));
        } else {
            // 如果添加失败，说明当前类确实有这个方法，直接交换即可
            method_exchangeImplementations(originalMethod, swizzledMethod);
        }
    });
}

// 看起来像死循环，其实不然！因为此时 IMP 已经交换了
// 调用 [self my_viewWillAppear:animated] 实际上执行的是原生的 viewWillAppear:
- (void)my_viewWillAppear:(BOOL)animated {
    NSLog(@"Tracking: 页面 %@ 即将显示", NSStringFromClass([self class]));
    // 执行原生逻辑
    [self my_viewWillAppear:animated]; 
}

@end
```

### 面试深挖：Method Swizzling 容易搞错的坑？

1. **为什么必须写在 `+load` 里，而不是 `+initialize`？**
  - `+load` 是在类被加载到内存时就调用的，确保交换发生在所有方法调用之前。而 `+initialize` 是在类第一次收到消息时才调用，可能会因为没调用而被遗漏。
2. **为什么必须用 `dispatch_once` 包裹？**
  - `+load` 可能会被手动调用（恶意调用），如果没有 `dispatch_once`，交换两次等于没交换，甚至引发混乱。
3. **直接 `method_exchangeImplementations` 有什么风险？（代码里 `class_addMethod` 的意义）**
  - 如果子类没有实现某个方法，直接去交换的话，拿到的会是**父类的 Method**。这时候交换会把父类的方法污染了，导致其他继承自该父类的对象调用时全军覆没。所以必须先尝试 `class_addMethod`。

---

## 四、 关联对象（Associated Objects）

**面试题：Category 能不能添加属性？**
不能直接添加。因为 Category 只能在运行期往类的 `rw_t` 里追加方法、协议和属性描述，**无法改变原本编译期就固定的实例大小（无法添加成员变量 ivar）**。

但可以通过 Runtime 的关联对象来模拟添加属性存取机制：

```objc
static const void *MyKey = &MyKey;

@implementation NSObject (MyCategory)

- (void)setMyProperty:(NSString *)myProperty {
    // 参数：对象，键，值，内存管理策略
    objc_setAssociatedObject(self, MyKey, myProperty, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSString *)myProperty {
    return objc_getAssociatedObject(self, MyKey);
}

@end
```

**底层原理**：关联对象并不存在被关联的对象内存里，而是由 Runtime 全局维护着一个 `AssociationsManager`（里面包含一个 `AssociationsHashMap` 哈希表），所有的关联对象都存在这里面。当对象被释放时，Runtime 的清理函数（在 `dealloc` 时底层的 `_object_remove_assocations`）会自动把这个对象在哈希表里的关联数据清空，所以不会造成内存泄漏。

---

## 五、 面试高频坑题（防杠指南）

1. `**[self class]` 和 `[super class]` 的区别？**
  - **绝杀坑题！** 两者输出**完全一样**。
  - **原理**：`super` 并不是一个指针，而是一个编译器指令。调用 `[super class]` 时，底层转化为了 `objc_msgSendSuper`。它的接收者（receiver）**依然是 `self` 本身**，只是告诉 Runtime：查找方法时，不要从当前类开始，**直接从父类开始找**。找到 `class` 方法后，执行时的 `this` 依然是当前对象，所以输出的类名一模一样。
2. **什么是 `isKindOfClass` 和 `isMemberOfClass` 的区别？**
  - `isMemberOfClass`：精准匹配。只判断当前对象的类是不是目标类，不会找父类。
  - `isKindOfClass`：模糊匹配。只要是目标类或者它的父类、祖父类，都返回 YES。
  - **深坑（类方法调用时）：** `BOOL res = [(id)[NSObject class] isKindOfClass:[NSObject class]]` 返回 YES（因为 NSObject 的元类的父类最后指向了 NSObject 类对象），而 `BOOL res = [(id)[AnyOtherClass class] isKindOfClass:[AnyOtherClass class]]` 返回 NO。
3. **能否在运行时动态创建一个类？**
  - 可以。利用 `objc_allocateClassPair` 创建，用 `class_addMethod` / `class_addIvar` 添加方法和变量，最后必须调用 `objc_registerClassPair` 注册到系统中才可用。
  - **注意点**：**一旦注册完成，就绝对不能再添加 Ivar（成员变量）了**。因为类的实例内存大小已经在注册时确定，再添加会破坏已创建实例的内存布局。

