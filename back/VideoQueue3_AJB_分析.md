# QueueSampleGroup3 面试复习版（AJB 实时预览）

## 1. 这份文档怎么用

这份文档不是“代码注释扩写版”，而是**面试复习版**。  
目标是让你在 2~5 分钟内，能把 `QueueSampleGroup3` 讲清楚：

- 它解决了什么问题
- 为什么要这么设计
- 参数怎么取舍
- 怎么证明效果
- 遇到问题怎么排查

---

## 2. 30 秒电梯回答（面试开场模板）

> 我在相机实时预览链路里设计了 `QueueSampleGroup3`，核心是一个轻量 AJB（Adaptive Jitter Buffer）调度器。  
> 它先建立 PTS 到本地 monotonic 时钟的映射，然后用“快升慢降”的 EMA 动态维护目标缓冲。  
> 在正常情况下平滑渲染，在积压时进入追赶模式（缩短等待、必要丢帧），在时间轴异常时重同步。  
> 结果是：低延时更稳定、首帧延时不再被长期继承、弱网下不会越播越慢。

---

## 3. 一分钟完整回答（面试通用模板）

> 这个方法主要解决实时预览中的三类问题：  
> 1) 帧到达抖动导致渲染节奏不稳；  
> 2) 首帧偶发高延时被后续流程继承；  
> 3) 队列积压时继续等待导致端到端延时滚大。  
>   
> 我用的方案是：  
> - 用 `basePts/baseMono` 把媒体时间映射到本地时间轴；  
> - 通过 `observedDelay = now - expected` 估计当前链路延时；  
> - 用快升慢降更新 `targetBuffer`，并做 deadband + 限幅；  
> - 每帧计算 `waitTime`，在积压时把等待压缩到 2ms，严重迟到则丢帧；  
> - 出现 PTS 跳变或异常超长等待时 resync。  
>   
> 这是一个“低延时优先”的预览策略，不追求零丢帧，而追求**可恢复实时性**。

---

## 4. 你要先讲清楚的业务背景

### 4.1 场景特征

- 相机实时预览（camera preview）
- 用户体感优先级：**跟手性（低延时） > 绝对平滑 > 零丢帧**

### 4.2 常见抖动来源

- 网络传输波动（Wi-Fi）
- 解码耗时波动（I 帧重）
- 渲染线程抢占

### 4.3 为什么“固定等待”不够

- 固定等待无法适配动态抖动
- 积压时继续等待会进一步加大延时
- 首帧高延时容易被固化

---

## 5. 核心设计原理（面试官最关注）

## 5.1 时间轴映射（PTS -> Monotonic）

定义一对锚点：

- `queue3BasePtsMs_`
- `queue3BaseMonoMs_`

映射公式：

`expectedMonoMs = queue3BaseMonoMs_ + (sampleTimeMs - queue3BasePtsMs_)`

解释：把媒体时间统一投影到本地时钟坐标系，后续所有调度决策都在一个时钟域内完成。

---

## 5.2 延时观测

`observedDelayMs = nowMs - expectedMonoMs`

含义：

- 正值：帧相对理论时刻偏晚
- 负值：帧相对理论时刻偏早

这个量是 AJB 的输入。

---

## 5.3 AJB 快升慢降（Asymmetric EMA）

误差定义：

`errMs = observedDelayMs - queue3TargetBufferMs_`

更新策略：

- `errMs > 0`（变差）：`target = 0.8 * observed + 0.2 * target`（快升）
- `errMs < 0`（恢复）：`target = alpha * observed + (1-alpha) * target`（慢降）
  - warmup: `alpha=0.2`
  - steady: `alpha=0.1`

为什么这么做：

- 变差时必须快速跟随，防卡顿
- 恢复时必须慢慢回收，防振荡

---

## 5.4 Deadband + Clamp

- deadband：`|err| <= 2ms` 不更新，避免抖动敏感
- min/max clamp：防止目标缓冲过小（连续迟到）或过大（高延时长尾）

---

## 5.5 等待、追赶、丢帧

截止时间：

