# 常见问题解决方案：C++ 层向上层（如 Java/Kotlin）的回调处理

在音视频（WebRTC/FFmpeg）或任何底层 C++ SDK 开发中，C++ 层向上层（Android 的 Java/Kotlin，iOS 的 Objective-C/Swift）进行回调是非常普遍且核心的场景。主要伴随着**线程切换**、**内存管理**、**大数据频繁拷贝导致性能下降**这三大痛点。

以下是具体的对接问题及详细的代码解决方案：

## 1. 线程环境问题（JNI 线程附加）

**坑点**：
C++ 底层的回调通常发生在它自己内部的子线程（比如解码线程、网络接收线程）。如果在这个 C++ 子线程直接调 JNI 找 Java 方法，程序会直接崩溃，因为该线程没有和 JVM 绑定（没有 `JNIEnv`）。

**处理方案**：
当 C++ 子线程需要回调 Java 时，必须先调用 `AttachCurrentThread` 获取当前线程的 `JNIEnv`，回调结束后调用 `DetachCurrentThread` 释放。

**代码示例**：

```cpp
// C++ 端：保存全局的 JavaVM
JavaVM* g_jvm = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// 在 C++ 子线程中的回调逻辑
void onDataReceivedFromNativeThread() {
    JNIEnv* env = nullptr;
    // 判断当前线程是否已经 Attach
    int getEnvStat = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    bool needDetach = false;
    
    if (getEnvStat == JNI_EDETACHED) {
        // 如果未绑定，则 Attach 到当前线程
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) {
            // Attach 失败处理
            return;
        }
        needDetach = true;
    }
    
    // 执行 JNI 回调调用 Java 层方法
    // env->CallVoidMethod(javaObj, methodId, ...);
    
    // 释放绑定
    if (needDetach) {
        g_jvm->DetachCurrentThread();
    }
}
```

*进阶优化*：频繁 `Attach/Detach` 非常消耗性能。实际项目中，为了避免重复附加和分离，通常会结合 POSIX 线程局部存储（TLS，即 `pthread_key_create`）来管理：只在线程第一次回调时 Attach，并在线程销毁时利用 TLS 的析构回调函数统一 Detach。

**代码示例（TLS 优化 JNI 线程附加）**：

```cpp
#include <pthread.h>
#include <jni.h>

// 保存全局 JavaVM 和 pthread_key
extern JavaVM* g_jvm;
pthread_key_t g_tls_key;

// 1. 线程销毁时的回调函数，统一执行 DetachCurrentThread
void detachThreadDestructor(void* arg) {
    JNIEnv* env = static_cast<JNIEnv*>(arg);
    if (env != nullptr && g_jvm != nullptr) {
        g_jvm->DetachCurrentThread();
    }
}

// 2. 在 JNI_OnLoad 或者引擎初始化时，创建 TLS key
void initJniTls() {
    // 创建 key，并绑定线程退出时的析构回调
    pthread_key_create(&g_tls_key, detachThreadDestructor);
}

// 3. 获取 JNIEnv 的辅助函数，利用 TLS 缓存
JNIEnv* getJniEnv() {
    JNIEnv* env = nullptr;
    // 获取当前线程的状态
    int status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    
    if (status == JNI_EDETACHED) {
        // 当前线程尚未 Attach，执行 AttachCurrentThread
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            // Attach 成功后，将 env 指针保存到当前线程的 TLS 中
            // 这样线程退出时，操作系统会自动调用 detachThreadDestructor 并传入 env
            pthread_setspecific(g_tls_key, env);
        }
    }
    return env;
}

// 4. C++ 子线程直接使用
void onDataReceivedOptimized() {
    // 直接获取 JNIEnv，底层处理了只 Attach 一次并自动释放
    JNIEnv* env = getJniEnv();
    if (env != nullptr) {
        // 执行回调...
        // env->CallVoidMethod(javaObj, methodId, ...);
    }
    // 注意：这里不再需要手动调用 DetachCurrentThread()，它会在线程销毁时自动执行
}

// 5. 实际调用与触发销毁的完整流程示例
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    // 在整个引擎/动态库加载时，调用一次即可
    // 向系统注册这把专属的 "钥匙(key)" 和对应的 "保洁员(destructor)"
    initJniTls();
    return JNI_VERSION_1_6;
}

// 模拟一个第三方库内部的 C++ 工作线程（比如 FFmpeg 的解码线程）
void* nativeWorkerThread(void* arg) {
    // 线程运行期间，可能会发生很多次回调
    for (int i = 0; i < 5; i++) {
        // 第一次调用 getJniEnv 时，会触发 AttachCurrentThread 并将 env 存入 TLS 抽屉
        // 后面 4 次调用，发现状态已经 Attach，直接返回 env（极大提升性能）
        onDataReceivedOptimized();
    }
    
    // 重点：线程执行完毕准备退出。
    // 此时操作系统会自动检查当前死亡线程的 TLS 抽屉，
    // 发现里面有存入的 JNIEnv，于是自动调用我们注册的 detachThreadDestructor！
    // 开发者在这个线程里完全不用写任何释放或 Detach 代码，实现了极致解耦。
    return nullptr;
}
```

