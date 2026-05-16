# 面向对象六大原则 SOLID：面试速记与原理详解

> **适用方向**：Android FFmpeg 音视频开发（Pipeline 架构 / SDK 设计 / 模块解耦方向）  
> **难度**：⭐⭐⭐  
> **预计阅读**：速记 10 分钟｜全文 35 分钟  
> **关联文档**：[[设计模式高频考点与项目实战回答]]、[[观察者模式详解]]、[[发布订阅模式详解]]  
> **关联项目**：[[核心-直播性能优化]]、[[核心-自动剪辑性能优化]]、[[核心-预览延时优化]]

---

## 📌 第一部分：面试速记（考前 10 分钟扫一遍）

### 一句话核心

> 面向对象六大原则的本质不是背概念，而是让复杂系统做到**职责清晰、依赖稳定、扩展容易、替换安全、调用边界干净**。

### 先解释：什么叫高内聚、低耦合？

> **高内聚**：一个模块内部的代码都围绕同一个目标工作，职责集中，改一个功能时主要改这个模块内部。  
> **低耦合**：模块之间只通过清晰、稳定的接口协作，彼此不依赖对方的内部细节，一个模块变化时尽量不影响其他模块。

举个音视频 Pipeline 的例子：

```text
采集 -> 渲染 -> 编码 -> 封装 -> 推流 -> ABR / 丢帧策略
```

如果所有逻辑都塞进一个 `LiveManager`，它既管 GPU 渲染，又管 MediaCodec 编码，还管 RTMP 推流、弱网 ABR 和丢帧策略，这就是**低内聚、高耦合**：职责混乱，任何一处变化都可能影响全链路。

更好的做法是把模块拆开：

```text
Renderer          只负责渲染
VideoEncoder      只负责编码
NetworkSender     只负责发送
AbrController     只负责码率决策
DropStrategy      只负责丢帧策略
```

这些模块内部围绕单一目标工作，就是**高内聚**；模块之间只通过 `VideoFrame`、`EncodedPacket`、`IVideoEncoder`、`INetworkSender` 这类稳定接口通信，就是**低耦合**。

**🗣️ 面试可背版本：**
> “高内聚、低耦合是面向对象设计的核心目标。
> * 高内聚指的是一个模块内部职责集中，所有代码都围绕同一个业务目标；
> * 低耦合指的是模块之间通过稳定接口协作，而不是互相知道内部实现。  

> 比如音视频 Pipeline 里，渲染模块就专心处理 OpenGL / Surface，编码模块就专心处理 MediaCodec / FFmpeg，网络模块就专心处理推流。它们之间传递的是帧、packet 和状态事件，而不是互相调用内部对象。  
> 这样后面我从 `glReadPixels` 切到 AHardwareBuffer 零拷贝，主要影响送帧和编码适配层，不会把 ABR、网络发送、业务 UI 都拖进来一起改。”

### 高频考点速查清单

1. **SRP 单一职责原则**：一个类只负责一个变化原因，避免一个大类控制全链路。
2. **OCP 开闭原则**：新增能力优先扩展新实现，而不是频繁修改老代码。
3. **LSP 里氏替换原则**：子类替换父类后，不能破坏父类承诺的行为契约。
4. **ISP 接口隔离原则**：接口要小而专，调用方不应该依赖自己不用的方法。
5. **DIP 依赖倒置原则**：高层业务依赖抽象接口，底层细节依赖这些抽象实现。
6. **LoD 迪米特法则**：模块之间少知道彼此内部结构，只和直接朋友通信。
7. **SOLID 与六大原则关系**：SOLID 严格来说是五大原则，国内面试常把迪米特法则一起合称六大原则。

### 用一个直播 Pipeline 例子串起来理解

假设现在有一条 Android 直播链路：

```text
Camera / Texture -> GPU 渲染 -> 编码 -> 封装 -> 推流 -> ABR / 丢帧
```

如果写成一个万能类，大概会变成这样：

```cpp
class LiveManager {
public:
    void renderFrame();
    void encodeFrame();
    void sendPacket();
    void updateBitrate();
    void dropFrame();
};
```

这个类看起来方便，但它的问题是：GPU 渲染、MediaCodec 编码、网络发送、弱网 ABR、丢帧策略全部混在一起。后面只想把 `glReadPixels` 改成 AHardwareBuffer 零拷贝，也可能牵连编码、推流、业务 UI，这就是典型的**低内聚、高耦合**。

更合理的拆法是：

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void render(const VideoFrame& frame) = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool encode(const VideoFrame& frame) = 0;
    virtual bool setBitrate(int bitrate) = 0;
};

class INetworkSender {
public:
    virtual ~INetworkSender() = default;
    virtual bool send(const EncodedPacket& packet) = 0;
};

