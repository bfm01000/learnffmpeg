# AI 驱动的大规模 KMP SDK 重构实践

> **作者定位**：Vertical Industry Camera SDK KMP 重构主导设计者  
> **文档用途**：高级工程师面试展示 · 技术分享 PPT 素材 · AI Engineering 能力证明  
> **核心命题**：AI 不是代码生成器，而是贯穿软件研发生命周期的工程放大器。

---

# 1. 背景：为什么这个项目适合 AI 驱动开发

## 1.1 KMP SDK 重构背景

我们面向 B 端机器人、无人机及第三方接入方，提供工业级全景相机 SDK。旧版 SDK 基于 C++ 核心 + Android JNI / iOS OC 双端封装，长期演进后积累了四个结构性问题：

| 痛点 | 具体表现 |
|------|----------|
| 接口割裂 | Android 与 iOS 对外 API 形状不一致，第三方需维护两套接入代码 |
| 巨型类 | 单一 `Camera` 类承担连接、参数、拍摄、预览、文件、固件全部职责 |
| 机型散落 | X3/X4/X5/Ace Pro/Go Ultra 等 15+ 机型 + 单/双镜头变体，差异散落在业务代码中 |
| 异步混乱 | 回调、阻塞、错误码、异常混用；Listener 命名风格不统一 |

公司目标明确：**对外提供一套统一 SDK，让第三方在一周内完成接入**。技术选型落在 Kotlin Multiplatform（KMP）——用 `commonMain` 共享业务逻辑，用 `expect/actual` 桥接平台差异，编译为 Android AAR 与 iOS Framework。

## 1.2 大型 SDK 重构的真正成本

大型工程重构的瓶颈从来不是"写代码"，而是：

```
理解大量历史代码  →  建立系统模型  →  设计合理抽象  →  控制迁移风险
```

以本项目为例：

- **历史代码体量**：底层 C++ Camera SDK（Insta360 onecamera/inskmp.insble）+ 双端封装层，涉及连接（BLE/WiFi/USB）、35+ 拍摄参数、20+ 拍摄模式、预览 Pipeline、文件传输、固件升级等子域。
- **跨平台语义鸿沟**：Android 用 JNI + `BluetoothDevice`，iOS 用 CoreBluetooth + `CBPeripheral`，但对外必须呈现同一套 API 形状。
- **ABI 兼容性约束**：SDK 面向外部第三方，任何 breaking change 都意味着客户集成成本；`@Deprecated` + `ReplaceWith` 是常态而非例外。
- **机型持续演进**：5 年内预计新增 5–10 种机型，接口不能频繁 breaking，差异必须收敛到配置层。

## 1.3 为什么大型 SDK 重构特别适合 AI 辅助

AI 在以下环节能显著降低认知成本，而工程师的判断力在关键决策点不可替代：

| 环节 | AI 可承担 | 工程师必须承担 |
|------|-----------|----------------|
| 历史代码理解 | 快速扫描、归纳接口模式、生成调用关系图 | 判断哪些模式是"技术债"、哪些是"领域约束" |
| 架构探索 | 提供 DDD / 分层 / 事件驱动等候选方案 | 结合 KMP 特性、ABI 约束、团队能力做 Trade-off |
| 机械迁移 | 模板化代码生成、Adapter 编写、兼容层 | 保证架构一致性、边界条件、线程安全 |
| Review 覆盖 | 从 SOLID、并发、生命周期等维度做初步审查 | 采纳/拒绝判断、Native 生命周期、线上场景验证 |
| 文档维护 | KDoc、Migration Guide、API Example | 对外 API 契约的最终定义与审批 |

**核心观点**：AI 扩大候选方案空间，工程师缩小到唯一正确决策。

---

# 2. 项目介绍

## 2.1 项目概述

**Vertical Industry Camera SDK KMP 重构**——将原有 C++ 双端封装 SDK 重构为 Kotlin Multiplatform 统一 SDK，对外暴露稳定的 `CameraDevice` 聚合根，Android / iOS 共享约 95% 业务代码。

## 2.2 技术栈

| 层次 | 技术 |
|------|------|
| 跨平台框架 | Kotlin Multiplatform |
| 异步模型 | Kotlin Coroutine + `SharedFlow` |
| 平台桥接 | `expect` / `actual` |
| Android 侧 | JNI → C++ Camera SDK |
| iOS 侧 | Framework → C++ Camera SDK |
| 底层协议 | Insta360 onecamera / inskmp.insble |

## 2.3 六层架构

