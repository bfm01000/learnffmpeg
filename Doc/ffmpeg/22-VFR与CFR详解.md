# 22 - VFR 与 CFR 详解

## 0. 本篇定位

| 项 | 说明 |
|---|---|
| 面试位置 | 帧率与时间戳专题：固定帧率、可变帧率和转码/播放/seek 的影响。 |
| 先背什么 | VFR 为什么不能按 frame_index/fps 算时间，转 CFR 会带来什么代价。 |
| 深入怎么学 | 结合 time_base、PTS、duration、音视频同步和抽帧任务理解。 |
| 关联阅读 | 17、24 |

---

> **重要度**：🔥🔥🔥 面试高频，尤其是 Seek、抽帧、剪辑场景
> **前置**：[[17-FFmpeg-Seek详解]] | [[01-数据结构与生命周期]]
> **关联**：[[05-H264-MP4-NALU]]（MP4 容器 stss 与 VFR 的关系）

---

## 面试速答模板（30 秒口语化回答）

### Q：VFR 和 CFR 有什么区别？VFR 下 Seek 怎么处理？

> "CFR 是固定帧率，每一帧的间隔完全一样；VFR 是可变帧率，帧间隔不固定，可能从 16ms 跳到 66ms。
>
> 核心区别在于 Seek：**Seek 全链路是'帧号 → PTS → av_seek_frame → 顺解'，第一步帧号转 PTS 在 CFR 下用公式 `frameId × 帧间隔` 就够了，VFR 下帧间隔不固定，公式算出来的 PTS 跟真实值越偏越远，偏几十帧都有可能。**
>
> 解决方案是初始化时全量 demux 扫描建索引——`av_read_frame` 不解码只读 packet 头，把每帧的 PTS 按顺序 push 进数组，下标就是 frameId。Seek 时直接查表拿真实 PTS，零公式、零误差。这也是 ExoPlayer 和剪映的做法。"

---

## 一、什么是 CFR 和 VFR

### 1.1 CFR（Constant Frame Rate，固定帧率）

视频的每一帧之间间隔**完全相同**。

```
帧序号:    0      1      2      3      4      5
PTS(ms):   0     33     66    100    133    166
帧间隔:       33     33     33     33     33     ← 全是 33ms
```

**典型场景**：专业摄像机拍摄、后期剪辑输出、传统广播电视。

### 1.2 VFR（Variable Frame Rate，可变帧率）

视频的帧之间间隔**不固定**，会变化。

```
帧序号:    0      1      2      3      4      5
PTS(ms):   0     33     66    100    133    200
帧间隔:       33     33     34     33     67     ← 不固定！最后一下跳了 67ms
```

**典型场景**：
| 场景 | 为什么产生 VFR |
|:---|:---|
| 手机录屏（Android ScreenRecord） | 屏幕静止时不产生帧，帧间隔可达几百 ms |
| iPhone 省电模式 | 系统降帧率，从 30fps 降到 15fps 再升回来 |
| 视频会议录制（WebRTC） | 网络拥塞时主动丢帧 |
| 监控摄像头 | 画面静止时降低编码帧率节省带宽 |
| OBS 采集游戏画面 | GPU 渲染时间不稳定，帧间隔波动 |
| 部分 Android 设备 Camera 输出 | ISP 处理时间波动 |

### 1.3 数学表达

```
CFR:  PTS(n) = n × Δ         其中 Δ 是常数（帧间隔）

VFR:  PTS(n) ≠ n × Δ         Δ 随 n 变化，不是常数
      PTS(n+1) - PTS(n) ≠ PTS(n) - PTS(n-1)
```

**CFR 是 VFR 的特例**——VFR 的帧间隔可以偶尔波动，也可能完全均匀。把 VFR 视频当成 CFR 处理不一定每次都错，但它是不可靠的。

---

## 二、VFR 对音视频开发的影响（五大核心问题）

### 问题 1：Seek 失效——帧号转 PTS 漂移

这是最致命的问题。Seek 的本质链路是：

```
frameId=100 → ??? → PTS → av_seek_frame(pts) → 顺解 → 目标帧
                ↑
          这一步崩了
```

**CFR 路径（可靠）**：
```cpp
int64_t target_pts = frame_id * frame_dur_ticks;  // 100 × 3003 = 300300 ✓
```

**VFR 路径（不可靠）**：
```cpp
double avg_dur = (double)total_dur_ticks / total_frames;  // 平均帧间隔
int64_t target_pts = frame_id * avg_dur;  // 100 × 3200 = 320000
// 但真实 PTS 可能是 335000 —— 偏了 15000 ticks ≈ 160ms ≈ 5 帧!
```

