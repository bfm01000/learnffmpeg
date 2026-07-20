# CameraParam 参数抽象：方案比较与取舍

> **所属项目**：Vertical Industry Camera SDK KMP 重构  
> **对应白皮书章节**：3.3 方案比较（Trade-off Analysis）/ 4.2 CameraParam\<T\> 参数抽象  
> **决策结论**：采纳 **Template Method**（`CameraParamImpl` 抽象基类）

---

## 1. 要解决什么问题

旧版 SDK 有 **35+ 相机参数**（ISO、快门、HDR、白平衡、分辨率……），每个参数一组散乱方法：

```kotlin
// 旧 SDK：每加一个参数，接口就要多 4 个方法
interface LegacyCamera {
    fun getIso(): Int
    fun setIso(value: Int)
    fun getSupportedIso(): List<Int>
    fun addIsoListener(listener: (Int) -> Unit)

    fun getShutterSpeed(): Long
    fun setShutterSpeed(value: Long)
    fun getSupportedShutterSpeed(): List<Long>
    fun addShutterSpeedListener(listener: (Long) -> Unit)

    // ... 再重复 33 次
}
```

带来的结构性问题：

| 问题 | 表现 |
|------|------|
| 接口膨胀 | `Camera` 类方法数破百，违反接口隔离原则（ISP） |
| 新增成本高 | 每加一个参数要改接口 + Android/iOS 双端实现 |
| API 不统一 | 命名风格各异（`getIso` vs `iso` vs `ISO`），IDE 补全困难 |
| 重复逻辑 | get/set/getSupported/listener 的线程安全、错误包装、日志各自复制 |

重构目标：把 35+ 组重复逻辑收敛成**一种统一形状**，同时满足：

1. **对外可发现**：`device.capture.exposureISO.setValue(800)` 比 `setIso(800)` 更清晰
2. **开闭原则**：新增参数不改已有接口
3. **KMP 跨端**：OC/Swift 第三方也能接入，不能依赖 Kotlin 语法糖
4. **可单测**：每个参数可脱离 Native 独立测试

---

## 2. 三种候选方案概览

AI 在架构探索阶段提供了三种实现路径，并附带完整代码骨架。工程师做 trade-off 后的决策如下：

| 方案 | 优点 | 缺点 | 决策 |
|------|------|------|------|
| A. Template Method（`CameraParamImpl`） | 新增参数只需实现 4 个方法；形状统一 | 继承层次可能变深 | **采纳** |
| B. Delegate 模式 | 组合优于继承；易 mock | 35+ 参数各自一个 Delegate 类，文件数翻倍 | 否决 |
| C. Property Delegate（`by cameraParam()`） | Kotlin 惯用法，调用侧最简洁 | KMP commonMain 支持有限；OC 侧无法消费 | 否决 |

> AI 还建议了第四种方案——`Map<String, Any>` 动态参数。立刻否决：**类型安全是 SDK 的生命线**。

---

## 3. 方案 A：Template Method（采纳）

### 3.1 设计思路

- **对外**：统一 `CameraParam<T>` 接口，五件套能力固化
- **对内**：`CameraParamImpl` 抽象基类实现模板方法，子类只填 4 个 hook
- **挂载**：`CameraCapture` 以属性形式暴露各参数

### 3.2 对外契约

```kotlin
interface CameraParam<T> {
    suspend fun getValue(): Result<T>
    suspend fun fetchValue(): Result<T>          // 强制从设备拉取
    suspend fun setValue(value: T): Result<Unit>
    suspend fun getSupported(): Result<List<T>>
    fun getName(): String
    fun addListener(listener: (T) -> Unit)
    fun removeListener(listener: (T) -> Unit)
}
```

### 3.3 模板方法实现

