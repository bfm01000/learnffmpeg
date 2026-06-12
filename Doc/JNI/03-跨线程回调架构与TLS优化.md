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

// ===== JNI_OnLoad：一次性初始化 =====
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
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