`deadline = expected + targetBuffer - smoothDelta`

等待时间：

`waitTime = deadline - now`

策略：

- 正常：`waitTime > 0` 则等待
- 积压（`pendingLeft > 0`）：等待上限压到 `2ms`，主动追赶
- 严重迟到且积压：丢帧，防止延时滚大

---

## 5.6 重同步（Resync）

触发条件（示例）：

- PTS 回退或跳变（`ptsGap < -5ms` 或 `> 2000ms`）
- 异常超长等待（`waitTime > 250ms`）

动作：

- 重建 `basePts/baseMono`
- 重置目标缓冲到合理区间

---

## 6. 面试高频追问与模板答案

### Q1：AJB 平滑的到底是什么？

**答法模板：**

> 平滑的是“时间轴上的调度目标”，不是像素内容。  
> 具体是平滑 `observedDelay` 的波动，把它转成相对稳定的 `targetBuffer`，再驱动每帧 `waitTime`，从而让渲染节奏稳定且可恢复低延时。

---

### Q2：为什么不是直接硬重置时钟？

**答法模板：**

> 硬重置恢复快，但会引入明显节奏突变。  
> AJB 快升慢降能在稳定性和低延时之间做连续过渡，体感更自然；同时我保留了异常场景的 resync 兜底。

---

### Q3：为什么允许丢帧？

**答法模板：**

> 这是实时预览，不是离线转码。  
> 在积压场景中，不丢帧会把历史延时全部背下来，用户看到的是“始终慢半拍”。  
> 适度丢帧能换回实时性，体验更符合预览目标。

---

### Q4：你怎么证明这个策略有效？

**答法模板：**

> 我会用 A/B 对比四类指标：  
> 1) 端到端延时中位数/95 分位；  
> 2) 队列积压峰值；  
> 3) 丢帧率；  
> 4) 卡顿次数（渲染间隔异常）。  
> 目标是延时显著下降，同时卡顿不恶化。

---

### Q5：首帧延时过大怎么纠正？

**答法模板：**

> 首帧直接渲染、仅建立基线，不额外等待；  
> 后续由 slow-down 回收目标缓冲；  
> 若出现积压再触发追赶与丢帧，避免首帧延时长期继承。

---

## 7. 参数讲解（面试可背版）

- `smoothDeltaMs_`：人为前置量，减体感延时  
- `kDeadbandMs=2`：小误差免更新，防抖  
- `fast-up=0.8`：抖动恶化时快速升缓冲  
- `slow-down alpha=0.2/0.1`：恢复时渐进回收  
- `minBufferMs`：避免目标过小导致连续迟到  
- `maxBufferMs`：避免目标过大导致延时长尾  
- `backpressure cap=2ms`：积压场景追赶实时  
- `dropThresholdMs`：判定“严重迟到是否丢帧”  
- `resync threshold=250ms`：异常保护阈值

---

## 8. 面试中的架构取舍怎么讲

### 8.1 目标函数

不是“零丢帧”，而是：

- 低延时（P0）
- 可恢复稳定（P1）
- 平滑（P2）

### 8.2 取舍逻辑

- 稳态：偏平滑
- 弱网/积压：偏追赶
- 异常：偏纠错（resync）

这是一套状态感知的动态策略，而不是单一固定参数。

---

## 9. 故障排查手册（实战）

### 9.1 现象：延时回不来

优先检查：

- `maxBufferMs` 是否过大
- `slow-down alpha` 是否过小
- 是否长期 `pendingLeft > 0`

### 9.2 现象：画面节奏抖

优先检查：

- deadband 是否过小
- `slow-down alpha` 是否过大
- `smoothDeltaMs_` 是否过激

### 9.3 现象：频繁重同步

优先检查：

- 上游 PTS 质量（回退/跳变）
- 重同步阈值是否过严
- 是否存在跨线程时序异常

---

## 10. 面试时可主动加分的点

- 你把策略和旧逻辑隔离（`QueueSampleGroup3` 独立状态），可灰度上线
- 你考虑了异常保护（resync）而非只讲 happy path
- 你能给出量化验证指标（P50/P95 延时、积压、丢帧、卡顿）
- 你能解释“为什么这个业务允许丢帧”

