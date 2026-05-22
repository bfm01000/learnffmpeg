# KMP 迁移过程中常见的问题

## 2. Swift 初始化 KMP 实例无法省略构造参数默认值

### 问题现象

Kotlin 中定义类或数据类时，构造方法参数可以设置默认值：

```kotlin
class UserInfo(
    val name: String = "",
    val age: Int = 0,
    val avatar: String? = null
)
```

在 Kotlin 侧调用时，可以只传部分参数，甚至不传参数：

```kotlin
val user = UserInfo()
val user2 = UserInfo(name = "Tom")
```

但导出给 iOS 后，Swift 调用 KMP 产物时通常需要显式传入所有构造参数，无法像 Kotlin 一样省略默认值：

```swift
let user = UserInfo(name: "", age: 0, avatar: nil)
```

### 原因说明

KMP 的 iOS 产物会通过 Objective-C Header 暴露给 Swift。Objective-C 方法本身不支持 Kotlin 这种参数默认值语义，所以 Kotlin/Native 自动生成桥接接口时，会丢弃默认参数信息。

也就是说，默认值只在 Kotlin 编译器语境中生效；一旦通过 Objective-C/Swift 边界暴露，Swift 只能看到一个需要完整参数列表的方法。

### 影响

这种问题在迁移过程中会让 iOS 侧调用成本明显增加：

- Swift 初始化 KMP 对象时必须补齐所有参数，即使大部分参数都有默认值。
- 参数越多，调用越冗长，也更容易传错默认值。
- 后续 Kotlin 新增构造参数时，即使有默认值，也可能导致 iOS 侧需要同步修改调用代码。

### 解决方案

在 Kotlin 中为跨端调用显式提供工厂方法，不依赖构造方法默认参数。

如果 iOS 可以正常访问 `companion object`，可以在类中增加伴生对象方法：

```kotlin
class UserInfo(
    val name: String,
    val age: Int,
    val avatar: String?
) {
    companion object {
        fun create(): UserInfo {
            return UserInfo(
                name = "",
                age = 0,
                avatar = null
            )
        }

        fun createWithName(name: String): UserInfo {
            return UserInfo(
                name = name,
                age = 0,
                avatar = null
            )
        }
    }
}
```

Swift 侧优先调用这些语义明确的工厂方法，而不是直接调用长参数构造方法。

如果鸿蒙或其他端无法稳定使用 `companion object`，建议改用普通对象工厂或顶层函数：

```kotlin
object UserInfoFactory {
    fun create(): UserInfo {
        return UserInfo(
            name = "",
            age = 0,
            avatar = null
        )
    }

    fun createWithName(name: String): UserInfo {
        return UserInfo(
            name = name,
            age = 0,
            avatar = null
        )
    }
}
```

或者提供平台更容易调用的顶层方法：

```kotlin
fun createUserInfo(): UserInfo {
    return UserInfo(
        name = "",
        age = 0,
        avatar = null
    )
}
```

### 建议

对外暴露给多端调用的 KMP Model，不建议把“默认值能力”只放在构造方法参数上。更推荐把默认初始化逻辑沉淀成明确的 `create()`、`empty()`、`default()`、`fromXxx()` 等方法，降低各端接入成本。

## 3. Kotlin 中方法参数带默认值怎么处理

### 问题现象

Kotlin 方法参数也支持默认值：

```kotlin
fun requestUserInfo(
    userId: String,
    forceRefresh: Boolean = false,
    timeoutMillis: Long = 3000
)
```

Kotlin 侧可以这样调用：

```kotlin
requestUserInfo(userId = "10001")
```

但 iOS/Swift 侧无法省略 `forceRefresh`、`timeoutMillis`，需要完整传参：

```swift
service.requestUserInfo(
    userId: "10001",
    forceRefresh: false,
    timeoutMillis: 3000
)
```

### 原因说明

原因与构造方法默认值一致：Kotlin 默认参数是 Kotlin 编译器提供的语法能力，导出到 Objective-C Header 后不会保留默认值语义。

### 解决方案

已有方法如果已经暴露给 iOS，短期内可以让 iOS 显式传入默认值，保证功能先可用。

后续新增或重构 KMP 对外 API 时，建议不要依赖方法参数默认值，而是拆成多个语义明确的方法：

