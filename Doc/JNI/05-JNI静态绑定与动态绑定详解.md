# 05-JNI 静态绑定与动态绑定详解

> **重要度**：🔥🔥🔥 必考核心
> **前置**：[[01-JNI线程模型与env管理]]（需先理解 `JNI_OnLoad` 和 `JNIEnv`）
> **后续**：[[02-引用管理与内存泄漏防护]] | [[03-跨线程回调架构与TLS优化]]

---

## 面试速答模板（30 秒口语化回答）

### Q：JNI 静态绑定和动态绑定有什么区别？为什么推荐动态绑定？

> "静态绑定是 JVM 按固定命名规则——`Java_包名_类名_方法名`——去 SO 库的符号表里搜索 C 函数，首次调用时才做 `dlsym` 查找，绑完了缓存起来。动态绑定是在 `JNI_OnLoad` 里调用 `RegisterNatives`，把 Java 方法名、方法签名和 C 函数指针一次性注册给 JVM，后续调用直接走函数指针，不需要符号搜索。
>
> 推荐动态绑定的四个理由：① 函数名可以随便起，不暴露包名类名，反编译更难追溯；② 首次调用就直走函数指针，没有 `dlsym` 开销；③ 方法签名在注册时就校验了，写错当场报错而不是等到调用时才 crash；④ 支持运行时切换实现——多版 SO 或 A/B 实验场景下可以动态换绑。"

---

## 一、先建立最基本的认知：两种绑定的本质区别

JNI 层的"绑定"解决的是同一个问题：**Java 层声明了 `native` 方法，JVM 怎么找到对应的 C/C++ 函数？**

两种答案，走的路径完全不同：

| | 静态绑定（Static Registration） | 动态绑定（Dynamic Registration） |
|---|---|---|
| **绑定时机** | 首次调用时（JVM 延迟解析） | SO 加载时（`JNI_OnLoad` 里） |
| **绑定方式** | JVM 扫描 SO 符号表，`dlsym` 按名查找 | `RegisterNatives` 直接注册函数指针 |
| **C 函数命名** | 必须严格遵循 `Java_包名_类名_方法名` | 任意合法的 C 函数名 |
| **方法签名在哪** | 编译后嵌在导出的符号名里（`__` 转义） | 运行时在 `JNINativeMethod` 结构体里显式提供 |
| **调用开销** | 首次需 `dlsym`（微秒级），后续走缓存 | 始终直走函数指针 |
| **错误发现时间** | 首次调用时才报 `UnsatisfiedLinkError` | 注册时当场报错 |
| **运行时换绑** | ❌ 不支持 | ✅ 支持（`UnregisterNatives` + 重新 `RegisterNatives`） |

用两张图来理解最直观：

```text
【静态绑定】
Java: native void foo();         ──→  JVM 首次调 foo() 时:
                                        1. 拼符号名: Java_com_example_MyClass_foo
                                        2. dlsym(so_handle, "Java_com_example_MyClass_foo")
                                        3. 找到函数指针，缓存到内部 Method 结构
                                        4. 调用
                                        5. 下次调用走缓存（不再 dlsym）

【动态绑定】
Java: native void foo();         ──→  JNI_OnLoad 时:
JNI_OnLoad:                            1. env->RegisterNatives(clazz, methods, count)
  RegisterNatives(...)                 2. JVM 直接把 C 函数指针写入内部 Method 结构
                                       3. 后续调用 foo()，JVM 直接跳转函数指针
```

---

## 二、静态绑定（Static Registration）深度拆解

### 2.1 核心规则：JVM 怎么从 Java 方法名拼出 C 符号名

静态绑定本质上是一个**字符串拼接 + 转义**的协议。JVM 的符号命名规则：

```text
Java_ + {全限定类名，用 _ 替代 .} + _ + {方法名}

对于重载方法（overloaded native methods），再加 __ + {参数类型签名缩写}
```

基本例子：

```java
package com.example.engine;

public class VideoEngine {
    native void init();                    // → Java_com_example_engine_VideoEngine_init
    native void release();                 // → Java_com_example_engine_VideoEngine_release
    native void setParams(int, String);    // → 重载？看下一条
}
```

