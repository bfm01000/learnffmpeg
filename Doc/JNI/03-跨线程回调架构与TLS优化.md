# 03-跨线程回调架构与 TLS 优化

> **重要度**：🔥🔥🔥 必考核心（面试区分点）
> **前置**：[[01-JNI线程模型与env管理]] | [[02-引用管理与内存泄漏防护]]
> **后续**：[[04-零拷贝与大数据跨语言传递]]

---

## 面试速答模板（30 秒口语化回答）

### Q：你们 SDK 是怎么处理 C++ 线程回调 Java 的？

> "三步。第一，回调对象用 `NewGlobalRef` 转成全局引用保存，防止被 GC。第二，C++ 子线程回调前，通过全局 `JavaVM` 的 `AttachCurrentThread` 获取当前线程的 `JNIEnv`。第三，为了避免频繁 Attach/Detach 的性能开销和忘记 Detach 的泄漏风险，我们用 **TLS（线程局部存储）** 做缓存——第一次回调时 Attach 并把 `JNIEnv` 存入 TLS，后续同线程直接复用。在线程销毁时，操作系统自动触发 TLS 析构函数，在里面调用 `DetachCurrentThread`，实现完全自动化的资源清理。"

---

## 一、为什么需要这个架构

回顾 01 和 02 的结论：
- `JNIEnv` 是线程私有的，C++ 子线程必须 `AttachCurrentThread` 才能获取
- 回调的 Java 对象必须用 `GlobalRef` 保存，且最后要 `DeleteGlobalRef`
- Attach 后必须 Detach，否则 JVM 资源泄漏

现在要把这些都串起来，做出一个**不泄漏、不崩溃、性能好**的完整回调方案。

### 直接方案的致命缺陷

```cpp
// ❌ 看起来没问题，实际上每处都可能挂
void onNativeEvent() {
    JNIEnv* env = nullptr;
    g_jvm->AttachCurrentThread(&env, nullptr);   // 问题1：每次都 Attach，性能差
    env->CallVoidMethod(g_callback, methodId);    // 问题2：回调中途发生异常 → 下面代码不执行
    g_jvm->DetachCurrentThread();                 // 问题3：忘了 Detach 就泄漏
}
```

三个问题：
1. **性能**：每次回调都 Attach/Detach 是一笔不小的开销
2. **异常安全**：如果回调中途抛异常或 return，Detach 就被跳过了
3. **黑盒线程**：C++ 线程可能是第三方库管理的，你不知道它什么时候死

---

## 二、TLS（Thread Local Storage）深度解析

### TLS 是什么

TLS 是一种变量存储机制——每个线程通过**同一个 Key** 访问到的值，都是**当前线程私有的**。

**比喻**：公共浴室里的储物柜。柜子编号 `001`（TLS Key）是全局统一的，但张三（线程 A）打开 `001` 拿出的是自己的毛巾，李四（线程 B）打开同一个 `001` 拿出的是自己的洗发水。柜子编号一样，内容线程隔离。

### POSIX TLS API

```cpp
// 1. 创建 Key，并注册析构函数
pthread_key_t key;
pthread_key_create(&key, destructor_function);

// 2. 当前线程存入私有数据
pthread_setspecific(key, data);

// 3. 当前线程取出私有数据
void* data = pthread_getspecific(key);
```

#### 各 API 参数详解

**`pthread_key_create(pthread_key_t* key, void (*destructor)(void*))`**

| 参数 | 含义 |
|:---|:---|
| `key` | 输出参数，OS 分配一个全局唯一的 Key 编号，写入 `*key` |
| `destructor` | 析构函数指针。**任何线程**退出时，如果它在这个 Key 上存了非 NULL 值，OS 就会调用 `destructor(存的那个值)`。传 `NULL` 表示不需要析构 |

**`pthread_setspecific(pthread_key_t key, const void* value)`**

这是理解 TLS 最核心的 API：

| 参数 | 含义 |
|:---|:---|
| `key` | 用哪个 Key 来存。**同一个 key 值在所有线程间共享**（比如全局变量 `g_env_key = 1`） |
| `value` | 要存的数据指针。**这个指针会被写入当前线程自己的 TCB 里**，而不是全局某处 |

关键：`pthread_setspecific(g_env_key, env)` 的执行过程是——

```text
pthread_setspecific(key=1, value=0x7f...)
    │
    └─ 拿到当前线程的 TCB（内核知道"谁在调我"）
    └─ TCB 里有个 void* tsd[] 数组
    └─ tsd[1] = 0x7f...（写进当前线程的私有槽位）
    └─ 返回

另一个线程调 pthread_setspecific(key=1, value=0x8a...)
    │
    └─ 拿到那个线程的 TCB
    └─ 那个线程的 tsd[1] = 0x8a...（写进它自己的私有槽位）
    └─ 两个线程互不干扰
```

