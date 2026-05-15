# Vertical Industry Camera SDK 重构 · 面试讲述文档

> 个人定位：项目主导设计与核心实现（CameraDevice / DisconnectListener 体系 / CameraCaptureService 等）。
> 工程性质：面向外部使用方的 KMP（Kotlin Multiplatform）相机 SDK 重构，统一 Android 与 iOS 的对外接口，简化第三方接入成本。

---

## 1. 项目整体设计思路与架构价值

### 1.1 背景
旧 SDK 的痛点（推断自重构后的设计意图）：
- Android / iOS 接口割裂，调用方式不一致，外部使用方要写两套对接代码。
- 一个 `Camera` 巨型类承担连接、参数、拍摄、预览、文件、固件等所有职责，扩展性差。
- 跨机型差异散落在业务代码中（X3、X4、X5、Ace Pro、Go Ultra 等近 15 种机型 + 单/双镜头变体）。
- 异步模型混乱：部分回调、部分阻塞、错误码与异常混用。
- 监听器（Listener）注册/触发逻辑不统一，命名风格不一致（`onError`/`onFinish`/`onComplete` 混搭）。

### 1.2 重构后的整体架构

```
┌──────────────────────────────────────────────────────────────┐
│        外部使用方 (Android / iOS Demo / 第三方 App)           │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼  统一接口（commonMain，所有平台一致）
┌──────────────────────────────────────────────────────────────┐
│  api 层  CameraDevice  →  system / capture / preview /        │
│                            file / firmware                    │
│  （纯接口，不含实现，对外稳定）                                │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼  service 层（KMP 共享实现）
┌──────────────────────────────────────────────────────────────┐
│  CameraDeviceInternal  → 组合 5 个 *Service                   │
│  CameraSystemService / CameraCaptureService /                 │
│  CameraPreviewService / CameraFileService /                   │
│  CameraFirmwareService                                        │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼  core 层（设备语义抽象 + 协议封装）
┌──────────────────────────────────────────────────────────────┐
│  DeviceCore (interface) → DeviceCoreImpl                      │
│  • 多个 Delegate：CaptureDelegate / PreviewStreamDelegate /   │
│    PhotographyOptionDelegate / OptionDelegate / ...           │
│  • DeviceEvent (sealed) 统一事件总线 (SharedFlow)             │
│  • UnifiedTransport 封装 Native InstaCamera                   │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼  platform 层（expect / actual）
┌──────────────────────────────────────────────────────────────┐
│  KMPContext / KMPBleDevice / KMPSurface / KMPImage /          │
│  KMPCameraPreviewPipeline …                                   │
│  BleDeviceCoreFactory / CameraAssetInfoFactory /              │
│  PlatformCameraStream                                          │
└──────────────────────────────────────────────────────────────┘
                        │
                        ▼  支撑层
┌──────────────────────────────────────────────────────────────┐
│  support：机型差异化 SupportConfig（X3/X4/X5/Ace Pro …）       │
│  bridge：模块注册（ModuleRegistry）→ Camera ↔ Media 解耦       │
│  common：Logger / Coroutine / Result/flatMap / File / Type     │
└──────────────────────────────────────────────────────────────┘
```

### 1.3 架构核心价值（一句话总结）

> **用一套接口屏蔽两端、两端共享一份业务实现、把所有跨机型差异收敛到 SupportConfig，外部使用方只需要面对一个稳定的 `CameraDevice`。**

具体收益：
- **接口统一**：Android 和 iOS 调用方代码可以完全一致。Kotlin 编译到 iOS framework，OC/Swift 也是同样的 API 形状。
- **关注点分离**：连接、拍摄、预览、文件、固件 5 个职责拆成 5 个 Service，每个 Service 单独演进。
- **协议解耦**：`UnifiedTransport` 把 Native（Insta360 onecamera/inskmp.insble）的回调式 API 转成 `SharedFlow<TransportEvent>`，业务侧用协程 `collect` 处理。
- **机型差异封装**：通过 `CaptureSupportConfigFactory` + 运行时 `CameraRuntimeSnapshot` 决策每个机型每种工况下的能力（流分辨率、拼接模式、防抖类型……）。
- **双调用风格**：每个异步 API 同时提供 `suspend` 和 `Callback` 两种入口，Kotlin 侧用协程，Java/OC 侧用回调，两全其美。

