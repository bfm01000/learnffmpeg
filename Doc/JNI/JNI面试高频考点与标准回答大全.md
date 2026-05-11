# JNI 高频面试考点与标准回答（口语化版）

在音视频开发或底层 SDK 开发的面试中，JNI（Java Native Interface）是绕不开的重头戏。面试官考察 JNI，主要看你是否踩过坑、懂不懂底层内存管理和性能优化。

以下是面试中最常问的 6 个 JNI 考点，附带底层原理和可以直接在面试中“背诵”的口语化标准回答。

---

## 考点 1：JNIEnv 是什么？能不能跨线程传递？

**底层原理：**
`JNIEnv`（JNI Environment）是一个指向 JNI 函数表指针的指针。它是**线程私有**的上下文环境。JVM 内部在管理线程时，依赖 `JNIEnv` 里面的环境状态变量，如果把 A 线程的 `JNIEnv` 交给 B 线程用，会导致 JVM 的内部状态彻底错乱，直接引发 Segmentation Fault（崩溃）。与之相对，`JavaVM` 是进程全局唯一的，所有线程共享。

**🗣️ 面试标准回答：**
> “`JNIEnv` 代表了 JNI 的上下文环境，它里面包含了所有调 Java 方法的 API。**它是绝对不能跨线程传递的**，因为它和当前线程是强绑定的。
> 如果我们在 C++ 的子线程想要回调 Java，我们不能直接拿主线程传过来的 `JNIEnv` 去用，而是必须拿着全局保存的 `JavaVM` 指针，调用 `AttachCurrentThread` 让 JVM 为当前的子线程分配一个新的 `JNIEnv`。用完之后，还要记得调用 `DetachCurrentThread` 进行解绑。”

---

## 考点 2：JNI 中的三种引用类型有什么区别？

**底层原理：**
JVM 的垃圾回收器（GC）需要知道 C++ 层是否在使用某个 Java 对象，这就需要引入“引用凭证”机制。
*   **LocalRef（局部引用）**：默认传过来的都是这个。作用域仅限当前 JNI 函数，函数 `return` 后 JVM 自动回收凭证。
*   **GlobalRef（全局引用）**：强引用，强行阻断 GC 回收对象，直到手动调用 `DeleteGlobalRef`。
*   **WeakGlobalRef（弱全局引用）**：弱引用，不阻断 GC 回收对象，但用完也需要手动调用 `DeleteWeakGlobalRef` 释放凭证内存。

**🗣️ 面试标准回答：**
> “JNI 里有三种引用类型：局部引用、全局引用和弱全局引用。
> **局部引用**是函数一返回就自动失效的。如果我们想在 C++ 层面长久保存一个 Java 对象（比如保存一个 Listener 以后回调），绝对不能直接存局部引用，否则函数一结束它就成野指针了。
> 必须使用 **全局引用 (`NewGlobalRef`)**，它相当于给 Java 对象加了强锁，GC 就不会回收它了。不过用全局引用一定要当心内存泄漏，不用的时候必须手动调用 `DeleteGlobalRef` 去释放。
> 第三种是**弱全局引用**，它不影响 GC 回收，通常用于做兜底防泄漏的缓存，使用前需要用 `IsSameObject` 判断一下对象是不是已经被 GC 杀掉了。”

---

## 考点 3：C++ 子线程如何安全地回调 Java？（TLS 优化）

**底层原理：**
见文档《SDK回调》与《TLS与JNI线程销毁解析》。考察你是否懂 `AttachCurrentThread` 以及如何优雅地释放。

**🗣️ 面试标准回答：**
> “我们的做法是通过 `JavaVM->AttachCurrentThread` 获取专属的 `JNIEnv` 来进行回调。
> 但是频繁 Attach 和 Detach 性能很差，并且如果在代码末尾手动 Detach 很容易因为异常或中途 return 导致遗漏。
> 所以在实际项目中，我们**利用了 POSIX 的 TLS（线程局部存储）机制**进行优化。在 C++ 初始化时，通过 `pthread_key_create` 注册一个包含了 `DetachCurrentThread` 的析构函数。这样子线程在第一次回调时只做一次 Attach，等这根线程死掉的时候，操作系统底层会自动触发这个析构函数去完成 Detach 操作。这样既做到了性能最优，又实现了绝对的解耦和安全。”