```cpp
// C 端严格按命名暴露函数
extern "C" JNIEXPORT void JNICALL
Java_com_example_engine_VideoEngine_init(JNIEnv* env, jobject thiz) { ... }

extern "C" JNIEXPORT void JNICALL
Java_com_example_engine_VideoEngine_release(JNIEnv* env, jobject thiz) { ... }
```

### 2.2 重载方法的命名：`__` 后缀

Java 的 native 方法可以重载，但 C 语言没有重载——函数名必须唯一。JVM 的做法是在方法名后追加 `__`（两个下划线）+ 缩短的类型签名：

```java
native void setParams(int width, int height);
// → Java_com_example_engine_VideoEngine_setParams__II

native void setParams(int width, int height, String codec);
// → Java_com_example_engine_VideoEngine_setParams__IILjava_lang_String_2
```

缩短规则是 JVM 内定的——去掉了 `;`、`/` 换成 `_`、`[` 保留等。但**实际工程里几乎没人手写重载方法的静态名**——太容易写错，都是重载时直接上动态注册。

### 2.3 `_` 的转义：最隐蔽的坑

如果 Java 类名或方法名本身包含 `_`，会和 JNI 的分隔符冲突。JVM 的规定是把 Java 的 `_` 转义成 `_1`：

```java
package com.example.my_engine;            // 包名里有 _

class My_Class {                          // 类名里有 _
    native void on_frame_ready();         // 方法名里有 _
}
// 这三个 _ 怎么区分？
// → Java_com_example_my_1engine_My_1Class_on_1frame_1ready
//
// 拆解:
//   Java_                              ← 固定前缀
//   com_example_my_1engine             ← my_engine → my_1engine
//   _My_1Class                         ← My_Class → My_1Class (_ 是包/类分隔符)
//   _on_1frame_1ready                  ← on_frame_ready → on_1frame_1ready
```

这导致一个非常反直觉的结论：**你无法仅凭 C 函数名可靠地反推出 Java 的类名和方法名**——因为 `_1` 到底是"真的 `_1`"还是"转义后的 `_`"，需要上下文判断。这也是为什么反编译工具对静态注册 JNI 的符号还原并不总是准确。

### 2.4 静态注册的内部流程（JVM 视角）

```
System.loadLibrary("myengine")
    → dlopen("libmyengine.so", RTLD_LAZY)    // 延迟符号解析
    → SO 加载进进程地址空间

↓ 之后某时，Java 代码首次调用 native 方法

MyClass.nativeInit() 首次调用:
    1. JVM 检查内部 Method 结构的 nativeFunction 字段 → NULL（还没绑）
    2. 进入 native 方法延迟解析（lazy resolution）
    3. 拼出符号名: "Java_com_example_MyClass_nativeInit"
    4. dlsym(so_handle, "Java_com_example_MyClass_nativeInit")
    5. 找到函数指针 → 写入 Method→nativeFunction
    6. 跳转执行
    7. 下次调用 → Method→nativeFunction 非空 → 直接跳转（已缓存，不走 dlsym）
```

**关键细节**：

- **`RTLD_LAZY` vs `RTLD_NOW`**：Android 上 `System.loadLibrary` 默认用 `RTLD_NOW`（立即解析所有符号），所以如果符号名写错，`loadLibrary` 并不报错——报错在**首次调用**时（`UnsatisfiedLinkError`）。这是因为 JNI 的静态绑定是 JVM 层面做的，不是 linker 做的。`dlopen` 成功 ≠ JVM 能调用。
- **缓存机制**：JVM 在首次解析后把函数指针缓存在 `Method` 结构体里，后续调用几乎没有额外开销。所以"静态注册每次调用都要符号搜索"是**谣言**——搜一次，之后直跳。
- **多线程首次调用**：JVM 内部用锁保护首次绑定的竞态，不会出现两个线程同时做 `dlsym` 注册两次的问题。

### 2.5 静态注册的优点

| 优点 | 说明 |
|------|------|
| 零额外代码 | 不需要写 `JNI_OnLoad`，不需要 `RegisterNatives`，C 函数写对名字就行 |
| 调试友好 | `nm -D libxxx.so \| grep Java_` 一目了然地看到所有 JNI 导出 |
| 兼容旧版 Android | 最早期的 Android JNI 只支持静态注册 |