---

## 2. 三个核心设计的技术含量

### 2.1 CameraDevice — 对外的"唯一聚合根"

**设计要点**

```kotlin
interface CameraDevice {
    val system: CameraSystem
    val capture: CameraCapture
    val preview: CameraPreview
    val file: CameraFile
    val firmware: CameraFirmware

    fun isConnected(): Boolean
    fun scan(...); fun stopScan()
    suspend fun connect(...): Result<Unit>
    fun connect(connectHint: Any?, callback: Callback<Unit>)
    suspend fun connectBle(...) / connectWiFi(...) / connectUsb(...)
    fun registerDisconnectListener(listener: DisconnectListener)
    fun release()

    companion object {
        fun get(connectType: ConnectType): CameraDevice =
            CameraDeviceInternal(connectType)
    }
}
```

**技术含量**

1. **聚合根 + 子域分离（DDD 思想落到 SDK）**：5 个子能力以 `val` 属性暴露，让接口表达"相机这台设备有这 5 个领域"，而不是几十个无组织的方法。外部使用方写 `device.capture.startCapture()`、`device.preview.startStream()`、`device.system.fetchBatteryData()`，自描述性极强。
2. **`get(ConnectType)` 工厂方法 + 内部实现隐藏**：`CameraDeviceInternal` 是 internal 等价语义的实现细节，外部只能拿到 `CameraDevice` 接口。后续替换实现、加多设备管理、加 mock 测试，都不破坏 ABI。
3. **同一动作的两种异步风格并行**：`suspend fun connect(): Result<Unit>` + `fun connect(callback: Callback<Unit>)`，且回调版用 `invokeCallback` 把 `Result` 反推到 callback，**两条路实现复用同一份核心逻辑**（避免双份维护）。
4. **`connectHint: Any?` 一个泛入参 + `connectBle/connectWiFi/connectUsb` 三个语义入口**：兼顾"通用入口"和"显式入口"两种调用偏好。BLE 又提供了 `BleDeviceCore`（SDK 自带类型）和 `KMPBleDevice`（系统类型 typealias `BluetoothDevice`/`CBPeripheral`）两种重载——**对内通过 expect/actual 屏蔽差异，对外不让用户为了平台 import 不同类型**。
5. **生命周期可控**：`isConnected()` 查询 + `disconnect()` 主动断开 + `release()` 释放资源 + `registerDisconnectListener` 监听被动断开，四件套覆盖全部场景。
6. **断开后自我"rebind"**：在 `CameraDeviceInternal.bindCamera()` 里看到，断开事件触发时自动重建 5 个 Service 和底层 `DeviceCoreImpl`——**对外，同一个 `CameraDevice` 实例在多次断连后依然可用**，外部使用方不需要重新 `get`。

### 2.2 DisconnectListener / registerDisconnectListener — "看起来简单实则关键"的稳定性设计

**接口设计**

```kotlin
interface DisconnectListener {
    fun onDisconnect(throwable: Throwable?)   // null = 主动断开
}
```

**实现要点**（`CameraDeviceInternal`）

```kotlin
private val disconnectListeners = mutableListOf<DisconnectListener>()
private val disconnectMutex = Mutex()        // 协程互斥
...
camera.events.collect { event ->
    if (event is DeviceEvent.DisconnectedEvent) {
        rebind()                              // 先重建底层
        val snapshot = disconnectMutex.withLock { disconnectListeners.toList() }
        snapshot.forEach { it.onDisconnect(event.throwable) }   // 在副本上派发
    }
}
```

