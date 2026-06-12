# 01-JNI 线程模型与 env 管理

> **重要度**：🔥🔥🔥 必考核心
> **前置**：无（JNI 知识体系的起点）
> **后续**：[[02-引用管理与内存泄漏防护]] | [[03-跨线程回调架构与TLS优化]]

---

## 面试速答模板（30 秒口语化回答）

### Q：JNIEnv 是什么？能不能跨线程传递？

> "`JNIEnv` 是 JNI 函数表的指针，**线程私有**，绝对不能跨线程传递。它和当前线程强绑定，把 A 线程的 env 给 B 线程用会直接崩溃。`JavaVM` 才是进程全局唯一的，所有线程共享。C++ 子线程要回调 Java，必须拿全局的 `JavaVM`，通过 `AttachCurrentThread` 为当前线程申请一个专属的 `JNIEnv`，用完再 `DetachCurrentThread` 释放。"

### Q：JNI_OnLoad 干什么的？动态注册和静态注册有什么区别？

> "`JNI_OnLoad` 是 SO 加载时的入口函数，我们在里面保存全局 `JavaVM`，并做**动态注册**。静态注册按固定命名规则（`Java_包名_类名_方法名`），JVM 首次调用时按名字搜索绑定，性能差、易被反编译。动态注册在 `JNI_OnLoad` 里用 `RegisterNatives` 直接把 Java 方法和 C++ 函数指针绑定，函数名随便起，更安全高效。"

---

## 一、JNIEnv vs JavaVM：两个核心概念

JNI 编程的入口只有两个东西，搞清它们的区别是写对 JNI 代码的前提：

| | JNIEnv | JavaVM |
| :--- | :--- | :--- |
| **作用域** | 线程私有 | 进程全局唯一 |
| **生命周期** | 随线程 Attach/Detach | 从 `JNI_OnLoad` 到进程结束 |
| **能否跨线程** | ❌ 绝对不能 | ✅ 可以保存到全局变量 |
| **如何获取** | Attach 后从 JVM 拿，或 JNI 函数参数 | `JNI_OnLoad` 参数传入 |
| **包含什么** | 所有调用 Java 方法的函数表 | `AttachCurrentThread` / `DetachCurrentThread` / `GetEnv` |

### 为什么 JNIEnv 不能跨线程？

JVM 在内部维护了每个线程的 JNI 上下文状态（局部引用表、异常状态等）。如果把 A 线程的 `JNIEnv*` 传给 B 线程用：

```text
B 线程拿着 A 线程的 env 调用 FindClass
    → JVM 在 A 线程的局部引用表里创建引用
    → 但 B 线程的局部引用表根本没有这个引用
    → 内部状态彻底错乱 → Segmentation Fault
```

**一句话**：`JavaVM` 是全局入口（可以保存），`JNIEnv` 是线程私有的工作台（每次要用时必须从当前线程获取）。

---

## 二、AttachCurrentThread：让 C++ 线程获得 JNI 能力

### 痛点场景

音视频 SDK 里有很多 C++ 内部线程——解码线程、渲染线程、网络回调线程。这些线程不是 Java 创建的，对 JVM 来说是"黑户"，没有自己的 `JNIEnv`。如果直接调用 JNI 函数 → **崩溃**。

### 解决方案

```cpp
// 全局保存 JavaVM（JNI_OnLoad 时存）
JavaVM* g_jvm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;                          // JavaVM 可以全局保存
    return JNI_VERSION_1_6;
}

// C++ 子线程中回调 Java
void callbackFromNativeThread() {
    JNIEnv* env = nullptr;

    // 1. 先检查当前线程是否已经 Attach
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);

    bool needDetach = false;
    if (status == JNI_EDETACHED) {
        // 2. 未绑定 → Attach
        g_jvm->AttachCurrentThread(&env, nullptr);
        needDetach = true;
    }

    // 3. 现在 env 可用了，正常调 JNI
    // env->CallVoidMethod(...)

    // 4. 用完必须 Detach
    if (needDetach) {
        g_jvm->DetachCurrentThread();
    }
}
```

### 核心规则

| 规则 | 原因 |
| :--- | :--- |
| Attach 后必须 Detach | 否则 JVM 内部线程资源泄漏，累积后崩溃 |
| 不能重复 Attach | 已 Attach 的线程再 Attach 是 no-op，但也不报错 |
| 不能在已 Detach 后继续用 env | env 失效，野指针崩溃 |
| 主线程（Java 创建的）已经 Attach | 不需要手动 Attach，也不需要 Detach |