---

## 考点 4：Java 和 C++ 之间传递大块音视频数据，如何避免性能瓶颈？

**底层原理：**
Java 的 `byte[]` 存放在 JVM 堆内存中，C++ 的 `uint8_t*` 存放在 Native 堆内存中。如果使用常规的 `GetByteArrayRegion` 或者 `SetByteArrayRegion`，本质上是一次彻头彻尾的内存拷贝（Deep Copy）。对于一帧 1080P 的画面来说，一秒拷贝 30 次，CPU 和内存带宽会被大量消耗，同时频繁申请大数组会引发严重的 Java GC 抖动（内存抖动）。

**🗣️ 面试标准回答：**
> “音视频数据量太大了，绝不能直接用 `byte[]` 传来传去，会有严重的拷贝损耗和 GC 抖动。
> 我们的做法是利用 **零拷贝（Zero Copy）思想**。
> 具体来说，我们使用 **`DirectByteBuffer` (直接内存)**。在 C++ 层分配好内存后，通过 JNI 的 `NewDirectByteBuffer` API，将这块内存的指针直接映射给 Java 层。Java 层拿到的这个 `ByteBuffer`，其实底层指向的就是 C++ 的那块内存。这样两边读写的是同一块物理内存，完全省去了数据拷贝的开销。”

---

## 考点 5：JNI_OnLoad 是做什么的？静态注册和动态注册有什么区别？

**底层原理：**
*   **静态注册**：按照固定命名规则（`Java_包名_类名_方法名`）暴露 C 函数。JVM 在第一次调用时去 SO 库里按名字搜索绑定。
*   **动态注册**：在 SO 库加载的入口 `JNI_OnLoad` 中，开发者自己搞一个映射数组，主动调用 `RegisterNatives` 把 Java 方法和 C++ 函数指针绑起来。

**🗣️ 面试标准回答：**
> “`JNI_OnLoad` 就相当于动态库被加载时的 `main` 函数，一般我们会在里面保存全局的 `JavaVM` 对象，并且做**动态注册**。
> 以前传统的做法是**静态注册**，函数名又长又丑（比如 `Java_com_xxxx_...`），而且 JVM 首次调用时还要去按名字搜索，性能稍差，也容易被反编译破解。
> 我们现在基本都用**动态注册**。就是在 `JNI_OnLoad` 里，准备一个 `JNINativeMethod` 数组，把 Java 的方法名和 C++ 的函数指针映射起来，然后调用 `env->RegisterNatives` 注册。这样函数名可以随便起，不仅更加安全，而且因为是直接绑定指针，首次调用的执行效率也更高。”

---

## 考点 6：除了没有释放 GlobalRef，JNI 开发还有哪些常见的内存泄漏场景？

**底层原理：**
JNI 提供了很多 `GetXXX` 和 `ReleaseXXX` 成对出现的 API。当你获取 Java 数组或字符串交给 C++ 处理时，JVM 可能会在底层做数据拷贝。如果不调 `Release` 告诉 JVM “我用完了”，JVM 内部申请的那块临时内存就永远不会被释放。

**🗣️ 面试标准回答：**
> “除了忘记调用 `DeleteGlobalRef` 会导致 Java 对象泄漏外，JNI 内部资源没释放也是重灾区。
> 最典型的就是字符串和数组操作。比如我们调用 `GetStringUTFChars` 把 Java 字符串转成 C++ 的 `char*`，或者用 `GetByteArrayElements` 获取数组指针。这些操作底层很可能发生了内存拷贝，相当于 JVM 借了一块内存给你用。
> 用完之后，**必须成对地调用** `ReleaseStringUTFChars` 或 `ReleaseByteArrayElements`。如果忘了调用，这块底层内存就直接漏掉了。我们的做法是封装一些类似于 C++ 智能指针的 RAII 包装类，利用 C++ 的析构函数来自动调 Release，保证万无一失。”