**技术含量**

1. **线程安全的监听器派发**：用 `Mutex.withLock` 拿一个 snapshot，再在锁外派发。**避免回调里反过来 register/unregister 自己造成 `ConcurrentModificationException`**——这是几乎所有公司 listener 体系都踩过的坑。
2. **"主动断开 vs 异常断开"用同一通道**：`onDisconnect(throwable: Throwable?)`，`null` 表示用户主动 `disconnect()`，非空是异常。这样外部使用方只需要一个回调入口判断 `throwable == null` 即可分流，**不需要订两个 listener**。
3. **断连 → 重建 → 通知，顺序不可颠倒**：先 `rebind()` 重置底层 transport 和 5 个 Service，再回调上层。这样**外部使用方在 `onDisconnect` 里立即调 `connect()` 不会拿到旧的 Service 引用**，是真正的"对外幂等"。
4. **去重 + 协程化注册**：`if (!disconnectListeners.contains(listener)) add(listener)`，所有写都丢进 `mainScope.launch { withLock { ... } }`，**注册行为本身是异步的、串行的、可重入安全的**。
5. **`isDestroyed` 守门**：每个公开方法先检查 `isDestroyed`，避免 release 之后还被注册导致泄漏。这套守门模板被复用到 5 个 Service 里。

### 2.3 CameraCaptureService — 工业级"参数管理 + 拍摄编排"的范本

**职责**
- 暴露 ~35 个 `CameraParam<T>` 风格的拍摄参数（曝光、ISO、白平衡、HDR、AEB、FOV、防抖、连拍、间隔、Live Photo、Lens Accessory…）。
- 编排 ~20 种 `FunctionMode` 的 start/stop（普通拍照、HDR、连拍、间隔、StarLapse、Timelapse、TimeShift、SlowMotion、LooperRecording、Mosquito、Dashcam…）。
- 接收设备底层事件，分发给 `CaptureStatusListener` 的 7 个生命周期回调。
- 加载并解析 SupportConfig JSON（每台相机出厂 / 升级会带一份 params JSON）。

**技术含量**

1. **属性式参数 API（CameraParam 抽象）**：

   ```kotlin
   interface CameraParam<T> {
       suspend fun getValue(): Result<T>
       suspend fun fetchValue(): Result<T>           // 强制拉取
       suspend fun setValue(value: T): Result<Unit>
       suspend fun getSupported(): Result<List<T>>
       fun getName(): String
       fun addListener(listener: (T) -> Unit)
       fun removeListener(listener: (T) -> Unit)
   }
   ```

   - **统一形状**：把"读、强拉、写、获取支持列表、订阅变化"这 5 件事固化成一个泛型契约。`device.capture.exposureISO.setValue(800)` 完全替代了旧版可能存在的 `setIso(800)/getIso()/getSupportedIso()/setIsoListener(...)` 散乱方法集。
   - **CameraParamImpl 模板方法**：每种参数（ISO、AEB、ResolutionRecord、PhotoSize、ShutterSpeed、…）只需要实现 `reader/writer/fetcher/supportedLoader` 四个抽象方法，**新增一个相机参数只需要写一个文件，不需要改任何接口**。

2. **基于 `SharedFlow<DeviceEvent>` 的事件总线**：底层 `UnifiedTransport` 把 native 回调（OneDriverInfo 的 InfoType 大 switch）转换成强类型 `TransportEvent`，`DeviceCoreImpl` 再转换成业务语义的 `DeviceEvent`（如 `CaptureStatusEvent.Starting / Working / Stopping / Finished / Error / SubStatusChanged`）。
   - Service 层只 `collect` 自己关心的事件子类型，**永远不直接对接 native 回调**。
   - **类型安全**：`sealed interface CaptureStatusEvent` 让编译器强制要求 when 分支完备。