**偏差为什么越往后越大**：

```
真实视频（VFR）的前 5 帧:
frameId:    0      1      2      3      4      5
真实PTS:    0    3000   6000   9000  12000  18000  ← 第 4→5 跳了 6000 ticks
帧间隔:       3000   3000   3000   3000   6000

用公式 frameId × 4000（平均间隔）:
算出的PTS:  0    4000   8000  12000  16000  20000
真实PTS:    0    3000   6000   9000  12000  18000
偏差:       0   +1000  +2000  +3000  +4000  +2000  ← 误差累积，不回零
```

帧间隔的波动会**按 frameId 倍数放大**——越往后偏得越离谱。

### 问题 2：帧计数不对——`nb_frames` 不可靠

```cpp
// ❌ 这个公式在 VFR 下直接崩
int total_frames = (int)(duration_sec * avg_frame_rate);

// 原因：avg_frame_rate 本身就是估计值
// MP4 的 moov/trak/mdia/mdhd 里的 timescale 和 duration
// 只是"总时长 × timescale"，不是"每帧固定间隔 × 帧数"
```

**FFmpeg 里跟帧率相关的字段有四个，含义各不相同**：

| 字段 | 来源 | CFR 下 | VFR 下 |
|:---|:---|:---|:---|
| `AVStream.avg_frame_rate` | 总帧数/总时长 | 准确等于真实帧率 | **不准确**，是平均值 |
| `AVStream.r_frame_rate` | 容器声明的最低帧率 | 同 CFR | **不准**，很多 VFR 文件这个值写死了 |
| `AVStream.time_base` | 容器定义的时间单位 | 跟帧率无关 | 跟帧率无关 |
| `AVCodecContext.framerate` | 编码器写入 | 准确 | **不保证** |

**唯一可靠的帧计数方式**：`av_read_frame` 完完整整扫一遍，计数器自增。

### 问题 3：音视频同步累积偏移

音频是连续的（采样率固定 48000Hz），视频帧间隔却时大时小。音视频同步算法通常以音频为基准调整视频显示时间。VFR 下帧间隔突变会让同步算法误判——到底是丢帧了还是正常间隔？频繁误判导致**画面抖动**。

### 问题 4：`av_seek_frame` 的 `AVSEEK_FLAG_FRAME` 也不保险

```cpp
// FFmpeg 提供按帧号 Seek
av_seek_frame(fmt, vidx, target_frame_id, AVSEEK_FLAG_FRAME);
```

这个 API 内部仍然依赖解码器维护的 sample 表（MP4 stts）。对大多数 MP4/MKV 是准的，但：
- 部分容器（FLV、TS）对 `AVSEEK_FLAG_FRAME` 支持不好
- FFmpeg 内部也是走"帧号→PTS→Seek"的换算路径，只是把换算做在了 demuxer 内部

### 问题 5：VFR 转 CFR 时的帧采样策略选择

```
VFR:  帧在时间轴上不均匀分布
CFR:  帧在时间轴上均匀分布

转换时有三种策略:
  - 丢帧：VFR 帧密度 > 目标 CFR → 丢弃多余的
  - 补帧：VFR 帧密度 < 目标 CFR → 复制上一帧
  - 重新采样：生成新帧（时间插值），计算量大
```

---

## 三、如何判断一个视频是 VFR 还是 CFR

### 3.1 读 AVStream 的帧率字段

```cpp
AVStream* vs = fmt_ctx->streams[vidx];
AVRational fr = vs->avg_frame_rate;

// 如果 num 和 den 是标准帧率，大概率是 CFR
// 比如 30000/1001 (29.97), 25/1 (25), 24000/1001 (23.976)
// 但如果 avg_frame_rate ≠ r_frame_rate，可能是 VFR

bool looks_like_vfr = (vs->avg_frame_rate.num != vs->r_frame_rate.num ||
                       vs->avg_frame_rate.den != vs->r_frame_rate.den);
```

但这不是可靠的——很多 VFR 文件的这两个字段被写入端写死了。

### 3.2 真正可靠的方法：计算帧间隔的方差

```cpp
// 完整 demux 扫描，记录每一帧的 pts
std::vector<int64_t> deltas;
std::vector<int64_t> pts_list;

AVPacket* pkt = av_packet_alloc();
int frame_count = 0;
while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == vidx) {
        if (frame_count > 0) {
            deltas.push_back(pkt->pts - pts_list.back());
        }
        pts_list.push_back(pkt->pts);
        frame_count++;
    }
    av_packet_unref(pkt);
}
av_packet_free(&pkt);

// 如果所有 delta 完全相同 → CFR
// 如果 delta 有变化 → VFR
bool is_cfr = std::adjacent_find(deltas.begin(), deltas.end(),
                                  std::not_equal_to<>()) == deltas.end();
```