class IFrameSubmitter {
public:
    virtual ~IFrameSubmitter() = default;
    virtual bool submit(const VideoFrame& frame) = 0;
};
```

然后让主流程只依赖这些抽象：

```cpp
class LivePipeline {
public:
    LivePipeline(std::shared_ptr<IFrameSubmitter> submitter,
                 std::shared_ptr<IVideoEncoder> encoder,
                 std::shared_ptr<INetworkSender> sender)
        : submitter_(std::move(submitter)),
          encoder_(std::move(encoder)),
          sender_(std::move(sender)) {}

    bool pushFrame(const VideoFrame& frame) {
        if (!submitter_->submit(frame)) {
            return false;
        }
        return encoder_->encode(frame);
    }

private:
    std::shared_ptr<IFrameSubmitter> submitter_;
    std::shared_ptr<IVideoEncoder> encoder_;
    std::shared_ptr<INetworkSender> sender_;
};
```

这段代码可以对应到六大原则：

1. **SRP 单一职责原则**：`IRenderer` 只管渲染，`IVideoEncoder` 只管编码，`INetworkSender` 只管发送，职责不混在一个 `LiveManager` 里。
2. **OCP 开闭原则**：新增 `HardwareBufferSubmitter` 时，主要是新增一个实现类，而不是大改 `LivePipeline` 主流程。
3. **LSP 里氏替换原则**：`MediaCodecEncoder` 和 `SoftwareEncoder` 都实现 `IVideoEncoder`，但必须遵守同样的 PTS、错误返回、flush 行为契约，才能安全互换。
4. **ISP 接口隔离原则**：渲染模块不依赖 `send()`，网络模块不依赖 `render()`，调用方只看到自己真正需要的方法。
5. **DIP 依赖倒置原则**：`LivePipeline` 依赖 `IVideoEncoder`、`INetworkSender` 这些抽象，而不是直接依赖 `MediaCodecEncoder` 或 `RtmpSender`。
6. **LoD 迪米特法则**：上层不要写 `pipeline.getEncoder().getCodec().getInputSurface().getNativeWindow()` 这种链式调用，而是通过 `IFrameSubmitter::submit()` 或 `EncoderSurfaceBridge` 这类稳定入口完成送帧。
7. **SOLID 与六大原则关系**：上面前五条是 SOLID，迪米特法则是国内面试里常一起讲的补充原则。

再看新增零拷贝路径时的写法：

```cpp
class CpuCopySubmitter : public IFrameSubmitter {
public:
    bool submit(const VideoFrame& frame) override {
        // glReadPixels -> libyuv -> MediaCodec
        return true;
    }
};

class HardwareBufferSubmitter : public IFrameSubmitter {
public:
    bool submit(const VideoFrame& frame) override {
        // AHardwareBuffer / Surface -> MediaCodec
        return true;
    }
};
```

主流程不需要关心底层到底是 CPU 拷贝还是零拷贝：

```cpp
std::shared_ptr<IFrameSubmitter> submitter;

if (enableLiveZeroCopy) {
    submitter = std::make_shared<HardwareBufferSubmitter>();
} else {
    submitter = std::make_shared<CpuCopySubmitter>();
}