```kotlin
abstract class CameraParamImpl<T>(
    protected val deviceCore: DeviceCore,
    private val paramName: String,
) : CameraParam<T> {

    // ── 子类必须实现的 4 个变化点 ──
    protected abstract suspend fun reader(): Result<T>
    protected abstract suspend fun writer(value: T): Result<Unit>
    protected abstract suspend fun fetcher(): Result<T>
    protected abstract suspend fun supportedLoader(): Result<List<T>>

    // ── 以下 35+ 参数共享，只写一次 ──
    override suspend fun getValue() = reader()
    override suspend fun fetchValue() = fetcher()
    override suspend fun setValue(value: T) = writer(value)
    override suspend fun getSupported() = supportedLoader()
    override fun getName() = paramName

    private val listeners = LinkedHashSet<(T) -> Unit>()
    override fun addListener(listener: (T) -> Unit) { listeners.add(listener) }
    override fun removeListener(listener: (T) -> Unit) { listeners.remove(listener) }

    protected fun notifyListeners(value: T) {
        listeners.toList().forEach { it(value) }  // snapshot 派发
    }
}
```

### 3.4 新增一个参数

```kotlin
// 新增文件：ExposureIsoCameraParam.kt —— 零接口变更
class ExposureIsoCameraParam(deviceCore: DeviceCore) : CameraParamImpl<Int>(
    deviceCore, "exposureISO"
) {
    override suspend fun reader() =
        deviceCore.readIntParam("exposureISO")

    override suspend fun writer(value: Int) =
        deviceCore.writeIntParam("exposureISO", value)

    override suspend fun fetcher() =
        deviceCore.fetchIntParam("exposureISO")

    override suspend fun supportedLoader() =
        deviceCore.getSupportedIntValues("exposureISO")
}
```

### 3.5 在聚合根中挂载

```kotlin
interface CameraCapture {
    val exposureISO: CameraParam<Int>
    val shutterSpeed: CameraParam<Long>
    val hdr: CameraParam<Boolean>
    // 新增参数：只加一行，不改已有签名

    suspend fun startCapture(): Result<Unit>
    fun startCapture(callback: Callback<Unit>)
}

class CameraCaptureService(private val deviceCore: DeviceCore) : CameraCapture {
    override val exposureISO = ExposureIsoCameraParam(deviceCore)
    override val shutterSpeed = ShutterSpeedCameraParam(deviceCore)
    override val hdr = HdrCameraParam(deviceCore)
}
```

### 3.6 调用侧

```kotlin
// Kotlin 协程
device.capture.exposureISO.setValue(800).getOrThrow()
val supported = device.capture.exposureISO.getSupported().getOrThrow()
device.capture.exposureISO.addListener { iso -> updateUI(iso) }

// OC/Swift：同一套方法名，配合 Callback 桥接
// device.capture.exposureISO.setValue(800, callback: ...)
```

### 3.7 单测

```kotlin
@Test
fun `exposureISO setValue and getValue`() = runTest {
    val param = ExposureIsoCameraParam(fakeDeviceCore)
    param.setValue(800).getOrThrow()
    assertEquals(800, param.getValue().getOrThrow())
}

@Test
fun `exposureISO listener notified on change`() = runTest {
    val param = ExposureIsoCameraParam(fakeDeviceCore)
    var received: Int? = null
    param.addListener { received = it }
    fakeDeviceCore.simulateParamChange("exposureISO", 1600)
    assertEquals(1600, received)
}
```

### 3.8 采纳理由

| 维度 | 表现 |
|------|------|
| 开闭原则 | 新增 `HdrCameraParam.kt` 即可，不改 `CameraParam<T>` 接口 |
| API 可发现性 | `device.capture.` 后 IDE 自动补全所有 `CameraParam` |
| OC/Swift 友好 | 暴露显式接口 + `Callback` 桥接，不依赖 Kotlin 特性 |
| DRY | 公共逻辑集中在 `CameraParamImpl`，35 份变 1 份 |
| 可测试 | mock `DeviceCore` 即可单测任意参数 |

**已知代价**：继承链 `XxxCameraParam → CameraParamImpl → CameraParam`，层次变深。在 35+ 参数场景下，这比文件数爆炸更可接受。

---

## 4. 方案 B：Delegate 模式（否决）

### 4.1 设计思路

组合优于继承：每个参数的 read/write/fetch/supported 各自一个 Delegate 类，再组装成 `CameraParam`。

### 4.2 代码形态