### 3.3 ffprobe 命令行

```bash
# 看帧率字段
ffprobe -v quiet -select_streams v:0 \
  -show_entries stream=avg_frame_rate,r_frame_rate,duration,nb_frames \
  input.mp4

# 逐帧导出 pts —— 最准确的方式
ffprobe -v quiet -select_streams v:0 \
  -show_entries packet=pts,pts_time,duration,duration_time \
  -of csv=p=0 input.mp4 | head -30

# 看帧间隔是否一致
ffprobe -v quiet -select_streams v:0 \
  -show_entries packet=pts_time \
  -of csv=p=0 input.mp4 \
  | awk 'NR>1 {diff=$1-prev; print diff} {prev=$1}'
# 如果输出不全是同一个数 → VFR
```

---

## 四、解决方案全景

### 4.1 方案对比

| 方案 | 适用场景 | 精确度 | 实现复杂度 | 额外开销 |
|:---|:---|:---|:---|:---|
| **全量 Demux 扫描建索引** | 所有场景 | 100% | 低 | 初始化一次性，10min 4K ≈ 150ms |
| **`AVSEEK_FLAG_FRAME`** | 仅限 MP4/MKV，CFR 为主 | 高 | 极低 | 无 |
| **VFR→CFR 转换（ffmpeg -vf fps）** | 预处理，对精度要求不极致 | 中 | 无（调命令行） | 需重新编码或 remux |
| **容错兜底（多抽几帧）** | 临时止血 | 低 | 极低 | 每次 Seek 多解几帧 |
| **帧级别索引 + `AVSEEK_FLAG_BYTE`** | 顶级方案，大厂做法 | 100% | 中 | 初始建索引 |

### 4.2 方案一：全量 Demux 扫描建索引（推荐）

这是业界标准做法，VFR/CFR 完全透明。

```cpp
// ===== 数据结构 =====
struct FrameIndex {
    std::vector<int64_t> frame_to_pts_;   // [frameId] → 真实 PTS
    std::vector<int>     keyframe_ids_;   // 所有关键帧的 frameId
    int total_frames_ = 0;
    bool is_vfr_ = false;

    // 构建索引：全量 demux，不解码
    int build(AVFormatContext* fmt, int vidx) {
        AVPacket* pkt = av_packet_alloc();
        int64_t prev_pts = AV_NOPTS_VALUE;

        av_seek_frame(fmt, vidx, 0, AVSEEK_FLAG_BYTE);

        while (av_read_frame(fmt, pkt) >= 0) {       // ← 只 demux，不解码
            if (pkt->stream_index != vidx) {
                av_packet_unref(pkt);
                continue;
            }

            frame_to_pts_.push_back(pkt->pts);       // frameId = push_back 次数即下标

            if (pkt->flags & AV_PKT_FLAG_KEY)
                keyframe_ids_.push_back(total_frames_);

            // VFR 检测：相邻帧间隔是否一致
            if (prev_pts != AV_NOPTS_VALUE && !is_vfr_) {
                int64_t cur_delta  = pkt->pts - prev_pts;
                int64_t prev_delta = (frame_to_pts_.size() >= 2)
                    ? frame_to_pts_[total_frames_] - frame_to_pts_[total_frames_ - 1]
                    : cur_delta;
                if (cur_delta != prev_delta) is_vfr_ = true;
            }
            prev_pts = pkt->pts;
            total_frames_++;
            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        return total_frames_;
    }

    // 精确 Seek：O(1) 查表
    int seek_to_frame(AVFormatContext* fmt, int vidx, int target_fid) {
        if (target_fid < 0 || target_fid >= total_frames_)
            return -1;

        int64_t target_pts = frame_to_pts_[target_fid];  // ← 零计算，直接查表

        // 二分找最近关键帧
        auto it = std::upper_bound(keyframe_ids_.begin(),
                                    keyframe_ids_.end(), target_fid);
        int kf_id = (it == keyframe_ids_.begin()) ? 0 : *(--it);

        return av_seek_frame(fmt, vidx,
                             frame_to_pts_[kf_id],
                             AVSEEK_FLAG_BACKWARD);
    }
};
```

**为什么 VFR 下 `frameId = push_back 计数器` 是正确的**：