LivePipeline pipeline(submitter, encoder, sender);
```

**🗣️ 面试可背版本：**
> “我理解六大原则可以放到一条音视频 Pipeline 里看。单一职责要求渲染、编码、推流、ABR 各自独立；开闭原则要求新增零拷贝路径时尽量新增 `HardwareBufferSubmitter`，而不是改乱主流程；里氏替换要求软编和硬编实现同一个编码接口时，PTS、flush、错误处理语义必须一致；接口隔离要求渲染模块不依赖推流接口；依赖倒置要求 Pipeline 依赖 `IVideoEncoder` 这种抽象，而不是依赖 MediaCodec 细节；迪米特法则要求上层不要一路 get 到 `ANativeWindow` 这种底层对象。严格说 SOLID 是五大原则，国内面试常把迪米特法则一起叫六大原则。”

---

### 考点 1：面向对象设计的六大原则是什么？

**考察意图：**  
面试官不是想听你机械背英文缩写，而是看你能不能把设计原则落到真实工程架构里。

**底层原理：**  
软件系统里最贵的不是第一次写代码，而是后续不断改需求、换实现、修兼容问题。六大原则都是在控制“变化的影响范围”：让稳定的业务流程依赖稳定抽象，让易变的实现细节被隔离在局部。

**🗣️ 面试标准回答：**
> “面向对象六大原则核心是为了高内聚、低耦合。严格说 SOLID 是五大原则，包括单一职责、开闭、里氏替换、接口隔离和依赖倒置；国内面试经常再加一个迪米特法则，合称六大原则。  
> 单一职责强调一个类只做一类事；开闭原则强调新增能力尽量扩展而不是改老代码；依赖倒置强调业务流程依赖抽象而不是具体实现。  
> 里氏替换保证子类可以安全替换父类；接口隔离避免大接口污染实现类；迪米特法则要求模块之间减少链式依赖。  
> 在音视频 Pipeline 里，我会把采集、渲染、编码、推流、ABR、丢帧策略拆开，并通过接口组合，这样后面切换软硬编、增加零拷贝或弱网策略时，改动范围比较可控。”

**💡 实战案例补充（来自[[核心-直播性能优化]]）：**
> “4K 直播优化里，原来如果把 GPU 渲染、`glReadPixels`、`libyuv` 转换、MediaCodec 编码、网络推流都堆在一个大流程里，后面引入 Surface / AHardwareBuffer 零拷贝时会牵一发动全身。我的理解是，六大原则真正解决的是这种工程演进问题：让主 Pipeline 稳定，把具体送帧策略、编码策略、ABR 策略放到可替换模块里。”

**👨‍💻 面试官追问：**
> Q：SOLID 是五大原则，为什么你说六大原则？  
> A：SOLID 严格是 SRP、OCP、LSP、ISP、DIP 五个；国内很多教材和面试会额外加迪米特法则，统称面向对象六大原则。我会先把这个口径讲清楚，避免概念混乱。

---

### 考点 2：单一职责原则是不是“一个类只能有一个方法”？

**考察意图：**  
看你是否理解“职责”的粒度，而不是把原则机械化。

**底层原理：**  
单一职责里的“单一”不是代码行数少，也不是方法数量少，而是“只有一个变化原因”。如果一个类同时因为编码参数变化、网络策略变化、渲染方式变化而频繁修改，它就承担了多个职责。

**🗣️ 面试标准回答：**
> “单一职责不是说一个类只能有一个方法，而是一个类最好只对应一个变化方向。  
> 比如音视频 SDK 里，编码器参数变化、网络推流变化、GPU 渲染变化，这些变化原因不一样，就不应该都塞进同一个 `LiveManager`。  
> 我更倾向于把它拆成 `Renderer`、`VideoEncoder`、`NetworkSender`、`AbrController` 这类模块。  
> 这样以后只改 ABR 阈值，就不应该碰编码器；只换硬编路径，也不应该影响网络发送。”

**💡 实战案例补充（来自[[核心-直播性能优化]]）：**
> “在 4K 直播项目里，推流卡顿的根因可能来自 GPU 读回、VPU 编码、网络发送、弱网队列堆积。我们用控制变量法排查时，本质也依赖职责拆分：本地录制正常但推流卡，说明编码和渲染不是主因，问题更可能在网络发送和队列背压。如果模块职责混在一起，排查链路会非常痛苦。”

**👨‍💻 面试官追问：**
> Q：拆太细会不会过度设计？  
> A：会，所以我不会按“类越小越好”去拆，而是按三个工程信号判断：**变化原因是否独立、运行边界是否独立、排查时是否需要单独验证**。  
> 比如 4K 直播里，GPU 渲染、VPU 编码、网络发送、ABR 决策就值得拆开，因为它们变化原因完全不同：渲染可能因为滤镜或纹理格式变化，编码可能因为 MediaCodec 兼容性或 H.264/H.265 参数变化，网络发送可能因为 RTMP 队列和弱网变化，ABR 则根据队列水位动态调码率。  
> 更关键的是，它们的线程和性能瓶颈也不同。我们排查掉帧时，会用控制变量法区分是 `glReadPixels` 读回慢、编码器 drain 不及时，还是网络发送阻塞。如果这些逻辑都塞在一个 `LiveManager` 里，日志、耗时统计、开关灰度都很难做。  
> 但如果两个逻辑总是一起变化、生命周期一致、没有独立替换和独立测试价值，我就不会强拆。比如编码器内部的参数校验和创建 codec 可以放在同一个 `MediaCodecEncoder` 里，不需要为了“单一职责”再拆成十几个小类。我的原则是：拆分要降低定位问题和扩展能力的成本，而不是增加调用链和理解成本。

---

### 考点 3：开闭原则在项目里怎么体现？

**考察意图：**  
面试官想知道你是否真的做过可演进架构，而不是只会说“对扩展开放，对修改关闭”。

**底层原理：**  
开闭原则的重点是隔离稳定点和变化点。主流程越稳定，具体策略越容易替换。它通常依赖抽象接口、策略模式、工厂模式、配置路由共同实现。

**🗣️ 面试标准回答：**
> “开闭原则不是说老代码永远不能改，而是新增能力时，优先通过扩展新实现来完成，避免在主流程里不断堆 `if-else`。  
> 比如原来推流是 CPU 拷贝路径，后来要加 Surface 直通和 AHardwareBuffer 零拷贝路径，我不会把所有细节都塞进主 Pipeline。  
> 我会抽象一个 `FrameSubmitter` 或 `VideoEncoder` 接口，CPU 拷贝、Surface、HardwareBuffer 分别是不同实现。  
> 主流程只知道“提交一帧”，不知道底层到底是 `glReadPixels` 还是零拷贝，这样新增路径时主要是加类，而不是大面积改老代码。”

**💡 实战案例补充（来自[[核心-直播性能优化]]）：**
> “4K 直播项目里，原路径是 `glReadPixels` 读回 CPU，再通过 `libyuv::ABGRToI420` 转换后送编码器，单次可能有 30ms 级别开销。后面引入 `AHardwareBuffer` / `Surface` 路径时，如果主流程依赖抽象送帧接口，就可以用参数化路由控制 `enableLiveZeroCopy`，旧业务继续走旧路径，新业务扩展零拷贝路径，风险会小很多。”

**👨‍💻 面试官追问：**
> Q：开闭原则和策略模式是什么关系？  
> A：开闭原则是设计目标，策略模式是常见实现手段。通过统一接口封装多个策略，新增策略时不改主流程，达到对扩展开放、对修改关闭。

---

### 考点 4：里氏替换原则为什么在 SDK 里特别重要？

**考察意图：**  
看你是否理解“接口背后的行为契约”，而不是只会写继承和多态。

**底层原理：**  
接口定义的不只是方法签名，还隐含输入输出语义、生命周期、线程约束、错误处理方式。子类只要破坏这些约定，即使编译通过，也会让调用方在运行时出问题。

**🗣️ 面试标准回答：**
> “里氏替换原则说的是，子类对象替换父类对象之后，程序行为不能被破坏。  
> 在 SDK 里这点很重要，因为我们经常用接口屏蔽软编、硬编、Mock 实现。  
> 比如 `IVideoEncoder` 约定了输入帧不能被修改、PTS 必须保持单调、失败通过错误码返回、`flush` 后不再接收新帧。  
> 那么 `MediaCodecEncoder` 和 `FFmpegSoftwareEncoder` 都必须遵守这些行为契约。  
> 如果硬编实现偷偷改了 PTS，或者失败时直接 crash，虽然它实现了接口，但从设计上已经违反了里氏替换。”

**💡 实战案例补充（来自[[核心-自动剪辑性能优化]]）：**
> “自动剪辑项目里，抽帧、渲染、推理会形成异构硬件并行流水线。如果把抽帧模块抽象成统一接口，那么 VPU 硬解抽帧和 FFmpeg 软解抽帧都必须保证同样的帧 ID 语义。我们后来把帧 ID 作为绝对索引贯穿流程，就是为了避免不同实现之间用浮点时间戳转换导致 999ms 和 1001ms 抽到不同帧，这本质上也是在维护接口契约。”

**👨‍💻 面试官追问：**
> Q：里氏替换和多态有什么区别？  
> A：多态解决“能不能替换”的语法问题，里氏替换解决“替换后行为是否正确”的语义问题。实现了同一个接口不代表一定符合里氏替换。

---

### 考点 5：接口隔离原则和单一职责原则有什么区别？

**考察意图：**  
看你是否能区分“类的职责”和“接口的依赖边界”。

**底层原理：**  
单一职责关注一个模块为什么变化；接口隔离关注调用方被迫知道多少东西。一个类可能职责清晰，但如果它暴露了一个巨大的接口，也会让调用方形成不必要依赖。

**🗣️ 面试标准回答：**
> “单一职责关注类本身的变化原因，接口隔离关注调用方不应该依赖自己用不到的方法。  
> 比如渲染模块只需要 `render(frame)`，它不应该被迫依赖 `send(packet)`、`setBitrate()`、`flushEncoder()` 这些编码和网络相关接口。  
> 在音视频项目里，我会把 `IRenderer`、`IVideoEncoder`、`INetworkSender`、`IAbrController` 拆开。  
> 这样每个模块只暴露必要能力，编译依赖更少，单元测试和替换实现也更简单。”

**💡 实战案例补充（来自[[核心-预览延时优化]]）：**
> “车载预览低时延优化里，AJB 只应该关心 packet 或 frame 的时间戳、接收时间、目标缓冲和丢帧策略，不应该依赖解码器内部的 H.264 Profile 细节，也不应该依赖 UI 渲染控件。接口隔离做得好，才能把时间轴映射、EMA 平滑、Resync 这些逻辑单独验证。”

**👨‍💻 面试官追问：**
> Q：接口拆得太多会不会很碎？  
> A：会，所以接口隔离不是盲目拆分，而是按调用者视角拆。多个调用者使用完全不同的方法集合时，就说明接口可能太胖；如果总是一起使用，就没必要强行拆。

---

### 考点 6：依赖倒置原则和依赖注入是一回事吗？

**考察意图：**  
看你是否能区分设计原则和具体实现方式。

**底层原理：**  
依赖倒置是原则：高层模块和低层模块都依赖抽象。依赖注入是实现手段：把具体依赖从外部传入，而不是在类内部 `new`。没有抽象的依赖注入只能减少创建耦合，不能真正降低设计耦合。

**🗣️ 面试标准回答：**
> “依赖倒置和依赖注入不是一回事。  
> 依赖倒置是设计原则，强调高层业务不要直接依赖底层细节，而是依赖抽象接口。  
> 依赖注入是实现方式，比如通过构造函数把 `IVideoEncoder` 注入到 `LivePipeline` 里。  
> 如果我注入的还是具体的 `MediaCodecEncoder`，那只是把创建位置换了，并没有真正倒置依赖。  
> 真正的关键是 Pipeline 依赖 `IVideoEncoder` 这种稳定抽象，具体的硬编、软编、Mock 编码器都在抽象后面替换。”

**💡 实战案例补充（来自[[核心-直播性能优化]]）：**
> “4K 直播项目里，业务主流程不应该直接依赖 `MediaCodec` 的各种设备兼容细节。更好的方式是让 Pipeline 依赖 `IVideoEncoder`、`IFrameSubmitter`、`INetworkSender` 这些抽象。这样后面通过 JNI 调 Java `MediaCodec.setParameters(Bundle)` 做动态码率，或者为了兼容性保留旧路径，都不会污染主流程。”

**👨‍💻 面试官追问：**
> Q：什么时候不需要依赖倒置？  
> A：如果某个对象非常稳定、没有替换需求、也不是跨模块边界，比如简单的值对象或局部工具函数，就没必要为了原则强行抽接口。

---

### 考点 7：迪米特法则在复杂 Pipeline 里怎么避免“链式调用灾难”？

**考察意图：**  
看你是否能识别模块之间过度暴露内部结构的问题。

**底层原理：**  
链式调用看起来方便，但会让上层知道下层内部层级。一旦内部结构变化，上层调用全要改。迪米特法则要求对象只和直接朋友通信，必要时用 Facade 或 Bridge 封装底层细节。

**🗣️ 面试标准回答：**
> “迪米特法则可以理解成少知道原则。一个模块不应该一路 `getEncoder().getCodec().getInputSurface().getNativeWindow()` 去操作底层对象。  
> 这种写法说明上层知道太多内部结构，后面底层从 Surface 换成 AHardwareBuffer，或者从 Java MediaCodec 换成 NDK 封装，上层都会被迫改。  
> 我更倾向于封装一个 `EncoderSurfaceBridge` 或者 `FrameSubmitter`，上层只调用 `submit(frame)` 或 `attachSurface(config)`。  
> 这样底层怎么拿 `ANativeWindow`、怎么处理 Sync Fence、怎么兼容机型，都被隔离在桥接层里。”

**💡 实战案例补充（来自[[核心-直播性能优化]]）：**
> “零拷贝改造时，如果业务层直接操作 `AHardwareBuffer` 的 usage flag、`EGLImage`、`GL_TEXTURE_EXTERNAL_OES`、编码器 Surface，那业务层会和硬件细节绑死。更合理的是让一层 SurfaceWriter / HardwareBufferSubmitter 负责这些细节，上层只表达‘我要把这一帧送到编码器’。”

**👨‍💻 面试官追问：**
> Q：迪米特法则和封装有什么关系？  
> A：迪米特法则是封装在对象协作层面的体现。封装不是把字段设成 private 就结束了，还要避免调用方通过一串 getter 理解你的内部结构。

---

## ⛳ 项目实战话术（STAR 法则版）

### 话术 1：4K 直播零拷贝重构里如何体现六大原则？

> **S（背景）**：在 Android 4K 全景直播项目里，原链路是 GPU 渲染后通过 `glReadPixels` 读回 CPU，再用 `libyuv` 转 I420，最后送 MediaCodec 编码。4K 场景下单次读回可能有 30ms 级开销，直播从 22fps 逐步掉到 5fps，发热也很明显。  
> **T（任务）**：目标是引入 GPU 到 VPU 的零拷贝路径，同时不能破坏旧业务路径，还要保留弱网 ABR 和 Smart Drop GOP 兜底能力。  
> **A（行动）**：架构上我按单一职责拆出渲染、送帧、编码、推流、ABR、丢帧策略；主 Pipeline 依赖 `IFrameSubmitter` 和 `IVideoEncoder` 抽象；CPU 拷贝、Surface 直通、AHardwareBuffer 路径作为不同实现；ABR 和 DropStrategy 独立于编码器。这样新增零拷贝路径主要是扩展实现，而不是重写主流程。  
> **R（结果）**：这套设计让新旧路径可以通过参数化开关独立启用，便于灰度和回滚；同时把 30ms 级 CPU 读回从主路径移除，为帧率恢复和发热下降打下基础。

### 话术 2：自动剪辑异构并行 Pipeline 里如何体现接口契约？

> **S（背景）**：自动剪辑 3.0 中，算法送帧频率从 600ms 提升到 200ms，抽帧、渲染、推理、拼接都可能成为瓶颈。  
> **T（任务）**：我们需要把 VPU 抽帧、GPU 渲染、NPU/CPU 推理解耦成并行流水线，同时保证不同实现下帧定位一致。  
> **A（行动）**：我会把抽帧模块抽象成稳定接口，并明确行为契约：输入是绝对帧 ID，输出必须对应同一个视频语义帧，而不是靠浮点时间戳来回转换。Smart Seek、顺解丢帧、普通 seek 都只是不同实现策略。  
> **R（结果）**：这样既符合依赖倒置和开闭原则，也符合里氏替换原则。后面优化 Seek 策略时，不会影响渲染和推理模块，避免了 999ms / 1001ms 这种时间戳误差导致的抽帧不一致。

---

## 🚧 雷区与加分项

**❌ 雷区（千万别说）：**

- “SOLID 就是六大原则。”这句话不严谨，SOLID 严格是五大原则，六大原则通常额外包含迪米特法则。
- “单一职责就是一个类只能有一个方法。”这是机械理解，真正判断标准是变化原因。
- “开闭原则就是不能改老代码。”实际工程里老代码当然会改，重点是稳定主流程，隔离变化点。
- “依赖倒置就是多写接口。”如果接口没有替换价值，或者抽象不稳定，只是在制造复杂度。
- “里氏替换就是继承。”继承只是语法，里氏替换关注替换后的行为契约。

**✅ 加分项（说出来眼前一亮）：**

- 能主动说明 “SOLID 五大原则 + 迪米特法则 = 国内常说六大原则”。
- 能把原则落到音视频 Pipeline：采集、渲染、编码、推流、ABR、丢帧策略。
- 能讲清楚“开闭原则是目标，策略模式 / 工厂模式 / 依赖倒置是实现手段”。
- 能强调行为契约：PTS 单调、frame 生命周期、flush 语义、错误返回方式。
- 能反向论证“为什么不是所有地方都要抽接口”，体现不过度设计。

---

## 📚 第二部分：原理深讲（吃透底层）

### 1. 基础概念：为什么设计原则比设计模式更底层？

设计模式是“常见问题的经验解法”，比如观察者模式解决一对多通知，策略模式解决算法替换，Builder 解决复杂对象构建。

设计原则更底层，它回答的是：为什么这个模式是合理的？为什么这里应该抽象？为什么这里不应该直接依赖具体类？

比如策略模式之所以常用，是因为它通常同时满足：

- **开闭原则**：新增策略不改主流程。
- **依赖倒置原则**：主流程依赖策略接口。
- **里氏替换原则**：不同策略必须遵守同一行为契约。
- **接口隔离原则**：策略接口只暴露调用方需要的方法。

所以面试里如果只说“我用了策略模式”，深度一般；如果能继续说“它解决了哪个变化点，符合哪些原则，有什么行为契约”，就会更像真实做过架构设计。

---

### 2. 六大原则之间的关系

可以把六大原则理解成三层：

**第一层：模块内部怎么组织**

- 单一职责原则：一个模块不要承担多个变化原因。
- 接口隔离原则：一个接口不要让调用方依赖无关能力。

**第二层：模块之间怎么依赖**

- 依赖倒置原则：高层业务依赖抽象，而不是依赖底层实现。
- 迪米特法则：模块之间减少对内部结构的了解。

**第三层：模块替换和演进是否安全**

- 开闭原则：新增能力时优先扩展新实现。
- 里氏替换原则：替换实现时不破坏原有行为契约。

在音视频项目里，这三层分别对应：

- 内部组织：渲染、编码、推流、ABR 拆成独立模块。
- 依赖关系：Pipeline 依赖 `IVideoEncoder`，不依赖 `MediaCodecEncoder`。
- 安全演进：软编 / 硬编 / Mock 编码器都遵守同一套 PTS、flush、错误返回契约。

---

### 3. 用代码看六大原则怎么落地

#### 3.1 不好的写法：巨型 LiveManager

```cpp
class LiveManager {
public:
    void startPush() {
        // 采集
        // GPU 渲染
        // glReadPixels 读回 CPU
        // libyuv 转 I420
        // MediaCodec 编码
        // RTMP 推流
        // 队列水位判断
        // 动态码率
        // Smart Drop GOP
    }