## 2. 对象生命周期与内存泄漏（野指针与 GlobalRef）

**坑点**：
C++ 需要持有上层的回调对象实例才能调用它。如果管理不当，极易导致 C++ 层持有已销毁的上层对象（野指针 Crash），或者一直持有导致上层对象无法回收（内存泄漏）。

**处理方案**：
必须在 JNI 层将 Java 传入的对象转换为**全局引用 (`NewGlobalRef`)**。当上层主动解绑时，调用 `DeleteGlobalRef` 释放它。

**代码示例（基础版裸指针管理）**：

```cpp
// C++ 端：管理 Java 监听器对象
jobject g_listener = nullptr;

// Java 层调用 setListener 传递回调对象
extern "C" JNIEXPORT void JNICALL
Java_com_example_NativeEngine_setListener(JNIEnv* env, jobject thiz, jobject listener) {
    if (g_listener != nullptr) {
        env->DeleteGlobalRef(g_listener);
    }
    if (listener != nullptr) {
        // 创建全局引用，防止被 GC 掉
        g_listener = env->NewGlobalRef(listener);
    } else {
        g_listener = nullptr;
    }
}

// 释放资源（如页面销毁时调用）
extern "C" JNIEXPORT void JNICALL
Java_com_example_NativeEngine_release(JNIEnv* env, jobject thiz) {
    if (g_listener != nullptr) {
        env->DeleteGlobalRef(g_listener);
        g_listener = nullptr;
    }
}
```

*进阶优化（主流做法：RAII 与智能指针管理）*：
在现代 C++（C++11 及以上）中，手动调用 `DeleteGlobalRef` 非常容易遗漏（比如发生异常提前 return）。主流做法是利用 **RAII 思想**封装一个 `JniGlobalRef` 类，然后结合 `std::shared_ptr` 进行生命周期管理。