**一句话**：`key` 决定"存进哪个槽位"，`value` 是"存什么"；但**存到谁的槽位里**，由"当前是哪个线程在调用"决定——这是 OS 在底层自动做的，调用者感知不到。

**`pthread_getspecific(pthread_key_t key)`**

| 参数 | 含义 |
|:---|:---|
| `key` | 要读取哪个 Key 对应的值 |
| 返回值 | `void*`——当前线程在 `key` 上存的值；如果还没存过，返回 `NULL` |

同样，读到的是**当前线程**的私有值，不需要传线程 ID——OS 隐式知道调用者是谁。

### TLS 析构函数的触发时机——OS 视角

这是面试中最能体现系统功底的知识点。当一个 C++ 线程死亡时（正常 return、`pthread_exit`、或被 `pthread_cancel`），操作系统执行以下流程：

```text
线程函数结束 / pthread_exit / pthread_cancel
    ↓
OS 进入线程清理流程（glibc __free_tcb）
    ↓
遍历当前线程的所有 TLS Key
    ↓
发现 Key 注册了析构函数，且当前线程对该 Key 存了非 NULL 值
    ↓
操作系统主动调用: destructor_function(存储的那个值)
    ↓
线程的栈内存、内核数据结构释放 → 线程彻底死亡
```

**关键**：无论线程怎么死的，这个析构函数都会被 OS 调用。这就是解耦的核心。

---

## 三、TLS + JNI 完整方案

### 架构图

```text
JNI_OnLoad (SO 加载)
    │
    ├─ 保存全局 JavaVM
    └─ pthread_key_create(&key, detachThreadDestructor)
    
C++ 子线程触发回调
    │
    ├─ getJniEnv():
    │   ├─ TLS 已有 env? → 直接返回（复用，0 开销）
    │   └─ TLS 没有?    → AttachCurrentThread → 存入 TLS → 返回
    │
    ├─ 通过 GlobalRef 调用 Java 方法
    │
    └─ (线程退出时) OS 自动调用 detachThreadDestructor → DetachCurrentThread
```

### 完整代码

```cpp
// ===== 全局状态 =====
JavaVM* g_jvm = nullptr;
pthread_key_t g_env_key;

// ===== TLS 析构函数：线程退出时自动 Detach =====
void detachThreadDestructor(void* arg) {
    JNIEnv* env = static_cast<JNIEnv*>(arg);
    if (env && g_jvm) {
        g_jvm->DetachCurrentThread();
    }
}

// ===== 获取当前线程的 JNIEnv（带 TLS 缓存）=====
JNIEnv* getJniEnv() {
    // 1. 先查 TLS 缓存
    JNIEnv* env = static_cast<JNIEnv*>(pthread_getspecific(g_env_key));
    if (env) return env;  // 已 Attach → 直接复用

    // 2. 再查是否已 Attach 但 TLS 里没有（兼容非本模块 Attach 的场景）
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_OK) {
        pthread_setspecific(g_env_key, env);  // 补录到 TLS
        return env;
    }

    // 3. 未 Attach → 执行 Attach
    if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        pthread_setspecific(g_env_key, env);  // 存入 TLS
    }
    return env;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    // g_env_key 为句柄
    pthread_key_create(&g_env_key, detachThreadDestructor);
    return JNI_VERSION_1_6;
}

// ===== C++ 子线程中的回调 =====
void notifyFromAnyThread() {
    JNIEnv* env = getJniEnv();
    if (!env) return;

    // 正常使用 env——不需要手动 Detach
    jclass clazz = env->GetObjectClass(g_callback);
    jmethodID mid = env->GetMethodID(clazz, "onEvent", "()V");
    env->CallVoidMethod(g_callback, mid);
    env->DeleteLocalRef(clazz);
    // 注意：不需要调 DetachCurrentThread——OS 会在线程死时自动调用
}
```

### 为什么不需要手动 Detach

- 线程第一次 Attach 时，`JNIEnv*` 被存入 TLS
- 线程死亡时，OS 调用 `detachThreadDestructor(JNIEnv*)`
- 函数内部执行 `DetachCurrentThread()`
- 无论线程正常结束、抛异常、被 cancel——OS 都会走到这个清理流程

**一句总结**：把"必须执行的清理代码"注册到 TLS 析构函数里，由 OS 来保证执行，而不是靠程序员记住。

---

## 四、这个方案的三个核心优势