```kotlin
interface ParamReader<T>   { suspend fun read(): Result<T> }
interface ParamWriter<T>   { suspend fun write(value: T): Result<Unit> }
interface ParamFetcher<T>  { suspend fun fetch(): Result<T> }
interface ParamSupported<T> { suspend fun load(): Result<List<T>> }

// 每个参数需要 4 个 Delegate + 1 个组装类
class IsoReader(private val core: DeviceCore) : ParamReader<Int> {
    override suspend fun read() = core.readIntParam("exposureISO")
}
class IsoWriter(private val core: DeviceCore) : ParamWriter<Int> {
    override suspend fun write(value: Int) = core.writeIntParam("exposureISO", value)
}
class IsoFetcher(private val core: DeviceCore) : ParamFetcher<Int> { /* ... */ }
class IsoSupportedLoader(private val core: DeviceCore) : ParamSupported<Int> { /* ... */ }

class ExposureIsoCameraParam(deviceCore: DeviceCore) : CameraParam<Int> {
    private val reader = IsoReader(deviceCore)
    private val writer = IsoWriter(deviceCore)
    private val fetcher = IsoFetcher(deviceCore)
    private val supportedLoader = IsoSupportedLoader(deviceCore)

    override suspend fun getValue() = reader.read()
    override suspend fun setValue(value: Int) = writer.write(value)
    // fetchValue / getSupported / listener 管理 —— 每个参数都要自己写一遍
}
```

### 4.3 文件数对比

```
方案 A（Template Method）:
  CameraParam.kt
  CameraParamImpl.kt              ← 公共逻辑只写一次
  ExposureIsoCameraParam.kt       × 35
  合计 ≈ 37 个文件

方案 B（Delegate）:
  CameraParam.kt + 4 个 Delegate 接口
  每个参数：Reader + Writer + Fetcher + SupportedLoader + 组装类
  合计 ≈ 35 × 5 = 175 个文件量级
```

### 4.4 否决理由

1. **样板代码爆炸**：每个 Delegate 的实现几乎相同，只是 param key 不同
2. **listener 管理无法复用**：组装类里还要各自实现 add/remove/notify
3. **「易 mock」优势不成立**：Template Method 下 mock `DeviceCore` 同样简单
4. **维护成本高**：改 listener 派发策略要动 35 个组装类

> 白皮书原话：「35+ 参数如果每个都用独立 Delegate 类，文件数从 35 变成 70+，且每个 Delegate 的样板代码几乎相同。」

---

## 5. 方案 C：Property Delegate（否决）

### 5.1 设计思路

Kotlin 惯用法：用 `by cameraParam()` 把参数包装成属性，调用侧最简洁。

### 5.2 代码形态

```kotlin
class CameraCaptureService(deviceCore: DeviceCore) : CameraCapture {
    override var exposureISO by cameraParam<Int>(deviceCore, "exposureISO")
    override var shutterSpeed by cameraParam<Long>(deviceCore, "shutterSpeed")
}

// 理想调用侧
device.capture.exposureISO = 800
val iso = device.capture.exposureISO
```

`cameraParam()` 工厂内部用 `ReadWriteProperty` 委托，把 get/set 转发到 `DeviceCore`。

### 5.3 否决理由

#### （1）KMP commonMain 支持有限

`by cameraParam()` 依赖自定义 `ReadWriteProperty` + operator，在 KMP 跨平台编译（尤其 iOS Framework 导出）时容易踩坑：

- 泛型 reified 限制
- inline delegate 在 Native 侧的导出形态不稳定
- 属性背后的 suspend 语义在 Framework 头文件中丢失

#### （2）OC/Swift 无法消费

对外 SDK 编译为 iOS Framework 后，OC/Swift 看到的是：

```swift
// ❌ 看起来像普通属性，但背后是 suspend + listener + getSupported
camera.capture.exposureISO = 800
```

Property delegate 的语法糖在 Kotlin 侧隐藏了五件套能力，**OC 侧拿不到等价 API**。SDK 对外契约必须是显式接口（见白皮书 3.1 节：不能直接消费 `suspend` / `Flow`）。

#### （3）丢失 API 可发现性