### 2.6 静态注册的缺点

| 缺点 | 说明 |
|------|------|
| 函数名又长又丑 | `Java_com_example_engine_VideoDecoder_nativeDecodeFrame` |
| 重构成本高 | Java 改了包名/类名/方法名，C 端所有函数名必须同步改，容易遗漏 |
| `_` 转义地狱 | 包名/类名/方法名中含 `_` 时转义规则晦涩，手写出错概率极高 |
| 暴露包结构 | `strings libxxx.so` 直接泄露出所有 Java 类的完整包路径 |
| 重载方法不友好 | 需要手写 `__II` 这种后缀，容易写错 |
| 首次调用延迟 | 首次调用时有 `dlsym` 开销（虽然只有微秒级，但高频调用场景会累积） |

---

## 三、动态绑定（Dynamic Registration）深度拆解

### 3.1 核心 API：`RegisterNatives`

```cpp
jclass clazz = env->FindClass("com/example/engine/VideoEngine");

JNINativeMethod methods[] = {
    {"init",           "()V",                     (void*)nativeInit},
    {"release",        "()V",                     (void*)nativeRelease},
    {"setParams",      "(II)V",                   (void*)nativeSetParams_II},
    {"setParams",      "(IILjava/lang/String;)V", (void*)nativeSetParams_IIString},
    {"getFrame",       "()Ljava/nio/ByteBuffer;", (void*)nativeGetFrame},
};

env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0]));
```

### 3.2 `JNINativeMethod` 结构体

```cpp
typedef struct {
    const char* name;       // Java 层的方法名（不带包名/类名）
    const char* signature;  // JNI 方法签名（§四 展开）
    void*       fnPtr;      // C/C++ 函数指针
} JNINativeMethod;
```

三要素一一对应：`name` 告诉 JVM 是 Java 的哪个方法，`signature` 告诉 JVM 参数和返回值类型（用于重载区分 + 运行时校验），`fnPtr` 就是跳转目标。

### 3.3 动态注册应该在哪里做？

**答案：`JNI_OnLoad` 里。这是唯一正确的时机。**

```cpp
JavaVM* g_jvm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;                                       // ① 保存 JavaVM

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    // ② 注册每一个有 native 方法的类
    registerNativeMethods(env, "com/example/engine/VideoEngine");
    registerNativeMethods(env, "com/example/engine/AudioEngine");

    return JNI_VERSION_1_6;                           // ③ 返回版本号
}

// 工厂函数，每个类调一次
static int registerNativeMethods(JNIEnv* env, const char* className) {
    jclass clazz = env->FindClass(className);
    if (!clazz) return JNI_ERR;

    // 根据 className 选择对应的注册表
    // ...

    if (env->RegisterNatives(clazz, methods, count) != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_OK;
}
```

**为什么必须在 `JNI_OnLoad` 里？**

- `JNI_OnLoad` 在 `System.loadLibrary` 内部被 JVM 同步调用，此时类加载器已就绪，`FindClass` 能找到自己的类。
- 如果延迟到第一次被调用时才注册，需要自己处理并发安全和异常路径，多此一举。
- `RegisterNatives` 对同一个方法只能调用一次——第二次调用会失败。`JNI_OnLoad` 恰好执行一次。

**特殊场景——延迟注册**：ProGuard/R8 混淆后类名变化，或插件化/热修复框架中 SO 先加载、类后加载。这种场景需要在 Java 层显式调一个 `nativeInit()`，在里面拿到类的 `jclass` 再做 `RegisterNatives`。但这属于非标场景，正常工程不这么做。

### 3.4 动态注册的内部流程（JVM 视角）

```
System.loadLibrary("myengine")
    → dlopen("libmyengine.so", RTLD_NOW)
    → dlsym(..., "JNI_OnLoad")           // linker 找 JNI_OnLoad
    → JNI_OnLoad(vm, reserved)           // JVM 调用 JNI_OnLoad
        → env->FindClass("com/example/engine/VideoEngine")
        → env->RegisterNatives(clazz, methods, 5)
            → JVM 内部: 对 methods 数组中每一项:
                1. 在 clazz 里找名为 name、签名为 signature 的 native 方法
                2. 将 fnPtr 直接写入该方法的 Method→nativeFunction 字段
                3. 找不到对应方法 → 返回 JNI_ERR
    → return JNI_VERSION_1_6
```

