# Android ByteBuffer 面试速记与原理详解

`ByteBuffer` 是 Java NIO 里用于读写字节数据的缓冲区。在 Android 音视频、网络、文件 I/O、JNI、硬件编解码场景中非常常见。

面试里问 `ByteBuffer`，通常不是只考 API，而是考你是否理解：**普通 Java 堆内存、直接内存、JNI 零拷贝、position/limit/capacity 读写模型，以及 DirectByteBuffer 的生命周期问题。**

---

## 一、面试高频问题与标准回答

### Q1：ByteBuffer 是什么？

`ByteBuffer` 是 Java NIO 提供的字节缓冲区，可以把它理解成一块可读写的字节数组，但它比普通 `byte[]` 多了读写位置管理能力。

它内部有三个核心指针：

- `capacity`：缓冲区总容量，创建后固定。
- `position`：当前读写位置。
- `limit`：当前可读或可写边界。

面试可以这样回答：

> `ByteBuffer` 是 Java NIO 中用于管理字节数据的缓冲区。它通过 `position`、`limit`、`capacity` 管理读写状态，既可以分配在 Java 堆上，也可以分配在 Native 直接内存上。Android 音视频里经常用它承载 PCM、H264、YUV 等二进制数据。

### Q2：HeapByteBuffer 和 DirectByteBuffer 有什么区别？

`ByteBuffer.allocate(size)` 创建的是堆内缓冲区：

```java
ByteBuffer buffer = ByteBuffer.allocate(1024);
```

它的数据在 Java 堆里，本质上背后通常是一个 `byte[]`，会受到 GC 管理。

`ByteBuffer.allocateDirect(size)` 创建的是直接缓冲区：

```java
ByteBuffer buffer = ByteBuffer.allocateDirect(1024);
```

它的数据在 Native 内存中，不在普通 Java 堆里。

核心区别：

| 类型 | 内存位置 | 优点 | 缺点 |
|---|---|---|---|
| `HeapByteBuffer` | Java 堆 | 创建快，GC 可直接管理 | JNI/系统 I/O 时可能需要拷贝 |
| `DirectByteBuffer` | Native 内存 | 更适合 JNI、Socket、文件、MediaCodec 等底层 I/O | 分配释放成本高，生命周期更难控制 |

一句话：

> HeapByteBuffer 适合普通 Java 逻辑；DirectByteBuffer 适合和 Native、系统 I/O、音视频底层模块交互，减少不必要的数据拷贝。

### Q3：DirectByteBuffer 为什么能减少 JNI 拷贝？

普通 `byte[]` 在 Java 堆里，C++ 层通过 JNI 访问时，经常需要 `GetByteArrayElements` 或 `GetByteArrayRegion`，JVM 可能会发生一次拷贝。

而 `DirectByteBuffer` 背后是 Native 内存，C++ 可以通过 JNI 直接拿到底层地址：

```cpp
uint8_t* data = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
jlong size = env->GetDirectBufferCapacity(buffer);
```

这样 Java 和 C++ 操作的是同一块 Native 内存，避免了 Java 堆到 Native 堆之间的大块数据拷贝。

面试可以这样说：

> DirectByteBuffer 的价值在于它背后是 Native 内存。Java 层拿到的是一个 ByteBuffer 对象，C++ 层可以通过 `GetDirectBufferAddress` 直接拿到底层指针，所以非常适合音视频 SDK 里传递大块 PCM、YUV、H264 数据。

---

## 二、ByteBuffer 的读写模型

`ByteBuffer` 最容易混的是 `position`、`limit`、`capacity`。

假设：

```java
ByteBuffer buffer = ByteBuffer.allocate(8);
```

初始状态：

```text
capacity = 8
position = 0
limit    = 8
```

写入数据：

```java
buffer.put((byte) 1);
buffer.put((byte) 2);
buffer.put((byte) 3);
```

写完后：