```
api        →  CameraDevice / CameraSystem / CameraCapture / CameraPreview / CameraFile / CameraFirmware
service    →  CameraDeviceInternal + 5 个 *Service（KMP 共享实现）
core       →  DeviceCore / UnifiedTransport / DeviceEvent（sealed）/ Delegate 体系
platform   →  KMPContext / KMPBleDevice / KMPSurface / expect/actual Factory
support    →  CaptureSupportConfigFactory + 机型差异化 JSON 配置
bridge     →  ModuleRegistry（Camera ↔ Media 解耦）
```

**一句话总结**：用一套接口屏蔽两端、两端共享一份业务实现、把所有跨机型差异收敛到 SupportConfig，外部使用方只需要面对一个稳定的 `CameraDevice`。

## 2.4 重构目标

- **统一 Android / iOS 接口**：`commonMain` 定义全部对外 API，编译到 iOS Framework 后 OC/Swift 也是同样的 API 形状。
- **共享 95% 业务代码**：连接编排、参数管理、拍摄状态机、事件分发全部在 `commonMain`。
- **降低第三方接入成本**：5 行代码完成首次接入；参数以属性形式暴露，IDE 自动补全友好。
- **新机型扩展成本最小化**：新增机型 = 一个 SupportConfig 文件 + enum 追加 + factory 分支。

---

# 3. 我的 AI Engineering Workflow

这是全文核心。我将 AI 引入整个软件研发生命周期，形成了一套 **AI-Augmented Engineering Workflow**：

```
需求分析
    ↓
AI Architecture Exploration
    ↓
方案比较（Trade-off Analysis）
    ↓
PoC 验证
    ↓
工程实现
    ↓
AI Code Review
    ↓
测试生成
    ↓
文档生成
```

以下逐阶段说明 **AI 做什么 / 我做什么 / 为什么这样分工**。

---

## 3.1 需求分析

### AI 做什么？

- 扫描旧 SDK 源码，归纳现有 API 清单（连接、参数、拍摄、预览、文件、固件）。
- 识别巨型类的方法分组，生成初步的职责域划分建议。
- 对比业界 KMP SDK 实践（如 Ktor、SQLDelight、Cash App 的 KMP 架构），提供参考模式。

### 我做什么？

- 定义重构边界：哪些能力必须保留、哪些可以废弃、哪些需要 `@Deprecated` 过渡。
- 明确对外契约：第三方接入方的真实调用场景、OC/Swift 调用约束（不能直接消费 `suspend` / `Flow`）。
- 确定非功能性需求：ABI 稳定性、断连恢复、BLE 离线可用、新机型不发版只更新 JSON。

### 为什么这样分工？

需求理解必须建立在对业务背景、客户集成成本、组织技术债的真实认知上。AI 可以快速"读完"代码，但不知道"为什么当初这样写"以及"改了这个客户会不会炸"。

---

## 3.2 AI Architecture Exploration

### AI 做什么？

- 针对"巨型 Camera 类"提供多种拆分方案：
  - 方案 A：按连接类型拆分（BleCamera / WifiCamera / UsbCamera）
  - 方案 B：按职责域拆分（DDD 聚合根 + 子域）
  - 方案 C：按平台拆分（AndroidCamera / IOSCamera，放弃 KMP 共享）
- 分析各方案在 KMP 下的优劣势：代码复用率、`expect/actual` 边界、测试可行性。
- 提供事件总线候选：Callback 链 / RxJava / SharedFlow / Channel。

### 我做什么？

- 根据 KMP 特性否决方案 C（业务行为不应走 `expect/actual`）。
- 根据 SDK 生命周期否决方案 A（连接类型是正交维度，不应与职责域耦合）。
- 选择方案 B 并细化为六层架构（api / service / core / platform / support / bridge）。
- 决定事件总线用 `SharedFlow<DeviceEvent>`——内部模块间传播，对外不暴露 Flow（OC 无法消费）。

### 为什么这样分工？

AI 提供候选空间，工程师负责最终决策。架构选择的依据不是"哪个模式更优雅"，而是"哪个模式在 KMP + 双端 Callback 桥接 + ABI 稳定约束下可持续演进 5 年"。

---

## 3.3 方案比较（Trade-off Analysis）

> 详细代码对照与决策框架见 [CameraParam-方案比较与取舍.md](./CameraParam-方案比较与取舍.md)。

以 **CameraParam 参数抽象** 为例，AI 协助比较了三种实现路径：

| 方案 | 优点 | 缺点 | 决策 |
|------|------|------|------|
| A. Template Method（`CameraParamImpl` 抽象基类） | 新增参数只需实现 4 个方法；形状统一 | 继承层次可能变深 | **采纳** |
| B. Delegate 模式 | 组合优于继承；易 mock | 35+ 参数各自一个 Delegate 类，文件数翻倍 | 否决 |
| C. Property Delegate（`by cameraParam()`） | Kotlin 惯用法，调用侧最简洁 | KMP commonMain 对 property delegate 支持有限；OC 侧无法消费 | 否决 |