---

## 三、JNI_OnLoad：SO 的 main 函数

### 它是什么

`JNI_OnLoad` 是 Java 调用 `System.loadLibrary("xxx")` 加载 SO 时，JVM 自动调用的入口函数。相当于动态库的"main 函数"。返回值为 `JNI_VERSION_1_6` 表示支持的 JNI 版本。

### 三件必做之事

```cpp
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    // 1. 保存全局 JavaVM
    g_jvm = vm;

    // 2. 动态注册 native 方法（推荐）
    JNIEnv* env = nullptr;
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);

    jclass clazz = env->FindClass("com/example/NativeEngine");
    JNINativeMethod methods[] = {
        {"nativeInit",    "()V", (void*)nativeInit},
        {"nativeRelease", "()V", (void*)nativeRelease},
    };
    env->RegisterNatives(clazz, methods,
        sizeof(methods) / sizeof(methods[0]));

    // 3. 返回 JNI 版本
    return JNI_VERSION_1_6;
}
```

### 配套的 JNI_OnUnload

```cpp
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    // SO 被卸载时调用（一般不会发生，但写了更规范）
    // 清理全局引用、释放资源
}
```

---

## 四、静态注册 vs 动态注册

### 静态注册（传统方式）

按固定命名规则暴露 C 函数——JVM 第一次调用时去 SO 库里按名字搜索：

```cpp
// 包名: com.example.NativeEngine
// 类名: NativeEngine
// 方法: nativeInit
extern "C" JNIEXPORT void JNICALL
Java_com_example_NativeEngine_nativeInit(JNIEnv* env, jobject thiz) {
    // 实现
}
```

**缺点**：
- 函数名又长又丑，一改名 Java 层也要改
- JVM 首次调用时需要按名字搜索符号表，性能略差
- 容易被反编译工具根据函数名猜出包名和类名
- 不支持运行时动态切换实现

### 动态注册（推荐）

在 `JNI_OnLoad` 里主动调用 `RegisterNatives` 绑定：

```cpp
JNINativeMethod methods[] = {
    {"nativeInit",    "()V",               (void*)nativeInit},
    {"nativeRelease", "()V",               (void*)nativeRelease},
    {"setCallback",   "(Ljava/lang/Object;)V", (void*)setCallback},
};
// JNINativeMethod 结构: {Java方法名, 方法签名, C函数指针}
```

**优点**：
- 函数名可以随便起，不暴露包名/类名
- 首次调用时直接函数指针跳转，无需符号搜索
- 可以通过改变注册表来运行时切换实现

### 方法签名速查

| Java 类型 | 签名 |
| :--- | :--- |
| `void` | `V` |
| `boolean` | `Z` |
| `int` | `I` |
| `long` | `J` |
| `float` | `F` |
| `double` | `D` |
| `String` | `Ljava/lang/String;` |
| `Object` | `Ljava/lang/Object;` |
| `int[]` | `[I` |
| `void method(int, String)` | `(ILjava/lang/String;)V` |

---

## 踩过的坑

| 坑 | 现象 | 正确做法 |
| :--- | :--- | :--- |
| 跨线程传递 JNIEnv | 随机崩溃 `SIGSEGV` | 每个线程通过 `JavaVM->GetEnv/AttachCurrentThread` 获取自己的 env |
| 忘记 Detach | JVM 内部线程引用泄漏，长时间运行后崩溃 | 确保每个 Attach 的线程最终都 Detach（用 TLS 自动管理，见 [[03-跨线程回调架构与TLS优化]]） |
| Attach 已绑定的线程 | 不报错，但 `needDetach` 标记为 false 导致没 Detach | 用 `GetEnv` 先检查状态，只对确实 Attach 的线程做 Detach |
| 在 `JNI_OnLoad` 中做重操作 | 阻塞 Java 层 `System.loadLibrary` | `JNI_OnLoad` 只做轻量初始化，重操作放到单独的 `init()` 调用 |
| 方法签名写错 | `RegisterNatives` 失败，或 NoSuchMethodError | 用 `javap -s` 查看编译器生成的真实签名 |

---

## 自检一问

> C++ 子线程里有一个 `JNIEnv*` 是从主线程保存下来的，能直接用吗？为什么？不能的话怎么办？

**答案**：不能。`JNIEnv` 线程私有，跨线程使用会崩溃。正确做法是用全局的 `JavaVM->AttachCurrentThread` 为子线程获取专属的 `JNIEnv`。