    void setBitrate(int bitrate);
    void renderFrame(void* texture);
    void encodeFrame(void* frame);
    void sendPacket(void* packet);
    void dropGop();
};
```

这段代码的问题不是“类太长”这么简单，而是它同时违反了多个原则：

- 违反单一职责：采集、渲染、编码、网络、ABR 都在一个类里。
- 违反接口隔离：任何调用方拿到 `LiveManager` 都能看到所有能力。
- 违反开闭原则：新增零拷贝路径时大概率继续改 `startPush()`。
- 违反依赖倒置：主流程直接依赖具体编码、推流、渲染细节。
- 违反迪米特法则：上层可能通过 `LiveManager` 间接操作各种内部对象。

#### 3.2 更合理的写法：稳定 Pipeline + 可替换策略

```cpp
struct VideoFrame {
    int64_t ptsUs = 0;
    int width = 0;
    int height = 0;
    void* nativeHandle = nullptr;
};

struct EncodedPacket {
    int64_t ptsUs = 0;
    bool keyFrame = false;
};

class IFrameSubmitter {
public:
    virtual ~IFrameSubmitter() = default;
    virtual bool submit(const VideoFrame& frame) = 0;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool encode(const VideoFrame& frame) = 0;
    virtual bool setBitrate(int bitrate) = 0;
    virtual bool flush() = 0;
};