**我的判断依据**：SDK 面向外部第三方，API 形状的可发现性（`device.capture.exposureISO.setValue(800)`）比 Kotlin 语法糖更重要；Template Method 让新增参数零接口变更，符合开闭原则。

---

## 3.4 PoC 验证

### AI 做什么？

- 生成 `CameraDevice` 聚合根 + `CameraParam<T>` 的最小可运行 PoC。
- 生成 `UnifiedTransport` → `DeviceEvent` 事件链路的 stub 实现。
- 生成 `expect/actual` 的 `KMPContext` / `KMPBleDevice` 样板代码。

### 我做什么？

- 在真实设备上验证 BLE 连接 → 参数读写 → 拍照 → 断连恢复的完整链路。
- 验证 iOS Framework 编译后 OC 侧能否正确调用 `Callback<T>` 桥接。
- 验证 `SharedFlow` 在 Native 回调线程与协程调度器之间的线程安全。

### 为什么这样分工？

PoC 的价值在于**证伪**，而非**证实**。AI 生成的 PoC 通常忽略 Native 生命周期、JNI 线程 attach、iOS BLE 权限等真实约束——这些只能靠工程师在真机上踩坑。

---

## 3.5 工程实现

### AI 做什么？

- 按架构分层批量生成 Service 实现骨架（`CameraCaptureService` / `CameraPreviewService` 等）。
- 将旧 SDK 的 `getXXX` / `setXXX` / `addXXXListener` 机械迁移为 `CameraParam<T>` 子类。
- 生成 `SupportConfig` JSON 解析逻辑和 factory 分支。
- 生成 `@Deprecated` + `ReplaceWith` 的兼容层。

### 我做什么？

- 亲自实现核心路径：`CameraDeviceInternal`（聚合根 + 断连 rebind）、`DisconnectListener` 体系、`CameraCaptureService` 拍摄编排器。
- 审查所有 AI 生成代码的线程安全、生命周期、错误处理。
- 建立 API Review Checklist，所有对外接口变更必须经过审批。

### 为什么这样分工？

AI 负责大量机械迁移（约 60–70% 的样板代码），我负责保证架构一致性。类比：AI 是施工队，工程师是总建筑师——施工队可以砌墙，但不能决定承重结构。

---

## 3.6 AI Code Review

### AI 做什么？

- 站在 Google Android SDK Reviewer 角度，对每次 PR 做结构化审查（详见第 6 章）。
- 检查 SOLID 违反、并发问题、生命周期泄漏、ABI 兼容风险。

### 我做什么？

- 逐条评估 AI 发现的问题：采纳 / 拒绝 / 延期，并记录理由。
- 补充 AI 无法发现的上下文：Native 回调线程、特定机型的协议 quirks、线上灰度数据。

### 为什么这样分工？

AI Review 是"广度覆盖"，人工 Review 是"深度判断"。两者叠加，覆盖率远超单一 Reviewer。

---

## 3.7 测试生成

### AI 做什么？

- 生成 `FakeDeviceCore` / `MockTransport` 样板。
- 为 `CameraParam<T>` 生成 JVM 单测（get/set/getSupported/listener）。
- 生成 `SharedFlow<DeviceEvent>` 的 Turbine 测试用例。

### 我做什么？

- 设计测试边界：哪些场景必须覆盖（断连 rebind、并发 register/unregister listener、destroyed 后调用）。
- 定义 Fake 的行为契约：Fake 不是"能跑就行"，必须模拟真实 DeviceCore 的状态机。

### 为什么这样分工？

AI 生成测试代码，工程师设计测试策略。没有边界设计的测试，覆盖率高但价值低。

---

## 3.8 文档生成

### AI 做什么？

- 从 KDoc 注释生成 API 参考文档。
- 生成 Migration Guide（旧 API → 新 API 对照表）。
- 生成 Quick Start 示例（5 行接入代码）。
- 生成架构图的 Mermaid / PlantUML 源码。

### 我做什么？

- 审查对外文档的准确性：每个 `CameraParam` 标注支持机型 / 支持 FunctionMode。
- 确保 Migration Guide 覆盖所有 `@Deprecated` 接口。
- 定义文档即契约：README 中的示例代码必须能编译通过。

### 为什么这样分工？

优秀 SDK 不只是代码，还包括开发体验（DX）。AI 加速文档产出，工程师保证文档与代码的一致性。

---

# 4. AI 辅助架构设计案例

## 4.1 CameraDevice 聚合根设计

### 问题

旧 SDK 的 `Camera` 类是一个巨型类，承担连接、参数、拍摄、预览、文件、固件全部职责。外部使用方面对一个几十上百方法的扁平接口，无法从 API 形状判断"这台相机有哪些能力"。