和静态注册的关键区别：

| 步骤 | 静态注册 | 动态注册 |
|------|----------|----------|
| 符号查找 | JVM 内部 `dlsym`，依赖 SO 的符号表 | `RegisterNatives` 直接写入，不查符号表 |
| 错误反馈 | 调用时才报 `UnsatisfiedLinkError` | 立即得知注册失败（`JNI_ERR`） |
| C 函数可见性 | 必须是**全局可见的符号**（不能是 `static`） | **不必导出**——函数指针直接传，符号可以 `static` |

最后一个差异极为重要：动态注册的 C 函数可以是 `static` 的——它不需要出现在 `.dynsym` 表里。这意味着：

- `strip` 之后静态注册的函数符号被干掉就找不到了，动态注册无影响
- `nm -D` 看不到动态注册的函数，增加逆向难度
- 减小了 `.dynsym` 段，SO 体积略小

### 3.5 动态注册的优点

| 优点 | 说明 |
|------|------|
| 函数名自由 | C 函数可以写成有意义的 `decodeFrame` 而非 `Java_xxx_xxx_decodeFrame` |
| 包结构不暴露 | SO 符号表里没有包名类名，逆向更难 |
| 注册即校验 | 方法签名写错、方法不存在，注册时直接报 `JNI_ERR` |
| 支持运行时换绑 | `UnregisterNatives` + 重新 `RegisterNatives`，适合 A/B 实验 |
| C 函数可 `static` | 缩小 `.dynsym` 段，减少 SO 体积，增加 strip 后的安全性 |
| 重载友好 | `name` + `signature` 的组合明确区分重载，不需要靠 `__II` 这种隐式命名 |
| 重构友好 | Java 重构后方法签名变化，只需改注册表字符串，C 函数名不变 |

### 3.6 动态注册的缺点

| 缺点 | 说明 |
|------|------|
| 需要维护注册表 | 每个 native 方法都要写一行 `JNINativeMethod`，方法多了注册表很冗长 |
| 多了 `JNI_OnLoad` | 必须写 `JNI_OnLoad`（但这本身是工程基础要求，不算实质缺点） |
| `FindClass` 的类名是字符串 | 混淆后类名变化需要适配 |

### 3.7 `RegisterNatives` 的返回值处理

```cpp
jint ret = env->RegisterNatives(clazz, methods, count);
if (ret != JNI_OK) {
    // 三种可能:
    // 1. clazz 为 null（FindClass 失败）
    // 2. methods 中某个 name+signature 在 clazz 里找不到对应方法
    // 3. 该方法已经被注册过了（不能重复 RegisterNatives）
    LOGE("RegisterNatives failed for class: %s, error: %d", className, ret);
    return JNI_ERR;
}
```

**常见失败原因**：

1. **方法签名写错**：`"()V"` 写成了 `"(V)"` → 找不到匹配方法
2. **ProGuard/R8 混淆**：Java 方法的签名在混淆后变了（参数/返回值类型没变所以不影响，但如果 `FindClass` 的类名写的是原始名，而实际类已被混淆，`FindClass` 返回 null）
3. **Java 层忘记声明 `native`**：方法存在但不是 `native` → `RegisterNatives` 拒绝非 native 方法
4. **重复注册**：同一个 `jclass` 对同方法调了两次 `RegisterNatives`

### 3.8 `UnregisterNatives`：动态换绑的基础

```cpp
// 注销某个类的全部 native 方法绑定
env->UnregisterNatives(clazz);
// 然后可以重新 RegisterNatives 一套新的实现
```

典型场景：**离线/在线引擎切换**。App 内置了一个轻量解码器（软件解码），但检测到设备支持硬件解码时，在运行时换绑 native 方法到硬件加速版实现。Java 层完全无感。

---

## 四、JNI 方法签名（Type Signature）完全指南

无论是静态注册的重载后缀（`__II`）还是动态注册的 `JNINativeMethod.signature` 字段，都需要写 JNI 方法签名。这是 JNI 编程最高频的"拼写错误"来源。