---

## 11. 速记卡（最后 10 秒）

> `QueueSampleGroup3 = 时间轴映射 + AJB 快升慢降 + 背压追赶 + 异常重同步`  
> 面向实时预览，以低延时可恢复为核心，必要时用丢帧换实时性。

---

## 12. 源码附录（面试前快速对照）

### 12.1 方法入口（独立新路径）

文件：`bmgmedia/src/main/cpp/arvbmg/camerapreview/CameraVideoQueue.cc`

```cpp
void CameraVideoQueue::QueueSampleGroup3(const sp<VideoSampleGroup> &sampleGroup) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(quit_) {
            return;
        }
    }
    auto pending = ++pendingTask3_;
    if(pending > gQueueSize) {
        LOG(ERROR) << "camera video queue3 pending too much( " << pending << ")";
    }
    dispatch_.AddAsyncTask([=] {
        // ...
    });
}
```

### 12.2 AJB 核心更新（快升慢降）

```cpp
constexpr double kDeadbandMs = 2.0;
double errMs = observedDelayMs - queue3TargetBufferMs_;
if(std::abs(errMs) > kDeadbandMs) {
    if(errMs > 0.0) {
        queue3TargetBufferMs_ = 0.8 * observedDelayMs + 0.2 * queue3TargetBufferMs_;
    } else {
        double alpha = queue3WarmupFrames_ < 10 ? 0.2 : 0.1;
        queue3TargetBufferMs_ = alpha * observedDelayMs + (1.0 - alpha) * queue3TargetBufferMs_;
    }
}
```

### 12.3 背压与丢帧

```cpp
if(pendingLeft > 0) {
    waitTimeMs = std::min(waitTimeMs, 2.0);
}

double dropThresholdMs = std::max(frameIntervalMs_ * 1.2, 40.0);
if(waitTimeMs < -dropThresholdMs && pendingLeft > 0) {
    dropFrame = true;
}
```

### 12.4 重同步

```cpp
if(waitTimeMs > 250.0) {
    queue3BasePtsMs_ = sampleTimeMs;
    queue3BaseMonoMs_ = nowMs;
    queue3TargetBufferMs_ = std::max(minBufferMs, smoothDeltaMs_);
    waitTimeMs = 0.0;
}
```

---

## 13. 一页纸面试回答（可直接背诵）

> 我在实时预览链路中实现了 `QueueSampleGroup3`，它是独立于旧逻辑的新调度器。  
> 核心做法是先做 PTS 到 monotonic 的时间轴映射，再基于 `observedDelay` 用快升慢降的 AJB 更新目标缓冲。  
> 每帧由 `deadline - now` 计算等待时间；积压时进入追赶模式，压缩等待并在必要时丢帧；出现 PTS 跳变或超长等待则重同步。  
> 这个设计的价值是：首帧高延时不再长期继承、弱网下不越播越慢、整体低延时更稳定。  
> 我用 P50/P95 延时、丢帧率、积压峰值、卡顿次数做 A/B 验证。

# CameraVideoQueue3 AJB 新手指南

## 1. 这份文档讲什么

这份文档解释 `CameraVideoQueue::QueueSampleGroup3` 的设计思想和实现逻辑，目标读者是第一次接触音视频实时调度的同学。

读完你应该能回答：

- 这个方法要解决什么问题？
- 什么是 AJB（Adaptive Jitter Buffer）？
- 为什么要“快升慢降”？
- 遇到首帧延时过大、队列积压、时间戳跳变时，代码怎么处理？
- 关键参数怎么调？

---

## 2. 背景问题：实时预览为什么会“越播越慢”

相机预览场景里，帧并不是严格均匀到达。常见抖动来源有：

- 网络波动（Wi-Fi 抖动）
- 编解码耗时波动（例如 I 帧更重）
- 渲染线程偶发卡顿

如果调度器只按固定时钟等待，会出现两个问题：