```
av_read_frame 按时间顺序逐个输出 packet:
  第 1 次 while → pkt.pts =     0 → push_back → frameToPts[0] = 0
  第 2 次 while → pkt.pts =  3003 → push_back → frameToPts[1] = 3003
  第 3 次 while → pkt.pts =  6006 → push_back → frameToPts[2] = 6006
  第 4 次 while → pkt.pts = 12012 → push_back → frameToPts[3] = 12012
  ↑ 帧间隔从 3003 跳到 6006，但 frameId 仍然是 0,1,2,3

查 frameId=3 → frameToPts[3] = 12012 → 100% 拿到第 3 帧
```

CFR 和 VFR 在这个索引里**没有区别**——frameId 是计数器，跟帧间隔无关。

**全量扫描耗时可接受吗？**

```
| 素材规格           | 总帧数  | 纯 demux 耗时 | 等效吞吐      |
|:------------------|:--------|:-------------|:-------------|
| 1080p30, 10min    | 18,000  | ~100 ms      | 180,000 帧/秒 |
| 4K30, 10min       | 18,000  | ~180 ms      | 100,000 帧/秒 |
| 4K60, 30min       | 108,000 | ~800 ms      | 135,000 帧/秒 |
```

**纯 demux 只是读容器层的 packet 头，不碰解码器**，10 分钟 4K 素材不到 200ms，一次性的开销换来后续每次 Seek 的 O(1) 查表。

### 4.3 方案二：`AVSEEK_FLAG_FRAME`（轻量方案）

如果确认只处理 MP4/MKV 且基本是 CFR：

```cpp
av_seek_frame(fmt, vidx, target_frame_id,
              AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD);
```

**局限性**：
- FLV/TS 容器不支持
- 依赖 `AVStream->nb_frames` 被容器正确填充（部分文件不写这个字段）
- VFR 下 FFmpeg 内部也是"frame_id → PTS → Seek"，本质走的同一条换算路径

### 4.4 方案三：VFR → CFR 预处理转换

适合一次性处理、后续所有操作都是 CFR 的场景：

```bash
# 重新采样帧（会实际改帧数据）
ffmpeg -i input_vfr.mp4 -vf "fps=30" -c:v libx264 output_cfr.mp4

# 只改容器标注不改帧内容（前提是帧间隔本来就基本均匀）
ffmpeg -i input_vfr.mp4 -vsync cfr -c copy output_cfr.mp4
```

**注意**：`-vf fps=30` 会丢帧或复制帧来满足目标帧率，帧内容会变（但不重编码整帧，只做帧选择）。

### 4.5 方案四：帧级别索引 + `AVSEEK_FLAG_BYTE`（终极方案）

在方案一的基础上更进一步——不只记录 PTS，还记录每一帧在文件中的字节偏移：

```cpp
struct FrameSampleInfo {
    int64_t pts;           // PTS (time_base 单位)
    int64_t byte_offset;   // 文件字节偏移 (来自 pkt->pos)
    int     size_bytes;    // 帧大小 (来自 pkt->size)
    bool    is_keyframe;
};

// 建索引时记录 pkt->pos
// Seek 时: av_seek_frame(fmt, vidx, info.byte_offset, AVSEEK_FLAG_BYTE);
// 连 PTS 都不需要了，直接跳字节偏移
```

这是 TikTok/快手等 UGC 平台的做法。

---

## 五、面试问题全景

### Q5.1 ⭐⭐ 什么是 VFR？实际项目中哪些场景会产生 VFR？

> 可变帧率，帧间隔不固定。手机录屏（静止时无帧输出）、iPhone 省电模式（动态降帧率）、WebRTC 录制（网络丢帧）、监控摄像头（静止省带宽）、OBS 采集（GPU 渲染不稳定）都会产生 VFR。一句话：**凡是实时采集且帧输出时间由硬件/网络决定的场景，都可能产生 VFR。**

### Q5.2 ⭐⭐ VFR 下 Seek 为什么比 CFR 难？核心区别是什么？

> Seek 的第一步是把帧号转成 PTS。CFR 下帧间隔固定，`frameId × 固定间隔` 就能算出准确 PTS；VFR 下帧间隔不固定，**`frameId × 平均间隔` 算出来的 PTS 越往后偏越远**——因为第 1 帧的间隔误差会被帧号放大：`偏差 = frameId × (真实间隔 - 平均间隔)`。
>
> 核心就是：**CFR 是线性映射，公式够用；VFR 是非线性映射，必须查表。**

### Q5.3 ⭐⭐⭐ VFR 下怎么实现帧精确 Seek？