### 4.1 基本类型签名表

| Java 类型 | JNI 签名 | 记忆法 |
|-----------|----------|--------|
| `void` | `V` | Void |
| `boolean` | `Z` | Zero/One（布尔就是 0/1） |
| `byte` | `B` | Byte |
| `char` | `C` | Char |
| `short` | `S` | Short |
| `int` | `I` | Integer |
| `long` | `J` | Java Long（L 已被引用类型占用） |
| `float` | `F` | Float |
| `double` | `D` | Double |

### 4.2 引用类型签名

| Java 类型 | JNI 签名 |
|-----------|----------|
| `String` | `Ljava/lang/String;` |
| `Object` | `Ljava/lang/Object;` |
| `MyClass` | `Lcom/example/MyClass;` |
| `byte[]` | `[B` |
| `int[]` | `[I` |
| `String[]` | `[Ljava/lang/String;` |
| `int[][]` | `[[I` |

核心规则：引用类型以 `L` 开头、`;` 结尾；数组用 `[` 前缀。

### 4.3 完整方法签名

```text
(参数类型1参数类型2参数类型3...)返回值类型

一句话: 括号里是参数（从左到右依次排），括号外是返回值
```

| Java 方法 | JNI 签名 |
|-----------|----------|
| `void foo()` | `()V` |
| `int bar(long, String)` | `(JLjava/lang/String;)I` |
| `byte[] getFrame()` | `()[B` |
| `void setSize(int, int)` | `(II)V` |
| `int process(byte[], int, int)` | `([BII)I` |

### 4.4 快速校验签名：`javap -s`

手写签名最容易出错——少一个分号、`L` 和 `;` 不配对。验证方法：

```bash
javap -s -p build/intermediates/javac/debug/classes/com/example/engine/VideoEngine.class
```

输出示例：

```text
public class com.example.engine.VideoEngine {
  native void init();
    descriptor: ()V

  native int setParams(int, java.lang.String);
    descriptor: (ILjava/lang/String;)I
}
```

**直接抄 `descriptor` 那一行**，一个字不改，100% 正确。

---

## 五、两种绑定的性能对比

### 5.1 首次调用开销

```text
静态注册: 首次调用 = dlsym(符号查找) + 函数跳转 ≈ 1-5 μs
动态注册: 首次调用 = 函数跳转                     ≈ <0.1 μs

首次之后两者等价——都走 Method→nativeFunction 缓存的函数指针。
```

**但这个差异在实际工程中几乎无关紧要**。理由：

- `dlsym` 只在**首次调用**时执行一次，后续走缓存。
- 一个 SO 通常有几十个 native 方法，假设每个首次调用的 `dlsym` 是 3μs，总计不到 0.2ms——在整个 App 生命周期里完全可以忽略。
- 真正决定性能的是**你 C 函数里面做了什么**，不是怎么被找到的。

所以"静态注册性能差"不应该成为你选择动态注册的**主要原因**。选动态注册的核心原因是**安全、可维护性、重构友好、错误早发现**。

### 5.2 SO 体积对比

```
静态注册: C 函数必须在 .dynsym 表里（每个约 24-32 字节的 ELF 符号项）
动态注册: C 函数可以是 static，不出现在 .dynsym

100 个 native 方法 × ~30 字节 ≈ 3KB。差异可以忽略。
但 .dynsym 里少了这些符号，strip 后的 SO 反向难度会略有提升。
```

---

## 六、工程实战：注册表组织模式

当 native 方法超过 10 个时，直接写一长串 `JNINativeMethod` 数组会很难维护。以下是工程上的推荐组织方式：

### 6.1 一个类一个注册函数

```cpp
// VideoEngine_jni.cpp
static void nativeInit(JNIEnv* env, jobject thiz) { ... }
static void nativeRelease(JNIEnv* env, jobject thiz) { ... }
static jobject nativeGetFrame(JNIEnv* env, jobject thiz) { ... }

static JNINativeMethod gVideoEngineMethods[] = {
    {"init",      "()V",                    (void*)nativeInit},
    {"release",   "()V",                    (void*)nativeRelease},
    {"getFrame",  "()Ljava/nio/ByteBuffer;",(void*)nativeGetFrame},
};

int registerVideoEngine(JNIEnv* env) {
    jclass clazz = env->FindClass("com/example/engine/VideoEngine");
    if (!clazz) return JNI_ERR;
    return env->RegisterNatives(clazz, gVideoEngineMethods,
        sizeof(gVideoEngineMethods) / sizeof(gVideoEngineMethods[0]));
}
```