```cpp
#include <memory>
#include <jni.h>

// 1. 封装一个 RAII 风格的全局引用包装类
class JniGlobalRef {
private:
    JavaVM* jvm;
    jobject globalRef;

public:
    JniGlobalRef(JNIEnv* env, jobject localRef) {
        env->GetJavaVM(&jvm);
        globalRef = env->NewGlobalRef(localRef);
    }

    ~JniGlobalRef() {
        if (globalRef != nullptr) {
            JNIEnv* env = nullptr;
            // 确保能在析构的当前线程拿到 env，如果析构发生在 C++ 子线程
            bool needDetach = false;
            if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
                jvm->AttachCurrentThread(&env, nullptr);
                needDetach = true;
            }
            env->DeleteGlobalRef(globalRef);
            if (needDetach) {
                jvm->DetachCurrentThread();
            }
        }
    }

    jobject get() const { return globalRef; }
    
    // 禁用拷贝，允许移动 (Rule of 5)
    JniGlobalRef(const JniGlobalRef&) = delete;
    JniGlobalRef& operator=(const JniGlobalRef&) = delete;
};

// 2. 业务层使用 shared_ptr 自动管理
std::shared_ptr<JniGlobalRef> g_smartListener = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_com_example_NativeEngine_setListener(JNIEnv* env, jobject thiz, jobject listener) {
    if (listener != nullptr) {
        // 创建智能指针，引用计数为 1
        g_smartListener = std::make_shared<JniGlobalRef>(env, listener);
    } else {
        // 置空时，智能指针引用计数归零，触发 JniGlobalRef 析构，自动调用 DeleteGlobalRef
        g_smartListener = nullptr; 
    }
}

void notifyJavaOptimized() {
    // 线程安全的拷贝，增加引用计数，防止在回调执行一半时被其他线程 release
    auto safeListener = g_smartListener;
    if (safeListener == nullptr) return;
    
    // JNIEnv* env = getJniEnv();
    // env->CallVoidMethod(safeListener->get(), methodId, ...);
}
```

### 致命隐患：如果 Java 层忘记解绑怎么办？

如果在 Java 层，将 `listener` 传给 C++ 之后，由于疏忽或者异常，一直没有调用 `setListener(null)`，那么底层的 `shared_ptr` 引用计数永远不会归 0。这将导致 `DeleteGlobalRef` 永远无法执行。
**后果**：不仅传递下去的 `listener` 对象会内存泄漏，如果它是一个匿名内部类，还会导致它隐式持有的外部 `Activity` 及所有 UI 资源全部泄漏（页面级泄漏）。

为了给不靠谱的上层代码兜底，优秀的底层 SDK 通常采用以下两种保底防泄漏策略：

#### 防泄漏策略一：使用 `WeakGlobalRef`（弱全局引用）
如果该回调并不是系统强依赖的（例如仅用于 UI 更新，UI 销毁后就无需更新），可以使用弱全局引用。`NewWeakGlobalRef` 的特性是：**它不阻止 JVM 的 GC 回收该对象**。

```cpp
// 修改底层的保存逻辑为弱引用
class JniWeakGlobalRef {
private:
    JavaVM* jvm;
    jweak weakRef; // 使用 jweak 而不是 jobject

public:
    JniWeakGlobalRef(JNIEnv* env, jobject localRef) {
        env->GetJavaVM(&jvm);
        weakRef = env->NewWeakGlobalRef(localRef);
    }
    
    // 析构过程与 GlobalRef 类似，调用 DeleteWeakGlobalRef
    ~JniWeakGlobalRef() {
        // ... Attach 环境 ...
        env->DeleteWeakGlobalRef(weakRef);
        // ... Detach 环境 ...
    }

    // 回调前必须判断对象是否已经被 GC 杀掉
    bool callJavaMethod() {
        JNIEnv* env = getJniEnv(); // 获取当前线程的 env
        if (env->IsSameObject(weakRef, NULL)) {
            // 返回 true 说明 Java 层的对象已经被 GC 回收了（比如页面退出了）
            // 此时不仅不能回调，还可以顺手通知 C++ 层清理这个 weakRef
            return false; 
        }
        // 对象依然存活，正常回调
        // env->CallVoidMethod(weakRef, methodId, ...);
        return true;
    }
};
```
**优势**：哪怕上层忘记 `setListener(null)`，Activity 被关闭后依然会被正常 GC，底层发现对象死了也会安静地终止回调，完美防止了泄漏。