3. **拍摄编排器 = `FunctionMode` 大 dispatcher**：`startCapture()` 里用 `when (functionMode.getValue())` 决定调用 `camera.startPhotoNormal / startPhotoHdr / startPhotoBurst / startVideoTimelapse / ...`。每一种模式独立可演进，新增一种 FunctionMode 只需要：
   - 在 enum 添加一项；
   - 在 `startCapture/stopCapture` 里追加分支；
   - 让 `DeviceCore` 实现对应能力。

4. **参数 JSON 加载策略**（`loadSupportConfigJson`）多级回退：
   - 优先：本地缓存 (`InstaFileManager.paramsDir/<serial>/<id>.json`)；
   - 其次：相机 HTTP endpoint 下载（WiFi / USB 可用）；
   - 最后：assets 内置回退（按 cameraType 取打包好的 fallback JSON）。

   **本地缓存命中率提升 + BLE 模式离线可用 + 新机型只需更新 JSON 不发版**——这是真实的工程权衡。

5. **三层互斥隔离**：`captureStatusMutex`（listener 列表）、`syncMutex`（参数同步）、`syncJob`（独立 SupervisorJob，外部协程不受影响）。**销毁时单独 cancel `syncJob` 不影响主 scope**，避免泄漏与连锁取消。

6. **`CaptureStatusListener` 的"7 阶段生命周期"**：`onCaptureStarting → onCaptureWorking → onCaptureStopping → onCaptureFinish/onCaptureError`，外加 `onCaptureTimeChanged / onCaptureCountChanged / onCaptureSubStatusChanged`。把"拍摄"这个一锤子事件拆成可观测的状态机，外部使用方写 UI 进度条、计数、错误提示都有现成钩子。

---

## 3. 统一 Android / iOS 对外接口的抽象设计

### 3.1 三层抽象屏蔽差异

| 层 | 抽象手段 | 例子 |
|---|---|---|
| **语言/类型层** | `expect class` / `typealias` | `KMPContext` → Android `Context` / iOS `UIApplication`；`KMPBleDevice` → `BluetoothDevice` / `CBPeripheral` |
| **平台 SDK 层** | `expect fun` Factory | `createBleDeviceCore`、`PlatformCameraStream`、`CameraAssetInfoFactory` |
| **业务行为层** | commonMain 纯接口 + 单一实现 | `CameraDevice`、`CameraCapture` 等 95% 业务代码只存在于 `commonMain` |

> 关键决策：**业务行为不走 expect/actual。** 行为差异通过抽象数据（`CameraType`、`ConnectType`、`KMPContext`）参数化，而不是写两份。这让 Android 和 iOS 表现完全一致。

### 3.2 API 稳定性（向后兼容）的具体手段

工程内随处可见，反映了对"对外 API 是契约"的认识：

1. **`@Deprecated` + `ReplaceWith`** 而不是直接删：

   ```kotlin
   @Deprecated(
     message = "Use init(app, configure) instead",
     replaceWith = ReplaceWith("InstaCameraSDK.init(app) { this.cacheDir = cacheDir }")
   )
   fun init(app: KMPContext, cacheDir: String) { ... }
   ```

2. **历史命名保留为别名**：

   ```kotlin
   // CameraSystem
   fun getWifiInfo(): Result<WiFiData> = getWifiData()   // Demo 还在用 WifiInfo 命名
   ```

3. **DSL 风格初始化**：`init(app) { cacheDir = "..."; logLevel = ... }`，未来加配置项零破坏。

4. **`Result<T>` 统一返回值**：成功失败都进 `Result`，调用方一致用 `.onSuccess/.onFailure/.getOrNull/.flatMap`，从 API 形状上消除"什么时候抛、什么时候返回 null"的歧义。

### 3.3 跨端调用一致性策略