### AI 如何帮助

1. **分析旧接口**：AI 扫描旧 `Camera` 类，按职责域自动分组（连接 12 个方法、参数 35+ 个、拍摄 20+ 个……），生成职责域划分报告。
2. **提供 DDD 方案**：AI 建议使用 Aggregate Root 模式——`CameraDevice` 作为聚合根，下挂 5 个子域接口（`system` / `capture` / `preview` / `file` / `firmware`）。
3. **Review API 合理性**：AI 指出 `connect(connectHint: Any?)` 的类型安全问题，建议改为 `sealed class ConnectHint`（我采纳了建议，列入下一版本改进）。

### 最终架构（人工决策）

```kotlin
interface CameraDevice {
    val system: CameraSystem
    val capture: CameraCapture
    val preview: CameraPreview
    val file: CameraFile
    val firmware: CameraFirmware

    fun isConnected(): Boolean
    suspend fun connect(connectHint: Any?): Result<Unit>
    fun connect(connectHint: Any?, callback: Callback<Unit>)
    fun registerDisconnectListener(listener: DisconnectListener)
    fun release()

    companion object {
        fun get(connectType: ConnectType): CameraDevice
    }
}
```

**关键设计决策（我主导）**：

| 决策 | 理由 |
|------|------|
| 聚合根 + 5 子域 | 接口自描述：`device.capture.startCapture()` 比 `device.startCapture()` 更清晰 |
| `get(ConnectType)` 工厂 | 隐藏 `CameraDeviceInternal`，后续可替换实现而不破坏 ABI |
| suspend + Callback 双形态 | Kotlin 用协程，OC/Java 用回调，核心逻辑只写一份 |
| 断连自动 rebind | 外部拿到 `CameraDevice` 实例后终身可用，不需要重新 `get` |

---

## 4.2 CameraParam\<T\> 参数抽象

### 问题

35+ 相机参数，旧方式为每个参数一组 `getXXX` / `setXXX` / `getSupportedXXX` / `addXXXListener`，导致接口膨胀、新增参数需要改接口。

### AI 如何帮助比较方案

AI 提供了三种候选方案的完整代码骨架和优劣分析（见 3.3 节 Trade-off 表）。额外一点：AI 还建议了第四种方案——**直接用 `Map<String, Any>` 动态参数**，我立刻否决——类型安全是 SDK 的生命线。

### 最终方案：Template Method

```kotlin
interface CameraParam<T> {
    suspend fun getValue(): Result<T>
    suspend fun fetchValue(): Result<T>
    suspend fun setValue(value: T): Result<Unit>
    suspend fun getSupported(): Result<List<T>>
    fun getName(): String
    fun addListener(listener: (T) -> Unit)
    fun removeListener(listener: (T) -> Unit)
}
```

`CameraParamImpl` 抽象基类只需实现 `reader` / `writer` / `fetcher` / `supportedLoader` 四个方法。**新增一个相机参数 = 新增一个文件，零接口变更**。

**为什么选 Template Method 而非 Delegate**：35+ 参数如果每个都用独立 Delegate 类，文件数从 35 变成 70+，且每个 Delegate 的样板代码几乎相同。Template Method 把"变化点"收敛到 4 个抽象方法，符合 DRY 原则。

---

## 4.3 SharedFlow 事件总线设计

### 问题

底层 C++ Camera SDK 通过 Native Callback 推送事件（连接状态、拍摄进度、参数变化、错误……），旧 SDK 在业务层直接注册 Native Callback，导致：

- 业务代码与 Native 线程模型耦合
- 事件类型是魔法数字（`InfoType` 大 switch），无编译期安全
- 多个 Service 各自注册 Callback，生命周期管理混乱

### AI 如何帮助

AI 分析了事件从 Native 到业务层的完整链路，建议三层转换：

```
Native Callback
    ↓  UnifiedTransport（platform 层）
TransportEvent（协议层事件）
    ↓  DeviceCoreImpl（core 层）
DeviceEvent（业务语义事件，sealed interface）
    ↓  *Service（service 层）
业务处理（collect 自己关心的子类型）
```

AI 还提供了 `sealed interface DeviceEvent` 的完整定义草案，包括 `CaptureStatusEvent`（Starting / Working / Stopping / Finished / Error / SubStatusChanged）和 `DisconnectedEvent`。

### 最终决策（人工判断）

| 决策 | 理由 |
|------|------|
| 业务层不直接依赖 Native Callback | 解耦线程模型；Native 回调在 Transport 层统一消费 |
| 用 `SharedFlow` 而非 `Channel` | 事件总线需要多订阅者；`SharedFlow` 的 `replay=0` 天然适合"只消费新事件" |
| 用 `sealed interface` 而非字符串 | 编译器强制 `when` 分支完备，新增事件类型时编译期报错 |
| 对外不暴露 `Flow` | OC/Swift 无法消费 Kotlin Flow；对外统一用 Listener 接口 |