### 6.2 `JNI_OnLoad` 统一调度

```cpp
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    // 每个 Java 类对应一个注册函数，失败即返回
    if (registerVideoEngine(env) != JNI_OK) return JNI_ERR;
    if (registerAudioEngine(env) != JNI_OK) return JNI_ERR;
    if (registerMediaCodecHelper(env) != JNI_OK) return JNI_ERR;

    return JNI_VERSION_1_6;
}
```

### 6.3 宏化简：减少书写错误

```cpp
// 强烈建议用宏缩小注册表的描述长度，也是面试"工程细节"的加分点
#define JNI_METHOD(name, sig) { name, sig, (void*)JNI_##name }

// 对应的 C 函数命名加统一前缀
static void JNI_init(JNIEnv* env, jobject thiz) { ... }
static void JNI_release(JNIEnv* env, jobject thiz) { ... }
static jobject JNI_getFrame(JNIEnv* env, jobject thiz) { ... }

static JNINativeMethod gMethods[] = {
    JNI_METHOD("init",      "()V"),
    JNI_METHOD("release",   "()V"),
    JNI_METHOD("getFrame",  "()Ljava/nio/ByteBuffer;"),
};
```

`JNI_METHOD` 宏把 name/signature/fnPtr 三要素压成一行——写法更紧凑，且**方法名只出现一次**（不会出现方法名字符串和函数指针不一致的笔误）。

---

## 七、常见坑与防坑指南

### 坑 1：C 函数忘记 `extern "C"`

```cpp
// ❌ C++ 文件里直接写，编译器做了 name mangling
void Java_com_example_Foo_bar(JNIEnv* env, jobject thiz) { }
// dlsym 搜 "Java_com_example_Foo_bar" ——找不到！
// 实际符号是 _Z27Java_com_example_Foo_barP7JNIEnvP8_jobject

// ✅ 正确：加 extern "C"
extern "C" {
JNIEXPORT void JNICALL
Java_com_example_Foo_bar(JNIEnv* env, jobject thiz) { }
}
```

`JNIEXPORT` 和 `JNICALL` 宏本身已经包含了平台相关的可见性修饰，但**不包含 C 链接**。所以 C++ 文件里静态注册必须再套一层 `extern "C"`（或把整个 `.cpp` 的 JNI 函数都包进去）。

动态注册不受此影响——函数指针直接传，不需要符号名查找——但加了 `extern "C"` 可以防止不小心在别处 `dlsym` 用错。

### 坑 2：静态注册时方法名中间多/少 `_`

```java
// Java
package com.example.myengine;
class VideoDecoder {
    native void decodeFrame();   // 注意: decodeFrame，中间没有 _
}
```

```cpp
// ❌ 习惯性在单词中间加 _
Java_com_example_myengine_VideoDecoder_decode_Frame(...)
// → JVM 找的是 "decode_Frame"，Java 层声明的却是 "decodeFrame"
// → UnsatisfiedLinkError

// ✅ 正确
Java_com_example_myengine_VideoDecoder_decodeFrame(...)
```

### 坑 3：包名中有 `_` 没做 `_1` 转义

```java
package com.my_company.sdk;   // "my_company" 含 _
class NativeCore {
    native void init();
}
```

```cpp
// ❌ 直接把包名的 _ 当分隔符写
Java_com_my_company_sdk_NativeCore_init(...)
// → JVM 会把 "my_company" 解析为 类 "my" 下有个方法 "company_sdk_NativeCore_init"

// ✅ 正确: my_company → my_1company
Java_com_my_1company_sdk_NativeCore_init(...)
```

解决办法：用 `javap -s` 看 class 文件里有没有含 `_` 的命名。如果有，**直接改用动态注册**——手写 `_1` 转义不可靠。

### 坑 4：`FindClass` 在非主线程的 classloader 问题