```kotlin
fun requestUserInfo(userId: String) {
    requestUserInfo(
        userId = userId,
        forceRefresh = false,
        timeoutMillis = 3000
    )
}

fun requestUserInfoWithRefresh(userId: String, forceRefresh: Boolean) {
    requestUserInfo(
        userId = userId,
        forceRefresh = forceRefresh,
        timeoutMillis = 3000
    )
}

fun requestUserInfo(
    userId: String,
    forceRefresh: Boolean,
    timeoutMillis: Long
) {
    // 实际请求逻辑
}
```

对外暴露时，优先让 Swift、鸿蒙等端调用短参数方法；完整参数方法只在确实需要自定义所有参数时使用。

### 命名建议

方法拆分时不要只用参数数量区分含义，建议在命名上表达业务意图：

- `requestUserInfo(userId)`
- `requestUserInfoWithRefresh(userId, forceRefresh)`
- `requestUserInfoWithTimeout(userId, timeoutMillis)`
- `requestUserInfoFull(userId, forceRefresh, timeoutMillis)`

这样多端调用时可以减少“这个 Boolean 到底是什么意思”的理解成本。

## 5. Kotlin 中 data class 自动生成的 copy 方法带默认值

### 问题现象

Kotlin 的 `data class` 会自动生成 `copy()` 方法，并且 `copy()` 的每个参数默认值都是当前对象的字段值：

```kotlin
data class UserInfo(
    val name: String = "",
    val age: Int = 0,
    val avatar: String? = null
)
```

Kotlin 侧可以只修改某一个字段：

```kotlin
val newUser = oldUser.copy(name = "Tom")
```

但导出到 iOS 后，Swift 无法使用这种“只传变更字段”的能力。桥接层看到的是一个带完整参数列表的 `copy()` 方法，因此 Swift 侧需要传入所有字段：

```swift
let newUser = oldUser.copy(
    name: "Tom",
    age: oldUser.age,
    avatar: oldUser.avatar
)
```

字段越多，调用越繁琐，也越容易漏传或错传。

### 原因说明

`data class copy()` 本质上也是一个带默认参数的方法：

```kotlin
fun copy(
    name: String = this.name,
    age: Int = this.age,
    avatar: String? = this.avatar
): UserInfo
```

由于 Objective-C 不支持默认参数，Kotlin/Native 导出后会要求调用方传入完整参数列表。初始化方法同理，如果构造参数有默认值，Swift 侧也无法直接省略。

### 解决方案

在 KMP 中手动增加跨端友好的创建方法和局部复制方法，避免让 Swift、鸿蒙等端直接依赖自动生成的 `copy()`。

推荐增加 `create()` 类方法或工厂方法，处理默认初始化：

```kotlin
data class UserInfo(
    val name: String,
    val age: Int,
    val avatar: String?
) {
    companion object {
        fun create(): UserInfo {
            return UserInfo(
                name = "",
                age = 0,
                avatar = null
            )
        }
    }
}
```

同时增加业务需要的 `doCopy()`、`copyWithXxx()` 或 `updateXxx()` 实例方法：

```kotlin
data class UserInfo(
    val name: String,
    val age: Int,
    val avatar: String?
) {
    fun doCopy(
        name: String,
        age: Int,
        avatar: String?
    ): UserInfo {
        return copy(
            name = name,
            age = age,
            avatar = avatar
        )
    }

    fun copyWithName(name: String): UserInfo {
        return copy(name = name)
    }

    fun copyWithAvatar(avatar: String?): UserInfo {
        return copy(avatar = avatar)
    }
}
```

如果字段很多，不建议为每个字段都无脑生成一个方法。优先根据真实业务场景提供常用的局部更新方法，例如 `copyWithName()`、`copyWithSelected()`、`copyWithStatus()`。

### 建议

`data class` 仍然可以在 KMP 内部使用，它对 Kotlin 侧非常方便。但只要这个 Model 会暴露给 iOS、鸿蒙或其他非 Kotlin 端，就需要额外设计跨端友好的 API：

- 默认初始化使用 `create()`、`empty()`、`default()` 等明确方法。
- 局部修改使用 `copyWithXxx()`、`updateXxx()` 等明确方法。
- 不把 Kotlin 默认参数当作跨端 API 契约。
- 新增字段时同步检查 Swift、鸿蒙等端是否需要调整调用方式。

## 面试回答思路