class INetworkSender {
public:
    virtual ~INetworkSender() = default;
    virtual bool send(const EncodedPacket& packet) = 0;
};

class IAbrController {
public:
    virtual ~IAbrController() = default;
    virtual int updateBitrate(int queueSize, int currentBitrate) = 0;
};

class LivePipeline {
public:
    LivePipeline(std::shared_ptr<IFrameSubmitter> submitter,
                 std::shared_ptr<IVideoEncoder> encoder,
                 std::shared_ptr<INetworkSender> sender,
                 std::shared_ptr<IAbrController> abr)
        : submitter_(std::move(submitter)),
          encoder_(std::move(encoder)),
          sender_(std::move(sender)),
          abr_(std::move(abr)) {}

    bool pushFrame(const VideoFrame& frame) {
        if (!submitter_->submit(frame)) {
            return false;
        }
        return encoder_->encode(frame);
    }

private:
    std::shared_ptr<IFrameSubmitter> submitter_;
    std::shared_ptr<IVideoEncoder> encoder_;
    std::shared_ptr<INetworkSender> sender_;
    std::shared_ptr<IAbrController> abr_;
};
```

这段代码体现的原则：

- `LivePipeline` 只组织主流程，不处理每个底层细节，符合单一职责。
- `IFrameSubmitter`、`IVideoEncoder`、`INetworkSender` 分开，符合接口隔离。
- Pipeline 依赖接口，不依赖具体类，符合依赖倒置。
- 新增 `HardwareBufferSubmitter` 或 `SurfaceSubmitter` 时，不需要改 Pipeline 主逻辑，符合开闭原则。
- 所有 `IVideoEncoder` 实现都必须遵守同样的 PTS、flush、错误处理契约，符合里氏替换。

---

### 4. 每个原则在音视频项目中的典型映射

#### 4.1 SRP：拆的是变化原因，不是代码行数

在直播系统里，下面这些变化原因应该尽量隔离：

- 编码器变化：软编、硬编、H.264、H.265、码率调整。
- 渲染变化：OpenGL ES、Vulkan、滤镜链、贴图格式。
- 送帧变化：CPU copy、Surface、AHardwareBuffer、GPU Blit。
- 网络变化：RTMP、SRT、WebRTC、弱网重传。
- QoS 变化：ABR、Smart Drop GOP、队列水位策略。

如果这些都在一个类里，任何一处优化都会扩大回归范围。

#### 4.2 OCP：新增能力不要污染主流程

典型场景是送帧路径演进：

```text
旧路径：GPU -> glReadPixels -> CPU RGBA -> libyuv -> I420 -> MediaCodec
新路径：GPU -> Surface / AHardwareBuffer -> MediaCodec
折中路径：GPU -> GL_TEXTURE_EXTERNAL_OES -> Encoder Surface
```

如果主流程只依赖 `IFrameSubmitter`，那么这些路径只是不同实现。新增路径时，主流程稳定，变化集中在策略实现和工厂选择里。

#### 4.3 LSP：替换实现必须保证行为一致

以 `IVideoEncoder` 为例，接口契约至少包括：

- 输入帧的所有权：编码器不能随意修改调用方仍在使用的帧。
- PTS 语义：输出 packet 的 PTS 必须和输入 frame 的媒体时间一致。
- 线程模型：是否允许多线程调用要明确。
- 错误处理：失败返回错误码，而不是某个实现直接 abort。
- flush 语义：flush 后是否还能继续接收帧，要所有实现一致。

这些约定比函数签名更重要。函数签名只保证“能调用”，行为契约保证“能替换”。

#### 4.4 ISP：接口要按调用方视角拆

不要设计这种胖接口：

```cpp
class IMediaEngine {
public:
    virtual void render(const VideoFrame& frame) = 0;
    virtual void encode(const VideoFrame& frame) = 0;
    virtual void send(const EncodedPacket& packet) = 0;
    virtual void setBitrate(int bitrate) = 0;
    virtual void updateJitterBuffer(int delayMs) = 0;
};
```

因为渲染模块不需要知道推流，AJB 不需要知道编码参数，网络模块也不应该依赖渲染方法。

更合理的是按调用方拆小接口：

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void render(const VideoFrame& frame) = 0;
};

class IJitterController {
public:
    virtual ~IJitterController() = default;
    virtual int calculateTargetDelay(int networkDelayMs) = 0;
};
```