1. 首帧如果晚到很多，后面会把这份延时“继承”下去；
2. 当队列已经积压时还继续等待，会进一步增加端到端延时。

`QueueSampleGroup3` 的目标是：**稳定优先，但低延时更优先**。

---

## 3. 一句话理解 QueueSampleGroup3

`QueueSampleGroup3` 做的事情可以概括成：

1. 建立一个“媒体时间(PTS) -> 本地单调时钟”的映射；
2. 用 AJB 动态维护一个“目标缓冲时间”；
3. 每帧计算“该等多久”；
4. 若积压就主动追赶，必要时丢帧；
5. 遇到时间轴异常就重建基线（resync）。

---

## 4. 关键概念（先懂这些再看代码）

### 4.1 PTS（`sampleTimeMs`）

帧在媒体流里的时间戳，表示“这帧在视频时间轴上的位置”。

### 4.2 本地时间（`nowMs`）

来自 `CLOCK_MONOTONIC` 的本地时钟，单调递增，用于等待和调度。

### 4.3 时间轴基线（`queue3BasePtsMs_`, `queue3BaseMonoMs_`）

它们是一对锚点，用来把 PTS 映射到本地时间：

`expectedMonoMs = queue3BaseMonoMs_ + (sampleTimeMs - queue3BasePtsMs_)`

可以理解为：先把“媒体时间”搬到“本地时钟坐标系”里。

### 4.4 观测延时（`observedDelayMs`）

`observedDelayMs = nowMs - expectedMonoMs`

表示当前帧相对理论渲染时刻“晚了多少/早了多少”。

### 4.5 目标缓冲（`queue3TargetBufferMs_`）

AJB 核心状态。它决定我们希望保留多少缓冲来对抗抖动。

- 大一点：更稳，延时高
- 小一点：更实时，抗抖能力弱

---

## 5. “快升慢降”到底是什么

当观测延时突然升高时，目标缓冲要**快速跟上**，避免卡顿：

- Fast-up: `target = 0.8 * observed + 0.2 * target`

当观测延时下降（网络恢复）时，目标缓冲要**慢慢回收**，避免来回抖动：

- Slow-down: `target = alpha * observed + (1-alpha) * target`
- warmup 阶段 `alpha=0.2`，稳态阶段 `alpha=0.1`

这就是“快升慢降”的本质：**先稳住，再降延时**。

---

## 6. 代码流程（按一次帧处理顺序）

### 步骤 1：入口保护

如果已经 `quit_`，直接返回，不再调度。

### 步骤 2：异步执行 + 积压计数

帧通过 `dispatch_.AddAsyncTask` 异步处理，并用 `pendingTask3_` 统计积压程度。

### 步骤 3：首帧初始化

首次进入时：

- 建立映射基线（`basePts/baseMono`）
- 初始化目标缓冲（下限为 `smoothDeltaMs_`）
- `waitTimeMs=0`，首帧直接渲染，减少首屏时间

### 步骤 4：普通帧计算

1. 检查 `ptsGapMs`，若发生大跳变（回退或过大前跳），重建基线；
2. 计算 `expectedMonoMs` 与 `observedDelayMs`；
3. 用快升慢降更新 `queue3TargetBufferMs_`；
4. 对目标缓冲做限幅（最小/最大值）；
5. 计算最终等待时间：
   - `deadline = expected + targetBuffer - smoothDelta`
   - `waitTime = deadline - now`

### 步骤 5：追赶与丢帧

若 `pendingLeft > 0`（说明还有积压）：

- 等待时间上限压到 2ms（优先追赶实时）
- 若当前帧已严重迟到，则丢帧（防止越播越慢）

### 步骤 6：异常重同步

若 `waitTimeMs > 250ms`，认为时间轴可能异常，重建基线并立即渲染。

---

## 7. 为什么它能纠正“首帧延时过大”

核心原因有三点：

1. 首帧不等待直接渲染，不额外叠加等待；
2. 后续靠 AJB 的 slow-down 逐步回收目标缓冲；
3. 积压时启用追赶模式（最多等 2ms + 必要丢帧）快速拉回实时。