面试官问“KMP 接入过程中遇到过哪些坑”时，不建议只回答“默认参数 iOS 不支持”。更好的回答方式是按“跨端 API 设计、生命周期、线程模型、异常处理、类型导出、平台差异”几个方向展开。

可以这样概括：

> 我们接入 KMP 时，主要踩过几类坑。第一类是 Kotlin 语法能力导出到 iOS 后不完整，比如默认参数、`data class copy()`、泛型和部分 Kotlin 特有类型在 Swift 侧不好用。第二类是生命周期和内存管理问题，比如 KMP 对象持有 Swift 回调，Swift 又持有 KMP 对象，容易形成循环引用。第三类是协程、线程和回调切换问题，尤其是网络请求、Flow 数据流回到 UI 层时必须明确切主线程。第四类是异常、集合、时间、数字等类型跨端表现不一致，需要在 shared 层设计稳定的 DTO 和统一封装。

下面这些问题都可以作为面试时的补充案例。

## 6. KMP 中的循环引用在 iOS 平台不释放

### 问题现象

KMP 接入 iOS 后，某些页面退出了，但对应的 KMP 对象、Swift 页面对象或回调对象没有释放。多进出几次页面后，内存持续上涨。

常见场景是：

- Swift 的 `ViewController` 持有 KMP 暴露出来的 `Presenter`、`Editor`、`ViewModel`。
- KMP 对象内部又持有 Swift 传进来的 callback、delegate 或 dependency。
- Swift callback 闭包里又捕获了 `self`。

于是形成类似下面的引用链：

```text
ViewController -> KMPEditor -> SwiftCallback -> ViewController
```

### 原因说明

iOS 使用 ARC 做引用计数管理，只要对象之间形成强引用环，就不会自动释放。Kotlin/Native 虽然有自己的内存管理，但当 KMP 对象和 Swift/Objective-C 对象互相持有时，依然需要关注引用关系。

如果 KMP 中长期持有 iOS 传入的对象，比如 callback、delegate、listener，就很容易出现泄漏。

### 解决方案

在 KMP 中不要强持有生命周期属于 iOS 页面的对象。可以为跨端封装一个弱引用能力：

```kotlin
expect class WeakRef<T : Any>(value: T) {
    fun get(): T?
}
```

Android 侧可以用 `java.lang.ref.WeakReference`：

```kotlin
actual class WeakRef<T : Any> actual constructor(value: T) {
    private val ref = java.lang.ref.WeakReference(value)

    actual fun get(): T? {
        return ref.get()
    }
}
```

iOS 侧可以使用 Kotlin/Native 提供的弱引用包装能力：

```kotlin
actual class WeakRef<T : Any> actual constructor(value: T) {
    private val ref = kotlin.native.ref.WeakReference(value)

    actual fun get(): T? {
        return ref.value
    }
}
```

然后把 `KMPEditor` 内部保存的 dependency、callback、delegate 等替换成弱引用：

```kotlin
class KMPEditor(
    dependency: EditorDependency
) {
    private val dependencyRef = WeakRef(dependency)

    fun notifyChanged() {
        dependencyRef.get()?.onChanged()
    }
}
```

Swift 侧闭包也要注意使用 `[weak self]`：

```swift
editor.setCallback { [weak self] result in
    self?.render(result)
}
```

### 面试回答点

可以说：

> 我们遇到过 KMP 对象在 iOS 侧不释放的问题，后来发现是 shared 层强持有了 Swift 传进来的 callback，Swift 页面又持有 KMP 对象，形成了引用环。解决方式是 shared 层对 delegate、callback 这类对象统一用弱引用封装，Android 和 iOS 分别用 `actual` 实现，同时 Swift 闭包里也配合 `[weak self]`。

## 7. `suspend`、协程和 Flow 到 iOS 后调用方式不一致

### 问题现象

Kotlin 侧很自然地使用 `suspend` 或 `Flow`：

```kotlin
suspend fun loadUserInfo(userId: String): UserInfo

fun observeUserInfo(userId: String): Flow<UserInfo>
```

但导出给 iOS 后，Swift 不一定能像 Kotlin 那样自然地使用协程。不同 Kotlin 版本、不同导出方式下，`suspend` 可能表现为 completion 回调，也可能通过 Swift async/await 调用；`Flow` 则通常不能直接当成 Swift 的 Combine 或 async sequence 使用。

### 原因说明