```text
position = 3
limit    = 8
capacity = 8
```

此时如果想从头读刚刚写入的数据，需要调用：

```java
buffer.flip();
```

`flip()` 的作用是把缓冲区从写模式切到读模式：

```text
limit = old position = 3
position = 0
```

然后读取：

```java
while (buffer.hasRemaining()) {
    byte value = buffer.get();
}
```

读完后：

```text
position = 3
limit    = 3
```

如果想重新写，可以调用：

```java
buffer.clear();
```

`clear()` 不会清空真实数据，只是重置指针：

```text
position = 0
limit    = capacity
```

常见 API 总结：

- `put()`：写数据，`position` 向后移动。
- `get()`：读数据，`position` 向后移动。
- `flip()`：写完切读。
- `clear()`：准备重新写，旧数据逻辑上作废。
- `rewind()`：重新从头读，不改变 `limit`。
- `remaining()`：`limit - position`。

---

## 三、Android 音视频里为什么常用 ByteBuffer

### 1. MediaCodec 输入输出

Android `MediaCodec` 的老接口中，经常通过 `ByteBuffer` 获取输入输出缓冲区：

```java
int index = codec.dequeueInputBuffer(timeoutUs);
if (index >= 0) {
    ByteBuffer input = codec.getInputBuffer(index);
    input.clear();
    input.put(frameData);
    codec.queueInputBuffer(index, 0, frameSize, ptsUs, 0);
}
```

编码器输出也类似：

```java
int index = codec.dequeueOutputBuffer(info, timeoutUs);
if (index >= 0) {
    ByteBuffer output = codec.getOutputBuffer(index);
    output.position(info.offset);
    output.limit(info.offset + info.size);
    // 从 output 中读取 H264/H265 数据
    codec.releaseOutputBuffer(index, false);
}
```

这里 `ByteBuffer` 只是 Java 层拿到编码器 Buffer 的一个视图，真正的底层内存可能由系统编解码器管理。

### 2. JNI 大块数据传递

比如 C++ SDK 产生一帧 PCM 或 H264 数据，需要回调给 Java：

```cpp
void callbackToJava(JNIEnv* env, jobject listener, uint8_t* data, int size) {
    jobject buffer = env->NewDirectByteBuffer(data, size);
    env->CallVoidMethod(listener, onDataMethod, buffer, size);
}
```

Java 层：

```java
void onData(ByteBuffer buffer, int size) {
    byte[] dst = new byte[size];
    buffer.get(dst);
}
```

如果 Java 层只是转交给下一个 Native/系统模块，可以尽量不要转成 `byte[]`，否则又会发生一次拷贝。

---

## 四、DirectByteBuffer 和 JNI 的两种常见用法

### 用法 1：Java 分配，C++ 使用

Java 层分配：

```java
ByteBuffer buffer = ByteBuffer.allocateDirect(1024 * 1024);
nativeFillBuffer(buffer);
```

C++ 层写入：

```cpp
extern "C"
JNIEXPORT void JNICALL
Java_com_demo_Native_nativeFillBuffer(JNIEnv* env, jobject thiz, jobject buffer) {
    auto* data = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    jlong capacity = env->GetDirectBufferCapacity(buffer);

    if (!data || capacity <= 0) {
        return;
    }

    // C++ 直接写入 Java 传下来的 DirectByteBuffer 底层内存。
    data[0] = 1;
    data[1] = 2;
}
```

这种方式适合 Java 负责 Buffer 生命周期，Native 只临时使用。

### 用法 2：C++ 分配，Java 使用

C++ 层分配 Native 内存并包装成 `DirectByteBuffer`：

```cpp
uint8_t* data = new uint8_t[size];
jobject buffer = env->NewDirectByteBuffer(data, size);
```

Java 层拿到后可以读取这个 Buffer。