#### 4.5 DIP：依赖抽象是为了隔离不稳定细节

Android 音视频里不稳定细节很多：

- 不同机型 MediaCodec 行为不同。
- 有的设备支持 H.265，有的只支持 H.264。
- 有的路径支持 Surface，有的需要 CPU fallback。
- 动态码率在 Java `MediaCodec.setParameters(Bundle)` 和 NDK API 上兼容性不同。

这些都不应该泄漏到业务主流程。主流程应该只依赖稳定接口，具体兼容逻辑放在实现层。

#### 4.6 LoD：不要让上层理解底层对象树

不推荐：

```cpp
pipeline.getEncoder()
        .getCodec()
        .getInputSurface()
        .getNativeWindow()
        .setBufferCount(3);
```

推荐：

```cpp
encoderSurfaceBridge->configureSurface(surfaceConfig);
```

或者：

```cpp
frameSubmitter->submit(frame);
```

前者让上层知道太多内部结构，后者把底层细节封装在一个稳定入口后面。

---

### 5. 性能分析与典型陷阱

**陷阱 1：为了开闭原则疯狂抽象，导致性能路径变复杂**

在音视频实时链路里，抽象不能影响热路径性能。比如每帧 30fps / 60fps 调用的函数，要注意虚调用、锁、堆分配、引用计数的开销。原则是：热路径接口可以抽象，但对象创建、策略选择、复杂路由不要放在每帧循环里。