协程是 Kotlin 的异步模型，Swift 有自己的 async/await、Combine、闭包回调模型。两套异步体系不是同一个东西，KMP 只负责把接口导出，并不会自动把所有异步语义都转换成 iOS 最习惯的写法。

### 解决方案

shared 层对外提供更明确的跨端异步接口，不让 iOS 直接面对复杂的协程细节。

对于一次性请求，可以封装 callback 版本：

```kotlin
interface UserCallback {
    fun onSuccess(userInfo: UserInfo)
    fun onError(error: BusinessError)
}

fun loadUserInfo(
    userId: String,
    callback: UserCallback
) {
    scope.launch {
        try {
            val result = repository.loadUserInfo(userId)
            withContext(Dispatchers.Main) {
                callback.onSuccess(result)
            }
        } catch (throwable: Throwable) {
            withContext(Dispatchers.Main) {
                callback.onError(BusinessError.from(throwable))
            }
        }
    }
}
```

对于持续数据流，不建议把原始 `Flow` 直接暴露给 iOS，而是封装成可订阅、可取消的接口：

```kotlin
interface Disposable {
    fun dispose()
}

fun observeUserInfo(
    userId: String,
    callback: UserCallback
): Disposable {
    val job = scope.launch {
        repository.observeUserInfo(userId).collect { userInfo ->
            withContext(Dispatchers.Main) {
                callback.onSuccess(userInfo)
            }
        }
    }

    return object : Disposable {
        override fun dispose() {
            job.cancel()
        }
    }
}
```

### 面试回答点

可以说：

> 我们没有直接把所有 `suspend` 和 `Flow` 原样丢给 iOS，而是按业务场景封装了一层。一次性请求给 callback 或 async 方法，持续监听给 observe + disposable。这样 iOS 能明确知道什么时候开始订阅、什么时候取消，也能避免页面销毁后协程还在跑。

## 8. 回调线程不明确，iOS 更新 UI 崩溃或偶现异常

### 问题现象

KMP 中做网络请求、数据库查询或复杂计算后，直接回调 Swift。Swift 收到回调后更新 UI，可能出现 UI 不刷新、偶现崩溃，或者 Xcode 报主线程相关警告。

### 原因说明

Kotlin 协程默认在哪个线程回调，取决于当前 `CoroutineDispatcher` 和调用链。如果 shared 层没有明确切换到主线程，iOS 侧就可能在后台线程更新 UI。

### 解决方案

shared 层约定：所有对 UI 层的 callback 都在主线程回调。

```kotlin
scope.launch(Dispatchers.Default) {
    val result = repository.loadData()

    withContext(Dispatchers.Main) {
        callback.onSuccess(result)
    }
}
```

如果不同平台主线程调度器不一致，可以用 `expect/actual` 封装：

```kotlin
expect val MainDispatcher: CoroutineDispatcher
```

Android：

```kotlin
actual val MainDispatcher: CoroutineDispatcher = Dispatchers.Main
```

iOS：

```kotlin
actual val MainDispatcher: CoroutineDispatcher = Dispatchers.Main
```

业务代码统一使用：

```kotlin
withContext(MainDispatcher) {
    callback.onSuccess(result)
}
```

### 面试回答点

可以说：

> 还有一个坑是 shared 层回调到 iOS 时线程不明确。我们后来定了规范，凡是回调给 UI 层的接口，都必须在 shared 层切到 MainDispatcher，再通知 Swift，避免 iOS 在后台线程更新 UI。

## 9. Kotlin 异常直接抛到 Swift 侧会导致崩溃或不好处理

### 问题现象

Kotlin 代码中直接抛异常：

```kotlin
fun parseUser(json: String): UserInfo {
    if (json.isEmpty()) {
        throw IllegalArgumentException("json is empty")
    }
    return parser.decodeFromString(json)
}
```

Kotlin 侧可以用 `try-catch` 处理，但导出到 iOS 后，如果异常没有被正确声明或转换，Swift 侧可能不好捕获，严重时会直接崩溃。

### 原因说明

Kotlin 和 Swift 的异常机制不同。Kotlin 的 unchecked exception 不会天然变成 Swift 的 `throws`。跨语言边界直接抛异常通常不是一个稳定的 API 设计。

### 解决方案

KMP 对外 API 尽量不要直接抛异常，而是统一转换成业务结果类型：