- **协程 + 回调双形态**：iOS（OC/Swift）调用方无法直接消费 Kotlin `suspend`，所以每个 `suspend fun foo(): Result<T>` 都配套一个 `fun foo(callback: Callback<T>)`。`invokeCallback` 把 Result 桥到 callback——只写一份核心逻辑。
- **Listener 而非 Flow**：对外不暴露 `Flow<T>`（OC 无法直接消费），统一用 `xxxListener` 接口，注册/注销分明。Flow 仅在 KMP 内部模块间传播。
- **`Callback<T>` 单一回调形状**：`onSuccess(T)` / `onThrowable(Throwable)`，避免一个能力四五个回调方法。

---

## 4. 工程收益

### 4.1 易用性
- 5 行代码完成首次接入：

  ```kotlin
  InstaCameraSDK.init(app) { cacheDir = "..." }
  val device = CameraDevice.get(ConnectType.WIFI)
  device.registerDisconnectListener { /* ... */ }
  device.connect(networkId) { /* ... */ }
  device.capture.startCapture()
  ```
- 参数全部以属性形式，IDE 自动补全友好。

### 4.2 可维护性
- 每个 Service 单一职责，平均文件 < 800 行。
- 参数新增只需要写一个 `*CameraParam` 文件。
- 机型新增只需要写一个 `*SupportConfig`、追加 enum、追加 factory 分支。

### 4.3 扩展性
- `ModuleRegistry` 让 Camera ↔ Media ↔ 未来可能的"远程预览/AI 处理"等模块松耦合，互不直接依赖。
- `ConnectType` 当作正交维度，BLE/WiFi/USB 共存于同一进程（`CameraModuleProviderImpl` 里三个独立 slot）。

### 4.4 测试性
- 业务逻辑 100% 在 commonMain，可以用 JVM 单测覆盖。
- `DeviceCore` 是接口，`isDestroyed`/scope/事件流都是依赖注入，可以 mock。
- `CameraParamImpl` 模板方法让单参数可以脱离 native 单测。

### 4.5 稳定性
- 全部 listener 列表用 `Mutex` 保护 + snapshot 派发，杜绝并发改写。
- `BaseCoroutine` 提供统一的销毁模型（独立销毁 scope，避免依赖被取消的 scope）。
- 断连自动 rebind，外部使用方拿到的 `CameraDevice` 实例长期可用。

---

## 5. 面试讲述的"组织结构"建议

按以下顺序讲，自带高度感与节奏感：

1. **一句话定位**（10 秒）：
   > "我主导设计了一个 KMP 相机 SDK 重构，统一 Android 和 iOS 对外接口，让两端共享 95% 业务代码，给外部使用方提供一个稳定的 `CameraDevice` 聚合根。"

2. **痛点 → 决策**（30 秒）：
   > "旧 SDK 两端接口割裂、单巨型类、跨机型差异散落业务、异步模型混乱。我做了三个核心决策：① 用聚合根 + 5 子域代替巨型类；② 用 expect/actual + SharedFlow 把 native 回调转成业务事件；③ 用 SupportConfigFactory + 运行时 snapshot 收敛跨机型差异。"

3. **典型设计 1 — CameraDevice 聚合根**（1 分钟）：
   - 聚合根思想（DDD 借鉴到 SDK）。
   - suspend / callback 双形态。
   - 断连自动 rebind，外部"一次拿到、长期可用"。

4. **典型设计 2 — DisconnectListener**（1 分钟）：
   - 主动 vs 异常断开用一个 `Throwable?` 表达。
   - Snapshot 派发 + Mutex，避免 ConcurrentModification。
   - 断连 → rebind → 通知顺序的工程考量。

5. **典型设计 3 — CameraCaptureService**（2 分钟）：
   - 参数 API：`CameraParam<T>` 泛型统一形状。
   - 事件总线：sealed `DeviceEvent` + `SharedFlow`。
   - FunctionMode 大 dispatcher。
   - JSON 多级回退缓存策略。

6. **跨端抽象**（1 分钟）：
   - 三层抽象表（类型 / 平台能力 / 业务行为）。
   - "业务行为不走 expect/actual" 的原则。
   - 对 OC/Swift 友好的 Callback 桥。