**陷阱 2：接口抽象了，但行为契约没写清楚**

这会导致“表面符合 DIP，实际违反 LSP”。例如软解输出线性内存，硬解输出 Surface；如果 `VideoFrame` 没有明确内存类型和生命周期，上层很容易误用。

**陷阱 3：职责拆分后线程边界不清**

音视频 Pipeline 常见多线程：渲染线程、编码线程、网络线程、JNI 回调线程。如果只拆类，不定义线程归属和对象生命周期，反而会引入竞态。比如观察者回调要用 `shared_ptr` 局部保活，JNI 对象要用 `WeakGlobalRef` 防泄漏。

**陷阱 4：用设计原则掩盖不必要的复杂度**

如果一个模块只有一种实现，短期没有替换需求，也不是跨模块边界，不必急着抽接口。设计原则服务于变化，不是为了让代码看起来“架构很强”。

---

### 6. 实战代码示例：编码策略选择

下面是一段更贴近面试白板的示例，用策略模式体现 OCP、DIP、LSP。

```cpp
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool open(int width, int height, int bitrate) = 0;
    virtual bool encode(const VideoFrame& frame) = 0;
    virtual bool setBitrate(int bitrate) = 0;
    virtual bool close() = 0;
};

class MediaCodecEncoder final : public IVideoEncoder {
public:
    bool open(int width, int height, int bitrate) override {
        // 初始化 Android MediaCodec 硬编
        return true;
    }

    bool encode(const VideoFrame& frame) override {
        // 约定：不修改 frame，保持 PTS 语义
        return true;
    }

    bool setBitrate(int bitrate) override {
        // 实际项目可通过 Java MediaCodec.setParameters(Bundle) 做动态码率
        return true;
    }

    bool close() override {
        return true;
    }
};

class SoftwareEncoder final : public IVideoEncoder {
public:
    bool open(int width, int height, int bitrate) override {
        // 初始化 FFmpeg / x264 软编
        return true;
    }

    bool encode(const VideoFrame& frame) override {
        // 同样遵守 IVideoEncoder 的输入和 PTS 契约
        return true;
    }

    bool setBitrate(int bitrate) override {
        return true;
    }

    bool close() override {
        return true;
    }
};

std::unique_ptr<IVideoEncoder> createEncoder(bool preferHardware) {
    if (preferHardware) {
        return std::make_unique<MediaCodecEncoder>();
    }
    return std::make_unique<SoftwareEncoder>();
}
```