```kotlin
data class ResultData<T>(
    val data: T?,
    val error: BusinessError?
)

data class BusinessError(
    val code: Int,
    val message: String
)
```

对外暴露的方法内部兜住异常：

```kotlin
fun parseUserSafely(json: String): ResultData<UserInfo> {
    return try {
        ResultData(
            data = parser.decodeFromString(json),
            error = null
        )
    } catch (throwable: Throwable) {
        ResultData(
            data = null,
            error = BusinessError(
                code = -1,
                message = throwable.message ?: "unknown error"
            )
        )
    }
}
```

如果确实希望 Swift 侧按 `throws` 方式处理，需要结合 `@Throws` 明确声明，但项目中仍建议统一收敛异常模型，避免每个接口风格不一致。

### 面试回答点

可以说：

> 我们一开始 Kotlin 侧有些方法会直接抛异常，iOS 侧接起来不友好。后来统一约定 shared 层对外不直接抛业务异常，而是转换成 `ResultData` 或错误回调，Swift 只处理明确的 error model。

## 10. Kotlin 特有类型、泛型、密封类导出到 Swift 后不好用

### 问题现象

Kotlin 内部常用的类型，在 Swift 侧调用时可能不直观：

- `Result<T>`、复杂泛型、嵌套泛型导出后类型很难看。
- `sealed class` 在 Kotlin 中适合表达状态，但 Swift 侧不一定能像 Swift enum 一样优雅使用。
- `UInt`、`ULong`、`Long`、`BigDecimal`、`LocalDateTime` 等类型在不同平台表现不一致。
- Kotlin 可空类型、集合类型导出后，Swift 侧调用成本增加。

### 原因说明

Kotlin 的类型系统和 Swift 的类型系统并不是一一对应的。KMP 可以导出 API，但导出结果不一定符合 Swift 开发者的使用习惯。

### 解决方案

对外暴露的 DTO 尽量简单稳定：

```kotlin
data class UserInfoDTO(
    val userId: String,
    val userName: String,
    val age: Int,
    val avatarUrl: String?
)
```

跨端 API 中尽量少暴露以下内容：

- 复杂泛型，比如 `Map<String, List<Result<UserInfo>>>`。
- Kotlin 标准库中 Swift 不好理解的类型，比如 `kotlin.Result`。
- 过深的继承层级或 `sealed class` 状态树。
- 平台差异明显的时间、金额、无符号数字类型。

可以把复杂类型压平为 Swift 更容易消费的字段：

```kotlin
data class RequestStateDTO(
    val status: String,
    val message: String?,
    val userInfo: UserInfoDTO?
)
```

`status` 可以约定为 `"loading"`、`"success"`、`"error"`，各端再转换成自己的 enum。

### 面试回答点

可以说：

> KMP 不是把 Kotlin 内部模型原样暴露给其他端就完事了。我们后来专门区分了 internal model 和 export DTO，对外 DTO 尽量不用复杂泛型、`Result`、深层 sealed class，而是压平成 Swift、鸿蒙都好消费的结构。

## 11. List、Map 等集合跨端传递时可变性和性能容易被忽略

### 问题现象

Kotlin 中返回：

```kotlin
fun getUsers(): List<UserInfo>
```

Swift 侧能拿到集合，但使用体验和 Swift 原生 `[UserInfo]` 不完全一样。有些场景还会涉及集合拷贝、类型转换、可变性不一致等问题。

### 原因说明

Kotlin 的 `List`、`MutableList` 和 Swift 的 `Array` 不是同一个类型。KMP 桥接时会做包装或转换，调用方容易误以为它就是 Swift 原生集合。

如果频繁跨边界传递大集合，还可能带来额外性能成本。

### 解决方案

跨端 API 设计时注意三点：

- 对外优先返回只读集合，不让多端直接修改 shared 层内部集合。
- 大列表避免高频跨语言边界传递，可以分页、增量更新或只传变更项。
- 如果 iOS 需要原生数组体验，可以在 Swift 侧做一次适配转换。

Kotlin 侧不要把内部可变集合直接暴露出去：

```kotlin
class UserStore {
    private val users = mutableListOf<UserInfo>()

    fun getUsers(): List<UserInfo> {
        return users.toList()
    }
}
```

### 面试回答点

可以说：

> 集合也是一个坑。Kotlin 的 `List` 到 Swift 后不是完全等价于 Swift Array，所以我们对外只暴露只读集合，内部可变集合不直接传出去。大列表场景还要避免频繁跨端传递，尽量做分页或 diff。