---

## 4.4 expect/actual 跨平台抽象

### 问题

KMP 的核心价值是代码共享，但 Android 和 iOS 的类型系统、平台 SDK 调用完全不同。如何划定 `expect/actual` 的边界？

### AI 如何帮助

AI 提供了多种 `expect/actual` 划分方案，并分析了 Cash App、JetBrains 官方 Sample 的实践经验。

### 最终决策：三层抽象，业务行为不走 expect/actual

| 层 | 抽象手段 | 例子 |
|----|----------|------|
| 语言/类型层 | `expect class` / `typealias` | `KMPContext` → Android `Context` / iOS `UIApplication` |
| 平台 SDK 层 | `expect fun` Factory | `createBleDeviceCore`、`PlatformCameraStream` |
| 业务行为层 | commonMain 纯接口 + 单一实现 | `CameraDevice`、`CameraCapture` 等 95% 业务代码 |

**关键原则（我主导）**：

- **行为差异通过数据参数化**，而不是写两份代码。例如 `CaptureSupportConfigFactory.create(cameraType, runtimeSnapshot)` 根据运行时快照返回机型特定配置，而非 Android 一套逻辑、iOS 另一套。
- **`expect/actual` 只用于"不得不平台化"的部分**：类型别名、平台 SDK 调用、文件系统路径。所有业务编排、状态机、事件分发都在 `commonMain`。
- **对 OC/Swift 友好**：每个 `suspend fun` 配套 `fun xxx(callback: Callback<T>)`，`invokeCallback` 把 `Result` 桥接到 callback——只写一份核心逻辑。

这是 KMP 的高级实践：**最大化共享，最小化平台分叉**。

---

# 5. AI 辅助代码迁移实践

大型重构不是"让 AI 写代码"，而是**把迁移拆成可并行、可验证、可回滚的子任务**，AI 负责机械部分，工程师负责架构一致性。

## 5.1 迁移拆分策略

```
1. 接口抽取        →  从旧 Camera 类提取职责域，定义 api 层接口
2. 实现迁移        →  按 Service 逐个迁移，先 core 再 service
3. Adapter 编写     →  UnifiedTransport 把 Native Callback 转为 TransportEvent
4. 兼容层生成      →  @Deprecated + ReplaceWith 保留旧 API 形状
5. 测试补充        →  每个 Service 配套 Fake + 单测
6. Review          →  AI + 人工双重审查
```

## 5.2 各阶段 AI 与人工分工

### 阶段 1：接口抽取

- **AI**：扫描旧 `Camera` 类全部 public 方法，按职责域自动分组，生成 `CameraSystem` / `CameraCapture` 等接口草案。
- **我**：审查分组合理性，合并过度拆分，确保每个子接口不超过 30 个方法（`CameraSystem` 最终仍有 ~100 个方法，已知待改进）。

### 阶段 2：实现迁移

- **AI**：按 `CameraParam<T>` 模板，将旧 `getIso()` / `setIso()` / `getSupportedIso()` 批量生成为 `ExposureIsoCameraParam` 等子类。
- **我**：亲自实现 `CameraDeviceInternal`（断连 rebind 逻辑）、`CameraCaptureService`（FunctionMode 大 dispatcher + 拍摄状态机）。

### 阶段 3：Adapter 编写

- **AI**：生成 `UnifiedTransport` 的 Native Callback 注册样板，以及 `InfoType` → `TransportEvent` 的初步映射表。
- **我**：审查映射完整性，补充 AI 遗漏的边界事件（如 `SubStatusChanged`、固件升级进度）。

### 阶段 4：兼容层生成

- **AI**：扫描旧 API 的所有 public 方法，自动生成 `@Deprecated(message, replaceWith)` 注解和转发实现。
- **我**：决定哪些旧 API 可以直接删除（内部方法）、哪些必须保留兼容（外部已在使用）。

### 阶段 5–6：测试与 Review

见第 7、6 章。

## 5.3 迁移中的质量控制

- **每个 Service 迁移完成后独立可编译**：不等待全部完成再集成，降低集成风险。
- **AI 生成代码的统一标记**：所有 AI 生成的文件在 commit message 中标注 `[AI-generated]`，人工审查后才 merge。
- **禁止 AI 直接修改 api 层接口**：对外 API 是契约，只能由 API Owner 手动修改。

---

# 6. AI Code Review 实践

## 6.1 Review Prompt 设计

我设计了一套结构化的 AI Review Prompt，每次 PR 提交时自动触发：