> **深入理解：`NewWeakGlobalRef` 的双层“回收”机制**
>
> 很多开发者在 `WeakGlobalRef` 上踩坑，是因为没有区分开**Java 对象的回收**和**JNI 弱引用凭证本身的回收**：
>
> 1. **它指向的【Java 对象】什么时候会被回收？**
>    当 Java 层没有任何“强引用”指向这个对象，且触发了 GC 时。`NewWeakGlobalRef` 的核心特性是“绝不阻碍 GC”。只要页面销毁、强引用断开，GC 就会正常回收该 Java 对象释放内存。此时在 C++ 层调用 `env->IsSameObject(weakRef, NULL)` 会返回 `true`。
> 2. **那个【`jweak` 弱引用凭证本身】什么时候会被回收？**
>    **永远不会自动回收！必须由 C++ 手动调用 `DeleteWeakGlobalRef` 才能回收。** 虽然引用的 Java 对象灰飞烟灭了，这张“凭证卡片”依然稳稳地占据着 JNI 底层引用表（Reference Table）的内存空间。如果不清理，JNI 引用表迟早会被塞满失效凭证，最终导致 **JNI 引用表溢出（JNI ERROR: JNI global reference table overflow）** 并让程序崩溃。
>
> **因此，正确的姿势是**：`weakRef` 保证了不阻挡 Java 对象被 GC；但在对象被回收或不再需要时，**C++ 层依然有绝对的责任调用 `DeleteWeakGlobalRef` 去释放这个 JNI 资源**（这也是为什么上面的封装中，析构函数里依然必须调用 delete 的原因）。

#### 业界主流真相：为何大厂 SDK 偏爱强引用（GlobalRef）+ 强制绑定？

虽然 `WeakGlobalRef` 听起来很完美，但在实际的大型 SDK 开发中，它存在一个**极其隐蔽且致命的缺陷**，导致如 WebRTC、ExoPlayer 等顶级项目极少把它作为首选方案。

**致命缺陷：回调神秘消失**
如果底层使用了 `WeakGlobalRef`，Java 层**必须**用一个成员变量强引用保活 listener。但业务方经常随手传递**匿名内部类**：
```java
engine.setListener(new VideoFrameListener() { ... }); // 匿名对象！
```
此时，如果触发一次微小的 GC，这个匿名对象会立刻被杀掉。后果是：**引擎还在跑，页面也没关，但回调神秘消失了！** 并且没有任何报错，极大地增加了排查成本。

**真正的业界主流做法（强规矩与强兜底）：**

1. **底层坚决使用 `GlobalRef`（强引用兜底）**
   只要传过来，底层无条件 `GlobalRef` 锁死，确保即使业务方传了匿名内部类，回调也绝对不会神秘消失。
2. **生命周期强制大清洗（一刀切）**
   不依赖上层零散地去 `setListener(null)`，而是提供强制的 `engine.release()` 接口。只要引擎销毁，底层 C++ 的析构函数里将所有的 `shared_ptr` 清空，所有的 `GlobalRef` 全部 `Delete`，一剑斩断所有羁绊。
3. **终极警告：如果不调 release 怎么办？（日志恐吓法）**
   在 Java 层的 Engine 类里，利用 `finalize()` 或 `Cleaner` 机制，在引擎对象将要被回收时做检查。如果发现业务方忘了调 `release()`，直接打印全屏飘红的恐怖报错，甚至在 Debug 模式下抛出异常崩溃。**不替业务方擦屁股，但会大声报警。**