这段代码在面试里可以这样讲：

> “`LivePipeline` 不直接依赖 `MediaCodecEncoder`，而是依赖 `IVideoEncoder`，这是依赖倒置。  
> 新增一个 `Av1HardwareEncoder` 或者 `DebugMockEncoder` 时，只要实现同一个接口，主流程不用改，这是开闭原则。  
> 但所有实现都必须遵守同样的帧生命周期、PTS、flush、错误处理契约，否则就违反里氏替换原则。”

---

### 7. 为什么不用“一个万能 Engine 类”？

反向论证时可以这样说：

> “万能 Engine 类短期写起来快，但长期会变成所有变化的汇聚点。编码器兼容性、Surface 生命周期、JNI 回调、网络弱网、ABR、丢帧策略都在里面，任何一个小改动都可能影响主链路。  
> 音视频项目最怕这种隐式耦合，因为问题通常不是编译时报错，而是运行 10 分钟后掉帧、发热、音画不同步或者某个机型崩溃。  
> 所以我不会为了设计而设计，但会把高频变化点拆出来：编码策略、送帧路径、网络发送、QoS 策略、回调观察者管理，这些都值得独立抽象。”

---

### 8. 进阶问题与延伸阅读

**面试官可能继续深挖：**

- 设计原则和设计模式的关系是什么？
- 策略模式如何体现开闭原则？
- 依赖倒置和依赖注入有什么区别？
- C++ 里接口用纯虚类有什么成本？
- Android JNI 回调里如何结合观察者模式和弱引用避免泄漏？
- 音视频 Pipeline 的模块拆分边界怎么确定？

**建议关联复习：**

- [[设计模式高频考点与项目实战回答]]
- [[观察者模式详解]]
- [[发布订阅模式详解]]
- [[C++锁面试速记与原理详解]]

---

## 🎯 一句话总结

> 六大原则的价值不是让代码“看起来面向对象”，而是让音视频 Pipeline 在新增能力、替换实现、定位问题时，改动范围始终可控。