```
请站在 Google Android SDK Reviewer 角度，对以下代码变更进行审查：

1. API 稳定性：是否有 breaking change？@Deprecated 是否正确？
2. SOLID 原则：是否违反单一职责？是否有不必要的依赖？
3. 并发安全：Mutex 使用是否正确？是否有竞态条件？
4. 生命周期：是否有泄漏风险？destroyed 后调用是否安全？
5. ABI 兼容：public 接口变更是否影响已发布的 Framework？
6. 可扩展性：新增机型/参数/模式是否需要改接口？

对每个发现的问题，标注严重程度（Critical / Major / Minor）和建议修复方案。
```

## 6.2 AI 发现的真实问题

> 两项 Major 问题的详细成因与改造方案见 [已知不足与后续改进.md](./已知不足与后续改进.md)。

| AI 发现 | 严重程度 | 我的决策 | 理由 |
|---------|----------|----------|------|
| `CameraParam<T>` 的 listeners 用 `LinkedHashSet` 裸用，没加锁 | Major | **采纳** | 纳入 `ListenerRegistry<T>` 统一改造计划 |
| `connect(connectHint: Any?)` 类型不安全 | Major | **采纳**（下一版本） | 改为 `sealed class ConnectHint`，但当前版本需保持兼容 |
| `CameraSystem` 接口超过 100 个方法，违反 ISP | Major | **采纳**（下一版本） | 按子域拆分为 6 个子接口 |
| AI 建议用 `StateFlow` 替代 `SharedFlow` 做事件总线 | Minor | **拒绝** | 事件总线不需要状态持有；`StateFlow` 的 `replay=1` 会导致新订阅者收到旧事件 |
| AI 建议把 `DeviceCore` 改为 `sealed class` 层次 | Minor | **拒绝** | `DeviceCore` 是接口，实现类可能多个（真实设备 / Fake / Mock），sealed 会限制扩展 |
| AI 发现 `rebind()` 期间外部缓存的 `device.capture` 引用可能失效 | Critical | **采纳**（下一版本） | 用 wrapper + `volatile` 持有当前实例 |
| AI 建议所有方法在 destroyed 后抛异常 | Major | **部分采纳** | 当前静默 return，下一版本改为 `Result.failure(AlreadyReleasedException)` |

## 6.3 Review 流程

```
PR 提交 → AI 自动 Review（5 分钟内出报告）→ 工程师逐条评估 → 采纳项修复 → 人工 Review 确认 → Merge
```

**关键原则**：AI Review 是必选项而非可选项，但 AI 的判断不是最终判断。每一条 AI 建议都必须有"采纳/拒绝/延期"的明确决策和理由记录。

---

# 7. AI 辅助测试生成

## 7.1 测试分层策略

| 层次 | 测试类型 | AI 生成 | 人工设计 |
|------|----------|---------|----------|
| core 层 | `FakeDeviceCore` + 事件流测试 | 生成 Fake 骨架 + Turbine 用例 | 定义 Fake 行为契约（状态机） |
| service 层 | `CameraCaptureService` 单测 | 生成参数读写测试 | 设计断连 rebind、并发 register 场景 |
| api 层 | 接口契约测试 | 生成 `@Deprecated` 兼容性测试 | 定义 ABI 不变性断言 |
| platform 层 | 集成测试 | 不生成（需真机） | 手动验证 BLE/WiFi/USB 连接链路 |

## 7.2 AI 生成的测试示例

### FakeDeviceCore

```kotlin
class FakeDeviceCore : DeviceCore {
    private val _events = MutableSharedFlow<DeviceEvent>()
    override val events: SharedFlow<DeviceEvent> = _events.asSharedFlow()

    var isConnected = false
        private set

    suspend fun simulateConnect() {
        isConnected = true
        _events.emit(DeviceEvent.ConnectedEvent)
    }

    suspend fun simulateDisconnect(throwable: Throwable? = null) {
        isConnected = false
        _events.emit(DeviceEvent.DisconnectedEvent(throwable))
    }

    suspend fun simulateCaptureStatus(event: CaptureStatusEvent) {
        _events.emit(DeviceEvent.CaptureStatusEventWrapper(event))
    }
}
```

### CameraParam 单测

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

## 7.3 工程师设计的测试边界

AI 不会主动想到这些场景，但它们对 SDK 稳定性至关重要：

- **断连 rebind 后 listener 仍然有效**：注册 listener → 断连 → 自动 rebind → 重连 → listener 收到新事件。
- **并发 register/unregister**：两个协程同时 register 和 unregister 同一个 listener，不应 `ConcurrentModificationException`。
- **destroyed 后调用安全**：`release()` 后调用任何方法，应静默失败或返回 `Result.failure`（当前是静默 return，待改进）。
- **syncJob 独立生命周期**：`CameraCaptureService` 的 `syncJob` 销毁时不应影响主 scope。

---