所以首帧高延时不会被永久“钉死”。

---

## 8. 关键参数速查表

- `smoothDeltaMs_`
  - 作用：渲染前置量，减小体感延时
  - 过大：可能带来抖动
  - 过小：实时性下降

- `kDeadbandMs = 2.0`
  - 作用：小误差不更新，防止目标缓冲抖动

- `fast-up = 0.8`
  - 作用：抖动变坏时快速升高缓冲，优先稳定

- `slow-down alpha = 0.2(warmup) / 0.1(steady)`
  - 作用：网络恢复时逐步降延时，防振荡

- `minBufferMs = min(frameIntervalMs_, 10.0)`
  - 作用：给缓冲下限，避免目标过小导致连环迟到

- `maxBufferMs = max(frameIntervalMs_ * 3.0, 120.0)`
  - 作用：给缓冲上限，避免目标过大导致长尾延时

- `backpressure wait cap = 2.0ms`
  - 作用：积压时快速追赶

- `dropThresholdMs = max(frameIntervalMs_ * 1.2, 40.0)`
  - 作用：判断是否“严重迟到到该丢帧”

- `resync threshold = 250ms`
  - 作用：异常长等待保护，强制重建基线

---

## 9. 与旧方法的关系

`QueueSampleGroup3` 是独立新路径：

- 不改 `QueueSampleGroup`
- 不改 `QueueSampleGroup2`
- 使用独立状态变量（`queue3*`）

因此可灰度接入、可 A/B 对比，风险可控。

---

## 10. 新手调参建议（先从这组起步）

### 30fps 预览

- `smoothDeltaMs_`: 8~12
- `maxBufferMs`: 100~140
- `dropThresholdMs`: 35~45

### 60fps 预览

- `smoothDeltaMs_`: 4~8
- `maxBufferMs`: 80~120
- `dropThresholdMs`: 20~30

建议每次只改 1~2 个参数，并记录：

- 端到端延时
- 丢帧率
- 卡顿次数
- 队列积压峰值

---

## 11. 常见误区

- 误区 1：把 `slow-down` 调太大（例如 0.4）
  - 结果：目标缓冲忽上忽下，画面节奏不稳

- 误区 2：完全禁止丢帧
  - 结果：弱网下延时不断累积，实时预览体验更差

- 误区 3：`maxBufferMs` 过大
  - 结果：稳定是稳定了，但延时回不来

---

## 12. 总结

`QueueSampleGroup3` 的设计核心是：  
**用 AJB 管理“时间缓冲”，用追赶/丢帧控制“实时性”，用重同步处理“异常时间轴”。**

它不是追求“永不丢帧”，而是追求实时预览里更重要的目标：  
**低延时、可恢复、可控抖动。**

---

## 13. 附录：相关源码（可直接对照阅读）

### 13.1 头文件接口与状态变量

```cpp
/// 相机预览新调度器（独立于旧流程）
void QueueSampleGroup3(const sp<VideoSampleGroup> &sampleGroup);

std::atomic_int pendingTask3_{0};
std::mutex queue3Mutex_;
bool queue3Started_ = false;
double queue3BasePtsMs_ = 0.0;
double queue3BaseMonoMs_ = 0.0;
double queue3TargetBufferMs_ = 0.0;
double queue3LastPtsMs_ = 0.0;
int queue3WarmupFrames_ = 0;
```

### 13.2 `QueueSampleGroup3` 方法实现