> 初始化时全量 demux 扫描建索引——`av_read_frame` 不解码只读 packet 头，每帧的 PTS 按顺序 push 进 `vector<int64_t>`，下标就是 frameId。Seek 时直接 `frameToPts[targetFid]` 拿真实 PTS，零计算。VFR 和 CFR 在这个索引下完全透明——frameId 是扫描计数器的值，跟帧间隔无关。

### Q5.4 ⭐⭐ `avg_frame_rate` 和 `r_frame_rate` 在 VFR 下分别是什么含义？

> `avg_frame_rate = 总帧数 / 总时长`，它是平均值，VFR 下不等于实际帧率。`r_frame_rate` 是容器声明的最低帧率，很多 VFR 文件这个字段被写入端写死了（比如写 30fps 但实际帧间隔乱跳），不能信。**VFR 下这两个字段都不能用来做帧号换算。**

### Q5.5 ⭐⭐⭐ 全量 demux 扫描建索引会不会太慢？怎么解释给面试官？

> 纯 demux 不解码——`av_read_frame` 只读取容器层的 packet 头（pts/dts/flags/pos/size），不触发 `avcodec_send_packet` / `avcodec_receive_frame`。实测 10 分钟 4K30fps 素材约 18000 帧，扫描耗时 ~180ms——一次性的开销。用户打开素材本来就有一个加载过程，这 180ms 完全可以接受。
>
> 对比收益：建完索引后，每次 Seek 从 `O(解封装+GOP追帧)` 降到 `O(1) 查表 + GOP 追帧`，高频抽帧场景（如自动剪辑 200ms/帧）收益极大。

### Q5.6 ⭐⭐⭐ `AVSEEK_FLAG_FRAME` 在 VFR 下用了会怎样？

> 对 MP4/MKV，FFmpeg 的 demuxer 内部维护了 sample 表，`AVSEEK_FLAG_FRAME` 会把 frame_id 映射到正确的 sample，**VFR 下也能拿到正确帧**。但这不是所有容器都支持（FLV/TS 不支持），而且它本质上"帧号→PTS→Seek"的换算被做在 demuxer 内部，行为和性能由 FFmpeg 版本决定，**你控制不了**。所以生产环境更稳妥的做法是自己建索引。

### Q5.7 ⭐⭐ VFR 视频直接送去编码会有什么问题？

> 编码器期望的是固定帧间隔的帧序列。VFR 直接送编码，帧间隔忽大忽小，编出来的码流会带突变的 PTS 间隔，部分播放器可能卡顿或音画不同步。这就是为什么 OBS/剪映等工具在编码前会做 VFR→CFR 转换（通过丢帧/补帧到目标帧率）。

### Q5.8 ⭐ 怎么快速判断一个 MP4 是不是 VFR？

> 三个方法从快到慢：
> 1. `ffprobe` 看 `avg_frame_rate` 和 `r_frame_rate` 是否一致——不一致大概率是 VFR（但不绝对可靠）
> 2. `ffprobe -show_entries packet=pts_time` 导出来，看相邻 PTS 差值是否都相等——有一个不等就是 VFR
> 3. 代码里全量 demux 扫描，记录相邻帧间隔，检查是否全部相同

---

## 六、一句话总结

| 维度 | CFR | VFR |
|:---|:---|:---|
| 帧间隔 | 固定常数 | 可变 |
| 帧号→PTS | 公式 `frameId × Δ` | **必须查表** |
| Seek 方案 | 公式够用，也可建索引 | 建索引是唯一靠谱方案 |
| `avg_frame_rate` | 准确 | **不准**，是平均值 |
| `AVSEEK_FLAG_FRAME` | 可用 | MP4/MKV 可用，但不完全受控 |
| 全量扫描建索引 | 多此一举（CFR 公式就够了） | **业界标准做法** |
| 为什么会产生 | 专业设备录制、后期输出 | 实时采集、网络传输、省电模式 |

> **核心认知**：VFR 不是 Bug，是各种实时采集场景下的自然产物。开发者不能指望素材是 CFR——能做的是让系统对 VFR 透明。**建索引不是在"优化"，而是在"让系统用正确的方式描述帧"——用计数而不是推导。**

---

## 🔗 关联文档

- [[17-FFmpeg-Seek详解]] — Seek 三部曲、AVSEEK_FLAG_FRAME/BYTE 深入用法
- [[05-H264-MP4-NALU]] — MP4 moov box、stss/stts 与帧索引的底层对应
- [[01-数据结构与生命周期]] — AVStream、AVPacket、time_base 等核心结构深入讲解
- [[20-音视频开发面试题库-中高级]] — Q10.9 VFR Seek 问题、Q1.12 关键帧与 PTS 判断