# 8. AI 辅助文档生成

## 8.1 文档体系

| 文档类型 | AI 生成内容 | 人工审查重点 |
|----------|-------------|--------------|
| KDoc | 从接口定义自动生成参数说明、返回值、异常 | 标注支持机型 / FunctionMode |
| README / Quick Start | 5 行接入示例 + 架构概览 | 示例代码必须能编译通过 |
| Migration Guide | 旧 API → 新 API 对照表 | 覆盖所有 `@Deprecated` 接口 |
| API Example | 每个子域的典型用法（连接/拍照/预览/下载） | 示例的完整性、错误处理 |
| Architecture Diagram | Mermaid 六层架构图 | 层次边界是否准确 |

## 8.2 开发体验（DX）作为一等公民

优秀 SDK 不只是代码质量，还包括：

- **5 行接入**：`init` → `get` → `registerDisconnectListener` → `connect` → `startCapture`。
- **IDE 友好**：参数以 `device.capture.exposureISO` 属性形式暴露，自动补全即文档。
- **错误可观测**：`Result<T>` 统一返回值，`.onFailure { }` 处理错误，消除"什么时候抛异常"的歧义。
- **迁移无痛**：`@Deprecated` + `ReplaceWith` 让旧代码一键迁移，不需要读 Migration Guide 也能编译通过。

---

# 9. AI 使用中的失败案例

**不把 AI 描述成万能。** 以下是真实失败案例，以及我作为高级工程师如何判断和纠正。

## 9.1 AI 推荐过度抽象

**场景**：AI 建议为每个 `FunctionMode`（20+ 种拍摄模式）各创建一个 `CaptureStrategy` 接口 + 实现类，用 Strategy 模式替代 `when` dispatcher。

**问题**：20+ 个 Strategy 类，每个只有 `start()` / `stop()` 两个方法，且逻辑高度相似（都是调用 `DeviceCore` 的不同方法）。过度抽象导致文件数翻倍，且新增 FunctionMode 需要同时改 enum、dispatcher、Strategy 三处。

**我的决策**：**拒绝**。保留 `when` dispatcher——对于 20 个分支的线性分发，一个简单的 `when` 比 20 个 Strategy 类更清晰、更易维护。Strategy 模式适合"行为差异大"的场景，不适合"调用不同方法"的场景。

## 9.2 AI 推荐错误 KMP 方案

**场景**：AI 建议把 `CameraCaptureService` 的拍摄编排逻辑拆为 `androidMain` 和 `iosMain` 各一份实现，因为"两平台调用 Native 的方式不同"。

**问题**：这直接否定了 KMP 的核心价值。拍摄编排是业务逻辑，与平台无关；平台差异已经在 `DeviceCore` → `UnifiedTransport` → Native 链路中被屏蔽了。

**我的决策**：**拒绝**。重申"业务行为不走 expect/actual"原则。AI 不了解我们的架构分层，把平台差异和业务逻辑混为一谈。

## 9.3 AI 忽略 Native 生命周期

**场景**：AI 生成的 `UnifiedTransport` 在 Native Callback 中直接 `emit` 到 `SharedFlow`，没有考虑 JNI 线程 attach/detach 和回调线程切换。

**问题**：Android 上 Native 回调在 C++ 线程触发，直接 emit 会导致协程在错误线程执行；iOS 上 CoreBluetooth 回调在后台队列，同样需要切换到主线程。

**我的决策**：**采纳 AI 的骨架，人工补充线程切换**。在 Transport 层统一 `withContext(Dispatchers.Main)` 后再 emit，Native 回调线程模型由工程师把控。

## 9.4 AI 忽略 ABI 兼容

**场景**：AI 建议把 `CameraDevice.connect(connectHint: Any?)` 直接改为 `connect(hint: ConnectHint)`，删除旧签名。

**问题**：已发布的 SDK 版本中，外部客户可能正在使用旧签名。直接删除会导致客户编译失败。

**我的决策**：**拒绝删除，采纳新增**。保留旧签名并标记 `@Deprecated`，新增 `ConnectHint` 签名，Migration Guide 中提供迁移路径。

## 9.5 高级工程师的价值

> 不是让 AI 替自己思考，而是知道什么时候相信 AI，什么时候挑战 AI。

| AI 擅长 | AI 不擅长 |
|---------|-----------|
| 提供候选方案 | 在约束下选择唯一方案 |
| 生成样板代码 | 保证架构一致性 |
| 发现模式化问题 | 发现上下文相关问题 |
| 快速产出文档 | 保证文档与代码一致 |
| 类比业界实践 | 理解本公司技术债 |

---

# 10. 最终收益

## 10.1 技术收益