但要注意：`NewDirectByteBuffer` 只是创建 Java 对象包装这块 Native 内存，**不会自动帮你 delete[] data**。这块 Native 内存必须由 C++ 自己设计释放时机。

所以更推荐：

- 谁分配，谁释放。
- Java 持有期间，C++ 不能提前释放。
- C++ 释放前，要确保 Java 不再访问。

---

## 五、ByteBuffer 常见坑

### 坑 1：忘记 flip，导致读不到数据

```java
ByteBuffer buffer = ByteBuffer.allocate(8);
buffer.put((byte) 1);
buffer.put((byte) 2);

// 忘记 flip，position 已经在 2，直接读会从当前位置继续读。
byte value = buffer.get();
```

正确写法：

```java
buffer.flip();
byte value = buffer.get();
```

### 坑 2：clear 不是清空数据

`clear()` 只是重置 `position` 和 `limit`，并不把底层内存置零。

```java
buffer.clear();
```

它的语义是“我要重新写了，之前的数据逻辑上不要了”。

### 坑 3：DirectByteBuffer 不是越多越好

`DirectByteBuffer` 分配的是 Native 内存，分配和释放成本比普通 Java 堆对象高。如果频繁创建大量 DirectByteBuffer，可能导致 Native 内存压力大，甚至 OOM。

实际项目中通常会：

- 复用 DirectByteBuffer。
- 使用 Buffer Pool。
- 避免在高频回调中反复 allocateDirect。

### 坑 4：Native 内存生命周期不清晰

如果 C++ 用 `NewDirectByteBuffer` 包装了一块 Native 内存，Java 只是拿到一个壳。Java 对象还活着，不代表底层 Native 内存一定还活着。

危险情况：

```text
C++ 分配 data
    ↓
NewDirectByteBuffer(data)
    ↓
回调给 Java
    ↓
C++ 提前 delete[] data
    ↓
Java 继续读 ByteBuffer，发生野指针访问
```

所以 DirectByteBuffer 用在跨语言场景时，一定要明确所有权。

---

## 六、ByteBuffer 和 byte[] 怎么选

### 适合用 byte[] 的场景

- 数据量小。
- 纯 Java 逻辑处理。
- 不涉及频繁 JNI 调用。
- 生命周期简单。

### 适合用 DirectByteBuffer 的场景

- 大块二进制数据。
- 高频跨 JNI 传递。
- Socket/FileChannel/MediaCodec 等底层 I/O。
- 音视频帧数据，比如 PCM、YUV、H264、H265。
- 希望减少 Java 堆内存拷贝和 GC 压力。

面试可以这样回答：

> 小数据用 `byte[]` 更简单；大数据、高频 JNI、音视频 I/O 更适合 `DirectByteBuffer`。但 DirectByteBuffer 不是免费优化，它的分配成本和生命周期管理更复杂，所以实际项目里要配合复用和 Buffer Pool。

---

## 七、面试总结

可以这样收尾：

> `ByteBuffer` 是 Java NIO 的字节缓冲区，核心通过 `position`、`limit`、`capacity` 管理读写状态。Android 里它常用于 MediaCodec、网络、文件 I/O 和 JNI 数据传递。普通 HeapByteBuffer 在 Java 堆上，使用简单但跨 JNI 可能有拷贝；DirectByteBuffer 在 Native 内存上，C++ 可以通过 `GetDirectBufferAddress` 直接访问，适合音视频这类大块数据高频传递场景。实际使用时要注意 `flip/clear` 的语义，以及 DirectByteBuffer 的 Native 内存生命周期，避免野指针和内存泄漏。

如果结合 SDK 项目经验，可以补一句：

> 在我们的 SDK 里，如果只是传递控制参数，用普通 Java 对象或 `byte[]` 就够了；但如果是 PCM、YUV、H264 这种大块连续数据，就优先考虑 `DirectByteBuffer` 或 Native Buffer 池，减少 Java 堆拷贝和 GC 抖动。