> **疑问：如果依然需要主动调用（`setNull` 或 `release`），这跟纯手动申请/释放（`malloc/free`）有什么区别？**
>
> 这是一个非常深刻的疑问！区别在于**多线程安全**和**异常安全（异常安全）**：
>
> 1.  **多线程安全的天壤之别（最核心区别）**：
>     *   **纯手动管理（裸指针）**：如果你在 Java 层调了 `release()`，C++ 把 `GlobalRef` 给 Delete 了。但此时底层的解码线程**恰好正在执行回调中途**，拿着刚被干掉的野指针调用 `CallVoidMethod`，App 会瞬间 **Crash**（Segmentation Fault）。
>     *   **`shared_ptr` 管理**：C++ 解码线程在回调前会执行 `auto safeListener = g_smartListener`，这会让引用计数 +1。此时就算 Java 层调了 `release` 置空了全局变量，底层对象也不会立刻析构。只有等这最后一次回调安全跑完，局部的 `safeListener` 销毁（引用计数归零），才会真正触发 `DeleteGlobalRef`。完美解决了多线程下资源释放导致的 Crash 绝症。
> 2.  **异常安全与防错漏（RAII）**：
>     如果在 C++ 中途发生了异常（比如抛出 C++ Exception，或者中途 `return` 了），纯手动写的 `DeleteGlobalRef` 极大概率会被跳过不执行。而基于 `shared_ptr` 和 RAII 封装的机制，无论函数是如何退出的，栈展开（Stack Unwinding）一定会保证局部智能指针的析构，从而确保 `DeleteGlobalRef` 被 100% 触发。

## 3. 高频回调与大数据拷贝（性能瓶颈）

**坑点**：
音视频帧（如 YUV 或 PCM 数据）数据量极大，且帧率高。如果每次回调都把 C++ 的 `char*` 拷贝成 Java 的 `byte[]`，会产生极大的 CPU 消耗并引发频繁的 GC（内存抖动），严重影响性能。

**处理方案**：
利用**零拷贝（Zero Copy）**思想，使用 `DirectByteBuffer` (直接内存)。C++ 层通过 `NewDirectByteBuffer` 将 C++ 内存地址直接映射给 Java。

**代码示例**：

```cpp
// C++ 端：将音视频裸数据通过 DirectByteBuffer 传给 Java
void onVideoFrameCallback(JNIEnv* env, jobject listener, uint8_t* frameData, int size) {
    // 重点：将 C++ 指针直接包装为 ByteBuffer 传给 Java，不发生内存拷贝
    jobject byteBuffer = env->NewDirectByteBuffer(frameData, size);
    
    jclass listenerClass = env->GetObjectClass(listener);
    jmethodID onFrameMethod = env->GetMethodID(listenerClass, "onFrame", "(Ljava/nio/ByteBuffer;)V");
    
    // 回调 Java
    env->CallVoidMethod(listener, onFrameMethod, byteBuffer);
    
    env->DeleteLocalRef(byteBuffer);
    env->DeleteLocalRef(listenerClass);
}
```

```java
// Java 端接收
public interface VideoFrameListener {
    void onFrame(ByteBuffer buffer);
}

// 实现回调
@Override
public void onFrame(ByteBuffer buffer) {
    // 这里拿到的 buffer 指向的是 C++ 内存空间，直接读取/渲染，无需二次拷贝
    // 注意：该 buffer 的生命周期由 C++ 决定，不可在当前方法外长期持有
}
```

## 4. 异步回调的现代化封装（Kotlin Coroutines 适配）

**坑点**：
传统的 Callback 嵌套让上层代码难以维护（Callback Hell）。

**处理方案**：
利用 Kotlin 协程对底层回调进行流式和挂起函数的封装。对于单次回调使用 `suspendCancellableCoroutine`，对于高频状态/数据流回调使用 `callbackFlow`。

**代码示例**：

单次操作（例如初始化或登录）：

```kotlin
suspend fun loginNative(userId: String): Boolean = suspendCancellableCoroutine { continuation ->
    NativeEngine.login(userId, object : LoginCallback {
        override fun onSuccess() {
            continuation.resume(true)
        }
        override fun onError(code: Int, msg: String) {
            continuation.resumeWithException(RuntimeException(msg))
        }
    })
    
    // 如果协程被取消，通知底层取消操作
    continuation.invokeOnCancellation {
        NativeEngine.cancelLogin()
    }
}
```

数据流操作（例如接收音视频帧流）：