| 指标 | 重构前 | 重构后 | 备注 |
|------|--------|--------|------|
| 外部接入代码量 | ~XX 行 | ~5 行 | 按真实数据替换 |
| Android/iOS 业务代码共享率 | 0%（两套实现） | ~95% | commonMain 统一实现 |
| 新增机型平均改动文件数 | 10+ 个文件 | 2–3 个文件 | SupportConfig + enum + factory |
| 新增拍摄参数成本 | 改接口 + 双端实现 | 新增 1 个 CameraParam 文件 | 零接口变更 |
| 新增 FunctionMode 成本 | 双端各改 + Native | 1 行 enum + 1 处分支 | 单端改动 |
| 新同事上手适配新机型 | 2–3 周 | ~1 周 | 内部团队反馈 |

## 10.2 AI 工程收益

| 指标 | 估算提升 | 说明 |
|------|----------|------|
| 方案探索速度 | ~3–5x | AI 快速提供 3–5 个候选方案 + 优劣分析，人工只需做 Trade-off |
| 重构周期 | 缩短 ~40% | 机械迁移（参数类、兼容层、样板 Service）由 AI 完成 |
| Review 覆盖 | 增强 ~2x | AI 自动审查 + 人工深度审查，叠加覆盖 |
| 文档维护成本 | 降低 ~60% | KDoc、Migration Guide、API Example 自动生成 |
| 跨语言学习成本 | 显著降低 | 作者原长项是 C++，Kotlin/KMP 通过 AI 辅助快速上手 |

## 10.3 审慎声明

以上数据为估算值，保留可替换占位符。AI 的收益高度依赖工程师的架构能力和 Review 质量——同一个 AI 工具，不同工程师产出质量可能差 10 倍。

---

# 11. 面试回答版本

## 问题："你工作中如何使用 AI 提升效率？"

### 3 分钟回答

> 我最近在主导一个 KMP 相机 SDK 的大规模重构，统一 Android 和 iOS 对外接口。这个项目涉及 15+ 机型、35+ 拍摄参数、20+ 拍摄模式，底层是 C++ Native SDK。我原本的长项是 C++ 和音视频底层，Kotlin/KMP 是跨语言学习。
>
> 我没有把 AI 当"代码生成器"，而是把它引入了整个软件研发生命周期，形成了一套 AI-Augmented Engineering Workflow。
>
> **第一，架构探索阶段。** 旧 SDK 是一个巨型 Camera 类，我让 AI 扫描全部方法按职责域分组，并提供了 DDD 聚合根、按连接类型拆分、按平台拆分三种方案。我结合 KMP 特性——业务行为不走 expect/actual、ABI 必须稳定——选择了聚合根 + 六层架构。AI 提供候选空间，我做 Trade-off 决策。
>
> **第二，代码迁移阶段。** 我把迁移拆成六步：接口抽取、实现迁移、Adapter 编写、兼容层生成、测试补充、Review。AI 负责机械部分——比如把 35 个参数的 get/set/getSupported 批量生成为 CameraParam 子类，生成 @Deprecated 兼容层。我亲自实现核心路径——CameraDevice 断连 rebind、DisconnectListener 线程安全派发、CameraCaptureService 拍摄编排器。
>
> **第三，Code Review 阶段。** 我设计了一套结构化 Prompt，让 AI 站在 Google SDK Reviewer 角度审查每次 PR——API 稳定性、SOLID、并发安全、生命周期、ABI 兼容。AI 发现了比如 CameraParam listener 没加锁、rebind 期间引用失效等问题。我逐条评估采纳或拒绝，并记录理由。
>
> **第四，失败案例。** AI 也犯过错——推荐过度 Strategy 抽象、建议把业务逻辑拆到 androidMain/iosMain、忽略 Native 回调线程模型。我的价值不是让 AI 替我想，而是知道什么时候信它、什么时候挑战它。
>
> 最终效果是：外部接入从 ~XX 行降到 5 行，两端共享 95% 业务代码，新增机型从改 10+ 文件降到 2–3 个文件。我个人在不会 Kotlin 的情况下，通过 AI 辅助一个多月交付了覆盖 15 种机型的系统。
>
> 我认为 AI 时代高级工程师的核心竞争力变了——以前比谁能写代码，现在比谁能定义问题、设计方案、验证 AI 输出、做架构决策。AI 是能力放大器，不是替代品。

---

> **使用建议**：
> - 面试时根据追问节奏选讲，先抛"聚合根 + KMP 跨端 + 监听器稳健"三个最厚的点。
> - 如果对方深挖 AI Workflow，展开第 3 章的分阶段分工。
> - 如果对方深挖架构，展开第 4 章的四个设计案例。
> - 如果对方质疑 AI 能力，主动讲第 9 章的失败案例——这是加分项。
> - 所有数据占位符（~XX 行）请替换为你的真实数据。