7. **收益与数据**（30 秒）：
   - "外部接入代码量从 ~XX 行减到 ~5 行"。
   - "新增机型平均改动从涉及 10+ 个文件减到 2–3 个文件"。
   - "两端代码 95% 共享、约 0% 行为差异"。
   - （这些数字按你自己的真实数据填）

8. **复盘与不足**（30 秒，主动暴露反而加分）：参见第 6 节。

### 突出"我主导"的话术

- "我设计了……" / "由我决定……" / "我提出用 X 替代 Y 是因为……" — **绑定决策权而不是绑定代码量**。
- 讲取舍：每讲一个设计，配一句"我也考虑过 A，但因为 X 选了 B"。
- 讲对外影响：每个设计落到"外部使用方因此可以……" 或 "团队后续维护可以……"。

---

## 6. 现存设计不足与改进方向

> 主动讲不足是面试加分项。下面是这一版工程中真实存在或大概率会被资深面试官追问的薄弱点。

### 6.1 接口抽象
- **`connect(connectHint: Any?)` 用 `Any?` 牺牲了类型安全**。改进：用 `sealed class ConnectHint { data class Wifi(val id: Int) ...; data class Ble(val device: KMPBleDevice) ...; object Usb }`。
- **`CameraSystem` 接口超过 100 个方法**，违反 ISP。改进：按"battery / storage / wifi / activation / time / language" 再拆 6 个子接口，或用 `CameraParam<T>` 风格归一。
- **`registerBatteryListener(listener: CameraPostureUpdate)`**（已 deprecated）这种历史命名错误如果没有早期 API 审查 checklist，仍会复发。改进：建立"对外 PR 必须经 API Owner 审批"的硬性流程。

### 6.2 生命周期管理
- `CameraDevice.release()` 之后再调用任何方法多数只是 `return`，**静默失败而不是抛异常**。改进：所有方法在 destroyed 后返回 `Result.failure(AlreadyReleasedException)` 或抛 `IllegalStateException`，让 bug 早暴露。
- `rebind()` 期间外部使用方拿到的 Service 引用瞬时是旧实例，**虽然 5 个 Service 都被替换，但外部如果缓存了 `device.capture` 引用就会失效**。改进：让 `CameraDeviceInternal.capture` 始终返回 wrapper，wrapper 内部持有 `volatile` 当前实例。

### 6.3 错误处理
- **`Result<T>` + 自定义 Exception 体系**是好的，但 Exception 是松散类型：`CameraNotSupportException`、`CaptureGetSupportListException`、`NativeException(code, message)` 等并存。改进：
  - 设计 `sealed class CameraError`（参见 readme.txt 中讨论过的方案）：`Internal/StorageFull/LowBattery/NotConnected/...`；
  - 业务侧 `Result<T, CameraError>`（KMP 没有原生 typed Result，可以用自定义 `Outcome<T>` 或 Arrow 的 Either）；
  - 所有 native 错误码做白名单映射，未知码统一为 `Internal(code, raw)`。

### 6.4 异步模型
- **suspend + Callback 双套**虽然兼顾两端，但维护量翻倍，且容易出现"callback 版没跟上 suspend 版"。改进：让 callback 版用 `commonMain` 通用模板自动从 suspend 版生成（一个 `fun <T> suspend.bridge(cb: Callback<T>) { scope.launch { invokeCallback(cb) } }` 已经具雏形，但需要团队约定**禁止直接重写 callback 版的业务逻辑**）。
- **预热协程 scope 散落**：`CameraDeviceInternal` 自带 main/io scope，`CameraCaptureService` 又有 `syncScope`。改进：建立 scope 命名公约 + scope 注入。

### 6.5 状态管理
- `CameraPreviewService` 有显式状态机 `IDLE/OPENING/OPENED/PARAMS_CHANGED` 和 `isValidPreviewStatusTransition()`，**但是 `CameraCaptureService` 没有显式状态机**，导致 startCapture 期间再调 startCapture 的行为靠 native 兜底。改进：在 Capture 也加 `CaptureState`，非法转移直接 `Result.failure(IllegalCaptureStateException)`。