```kotlin
// 将底层的帧回调转化为冷流 (Flow)
fun getVideoFrameFlow(): Flow<ByteBuffer> = callbackFlow {
    val listener = object : VideoFrameListener {
        override fun onFrame(buffer: ByteBuffer) {
            // 发送数据到流中
            trySend(buffer).isSuccess
        }
    }
    
    // 注册底层监听
    NativeEngine.setListener(listener)
    
    // 当该 Flow 收集被取消时，清理底层监听，防止内存泄漏和性能浪费
    awaitClose {
        NativeEngine.setListener(null)
    }
}

// 收集使用
lifecycleScope.launch {
    getVideoFrameFlow().collect { buffer ->
        // 处理渲染...
    }
}
```

---
**总结**：在对接 C++ 层回调时，**稳定性**（通过 `AttachCurrentThread` 防崩溃、`GlobalRef` 防内存泄漏）和 **性能**（通过 `DirectByteBuffer` 减少数据拷贝）是核心优化点，而通过 `callbackFlow` 等协程特性则能提供更优雅现代的 API 体验。

# 常见问题解决方案：Kotlin Multiplatform (KMP) 改造常见坑与处理

Kotlin Multiplatform (KMP) 作为一个跨平台解决方案，近年来虽然已经非常成熟（尤其是配合 Compose Multiplatform 之后），但在实际将现有原生项目进行 KMP 改造的过程中，仍然有不少经典的“坑”。

通常来说，KMP 改造的坑主要集中在**内存模型**、**互操作性 (Interop)**、**生态替换**以及**构建配置**四大方面。以下是常见的坑及相应的处理方案：

## 1. 跨语言互操作性 (Interop) 的坑（Kotlin vs iOS）

KMP 在 iOS 端的底层机制是将 Kotlin 编译成 Objective-C 头文件，而不是直接编译成 Swift。这会导致许多 Kotlin 的高级特性在 Swift 中“水土不服”。

*   **坑 1：泛型丢失与类型擦除**
    *   **现象**：Kotlin 的泛型集合（如 `List<MyModel>`）在 Swift 中可能会变成 `NSArray` 或者泛型约束非常模糊。
    *   **处理**：尽量在公共 API 边界使用基础类型或明确的具体类，避免过度复杂的泛型嵌套。