`JNI_OnLoad` 里 `FindClass` 用的是 System ClassLoader，能找到 App 自己的类。但如果延迟注册（在某个 C++ 子线程的回调里做 `FindClass`），该类可能对当前线程的 ClassLoader 不可见 → `FindClass` 返回 null → `RegisterNatives` 时 clazz 为 null → crash。

**正解**：如果需要延迟注册，在 `JNI_OnLoad` 里先用 `FindClass` 拿到 `jclass` 并转为 `GlobalRef` 保存，后续注册时直接用这个 `GlobalRef`。

### 坑 5：混淆后类名不匹配

```cpp
// 这段代码在 minifyEnabled true 后会挂
env->FindClass("com/example/engine/VideoEngine");
// ProGuard 把 VideoEngine 混淆成了 a.b.c → FindClass 返回 null
```

**正解**：在 ProGuard 规则里 keep 所有含 native 方法的类：

```proguard
-keepclasseswithmembernames class * {
    native <methods>;
}
```

或让 Java 层在 `JNI_OnLoad` 之前主动传一个 `Class` 对象过来——不过 `JNI_OnLoad` 是 `System.loadLibrary` 内部同步执行的，无法在此之前传参。通常 ProGuard keep 规则是最干净的方案。

### 坑 6：`RegisterNatives` 返回 `JNI_ERR` 但没检查

```cpp
// ❌ 忽略了返回值，签名写错也默默"成功"了
env->RegisterNatives(clazz, methods, 5);

// ✅
if (env->RegisterNatives(clazz, methods, 5) != JNI_OK) {
    LOGE("RegisterNatives FAILED for %s", className);
    // 走到这里说明某个 method 在 Java 层找不到，或者签名不匹配
    return JNI_ERR;
}
```

### 坑 7：`JNI_OnLoad` 返回 `JNI_VERSION_1_6` 但实际用了 1.4 的 API

这个不会直接报错，但如果大量用了 1.6 的 API（如 `GetObjectRefType`），而返回了 1.4，部分 JVM 实现可能行为不一致。**把返回值和实际调用的 API 版本对齐**。

---

## 八、对比总结与选型建议

```text
┌──────────────────────────────────────────────────────────────┐
│                      选型决策树                              │
│                                                              │
│  你的场景？                                                  │
│  ├─ 新项目、正式 SDK ──→ 必须动态注册                        │
│  ├─ 只有 1-2 个 native 方法 ──→ 静态注册也行，动态更好       │
│  ├─ 老旧项目、已有大量静态注册 ──→ 不改，保持一致性 > 重构   │
│  ├─ 需要运行时换绑实现 ──→ 只能动态注册                      │
│  ├─ 插件化框架、SO 与类分离加载 ──→ 延迟动态注册             │
│  └─ 学习/教程/HelloWorld ──→ 静态注册概念上更容易理解        │
└──────────────────────────────────────────────────────────────┘
```

**唯一需要回避**的场景：任何对外交付的 SDK。静态注册暴露完整包路径和类名，`strings libsdk.so` 就能拿到全部 native 方法清单，这是安全红线。动态注册是最低要求。

---

## 九、自检（高频面试题改编）

1. 静态注册的 C 函数命名规则是什么？包名中的 `.` 和 `_` 分别怎么处理？给一个 Java 方法写出对应的 C 函数名。
2. 动态注册的核心 API 是什么？`JNINativeMethod` 结构体的三个字段分别是什么含义？
3. JVM 在静态注册首次调用时做了什么？`dlsym` 是否每次都执行？
4. 为什么动态注册的 C 函数可以是 `static` 的？这带来了什么安全优势？
5. 解释 `"()V"` 和 `"([BII)I"` 这两个方法签名对应的 Java 方法声明。
6. 如何验证手写的 JNI 方法签名是正确的？
7. `RegisterNatives` 失败可能的原因有哪些？（至少说出三种）
8. `JNI_OnLoad` 返回 `JNI_ERR` 和返回 `JNI_VERSION_1_6` 有什么区别？调用方（`System.loadLibrary`）会怎样？
9. 如果你接手了一个全部用静态注册的老项目，你会建议全部迁移到动态注册吗？为什么/为什么不？
10. 混淆（ProGuard/R8）对静态注册和动态注册分别有什么影响？