```cpp
void QueueSampleGroup3(const sp<VideoSampleGroup> &sampleGroup) {
    /**
     * QueueSampleGroup3 设计目标：
     * 1) 不影响旧版 QueueSampleGroup/QueueSampleGroup2 的行为；
     * 2) 以“低延时优先”为实时预览策略；
     * 3) 用快升慢降(Asymmetric EMA)平滑抖动，避免首帧延时被长期继承；
     * 4) 在积压(backpressure)时主动追赶，而不是继续等待导致延时滚大。
     */
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(quit_) {
            return;
        }
    }

    auto pending = ++pendingTask3_;
    if(pending > gQueueSize) {
        LOG(ERROR) << "camera video queue3 pending too much( " << pending << ")";
    }

    dispatch_.AddAsyncTask([=] {
        auto pendingLeft = --pendingTask3_;
        double sampleTimeMs = sampleGroup->GetMediaTimeMs();
        double nowMs = videoClock_->GetCurrentTimeMs();
        double waitTimeMs = 0.0;
        bool dropFrame = false;

        {
            std::unique_lock<std::mutex> lock(queue3Mutex_);
            if(!queue3Started_) {
                queue3Started_ = true;
                queue3BasePtsMs_ = sampleTimeMs;
                queue3BaseMonoMs_ = nowMs;
                queue3TargetBufferMs_ = std::max(0.0, smoothDeltaMs_);
                queue3LastPtsMs_ = sampleTimeMs;
                queue3WarmupFrames_ = 1;
                waitTimeMs = 0.0;
            } else {
                double ptsGapMs = sampleTimeMs - queue3LastPtsMs_;
                if(ptsGapMs < -5.0 || ptsGapMs > 2000.0) {
                    queue3BasePtsMs_ = sampleTimeMs;
                    queue3BaseMonoMs_ = nowMs;
                    queue3TargetBufferMs_ = std::max(0.0, smoothDeltaMs_);
                    queue3WarmupFrames_ = 1;
                }

                double expectedMonoMs = queue3BaseMonoMs_ + (sampleTimeMs - queue3BasePtsMs_);
                double observedDelayMs = nowMs - expectedMonoMs;

                if(queue3WarmupFrames_ == 1 && queue3TargetBufferMs_ <= 0.0) {
                    queue3TargetBufferMs_ = std::max(0.0, observedDelayMs);
                }

                constexpr double kDeadbandMs = 2.0;
                double errMs = observedDelayMs - queue3TargetBufferMs_;
                if(std::abs(errMs) > kDeadbandMs) {
                    if(errMs > 0.0) {
                        queue3TargetBufferMs_ = 0.8 * observedDelayMs + 0.2 * queue3TargetBufferMs_;
                    } else {
                        double alpha = queue3WarmupFrames_ < 10 ? 0.2 : 0.1;
                        queue3TargetBufferMs_ = alpha * observedDelayMs + (1.0 - alpha) * queue3TargetBufferMs_;
                    }
                }

                double minBufferMs = std::min(frameIntervalMs_, 10.0);
                double maxBufferMs = std::max(frameIntervalMs_ * 3.0, 120.0);
                queue3TargetBufferMs_ = std::max(minBufferMs, std::min(queue3TargetBufferMs_, maxBufferMs));

                double deadlineMs = expectedMonoMs + queue3TargetBufferMs_ - smoothDeltaMs_;
                waitTimeMs = deadlineMs - nowMs;

                if(pendingLeft > 0) {
                    waitTimeMs = std::min(waitTimeMs, 2.0);
                }

                double dropThresholdMs = std::max(frameIntervalMs_ * 1.2, 40.0);
                if(waitTimeMs < -dropThresholdMs && pendingLeft > 0) {
                    dropFrame = true;
                }

                if(waitTimeMs > 250.0) {
                    queue3BasePtsMs_ = sampleTimeMs;
                    queue3BaseMonoMs_ = nowMs;
                    queue3TargetBufferMs_ = std::max(minBufferMs, smoothDeltaMs_);
                    waitTimeMs = 0.0;
                }

                queue3LastPtsMs_ = sampleTimeMs;
                queue3WarmupFrames_++;
            }
        }

        if(dropFrame) {
            if(isDebug()) {
                LOG(INFO) << std::fixed << "queue3 drop late frame sampleTimeMs " << sampleTimeMs
                          << " pendingLeft " << pendingLeft;
            }
            return;
        }

        if(!renderAtOnce_ && waitTimeMs > 0.0) {
            if(!Wait(waitTimeMs)) {
                return;
            }
        }
        RenderSample(sampleGroup);
    });
}
```