```kotlin
// 方案 A：IDE 补全 device.capture. 能看到所有 CameraParam
device.capture.exposureISO.setValue(800)
device.capture.exposureISO.getSupported()
device.capture.exposureISO.addListener { ... }

// 方案 C：属性上没有 getSupported / addListener
device.capture.exposureISO = 800
device.capture.exposureISO.getSupported()  // ❌ Int 上没有这个方法
```

要补全能力，还得额外挂 extension 或 companion，反而更乱。

---

## 6. 三种方案并排对比

```
                    旧 SDK              方案 A              方案 B              方案 C
                    ──────              ──────              ──────              ──────
对外调用            getIso()/setIso()   param.setValue()    param.setValue()    iso = 800
新增参数成本        改接口+双端实现      1 个文件            4~5 个文件          1 行 delegate
公共逻辑复用        无                  CameraParamImpl     每个 delegate 重复   cameraParam() 工厂
OC/Swift 友好       一般                ✅ 显式接口          ✅ 显式接口          ❌ 语法糖不可导出
IDE 可发现性        方法名不统一         ✅ capture.xxx       ✅                  ⚠️ 属性掩盖能力
单测                难                  ✅ 直接测 param      ✅ 但文件多          ⚠️ delegate 难拆
继承 vs 组合        —                   继承（可控）         组合（文件爆炸）     委托（跨端风险）
```

---

## 7. 决策框架：为什么是这个顺序

架构选择的依据不是「哪个模式更优雅」，而是：

> **哪个模式在 KMP + 双端 Callback 桥接 + ABI 稳定约束下可持续演进 5 年**

对 CameraParam 而言，决策权重如下：

```
                    权重
对外 API 可发现性     ████████████  最高 —— 第三方一周接入
ABI / OC 兼容        ███████████   高 —— 不能依赖 Kotlin 语法糖
开闭原则（零接口变更） ██████████    高 —— 35+ 参数持续演进
DRY / 维护成本       █████████     中高 —— 公共逻辑只写一次
继承深度             ████          低 —— 可接受的代价
```

**Template Method 在最高权重的三项上全胜**，继承变深是唯一可接受的 trade-off。

---

## 8. 与整体架构的关系

CameraParam 不是孤立设计，它嵌在六层架构的 **api + service** 层：

```
CameraDevice（聚合根）
  └── capture: CameraCapture
        ├── exposureISO: CameraParam<Int>     ← 本文讨论的抽象
        ├── shutterSpeed: CameraParam<Long>
        └── startCapture() / stopCapture()
              ↓
        CameraCaptureService（service 层，KMP 共享实现）
              ↓
        DeviceCore → UnifiedTransport → Native C++ SDK
```

事件变化通过 `SharedFlow<DeviceEvent>` 在内部传播，`CameraParam` 的 listener 从事件总线订阅自己关心的参数变更——**对外仍是 Listener 接口，不暴露 Flow**。

---

## 9. 已知不足与后续改进

> 详细分析见 [已知不足与后续改进.md](./已知不足与后续改进.md)。

当前实现中 AI Code Review 发现的问题（白皮书 6.2 节）：

| 问题 | 严重程度 | 计划 |
|------|----------|------|
| `CameraParam<T>` 的 listeners 用 `LinkedHashSet` 裸用，没加锁 | Major | 纳入 `ListenerRegistry<T>` 统一改造 |
| `CameraSystem` 仍有 100+ 方法未归一 | Major | 下一版本按子域拆分或推广 `CameraParam<T>` |

---

## 10. 面试 30 秒版

> AI 给了三个 CameraParam 实现方案。我否决 Delegate，因为 35 个参数会变成 100+ 个几乎相同的文件；否决 Property Delegate，因为 OC 接不了、KMP 导出也不稳。最终选 Template Method：`CameraParamImpl` 把公共逻辑写一次，每个参数只实现 4 个 hook，对外 `device.capture.exposureISO.setValue(800)` 可发现、可补全、双端一致，新增参数零接口变更。

---

## 相关文档

- [AI驱动的大规模KMP-SDK重构实践-技术白皮书.md](./AI驱动的大规模KMP-SDK重构实践-技术白皮书.md) — 3.3 / 4.2 节
- [预备-KMP-Json配置化重构.md](./预备-KMP-Json配置化重构.md) — 2.3 CameraCaptureService 范本
