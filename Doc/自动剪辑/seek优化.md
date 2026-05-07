# FrameReaderInternal Seek 优化设计文档




---

## 1. 问题背景

当前 `FrameReaderInternal::Seek` 在 demuxer 层使用 **`AVSEEK_FLAG_BACKWARD`**（通过 `MediaAsset::Seek` 封装），并在每次 Seek 后调用 **`videoOutput_->Flush()`**。

典型场景：**GOP = 500ms**，关键帧在 0ms、500ms、1000ms…

| 操作 | 行为 |
|------|------|
| Seek 100ms | 回退到 ≤100ms 的最近关键帧 → **0ms** → Flush → 从 0ms 解码 |
| Seek 300ms | 同上 → **0ms** → Flush → 再次从 0ms 解码 |
| Seek 480ms | 同上 → **0ms** → Flush → 再次从 0ms 解码 |

**现象**：同一 GOP 内多次顺序 Seek 会 **重复解码 0→目标** 区间的帧，CPU/GPU 解码与内存带宽浪费明显；导出多帧、时间线 scrub 等场景下问题放大。

---

## 2. 原实现分析（根因）

### 2.1 解码流程与 GOP 结构

- **视频压缩依赖参考关系**：除 I 帧外，P/B 帧依赖前向（或双向）参考帧；解码器内部维护 **DPB / 参考帧队列**。
- **Seek 语义**：要显示时间 \(T\) 的帧，解码器必须从 **该 GOP 起始可独立解码帧** 起顺序解码到 \(T\)。

因此：**从 0ms 解码到 480ms 是「正确性所需」的最小解码路径之一**，不是 bug，而是 **H.264/H.265 参考链** 的必然结果。

### 2.2 BACKWARD Seek 与时间戳

- **`AVSEEK_FLAG_BACKWARD`**：Seek 到 **不大于目标时间戳的最近关键帧**，保证解码器从 **干净入口** 进入 GOP。
- **Flush 的必要性**：Demuxer 位置跳变后，解码器内 **未输出的参考帧与缓冲包** 若不清空，会与 **新 GOP 数据** 混用，导致花屏、崩溃或 PTS 错乱。故 **跨 GOP 或任意 demuxer Seek 后 Flush 是标准做法**。

### 2.3 根因归纳

| 层级 | 问题 |
|------|------|
| **正确性** | 同一 GOP 内从 I 解码到目标时间是必须的；BACKWARD + Flush 保证状态一致。 |
| **性能** | **未复用** 已解码结果：每次 Seek 都 **丢弃** 解码器已推进到的状态，重新从 GOP 头解码。 |

**结论**：性能问题的根因是 **「状态丢弃」**。

---

## 3. 优化方案设计：智能前向 Seek (Smart Seek)

**原则**：不推翻 `MediaAsset` + `MediaFrameOutput` 架构；**增量** 增加 **智能 Seek 决策**。

### 3.1 为什么选择“前向解码”而不是“GOP 帧缓存”？

最初考虑过维护一个 `std::map<PTS, Frame>` 的 GOP 内缓存。但存在致命风险：
1. **解码器状态断层**：如果从缓存返回了 300ms 的帧，但此时解码器实际在 480ms，那么下一次调用 `NextVideoFrame` 时，解码器会吐出 481ms 的帧，而不是 301ms 的帧。这破坏了上层顺序读取的契约。
2. **硬件解码器兼容性与 Buffer 耗尽风险**：硬件解码器（如 Android MediaCodec）输出的 `MediaSample` 内部 Buffer 往往归解码器底层的固定内存池所有。
   - `Flush` 后这些 Buffer 可能被强制回收，导致缓存的帧变成悬垂指针（引发花屏或崩溃）。
   - **如果上层（如缓存层）一直持有这些 `MediaSample` 不释放，而解码又在同时进行，会不会内存溢出？** 
     - 对于**硬件解码器**，通常不会直接导致传统意义上的内存溢出（OOM），而是会导致**解码器卡死（Stall / Starvation）**。因为硬件解码器的输出 Buffer 数量是极其有限的（通常只有 4~16 个）。如果上层缓存持有了过多的帧不释放，解码器就会因为拿不到空闲的输出 Buffer 而永远阻塞，导致整个解码流水线挂起。
     - 对于**软件解码器**（如 FFmpeg 软解），帧内存通常是在堆（Heap）上动态分配的，如果上层无节制地持有大量帧，则确实会导致真正的**内存溢出（OOM）**。

**结论**：最安全、且完美解决 `100 -> 300 -> 480` 顺序 Seek 问题的方案是 **Smart Seek（前向顺序解码）**。

### 3.2 Smart Seek 核心思路

- 维护 **`currentDecodedSrcTimeMs_`**，记录解码器当前实际输出的最新时间戳。
- 当上层请求 **`Seek(targetMs, accurate=true)`** 时：
  - 计算距离：`distance = targetMs - currentDecodedSrcTimeMs_`
  - **如果 `0 <= distance <= 1500ms`**（阈值可配）：
    - **不调用** `asset_->Seek`（不动 Demuxer）。
    - **不调用** `videoOutput_->Flush`（不清空解码器）。
    - 直接在一个 `while` 循环中调用 `NextVideoFrameInternal`，**顺序解码并丢弃中间帧**，直到解码出 `outSrcTime >= targetMs` 的帧。
  - **如果 `distance < 0` (回退) 或 `distance > 1500ms` (跨度太大)**：
    - 走经典路径：`asset_->Seek` + `Flush` + 从关键帧重新解码。