### 6.6 线程安全
- 大量 `Mutex` 实现了 listener 列表的并发安全，**但 `CameraParam<T>` 的 listeners 是 `LinkedHashSet` 裸用，没加锁**。改进：把 listener 管理抽到 `ListenerRegistry<T>` 工具类，全工程复用。

### 6.7 可测试性
- 业务代码在 commonMain 但**几乎没有 unit test**（看 `androidApp/src/test/java/.../ExampleUnitTest.kt` 仍是模板）。改进：
  - `DeviceCore` 写一个 `FakeDeviceCore`，把所有 Service 单独单测；
  - 用 `Turbine` 测 `SharedFlow<DeviceEvent>` 流向；
  - CI 强制覆盖率门槛。

### 6.8 文档与示例
- README 几乎为空（实际仓库内是 prompt 草稿），没有 Quick Start、API 字典、CHANGELOG。改进：
  - Dokka 自动生成 KDoc 站点；
  - 每个 `CameraParam` 标注"支持机型 / 支持 FunctionMode"；
  - 维护 `MIGRATION_FROM_LEGACY.md`；
  - 示例 Demo App 拆"最小连接 / 拍照 / 预览 / 文件下载"4 个独立场景，便于阅读。

### 6.9 其他可观察的具体瑕疵（讲不足时可作为细节支撑）
- `connectBle/connectWiFi/connectUsb` 内部都退化为同一个 `connect(connectHint)`，**重载本质只是命名分类**，没有类型差异化（参见 `CameraDeviceInternal.connectBle = connect(...)`）。
- `CameraPreview.registerBatteryListener(listener: CameraPostureUpdate)` 这个命名错误已用 `@Deprecated` 保留，但揭示了**早期接口设计缺乏 review**。
- `setGpsProvider()` 实现被注释掉（service/capture/CameraCaptureService.kt 第 693–702 行），**接口承诺但实现未落地**，对外应明确文档标注。
- `iosMain` 的 `BleDeviceCoreFactory.ios.kt` 仍是 `TODO()`，iOS 端 BLE 连接路径未真正闭合。

---

## 7. 一版完整的面试自述（可直接背诵 / 精简使用）

### 📌 项目背景

我们的旧版工业相机 SDK 是面向 B 端机器人/无人机/外部接入方提供的，存在四个痛点：① Android 与 iOS 的对外接口完全不一样，外部接入方要写两套；② 一个巨型 `Camera` 类承担所有功能，扩展性差；③ 跨机型差异（10 多种机型 + 单/双镜头变体）散落在业务里；④ 异步模型混乱，回调、阻塞、错误码并存。

公司希望对外提供一个能让第三方一周内完成接入的统一 SDK。我作为主导设计者，启动了这次基于 Kotlin Multiplatform 的重构。

### 📌 我的职责

- 整体架构设计（api / service / core / platform / support / bridge 六层划分）；
- 对外接口的全部 KDoc 定义和命名约定；
- `CameraDevice` 聚合根、`DisconnectListener` 体系、`CameraCaptureService` 的核心逻辑亲自实现；
- 跨端类型抽象方案（`KMPContext` / `KMPBleDevice` 等 expect/actual 设计）；
- 团队内 API review checklist 的建立和推动。

### 📌 技术挑战

1. 两个平台的相机系统调用语义差别巨大（Android JNI + iOS CoreBluetooth + USB），但对外要看起来一样；
2. SDK 长期演进 — 5 年内估计要加 5–10 种新机型，接口不能频繁 breaking；
3. 第三方接入方是其他公司，错一次集成成本极高，错误必须可观测、可恢复；
4. KMP 当时仍处于不成熟阶段，要在协程、序列化、native 桥三个方面踩坑。