## 12. 平台能力不能强行写在 commonMain，需要用 expect/actual 隔离

### 问题现象

在 shared 层写业务时，经常会遇到平台相关能力：

- Android 需要 `Context`。
- iOS 需要访问 `NSUserDefaults`、Keychain、文件目录。
- 日志、网络状态、设备信息、权限、加密能力，各平台 API 都不一样。

如果直接在 `commonMain` 里写平台代码，会编译不过，或者导致 shared 层越来越混乱。

### 原因说明

`commonMain` 只能放多端共同可用的 Kotlin 代码。平台 API 必须放在对应的 `androidMain`、`iosMain` 等 source set 中。

### 解决方案

使用 `expect/actual` 把平台差异隔离起来。

commonMain：

```kotlin
expect class PlatformStorage {
    fun putString(key: String, value: String)
    fun getString(key: String): String?
}
```

androidMain：

```kotlin
actual class PlatformStorage {
    actual fun putString(key: String, value: String) {
        // 使用 SharedPreferences
    }

    actual fun getString(key: String): String? {
        // 从 SharedPreferences 读取
        return null
    }
}
```

iosMain：

```kotlin
actual class PlatformStorage {
    actual fun putString(key: String, value: String) {
        // 使用 NSUserDefaults 或 Keychain
    }

    actual fun getString(key: String): String? {
        // 从 NSUserDefaults 或 Keychain 读取
        return null
    }
}
```

### 面试回答点

可以说：

> KMP 的 shared 层不是所有代码都能共享。我们遇到平台能力时会用 `expect/actual` 隔离，比如存储、日志、设备信息、加密等能力。commonMain 只定义接口和业务流程，具体平台实现放到 androidMain、iosMain。

## 13. iOS framework 导出依赖和包体问题

### 问题现象

KMP shared module 依赖越来越多后，iOS 接入 framework 时可能遇到：

- framework 变大。
- Swift 侧看不到某些依赖类型。
- 构建脚本复杂，Debug/Release、真机/模拟器产物容易混淆。
- 多个 framework 之间依赖重复或符号冲突。

### 原因说明

KMP 导出 iOS framework 时，需要明确哪些依赖要被导出给 iOS，哪些只是 shared 内部使用。`api`、`implementation`、`export` 的选择会影响 iOS 侧能看到什么，也会影响包体和 ABI。

### 解决方案

原则是：能不暴露就不暴露。

- shared 内部使用的依赖用 `implementation`。
- iOS API 签名里出现的类型，才考虑 `api` 或 framework `export`。
- 对外接口尽量使用自己定义的 DTO，不直接暴露第三方库类型。
- iOS framework 构建脚本区分好 Debug/Release、模拟器/真机。

### 面试回答点

可以说：

> iOS framework 导出也是坑。不是 shared 里依赖了什么都应该暴露给 iOS。我们后来控制 API 边界，对外尽量只暴露自己的 DTO，第三方库留在 shared 内部，减少 framework 导出依赖和包体膨胀。

## 14. 资源文件、图片和多语言不一定适合直接共享

### 问题现象

业务代码可以放到 shared 层，但图片、字符串、多语言、颜色、字体等资源迁移到 KMP 后，不一定能被 Android 和 iOS 以原来的方式使用。

### 原因说明

Android 和 iOS 的资源体系差异很大。Android 有 `res`、`R.string`、`R.drawable`，iOS 有 Asset Catalog、Localizable.strings。强行共享所有资源，可能会让两端接入成本变高。

### 解决方案

资源是否共享要看场景：

- 纯业务文案、错误码文案，可以考虑由 shared 层返回 key 或 message。
- UI 强相关资源，比如图片、颜色、字体，通常仍由各端管理。
- 多语言如果要 shared 管理，需要提前设计资源加载和 fallback 策略。

一个常见做法是 shared 层只返回业务状态或文案 key：

```kotlin
data class ErrorInfo(
    val code: Int,
    val messageKey: String
)
```

各端再根据 `messageKey` 映射自己的本地化文案。

### 面试回答点

可以说：

> 我们没有把所有资源都强行放进 KMP。业务错误码、状态可以共享，但 UI 资源和本地化文案更多还是让各端自己管理，shared 层返回 message key 或业务状态，避免破坏原生端资源体系。