| 优势 | 实现方式 |
| :--- | :--- |
| **高性能** | 每个线程只在第一次回调时 Attach 一次，后续直接从 TLS 取（O(1) 的 `pthread_getspecific`） |
| **零泄漏** | TLS 析构函数由 OS 保证调用，不会因为异常/提前 return 而遗漏 |
| **全解耦** | 回调代码不需要知道自己运行在哪个线程，`getJniEnv()` 自动处理一切 |

## 踩过的坑

| 坑 | 现象 | 正确做法 |
| :--- | :--- | :--- |
| 忘记创建 TLS Key | `pthread_getspecific` 返回垃圾值 | `JNI_OnLoad` 里确保 `pthread_key_create` 调用 |
| 主线程也调 Attach | 主线程已绑定，再次 Attach 虽不报错但可能引发双重 Detach | `GetEnv` 先检查，已绑定的不要 Detach |
| TLS 析构里调 JNI 函数 | Detach 时线程状态可能已不稳定 | 析构函数**只在里面调 DetachCurrentThread**，不做其他 JNI 操作 |
| 线程池线程复用 | 线程不死 → 析构函数不触发 → 永不 Detach（虽然没泄漏但 JVM 内部引用累积） | 线程池场景：在线程归还池时手动清理 TLS |
| `pthread_key_create` 多次调用 | 创建多个 Key，混乱 | `JNI_OnLoad` 全进程只调一次 |

---

## 扩展：不同线程需要不同清理逻辑怎么办

`pthread_key_create` 只能注册**一个**析构函数，所有线程共用。但实际场景中，不同线程可能持有不同资源（线程 A 只需 Detach，线程 B 还要释放自己 malloc 的缓冲区，线程 C 还要销毁自己创建的锁）。

三种方案，按场景选：

### 方案一：结构体 + 函数指针（最常用、最灵活）

不存裸 `JNIEnv*`，改成存一个**带清理策略的结构体指针**。全局析构函数只管"分派"，具体逻辑由线程自己注册：

```cpp
// ===== 线程上下文：把"数据"和"怎么清理"打包 =====
struct ThreadContext {
    JNIEnv* env;
    void (*cleanup)(ThreadContext*);  // 每个线程指定自己的清理函数
    void* userData;                   // 线程私有的额外资源
};

// ===== 全局唯一析构函数——行为由 ctx 自己决定 =====
void genericDestructor(void* arg) {
    ThreadContext* ctx = static_cast<ThreadContext*>(arg);
    if (!ctx) return;

    // 1. 先执行线程专属的清理逻辑
    ctx->cleanup(ctx);

    // 2. 再统一 Detach（所有线程都需要）
    if (ctx->env && g_jvm) {
        g_jvm->DetachCurrentThread();
    }
    delete ctx;
}

// ===== 各线程定义自己的清理策略 =====

// 线程 A：只需要 Detach，没有额外资源
void threadA_cleanup(ThreadContext* ctx) {
    // 什么都不用做
}

// 线程 B：需要在退出前释放自己 malloc 的缓冲区
void threadB_cleanup(ThreadContext* ctx) {
    free(ctx->userData);
}

// 线程 C：需要销毁自己创建的互斥锁
void threadC_cleanup(ThreadContext* ctx) {
    pthread_mutex_t* mtx = static_cast<pthread_mutex_t*>(ctx->userData);
    pthread_mutex_destroy(mtx);
}
```

**JNI_OnLoad 的变化**：只需把 `pthread_key_create` 的析构函数从 `detachThreadDestructor` 换成 `genericDestructor`，其余不变：

```cpp
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    // 换成了 genericDestructor——它只负责"分派"，具体清理由各线程自己决定
    pthread_key_create(&g_env_key, genericDestructor);
    return JNI_VERSION_1_6;
}
```

**使用方式**：每个线程在第一次 `getJniEnv` 时，创建自己的 `ThreadContext` 并指定自己的 `cleanup`：

```cpp
JNIEnv* getJniEnvWithContext(void (*cleanup)(ThreadContext*), void* userData) {
    ThreadContext* ctx = static_cast<ThreadContext*>(pthread_getspecific(g_env_key));
    if (ctx) return ctx->env;  // 已有 context → 复用

    ctx = new ThreadContext();
    ctx->cleanup = cleanup;
    ctx->userData = userData;

    // Attach + 写入 TLS（同之前逻辑）
    int status = g_jvm->GetEnv((void**)&ctx->env, JNI_VERSION_1_6);
    if (status != JNI_OK) {
        g_jvm->AttachCurrentThread(&ctx->env, nullptr);
    }
    pthread_setspecific(g_env_key, ctx);  // 存入 TLS
    return ctx->env;
}
```

### 方案二：多个 TLS Key，各管一类资源