*   **坑 2：默认参数、Sealed Class、扩展函数丢失**
    *   **现象**：Kotlin 的默认参数在 Swift 中无效（必须传所有参数）；Sealed Class 变成了普通的类，Swift 无法使用 `switch-case` 进行完备性检查；扩展函数变成了全局的静态方法。
    *   **处理**：
        *   **强烈推荐**引入 **[SKIE](https://skie.touchlab.co/) (Swift Kotlin Interface Enhancer)** 插件。它可以自动在编译期生成 Swift 代码，完美解决 Sealed Class、协程、默认参数等问题。
        *   或者在 Kotlin 层手动手写一层适配 Swift 的 Wrapper 接口。
*   **坑 3：异常处理**
    *   **现象**：Kotlin 的异常如果未捕获并抛到 iOS 端，会导致 App 直接崩溃 (Crash)。Swift 看不到 Kotlin 的异常。
    *   **处理**：在会抛出异常的公共 API 上添加 `@Throws(Exception::class)` 注解，这样在 Swift 端就会被转换为 `throws`，可以使用 `do-catch` 捕获。或者使用 `Result<T>` 封装返回值，通过状态码/状态类来传递错误。

## 2. 协程与并发模型的坑

虽然 Kotlin 1.7.20+ 引入了新的内存模型 (New Memory Model, NMM)，解决了以前臭名昭著的 `InvalidMutabilityException`（对象冻结问题），但并发仍然有坑。

*   **坑 1：挂起函数 (Suspend Functions) 在 iOS 的调用**
    *   **现象**：Kotlin 的 `suspend` 函数暴露给 iOS 时，会被转换为带有 completion handler 的方法，在 Swift 原生 async/await 中调用起来不够优雅。
    *   **处理**：使用 **KMP-NativeCoroutines** 这个库（或者前面提到的 **SKIE**），它们能将 Kotlin 的 `suspend` 和 `Flow` 自动转换为 Swift 的 `async/await` 和 `Combine/AsyncSequence`，大幅提升 iOS 开发体验。
*   **坑 2：主线程阻塞问题**
    *   **现象**：iOS 端调用 Kotlin 的耗时操作时，如果没有指定好 Dispatchers，极易阻塞主线程。
    *   **处理**：在 KMP 内部严格规范协程的调度。网络、数据库等耗时操作必须内部切换到 `Dispatchers.Default` 或自定义的后台线程执行，暴露给外部的 API 必须是“主线程安全”的。

## 3. 第三方生态替换与平台强依赖的坑

原生项目通常深度绑定了特定平台的优秀开源库。

*   **坑 1：原有库无法跨平台**
    *   **现象**：Android 的 Retrofit, Room, Gson, SharedPreferences 无法在 iOS 使用。
    *   **处理**：进行生态迁移。
        *   **网络**：`Retrofit` -> `Ktor`
        *   **序列化**：`Gson/Moshi` -> `kotlinx.serialization`
        *   **数据库**：`Room` -> `SQLDelight` (注：Room 最近也开始支持 KMP 了，如果是新项目可以直接尝试 Room KMP)
        *   **KV 存储**：`SharedPreferences` -> `Multiplatform Settings` 或者 `DataStore KMP`
*   **坑 2：Context 等平台强耦合对象**
    *   **现象**：很多历史代码随手就传一个 `Context`，导致代码无法下沉到 shared 模块。
    *   **处理**：重构为 **Clean Architecture**。使用 `expect/actual` 关键字来抽象平台差异，或者通过依赖注入 (DI 工具如 Koin) 在平台层实现接口后注入到共享层。坚决将业务逻辑与平台 API 解耦。

## 4. 工程与构建配置的坑 (Gradle & iOS 构建)

*   **坑 1：iOS 端编译速度变慢**
    *   **现象**：每次修改 Kotlin 代码，iOS 编译都需要先执行一次 Gradle 的 link 任务，非常慢。
    *   **处理**：
        *   在开发 iOS 时，尽量避免频繁修改 Kotlin 核心模块。
        *   如果是大型项目，可以考虑将 KMP 产物打包成 XCFramework 作为二进制库分发给 iOS 团队，而不是源码依赖。
*   **坑 2：CocoaPods 依赖冲突**
    *   **现象**：如果 KMP 模块内部通过 `cocoapods` 插件依赖了其他的 iOS 原生库，很容易和主工程的 Podfile 产生冲突或链接错误。
    *   **处理**：更推荐的方式是 KMP 层只做纯逻辑（不依赖第三方 iOS 闭源库），最后通过 SPM (Swift Package Manager) 或标准的 CocoaPods 将 Shared 模块暴露给 iOS 工程。尽量减少 Kotlin 直接去依赖和调用复杂的 iOS 原生库。

## 5. 架构状态管理 (ViewModel) 的坑

*   **坑：ViewModel 的生命周期跨平台**
    *   **现象**：Android 的 ViewModel 有自己的生命周期（如 `viewModelScope`），而 iOS 也有自己的 ViewController 生命周期，两端不好对齐。
    *   **处理**：
        *   方案 A：使用 JetBrains 官方最近推出的 `androidx.lifecycle.ViewModel` KMP 版本。
        *   方案 B：使用第三方跨平台 MVVM 框架（如 `MOKO mvvm` 或 `Decompose` 进行导航和状态管理）。
        *   方案 C：共享层只负责暴露 `StateFlow/SharedFlow`，由两端原生的 ViewModel 去持有并订阅这些 Flow。这是目前阻力最小的渐进式改造方案。