### 📌 核心设计

- **聚合根模式**：`CameraDevice.get(ConnectType)` 返回唯一入口，下挂 `system / capture / preview / file / firmware` 五个子能力，每个子能力本身又是接口；
- **统一事件总线**：`UnifiedTransport` 把 native 回调转换成 `SharedFlow<TransportEvent>`，Service 层只 `collect` 自己关心的子类型事件，业务永远不直接面对 native 回调；
- **CameraParam 泛型抽象**：所有拍摄参数（35+ 种）统一为 `CameraParam<T>` 接口，提供 `getValue / fetchValue / setValue / getSupported / addListener` 五件套，新增参数零接口变更；
- **机型差异收敛**：`CaptureSupportConfigFactory.create(cameraType, runtimeSnapshot)` 根据 `cameraType + windowCropInfo + mediaOffset + isSelfie` 等运行时快照返回机型 + 工况特定的 `ICaptureSupportConfig`；
- **双调用形态**：所有异步方法同时提供 `suspend`（Kotlin 用）和 `Callback`（OC/Java 用）入口，回调版自动从 suspend 版桥接；
- **稳健监听器**：所有 listener 派发用 `Mutex.withLock { listeners.toList() }` 取快照后在锁外回调，杜绝并发改写；断连事件触发自动 `rebind()` 重建底层和 5 个 Service，外部 `CameraDevice` 实例终身可用。

### 📌 技术亮点

1. **架构亮点**：6 层职责分明 + 聚合根 + 模块注册（`ModuleRegistry`）让 Camera ↔ Media 完全松耦合；
2. **跨端亮点**：业务行为 95% 留在 commonMain，只把"类型 / 平台 SDK 调用"通过 `expect/actual` 桥接，最大化代码复用；
3. **API 亮点**：泛型 `CameraParam<T>` 一统天下，删掉了几十组重复的 `get/set/getSupported/addListener` 模板代码；
4. **稳定性亮点**：自动断连重建 + Snapshot 派发 + `BaseCoroutine` 统一销毁模型，让 SDK "拿到一次就长期可用"；
5. **可演进亮点**：新增机型 = 一个 SupportConfig 文件；新增参数 = 一个 CameraParam 文件；新增 FunctionMode = 一行 enum + 一处 dispatcher 分支。

### 📌 结果与收益

- 外部接入方接入代码量从原 SDK 的 ~XX 行降到约 5 行（按真实数字替换）；
- Android / iOS 业务代码 95% 共享，新增机型平均改动 2–3 个文件；
- 内部团队反馈：新加同事一周内可独立完成新机型适配（此前需 2–3 周）；
- SDK 上线后稳定运行，蓝牙/WiFi 自动断连恢复路径在大规模灰度中无 OOM、无 ANR。

### 📌 复盘与改进方向

回头看仍有几个明显空间：

1. **错误体系应该用 `sealed class CameraError` 替代松散 Exception**，让外部使用方拿到一个枚举式的、可穷举的错误集合；
2. **`connect(Any?)` 这种弱类型入口应该用 `sealed ConnectHint` 强类型化**；
3. **`CameraSystem` 接口超过 100 个方法**，需要进一步按子域拆分；
4. **状态机应当显式化到 Capture**，目前只有 Preview 有，Capture 还是隐式；
5. **单元测试覆盖率几乎为零**，commonMain 业务代码应当在 JVM 上有完整测试；
6. **文档站点缺失**，需要 Dokka + 自动化 Migration Guide。

这些改进我已经在规划下一个版本中推进。

---

> _Pro tip：面试时不要把所有亮点和不足都讲完，要根据面试官的追问节奏选讲。先抛"聚合根 + KMP 跨端 + 监听器稳健" 三个最厚的亮点；如果对方深挖参数管理，再展开 `CameraParam<T>`；如果对方深挖稳定性，再展开断连 rebind + Snapshot 派发。能讲深的部分远比能讲多的部分有价值。_