**优化类型**：**性能优化**。

---

## 4. 核心代码改动

### 4.1 状态记录 (`frame_reader_internal.h` / `.cc`)

```cpp
// .h 中新增私有变量
double currentDecodedSrcTimeMs_ = -1.0;
double forwardSeekThresholdMs_ = 1500.0;

// .cc 的 SetMeta 中更新当前进度
void FrameReaderInternal::SetMeta(std::shared_ptr<ins::MediaSample> &outSample) {
    if(outSample != nullptr) {
        auto srcTsMs = TimestampToMsDouble(outSample->GetFrame()->pts, videoTrack_->Timebase());
        currentDecodedSrcTimeMs_ = srcTsMs; // 记录当前解码进度
        // ...
    }
}
```

### 4.2 智能 Seek 分支 (`frame_reader_internal.cc`)

```cpp
sp<Error> FrameReaderInternal::Seek(const double srcTimeMs, const bool accurate) {
    CHECK(init_) <<" seek but not init";
    seekSrcTimeMs_ = srcTimeMs;

    // --- 智能前向 Seek (Smart Seek) 优化 ---
    double currentPos = currentDecodedSrcTimeMs_ < 0 ? 0.0 : currentDecodedSrcTimeMs_;
    double distance = srcTimeMs - currentPos;
    
    // 仅针对 accurate=true 生效，且要求在阈值内、未到 EOF
    bool canDecodeForward = (accurate && distance >= 0.0 && distance <= forwardSeekThresholdMs_ && !readEof_ && !decodeEof_);

    if (canDecodeForward) {
        while (true) {
            std::shared_ptr<ins::MediaSample> outSample;
            auto error = NextVideoFrameInternal(outSample, false);
            if (error != kNoError) return error;
            
            if (outSample != nullptr) {
                auto frame = outSample->GetFrame();
                auto outSrcTime = TimestampToMsDouble(frame->pts, videoTrack_->Timebase());
                if (greateEqual(outSrcTime, srcTimeMs)) {
                    seekAccurateSample = std::move(outSample);
                    break;
                }
            } else {
                return NewErrorWithDescription(...);
            }
        }
        return kNoError; // 成功前向 Seek，跳过 Flush
    }
    // --- 智能前向 Seek 结束 ---

    // ... 经典 Seek (asset_->Seek + Flush) ...
}
```

---

## 5. 性能对比分析（定性 + 量级估算）

假设 30fps，GOP 500ms，约 **15 帧/GOP**，同 GOP 内顺序 Seek 3 次到 100/300/480ms。

| 指标 | 优化前 (Classic) | 优化后 (Smart Seek) |
|------|--------|------------------------|
| 解码帧数（3 次 Seek accurate） | ≈ 4 + 10 + 15 = **29** 帧（重复从 0 起） | 第一次 ≈4，第二次 ≈6，第三次 ≈5 → 共 **≈15** 帧 |
| Flush 次数 | 3 | **1**（仅第一次进 GOP） |
| Demuxer Seek | 3 | **1** |

**收益**：消除了 `O(N^2)` 的重复解码开销，变成了 `O(N)` 的顺序推进。对于导出多帧、连续提取缩略图等场景，性能提升显著。

---

## 6. 风险与边界情况

| 风险 | 说明 | 缓解 |
|------|------|------|
| **阈值设置不当** | 若 `forwardSeekThresholdMs_` 设得太大（如 5000ms），前向解码 150 帧可能比直接 Seek+Flush 还要慢。 | 默认设置为 `1500ms`（约 45 帧），这是一个经验上的安全平衡点。提供了 `SetForwardSeekThreshold` 供上层调优。 |
| **`accurate=false` 的处理** | 若 `accurate=false`，上层期望跳到关键帧。如果走 Smart Seek 会返回非关键帧。 | 逻辑中已限定 `canDecodeForward` 必须满足 `accurate == true`。 |
| **EOF 状态** | 接近文件尾时前向解码可能触发 EOF。 | 逻辑中已检查 `!readEof_ && !decodeEof_`，且 `NextVideoFrameInternal` 能正确向上传递 EOF 错误。 |

---

## 7. 后续可优化方向

### 7.1 导出多帧批处理 (Batch Export)
如果上层（如 `SingleReaderFrameOutput`）知道需要导出的所有时间点列表 `[t1, t2, ..., tn]`，可以提供一个批量接口。底层只需一次 Seek 到 `t1` 所在 GOP，然后顺序解码，在经过 `t1, t2...` 时分别输出，完全避免中间的 Seek 调用。

### 7.2 关键帧索引表 (Index Table)
如果需要在超长视频中频繁远距离 Seek，可以预先扫描或解析 MP4/MOV 的 moov atom 获取关键帧时间戳数组。这样在决定是“前向解码”还是“Seek+Flush”时，可以精确知道目标时间是否跨越了 GOP 边界，从而做出绝对最优的决策（取代目前的固定阈值猜测）。