不同类型资源用不同 Key，生命周期各自独立。OS 会**依次调用**每个 Key 的析构函数：

```cpp
pthread_key_t g_env_key;      // Key 0：管 JNIEnv → 析构 = DetachCurrentThread
pthread_key_t g_buffer_key;   // Key 1：管缓冲区 → 析构 = free
pthread_key_t g_mutex_key;    // Key 2：管锁     → 析构 = pthread_mutex_destroy

// JNI_OnLoad 里一次性创建
JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    pthread_key_create(&g_env_key,    detachThreadDestructor);
    pthread_key_create(&g_buffer_key, freeBufferDestructor);
    pthread_key_create(&g_mutex_key,  destroyMutexDestructor);
    return JNI_VERSION_1_6;
}
```

**适用场景**：资源类型是固定的（比如不管哪个线程，缓冲区就是 `free`，锁就是 `destroy`），只是不同线程**选择性持有**。线程 A 只存了 buffer → 只触发 `freeBufferDestructor`；线程 B 存了 buffer + mutex → 两个析构都触发。

**对比方案一**：方案一是"同一个 Key，不同线程不同行为"；方案二是"不同 Key，线程按需存取，行为由 Key 的析构函数决定"。

### 方案三：C++11 `thread_local` + RAII（推荐新项目）

完全不用 `pthread_key_create`，利用 C++ 标准库的类型安全机制。每个线程可以定义不同结构的局部对象，析构逻辑写在各自的析构函数里：

```cpp
// ===== 包装类：构造 = Attach，析构 = Detach =====
struct JniAttachment {
    JNIEnv* env;
    JniAttachment() { g_jvm->AttachCurrentThread(&env, nullptr); }
    ~JniAttachment() { g_jvm->DetachCurrentThread(); }
};

// ===== 线程 A：只需要 JNI 能力 =====
thread_local JniAttachment tls_jni_a;

// ===== 线程 B：除了 JNI，还要管缓冲区 =====
struct BContext {
    JniAttachment jni;
    uint8_t* buffer;
    BContext()  { buffer = (uint8_t*)malloc(4096); }
    ~BContext() { free(buffer); }  // ← 线程 B 退出时自动释放
};
thread_local BContext tls_ctx_b;

// ===== 线程 C：还要管锁 =====
struct CContext {
    JniAttachment jni;
    pthread_mutex_t mutex;
    CContext()  { pthread_mutex_init(&mutex, nullptr); }
    ~CContext() { pthread_mutex_destroy(&mutex); }
};
thread_local CContext tls_ctx_c;
```

**优点**：类型安全，编译器保证析构，不用写任何函数指针分派。**局限**：需要 C++11 以上，且只能在模块内部使用（跨 so 边界传递 `thread_local` 对象不安全）。

### 对比总览

| 方案 | 适用场景 | 复杂度 |
|:---|:---|:---|
| **结构体 + 函数指针** | pthread 原生，兼容旧代码，灵活度最高 | 中 |
| **多个 TLS Key** | 资源类型固定，不同线程按需存取 | 低 |
| **`thread_local` + RAII** | 新项目、C++11+、类型安全优先 | 低 |

---

## 自检一问

> 为什么把 `DetachCurrentThread` 放在 TLS 析构函数里，而不是手动写在回调代码末尾？

**答案**：因为 C++ 线程可能因异常提前退出、被第三方库 `pthread_cancel`、或者中途 return——手动写在代码末尾无法保证被执行。TLS 析构函数由操作系统在"线程死亡的最后时刻"自动调用，无论线程怎么死的都能走到，实现了 100% 的资源清理保证。

---

## 附录：Kotlin 协程适配（上层封装）

这一节不是 JNI 回调架构的必需部分，而是上层 API 形态的扩展。核心 JNI 方案仍然是前面的 `JavaVM` + TLS + `GlobalRef`；当上层从传统 Callback 迁移到 Kotlin 协程时，可以进一步封装：

```kotlin
// 单次异步操作用 suspendCancellableCoroutine
suspend fun initEngine(): Boolean = suspendCancellableCoroutine { cont ->
    NativeEngine.init(object : InitCallback {
        override fun onSuccess() { cont.resume(true) }
        override fun onError(msg: String) { cont.resumeWithException(RuntimeException(msg)) }
    })
    cont.invokeOnCancellation { NativeEngine.cancelInit() }
}

// 高频数据流用 callbackFlow
fun getVideoFrames(): Flow<ByteBuffer> = callbackFlow {
    val listener = object : VideoFrameListener {
        override fun onFrame(buffer: ByteBuffer) { trySend(buffer) }
    }
    NativeEngine.setListener(listener)
    awaitClose { NativeEngine.setListener(null) }
}
```
