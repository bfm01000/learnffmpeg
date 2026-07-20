# 大厂常见优化 vs 小厂易踩坑（音视频 SDK 视角）

> **适用场景**：面试谈工程视野、架构成熟度、为什么你的项目值得讲。  
> **核心观点**：小厂先「能跑」，大厂先「把边界、精度、吞吐和可观测性设计对」。自动剪辑的 frameId/Seek 精度问题就是典型——功能实现了，时间模型设计错了，规模一上来必炸。

---

### 📖 阅读路线

```
一→二    快速概览: 30 秒速记 + 六维度对照表         (面试开场用)
三       核心深度: 时间模型与索引 — 精度是怎么丢的    (最重要, 含模拟+代码+VFR进阶)
四       核心深度: Pipeline — 吞吐是怎么被吞掉的     (含模拟+代码)
五       核心深度: 实时调度 — 延时是怎么被固化的      (含模拟+代码)
六       工程素养: 开关/灰度/控制变量                (含模拟+代码)
七       横向补充: 跨平台 + 协作 + 可观测             (速查表)
八→十三  面试技巧: 打包话术、JD 对齐、自检清单       (考前过一遍)
```

> **面试策略**：三、四、五选一个你最有把握的深入讲（模拟→代码→flag 控制），
> 六用来展示工程素养，七用来兜底追问。不要试图全讲——挑一个讲透远好于全讲浅。

---

## 一、面试速记（30 秒）

> 小厂容易在**功能闭环**上投入多，在**时间模型、Pipeline 吞吐、时钟域、依赖隔离、可观测性**上投入少。  
> 我踩过几类典型坑：**精度**（自动剪辑 frameId/Seek）、**吞吐**（直播零拷贝+异步发送+ABR）、**实时**（预览首帧延时固化→AJB）、**协作**（libMNN 优先级不对齐）。  
> 大厂往往有标准范式，小厂要踩过一遍才沉淀成流程；我的价值是把踩过的坑抽象成团队可复用的规范。

---

## 二、这类问题的共性

| 维度 | 小厂常见 | 大厂常见 |
|---|---|---|
| 目标 | 功能上线 | 功能 + **契约 + 吞吐 + 可观测** |
| 时间 | `float` 秒做 Seek/边界 | **int64 pts + time_base / frameIndex** |
| 性能 | 改热点函数 | **Pipeline + 硬件并行 + 背压** |
| 实时 | 删 wait 或硬等 | **场景化调度器 + 动态缓冲** |
| 协作 | 觉得对方不配合 | **优先级对齐 + 方案降本** |
| 沉淀 | 个人经验 | **流程 / 规范 / 开关 / 文档** |

**面试金句**：

> 很多 Bug 看起来是业务问题，根因是**底层契约没设计好**——时间怎么表示、帧怎么索引、Pipeline 怎么背压、依赖怎么隔离。

---

## 三、时间与索引类（自动剪辑是标杆例子）

| 小厂容易犯 | 大厂做法 | 我们的例子 | 面试关键词 |
|---|---|---|---|
| 用 `float/double` 秒做 Seek、抽帧边界 | **int64 pts + time_base**，浮点只给 UI | 自动剪辑 2.0→3.0 | 离散 vs 连续 |
| 帧 ID ↔ 时间戳来回转 | **frameIndex 单向索引**，底层查表 | 3.0 帧 ID 主键 | Single Source of Truth |
| 每次 Seek 都 `av_seek + flush` | **Smart Seek** + 关键帧索引路由 | 同 GOP 顺解丢帧 | 解码器状态复用 |
| 不建索引，运行时猜关键帧 | 打开文件时建 **stss / index_entries** | `AVStream->index_entries` | O(1) GOP 判断 |
| CFR 假设 `frameId/fps` 算时间 | VFR 也维护 **frameId→pts 表** | VFR 追问标准答法 | 索引表 vs 公式 |
| 算法 benchmark 和 SDK 耗时对不上 | **分段打点**后再和算法对齐 | 3.0 抽帧耗时排查 | 先测量再优化 |

**详细文档**：[`自动剪辑-帧ID与时间戳Seek精度方案.md`](./自动剪辑-帧ID与时间戳Seek精度方案.md)

**口述示例**：

> 2.0 算法用 frameId、抽帧用 float 毫秒，双向转换会在 999ms/1001ms 边界偏帧。3.0 改成 frameId 主索引 + pts 整数 Seek，再配合 Smart Seek 和 index_entries——这套和大厂剪映、ExoPlayer 的思路一致。

### 🔬 模拟推演：float 秒的精度是怎么「偏」的

下面用一组真实参数走一遍，看看 float 转换在边界处到底发生了什么。

```
┌─────────────────────────────────────────────────────────┐
│ 场景设定                                                  │
│   视频: 29.97fps CFR, time_base = 1/90000               │
│   目标: 算法说「取第 1000 帧」，SDK 去 Seek 并抽帧        │
│   一帧时长: 90000/2997 ≈ 3003.003... ticks              │
│   第 1000 帧的真实 PTS: 1000 × 3003.003... = 3003003     │
│   (FFmpeg 实际存储: pts = 3003003, 整数值)                │
└─────────────────────────────────────────────────────────┘

▸ 小厂路径：frameId → float秒 → Seek → 偏帧

  Step 1: frameId → float 秒
    1000 ÷ 29.97 = 33.3667000333667...

    但是！float (IEEE 754 binary32) 只有 24 位有效精度（约 7 位十进制）
    存入 float 后实际值: 33.36669921875
    误差: 0.000814... 秒 ≈ 0.8ms

    看起来 0.8ms 很小？别急——

  Step 2: float 秒 → Seek 目标 pts
    av_seek_frame(fmt_ctx, stream_index,
                  (int64_t)(33.3666992 * 90000),  // = 3003002
                  AVSEEK_FLAG_BACKWARD);

    传给 FFmpeg 的 pts 是 3003002
    但第 1000 帧的真实 pts 是 3003003  ← 差了 1 个 tick！

  Step 3: FFmpeg 实际 Seek 到哪里？
    AVSEEK_FLAG_BACKWARD: 找 ≤ 3003002 的最近关键帧
    假设 GOP=30, 上一个关键帧在 pts=2972970 (第 990 帧)
    FFmpeg 定位到第 990 帧的关键帧，然后逐帧解到 3003002

    解码器输出 pts=3003002 的帧: 这是第 999 帧还是第 1000 帧？
    → 不确定！取决于 ffmpeg 内部 time_base 换算的取整方向
    → 面试官要的答案: 「帧的边界是不可靠的」

  Step 4: 边界放大效应
    如果算法又把这个结果往回算 frameId:
      frameId = pts / ticks_per_frame = 3003002 / 3003.003...
      = 999.666... → 取整得 999 或 1000，不确定

    这就叫「双向转换在边界偏帧」——每次 float ↔ 整数转换都可能偏一帧。
    29.97fps 下每 33ms 有一帧，毫秒级时间定位一旦偏到帧边界，就错帧。

▸ 大厂路径：frameId 主索引 + int64 pts → 精确命中

  Step 1: 打开文件时建索引
    AVStream->index_entries 已经存好了每个关键帧的 (pos, pts, flags)
    额外维护一张 frameId → pts 映射表（只在打开时建一次）:

      frameId   pts
      ───────   ───────
      0          0
      1          3003
      2          6006
      ...        ...
      999        2999997
      1000       3003003    ← 真实值，不是算出来的
      ...        ...

  Step 2: 查表 Seek
    target_pts = frame_to_pts_table[1000];  // 3003003，精确
    av_seek_frame(fmt_ctx, stream_index, 3003003,
                  AVSEEK_FLAG_BACKWARD);

    传给 FFmpeg 的 pts = 3003003 → 精确命中！
    配合 Smart Seek: 如果目标帧和当前解码位置在同一个 GOP 内，
    不 flush 解码器，直接顺解到目标帧（跳过不需要的帧即可）

  Step 3: 为什么不会偏？
    - pts 全程是 int64 整数，没有浮点参与
    - frameId→pts 是查表不是公式计算，VFR 也正确
    - 没有「往回算」的需求，frameId 就是主键，Single Source of Truth

┌─────────────────────────────────────────────────────────┐
│ 面试一句话总结:                                           │
│ 「float 的误差不是 0.8ms 的事——是帧边界的 0 或 1 问题。   │
│   精度损失在边界处被放大成了一帧的错位。                    │
│   大厂用 int64 pts 整数链路 + 查表索引，从模型上消灭误差。」 │
└─────────────────────────────────────────────────────────┘
```

### 💻 代码对比：一个 flag 控制走 float 还是走整数索引

```cpp
// =================================================================
// 时间索引: 小厂 float 公式 vs 大厂 int64 查表 (FFmpeg C API)
// =================================================================

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <unordered_map>
#include <cstdint>

// ---------- 配置: 一个 bool 控制路径 ----------
struct SeekCfg {
    bool use_frame_index = false;  // false=小厂, true=大厂
};

// ============================================================
// 完整流程: 打开文件 → 建索引(大厂) → Seek → 解码抽帧
// ============================================================
struct FrameSeeker {
    AVFormatContext  *fmt_ctx_  = nullptr;
    AVCodecContext   *codec_ctx_ = nullptr;
    int               video_idx_ = -1;

    // 大厂路径: frameId → pts 查表
    std::unordered_map<int, int64_t> frame_to_pts_;

    // ---------- 打开 + 可选建索引 ----------
    int open(const char *path, const SeekCfg &cfg) {
        // 1. 打开容器
        int ret = avformat_open_input(&fmt_ctx_, path, nullptr, nullptr);
        if (ret < 0) return ret;

        ret = avformat_find_stream_info(fmt_ctx_, nullptr);
        if (ret < 0) return ret;

        video_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO,
                                         -1, -1, nullptr, 0);
        AVStream *vs = fmt_ctx_->streams[video_idx_];

        // 2. 打开解码器
        const AVCodec *dec = avcodec_find_decoder(vs->codecpar->codec_id);
        codec_ctx_ = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(codec_ctx_, vs->codecpar);
        avcodec_open2(codec_ctx_, dec, nullptr);

        // 3. 大厂路径: 用 FFmpeg 内置的 index_entries 建 frameId → pts 映射
        if (cfg.use_frame_index) {
            build_frame_index(vs);
        }
        return 0;
    }

    // ---------- 建索引: 遍历 AVStream::index_entries ----------
    // ⚠️ index_entries 只存关键帧! 不是全部帧!
    //    来源: MP4 stss box / FLV keyframes index / TS PAT/PMT
    //    每种容器填充 index_entries 的方式不同:
    //      MP4:  stss box (Sync Sample Table) → 只有 I 帧
    //      FLV:  每个 tag 如果是 keyframe 就加一条
    //      TS:   PAT/PMT + 每个 I 帧的 PCR
    //    非关键帧 (P/B 帧) 不在 index_entries 里!
    //
    //    所以下面这个 for 循环只能拿到 ~GOP 数量 的条目
    //    比如 3000 帧视频 GOP=30 → nb_index_entries ≈ 100
    void build_frame_index(AVStream *vs) {
        av_log(nullptr, AV_LOG_INFO,
               "nb_index_entries=%d (keyframes only, GOP≈%d → ~%d frames total)\n",
               vs->nb_index_entries,
               vs->gop_size,
               vs->nb_index_entries * vs->gop_size);  // 仅 CFR 估算

        // 推算 frameId: CFR 下每帧时长 = time_base.den / (fps_num * time_base.num)
        AVRational frame_dur = av_inv_q(vs->avg_frame_rate);
        int64_t frame_dur_tb = av_rescale_q(1, frame_dur, vs->time_base);

        for (int i = 0; i < vs->nb_index_entries; i++) {
            const AVIndexEntry &e = vs->index_entries[i];
            // ⚠️ e.timestamp 是关键帧的 PTS, 不是全部帧!
            //    用 pts / frame_dur 推算 frameId:
            //      - CFR: 基本准确 (但首帧 offset 可能偏 1)
            //      - VFR: 完全错误, 参考后面 🧬 章节
            int frame_id = (int)(e.timestamp / frame_dur_tb);
            frame_to_pts_[frame_id] = e.timestamp;
        }
        // ⚠️ frame_to_pts_ 此时只包含关键帧的条目!
        //    查询非关键帧 frameId → 返回 -1 (miss)
        //    需要 fallback 到 calc_pts_float 或用全量扫描 (见 🧬 章节)
        av_log(nullptr, AV_LOG_INFO,
               "index: %d keyframes → %zu sparse entries (non-keyframes missing!)\n",
               vs->nb_index_entries, frame_to_pts_.size());
    }

    // ========== Seek 统一入口 ==========
    int seek_to_frame(int frame_id, const SeekCfg &cfg) {
        AVStream *vs = fmt_ctx_->streams[video_idx_];

        int64_t target_pts;
        if (cfg.use_frame_index) {
            // ======== 大厂: 查表 → 精确整数 ========
            target_pts = lookup_pts(frame_id);
            if (target_pts < 0) {
                av_log(nullptr, AV_LOG_WARNING,
                       "frame %d not in index, fallback to float\n", frame_id);
                target_pts = calc_pts_float(frame_id, vs);  // fallback
            }
        } else {
            // ======== 小厂: float 公式 ========
            target_pts = calc_pts_float(frame_id, vs);
        }
        return av_seek_frame(fmt_ctx_, video_idx_,
                             target_pts, AVSEEK_FLAG_BACKWARD);
    }

private:
    // ---------- 小厂: float 秒 → 误差累积 ----------
    static int64_t calc_pts_float(int frame_id, AVStream *vs) {
        // 帧率 → 浮点 (如 29.97)
        double fps = av_q2d(vs->avg_frame_rate);
        // frameId → float 秒
        double t_sec = (double)frame_id / fps;
        //                 ^^^^^^^^^^^^^^^^
        //   frameId=1000, fps=29.97 → 33.3667000333667...
        //   存入 double 精度够, 但后面乘 time_base.den 时
        //   int64_t 截断会偏 1 tick
        return (int64_t)(t_sec * vs->time_base.den + 0.5);
        //  ↑ 即便四舍五入, VFR 下公式本身也是错的
    }

    // ---------- 大厂: 查表, 全程 int64 ----------
    int64_t lookup_pts(int frame_id) {
        auto it = frame_to_pts_.find(frame_id);
        return (it != frame_to_pts_.end()) ? it->second : -1;
    }
};

// ============================================================
// 进阶: Smart Seek — 同 GOP 内不 flush 解码器
// ============================================================
int smart_seek(AVFormatContext *fmt_ctx, int vidx,
               AVCodecContext *dec_ctx,
               int64_t target_pts, int64_t last_decoded_pts) {

    AVStream *vs = fmt_ctx->streams[vidx];

    // 1. 判断目标帧是否在「当前 GOP 内+之后不远」
    //    当前 GOP 起点 ≈ 最近一个关键帧的 pts
    int64_t gop_start = av_rescale_q(
        last_decoded_pts, vs->time_base, AV_TIME_BASE_Q);
    int64_t gop_dur   = vs->duration > 0
        ? av_rescale_q(vs->duration, vs->time_base, AV_TIME_BASE_Q)
        : AV_TIME_BASE;  // 默认 1 秒 GOP

    if (target_pts > last_decoded_pts &&
        target_pts < last_decoded_pts + gop_dur / 2) {
        // —— 同 GOP, 不 Seek, 顺向解码然后丢帧 ——
        av_log(nullptr, AV_LOG_INFO,
               "Smart Seek: same GOP, decoding forward\n");
        return -1;  // 特殊返回值: 不需要 seek, 顺解即可
    }

    // 2. 不在当前 GOP: 用 index_entries 找最近关键帧
    int idx = av_index_search_timestamp(
        vs, target_pts, AVSEEK_FLAG_BACKWARD);
    if (idx >= 0) {
        int64_t kf_pts = vs->index_entries[idx].timestamp;
        int64_t kf_pos = vs->index_entries[idx].pos;
        // Seek 到关键帧位置 (比 target_pts 略早)
        // 然后顺向解到 target_pts
        return av_seek_frame(fmt_ctx, vidx, kf_pts,
                             AVSEEK_FLAG_BACKWARD);
    }

    // 3. Fallback: 标准 Seek
    return av_seek_frame(fmt_ctx, vidx, target_pts,
                         AVSEEK_FLAG_BACKWARD);
}

// ============================================================
// 使用示例
// ============================================================
// SeekCfg cfg;
// cfg.use_frame_index = true;          // ← 切到大厂路径
//
// FrameSeeker seeker;
// seeker.open("input.mp4", cfg);
//
// seeker.seek_to_frame(1000, cfg);     // 精确到第 1000 帧
//
// AVPacket *pkt = av_packet_alloc();
// AVFrame  *frm = av_frame_alloc();
// while (av_read_frame(fmt_ctx, pkt) >= 0) {
//     if (pkt->stream_index != vidx) { av_packet_unref(pkt); continue; }
//     avcodec_send_packet(dec_ctx, pkt);
//     avcodec_receive_frame(dec_ctx, frm);
//     // frm->pts 现在就是你要的第 1000 帧 (误差 ±0 帧)
//     break;
// }
```

> **核心结论**：换硬解码器（MediaCodec / VideoToolbox）只影响「怎么解码」这一步。
> 建索引是解封装层的事——`av_read_frame()` 在哪个平台都能用，
> 它只读容器 packet 头，根本不调解码器。**索引在容器，不在解码器。**

### 🧬 进阶：关键帧限制、VFR 与非固定 GOP

> 面试官大概率会追问两个更深的问题：
> 1. `index_entries` 里存的到底是什么？全不全？
> 2. VFR（可变帧率）下你的公式还能用吗？
> 这两个问题直击「你是用过 FFmpeg」还是「理解容器层」。

#### 🔑 关键事实：`index_entries` 只存关键帧

这是整个索引问题的根。用一张图看清 `index_entries` 里**有什么**、**缺什么**：

```
一个 90 帧的 MP4 文件, GOP=30:

全部帧 (av_read_frame 逐帧读):
  I₀ P₁ P₂ ... P₂₉  I₃₀ P₃₁ ... P₅₉  I₆₀ P₆₁ ... P₈₉
  ↑                 ↑                 ↑
  frameId=0         frameId=30        frameId=60

index_entries (MP4 stss box 自动解析):
  [0] { pos: 0x100,   timestamp: 0       }
  [1] { pos: 0x8A00,  timestamp: 900900  }    ← 只有 3 条!
  [2] { pos: 0x12000, timestamp: 1801800 }
  nb_index_entries = 3  ← 90 帧里只有 3 个 I 帧被索引

用 index_entries 建的 frame_to_pts_ (CFR 公式推断帧号):
  frame_to_pts_[0]  = 0
  frame_to_pts_[30] = 900900     ← 中间缺了 1~29, 31~59, 61~89!
  frame_to_pts_[60] = 1801800
  // 总共 3 条, 不是 90 条!

查询 frameId=50:
  lookup_pts(50) → frame_to_pts_.find(50) → NOT FOUND → 返回 -1
  → fallback 到 calc_pts_float(50)
  → 如果 VFR, float 公式也错 → Seek 偏帧
```

**根因**：`index_entries` 来自容器的 Seek 表（MP4 stss / FLV keyframes index），设计目的
是告诉解码器「可以从哪里开始解码」，不是给你建全帧索引的。**非关键帧根本不在里面**。

#### 🧬 VFR + 非固定 GOP：为什么 `pts / frame_dur` 会炸

上面的代码里有一行看起来很自然的推导，**在 VFR 或非固定 GOP 场景下它是错的**：

```cpp
// ❌ 这条公式假设 CFR + 帧时长恒定
//    + 只扫了 index_entries (仅关键帧!), 非关键帧全是查询黑洞
int frame_id = (int)(e.timestamp / frame_dur_tb);
```

```
┌─────────────────────────────────────────────────────────────────┐
│ 场景 A: VFR (Variable Frame Rate) — 帧间隔不是常数              │
│                                                                  │
│   iPhone 省电模式录制:                                           │
│     frame 0:  pts=0       dur=33ms  (30fps)                     │
│     frame 1:  pts=33000   dur=33ms                              │
│     frame 2:  pts=66000   dur=66ms  ← 手机降帧率了 (15fps)      │
│     frame 3:  pts=132000  dur=66ms                              │
│     frame 4:  pts=198000  dur=33ms  ← 恢复                       │
│                                                                  │
│   CFR 公式: frame_id = pts / (90000/30) = pts / 3003             │
│     frame 2: 66000 / 3003 = 21.97 → 21                           │
│     实际 frameId = 2  ← 差了 19 帧!                              │
│                                                                  │
│   结论: VFR 下 frameId 不能从 PTS 推导, 只能从顺序位置计数        │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ 场景 B: 非固定 GOP — 关键帧间隔不恒定                            │
│                                                                  │
│   场景自适应 GOP (常见于 x264/x265):                              │
│     GOP 0: I  [P P P ... P]  30 帧 → 下一个 I 在 frameId 30     │
│     GOP 1: I  [P P]           3 帧 → 下一个 I 在 frameId 33     │
│     GOP 2: I  [P P P ... P] 250 帧 → 下一个 I 在 frameId 283    │
│                                                                  │
│   只扫关键帧建索引:                                               │
│     frame_to_pts_[0]   = pts(I0)                                 │
│     frame_to_pts_[30]  = pts(I1)   ← 推算 frameId 有 VFR 问题    │
│     frame_to_pts_[33]  = pts(I2)   ← 跳跃完全不规律              │
│     frame_to_pts_[283] = pts(I3)                                 │
│                                                                  │
│   目标: Seek 到 frameId=150                                       │
│     关键帧索引里只有 [0, 30, 33, 283]                             │
│     → 找到 frameId 33 是关键帧, 但 frame 33~150 之间有 117 帧    │
│     → 不知道 frame 150 的精确 PTS!                                │
│                                                                  │
│   只能: Seek 到 frame 33 的关键帧, 然后逐帧解 117 帧               │
│   代价: 不知道要解多少帧, 不知道 frame 150 的 PTS (无法验证)      │
└─────────────────────────────────────────────────────────────────┘
```

#### 正确做法：全量扫描 + 单调 frameId 计数器 + 关键帧位图

核心思路改变：

```
旧思路 (CFR 假设):                    新思路 (VFR 兼容):
  pts → 除以帧时长 → frameId            按 sample 出现顺序分配 frameId (0,1,2,3...)
  只扫关键帧                             扫全部帧 (frame 和 keyframe 分开存)
                                         frameId 是序号, 不是算出来的
```

```cpp
// ============================================================
// VFR + 非固定 GOP 下的通用索引结构
// ============================================================

struct FrameIndex {
    // 全量: frameId(0,1,2...) → PTS
    // 对于长视频, 可以只存一个数据帧和一个稀疏的关键帧索引
    std::vector<int64_t> frame_to_pts;       // [frameId] = pts
    //                                   ↑ 下标即 frameId, 不推导!

    // 关键帧位图: 快速定位「frameId 之前最近的关键帧」
    std::vector<int>    keyframe_ids;        // 所有关键帧的 frameId 列表

    // 可选: 大文件只用关键帧稀疏索引 + frame 0/timebase 做线性近似
    bool is_sparse = false;  // true → frame_to_pts 只有关键帧
};
```

#### FFmpeg 全量扫描版 (VFR + 非固定 GOP 安全)

> **关键：`av_read_frame()` 是 demux（解封装），不是 decode（解码）。**
>
> ```
> av_read_frame()  只读容器层的 packet 头部 → 拿到 pts/dts/duration/flags/pos
>                  不调用解码器，不碰 YUV/RGB 像素，不消耗 GPU/NPU
>
> 耗时: 读一个 packet 头 ~几十微秒
>       10 分钟 4K 视频 (~18000 帧) → 全量 demux 扫描约 200~500ms
>       这个代价完全可以接受 (只建一次索引, Seek 时 O(1) 查表)
>
> 内存: 每帧存一个 int64_t PTS = 8 字节
>       18000 帧 × 8 = 144KB ← 可忽略
> ```
>
> 所以「全量扫描」不是「全量解码」，IO 和内存开销都很小。这是 ExoPlayer、ijkplayer 等
> 播放器框架的标准做法——容器层一次性遍历 packet 头，建好索引后随机 Seek 零成本。

```cpp
// ============================================================
// FFmpeg: demux 扫描 (不解码!) → 全量 frameId→PTS + 关键帧位图
// ============================================================
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

struct FrameIndexVFR {
    AVFormatContext *fmt_ctx_ = nullptr;
    int video_idx_ = -1;

    // ---------- 全量索引 ----------
    std::vector<int64_t> frame_to_pts_;   // frameId[0..N-1] → PTS
    //                             ↑ 下标即 frameId, 不是算出来的!
    std::vector<int>      keyframe_ids_;  // 所有关键帧的 frameId

    // ---------- 建索引: 读每一帧, 不跳过 ----------
    int build(AVFormatContext *fmt_ctx, int vidx) {
        fmt_ctx_  = fmt_ctx;
        video_idx_ = vidx;

        AVPacket *pkt = av_packet_alloc();
        int frame_id = 0;

        // 先 Seek 到文件头
        av_seek_frame(fmt_ctx_, video_idx_, 0, AVSEEK_FLAG_BYTE);

        while (av_read_frame(fmt_ctx_, pkt) >= 0) {
            if (pkt->stream_index != video_idx_) {
                av_packet_unref(pkt);
                continue;
            }

            // ★ 核心: frameId 就是递增序号, 不从 PTS 推导!
            frame_to_pts_.push_back(pkt->pts);

            if (pkt->flags & AV_PKT_FLAG_KEY) {
                keyframe_ids_.push_back(frame_id);
                // 此时我们同时知道:
                //   - 这个关键帧的 frameId (frame_id)
                //   - 这个关键帧的 PTS    (pkt->pts)
                //   - 这个关键帧的 pos    (pkt->pos)  ← 用于字节级 Seek
            }

            frame_id++;
            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        av_log(nullptr, AV_LOG_INFO,
               "index: %d frames, %zu keyframes (VFR-safe)\n",
               frame_id, keyframe_ids_.size());
        return frame_id;
    }

    // ---------- Seek: frameId → PTS 精确查表 ----------
    int64_t seek_to_frame(int target_frame) {
        if (target_frame < 0 ||
            target_frame >= (int)frame_to_pts_.size())
            return -1;

        int64_t target_pts = frame_to_pts_[target_frame];

        // 找到 target_frame 之前最近的关键帧
        int nearest_kf_id = find_nearest_keyframe(target_frame);

        if (nearest_kf_id < 0) {
            // 从文件头开始
            return av_seek_frame(fmt_ctx_, video_idx_,
                                 0, AVSEEK_FLAG_BYTE);
        }

        int64_t kf_pts = frame_to_pts_[nearest_kf_id];

        // Smart Seek: 如果 target 在当前 GOP 内且距离不远,
        // 直接顺解过去 (关键帧索引已保证不会往回跳)
        // 否则 Seek 到关键帧位置
        return av_seek_frame(fmt_ctx_, video_idx_,
                             kf_pts, AVSEEK_FLAG_BACKWARD);
    }

    // ---------- 二分查找最近关键帧 ----------
    int find_nearest_keyframe(int target_frame) const {
        // keyframe_ids_ 是升序的 frameId 列表
        auto it = std::upper_bound(
            keyframe_ids_.begin(), keyframe_ids_.end(), target_frame);
        if (it == keyframe_ids_.begin()) return -1;   // 目标在第一个关键帧之前
        return *(--it);  // 返回 ≤ target_frame 的最大关键帧 frameId
    }
};
```


#### 实际耗时：Demux 全扫到底要多久

用一组实测数据说话（Pixel 6, MP4 容器, 本地文件）：

```
格式    分辨率    时长     总帧数    全量 demux 耗时    等效吞吐
──────  ────────  ───────  ────────  ────────────────  ──────────
MP4     1080p30   1 min    1,800     15~30 ms          60,000+ 帧/秒
MP4     1080p30   10 min   18,000    80~150 ms         120,000+ 帧/秒
MP4     4K30      10 min   18,000    120~250 ms        72,000+ 帧/秒
MP4     4K60      30 min   108,000   500~1200 ms       90,000+ 帧/秒
TS      1080p30   10 min   18,000    200~500 ms        (TS 需逐包解析 PES 头)
FLV     1080p30   10 min   18,000    100~300 ms        (FLV tag 头固定 11 字节)
```

> **结论：对于典型素材（10 分钟以内），demux 全扫在一帧渲染时间（~33ms）的量级内完成，用户完全无感。**

耗时主要花在哪：

```
av_read_frame() 内部路径 (MP4):
  mov_read_packet()
    → 查 stsc (Sample-to-Chunk) 表: 已缓存, O(log N) 二分
    → 查 stsz (Sample Size) 表:     已缓存, O(1)
    → 查 stts (Time-to-Sample) 表:  已缓存, 遍历小表
    → 读 packet data:               read() 系统调用 ← 大头!
       
  但如果只需要 pts/flags 不要数据, 可以做「跳过 body」优化:
    → av_read_frame() + av_packet_unref() 不读数据部分
    → 或者直接用 FFmpeg 内部 mov_read_header 暴露的 sample table
```

#### 大厂也是这么做的吗

**是的，而且做得更激进。** 全量 demux 建索引是行业标准做法：

```
┌────────────────────────────────────────────────────────────────────┐
│ ExoPlayer (Google) — Android 官方播放器                             │
│                                                                     │
│   对 MP4: 直接用 Mp4Extractor 解析 stss/stts/stsz box               │
│           → 不走 sample-by-sample advance                           │
│           → 而是从 stts 表直接算出每一帧的 PTS (O(1))                │
│           → 关键帧位置从 stss 直接拿到                               │
│                                                                     │
│   对 TS/FLV/WebM: 需要逐 sample 扫描 (因为容器结构就是线性的)        │
│                                                                     │
│   index 存在内存, Seek 时:                                          │
│     SyncSampleEncryptionSeekMap → 二分查找关键帧 → 精确跳转          │
└────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────┐
│ ijkplayer (Bilibili) — 开源播放器                                   │
│                                                                     │
│   策略: 打开文件时后台线程做一次 av_read_frame() 全扫描              │
│         把每个 packet 的 (pts, pos, key_frame) 三元组存入数组        │
│         Seek 时: 二分查 pos → av_seek_frame(pos, BYTE)              │
│         比 pts seek 更准 (pts seek 可能受 time_base 换算影响)       │
└────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────┐
│ 剪映 / CapCut (ByteDance) — 视频编辑 SDK                            │
│                                                                     │
│   导入素材时做的事 (你看到的 "分析中..." 进度条):                     │
│     1. Demux 全扫: 建 frameId→PTS 索引                              │
│     2. 关键帧提取: 建 GOP 索引                                       │
│     3. 缩略图生成: 每 N 帧抽一帧解码→缩略图 (这个才慢!)               │
│     4. 音频波形: 解码音频→波形数据                                   │
│                                                                     │
│   第 1,2 步纯 demux, 耗时 < 1 秒 (10 分钟视频)                       │
│   第 3 步才是 "分析中" 进度条的大头 (需要解码)                        │
│   所以用户感知的 "慢" 是解码抽缩略图, 不是建索引                      │
└────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────┐
│ 我们 (Insta360 自动剪辑)                                             │
│                                                                     │
│   做法: av_read_frame() 全扫描, 每个 packet 记 (pts, is_key, pos)   │
│   耗时: 10 分钟 4K → ~150ms (Pixel 6), 用户点击素材到看到时间线      │
│         这段时间还包括缩略图任务调度, 纯索引 < 200ms                  │
│                                                                     │
│   对比小厂做法:                                                      │
│     不建全量索引 → 每次 Seek 用 float 算 → 偏帧                      │
│     或者: 建了索引但不区分关键帧 → Smart Seek 用不了                  │
│     → Seek 一次 flush 一次解码器 → 体验比 demux 全扫慢得多           │
└────────────────────────────────────────────────────────────────────┘
```

#### 关键认知

```
小厂的误区:
  「全量扫描 = 慢」→ 不建索引 → 每次 Seek 现场算 → 又慢又不准

大厂的逻辑:
  「全量 demux 扫描 = 一次性低成本 (几百 ms)」 
  → 建好索引后每次 Seek 都是 O(log N) 二分 + O(1) 查表
  → 一百次 Seek 省下来的时间远超建索引成本
```

而且大厂还会进一步优化——**不是每次打开文件都扫一遍**：

```
优化 1: 索引缓存到磁盘
  首次打开 → demux 全扫 → 序列化索引到文件 (如 video.mp4.idx)
  再次打开 → mmap 索引文件 → 零扫描, < 5ms 完成

优化 2: 按容器类型走捷径
  检测到 MP4 → 直接解析 moov box 里的 sample table
  不需要逐包 av_read_frame, 而是从 stts/stss 表 O(1) 算出每一帧

优化 3: 懒加载
  只建关键帧索引 (sparse)
  非关键帧的 PTS 按公式线性插值 (CFR) 或按需回扫 (VFR)
```

---

```
┌─────────────────────────────────────────────────────────────────┐
│  设计决策                    CFR + 固定 GOP    VFR + 非固定 GOP  │
│  ───────────────────────     ──────────────    ────────────────  │
│  frameId 来源                pts / frame_dur   顺序扫描的计数器   │
│  索引结构                    map<id, pts>      vector<pts> (下标) │
│  关键帧索引                  map<id, pts>      vector<id> (独立)  │
│  推导 frameId→PTS            O(1) 公式         O(1) 查表          │
│  关键帧→PTS                  O(1) 查表         O(log N) 二分      │
│  空间复杂度                  O(关键帧数)        O(总帧数)          │
│  对大文件可优化为            稀疏索引+线性近似  稀疏索引+回扫       │
└─────────────────────────────────────────────────────────────────┘

面试一句话:
  「VFR 下 frameId 不能从 PTS 推导——帧间隔不是常数。
   正确的做法是把 frameId 定义为扫描序号, PTS 存成数组下标。
   关键帧单独建一个升序 frameId 列表, Seek 时二分查找最近关键帧。
   全量 demux 扫描在 10 分钟视频上只要 ~150ms,
   这跟 ExoPlayer 的 Mp4Extractor、剪映的素材导入分析是一类思路。」
```

---

> **▸ 小节过渡**：时间模型解决的是「找到正确的帧」。找到之后，接下来要解决的是——
> **怎么让这一帧以最快的速度、最低的开销走完渲染→编码→发送全链路**。
> 这就是 Pipeline 问题：不是优化一个函数，是重新设计数据的流动方式。

---

## 四、Pipeline 与性能类

| 小厂容易犯 | 大厂做法 | 我们的例子 | 面试关键词 |
|---|---|---|---|
| 抽帧→渲染→推理 **串行死等** | **异构并行 Pipeline**（VPU/GPU/NPU） | 自动剪辑 3.0 | 生产者-消费者 |
| 只优化单函数，不看全链路 | **先画 Pipeline，再分段打点** | 4K 直播、预览延时 | 吞吐率 |
| GPU 读回 CPU 再送编码器 | **零拷贝** AHardwareBuffer / Surface | 4K 直播 | Gralloc / Sync Fence |
| 编码和网络 **同步写** | **异步队列 + 背压** | 直播 async send | 网络 I/O 解耦 |
| 弱网只降分辨率 | **队列水位 ABR**，保流畅 | 直播 ABR 60/15 帧水位 | QoS |
| 高频 alloc/free 帧缓冲 | **对象池 / AVBufferPool** | 自动剪辑渲染帧池 | 内存碎片 |
| 长素材一次性加载防抖内存 | **窗口化 / 滑动加载** | 30min≈270MB 问题 | 峰值内存 |

**口述示例**：

> 4K 直播我先用控制变量法：本地写 MP4 能满 30fps，推流不行，证明瓶颈在网络同步阻塞而不是编码器。然后零拷贝解 CPU 拷贝，异步队列解网络背压，水位 ABR 做弱网降级——这是 Pipeline 级优化，不是改一个函数。

**相关文档**：[`核心-直播性能优化.md`](../核心-直播性能优化.md)、[`核心-自动剪辑性能优化.md`](../核心-自动剪辑性能优化.md)

### 🔬 模拟推演：串行 Pipeline 怎么被「等」死 vs 大厂流水线怎么跑满

用 4K 直播推流的一条帧链路，把时间轴摊开，看小厂和大厂的差距。

```
┌─────────────────────────────────────────────────────────┐
│ 场景: 4K@30fps RTMP 直播推流，一帧的生命周期               │
│ 设每帧各环节纯耗时: GPU渲染 8ms, 编码 12ms, 网络send 15ms │
│ 目标: 30fps → 每帧总预算 33.3ms                           │
└─────────────────────────────────────────────────────────┘

▸ 小厂串行模式: 一帧做完再做下一帧

  时间轴 (每格 = 5ms):
  ─────────────────────────────────────────────────────────
  帧1: [GPU████████][CPU拷⣿⣿⣿][编码████████████][网络send███████████████]
        0          8    拷贝3ms  11         23                  38ms → 超预算！
        
  帧1: 8+3+12+15 = 38ms  ← 已经超过 33.3ms 预算，丢帧！
  帧2: 同理 38ms
  帧3: 同理 38ms
  ...
  
  实际帧率: 1000/38 ≈ 26.3fps，且 GPU/编码器/网络轮流闲置。
  
  每帧 38ms 里各单元的利用率:
    GPU:      8/38 = 21%  ← 79% 时间在喝茶
    CPU拷贝:   3/38 = 8%
    编码器:   12/38 = 32%  ← 68% 时间空闲
    网络线程: 15/38 = 39%  ← 最慢的一环堵死了所有人

  [同步网络 send 阻塞] 是最致命的:
    编码器编完一帧 → send() → 等 TCP 确认 → 15ms 后返回
    这 15ms 里编码器完全空闲，下一帧的渲染数据也堆在 GPU 里出不来

▸ 大厂流水线模式: 零拷贝 + 异步队列 → 各单元独立运转

  架构改造:
    1. 零拷贝: GPU 渲染目标直接是 MediaCodec 的 Input Surface
       → 去掉 CPU 拷贝 3ms，且 GPU 和编码器共享同一块显存
    2. 异步发送: 编码输出 → push 到发送队列 → 立刻返回编码下一帧
       网络线程从队列 pop → 慢慢 send，TCP 阻塞不影响编码器

  时间轴:
  帧1: [GPU██][编码████████]→[入队]
  帧2:       [GPU██][编码████████]→[入队]  ← GPU/编码不等网络
  帧3:              [GPU██][编码████████]→[入队]
  帧4:                     [GPU██][编码████████]→[入队]
            0    8    20   28    40   48    60ms

  网络线程独立运行，从队列取帧发送:
  网络:  [send帧1████████████][send帧2████████████][send帧3...
          20                 35                 50ms

  每帧端到端: 8+12 = 20ms（不含网络等待时间）
  实际产帧: 每 20ms 一帧 → 50fps，轻松稳住 30fps
  网络侧: 队列做缓冲，15ms 的网络抖动被队列吸收

▸ 弱网场景: 水位 ABR 怎么救场

  正常: 发送队列水位 2~3 帧（网络比编码快或持平）
  弱网: 发送速度下降，队列水位开始涨:
    
    水位 5 帧 → 正常，不管
    水位 15 帧 → 注意，但还能扛
    水位 60 帧 → 触发 ABR: 降码率（如 20Mbps→10Mbps），编码器输出变小
                → 队列不再增长
    水位回到 15 帧 → 恢复原码率

  小厂做法（降分辨率）：用户看到画面突然糊了，体验断崖式下降
  大厂做法: 降码率（同分辨率，画面颗粒感略增但不糊），
           用户几乎无感，队列水位降下来后自动恢复

┌─────────────────────────────────────────────────────────┐
│ 面试一句话总结:                                           │
│ 「串行模式下，最慢的环节是所有环节的上限。                   │
│   Pipeline 改造让每个硬件单元独立运转，瓶颈从单点变成吞吐。  │
│   这不是优化函数，是重新设计数据的流动方式。」                │
└─────────────────────────────────────────────────────────┘
```

### 💻 代码对比：一个 `LiveCfg` 控制三条优化路径 (FFmpeg C API)

```cpp
// =================================================================
// 4K RTMP 直播: 小厂同步阻塞 vs 大厂异步流水线 (FFmpeg C API)
// =================================================================

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

// ============================================================
// 配置: 三个独立开关
// ============================================================
struct LiveCfg {
    bool enable_async_send = false;  // 异步发送: 编码线程不阻塞 muxer
    bool enable_abr        = false;  // 队列水位驱动动态码率
    int  target_bitrate    = 20'000'000;  // 20 Mbps
    int  abr_high_water    = 60;     // 队列 > 60 帧 → 降码率
    int  abr_low_water     = 15;     // 队列 < 15 帧 → 恢复码率
};

// ============================================================
// 直播推流 Pipeline
// ============================================================
class LiveStreamer {
    LiveCfg cfg_;

    // ---- 编码器 ----
    const AVCodec *enc_    = nullptr;
    AVCodecContext *enc_ctx_ = nullptr;
    AVFrame       *enc_frame_ = nullptr;

    // ---- RTMP 输出 ----
    AVFormatContext *out_fmt_ = nullptr;
    AVStream        *out_vs_  = nullptr;   // 视频流

    // ---- 异步发送 ----
    std::mutex              pkt_mtx_;
    std::condition_variable pkt_cv_;
    std::queue<AVPacket *>  pkt_queue_;    // AVPacket 所有权转移
    std::thread             mux_thread_;
    std::atomic<bool>       running_{true};

public:
    // ========== 初始化编码器 + RTMP 输出 ==========
    int init(const LiveCfg &cfg) {
        cfg_ = cfg;
        int ret;

        // --- 1. 编码器 (H.264) ---
        enc_ = avcodec_find_encoder(AV_CODEC_ID_H264);
        enc_ctx_ = avcodec_alloc_context3(enc_);
        enc_ctx_->width    = 3840;       // 4K
        enc_ctx_->height   = 2160;
        enc_ctx_->time_base = {1, 30};   // 30 fps
        enc_ctx_->framerate = {30, 1};
        enc_ctx_->pix_fmt  = AV_PIX_FMT_YUV420P;
        enc_ctx_->bit_rate = cfg.target_bitrate;
        enc_ctx_->gop_size = 30;
        enc_ctx_->max_b_frames = 0;      // 直播不用 B 帧

        // 预设: 低延时
        av_opt_set(enc_ctx_->priv_data, "preset",   "ultrafast", 0);
        av_opt_set(enc_ctx_->priv_data, "tune",     "zerolatency", 0);
        av_opt_set(enc_ctx_->priv_data, "profile",  "high", 0);

        ret = avcodec_open2(enc_ctx_, enc_, nullptr);
        if (ret < 0) return ret;

        enc_frame_ = av_frame_alloc();
        enc_frame_->format = enc_ctx_->pix_fmt;
        enc_frame_->width  = enc_ctx_->width;
        enc_frame_->height = enc_ctx_->height;
        av_frame_get_buffer(enc_frame_, 0);

        // --- 2. RTMP 输出 ---
        ret = avformat_alloc_output_context2(
            &out_fmt_, nullptr, "flv", cfg.rtmp_url);
        out_vs_ = avformat_new_stream(out_fmt_, nullptr);
        avcodec_parameters_from_context(out_vs_->codecpar, enc_ctx_);
        out_vs_->time_base = enc_ctx_->time_base;

        // 写 RTMP 头
        ret = avformat_write_header(out_fmt_, nullptr);

        // --- 3. 启动异步 mux 线程 ---
        if (cfg_.enable_async_send) {
            mux_thread_ = std::thread(&LiveStreamer::mux_loop, this);
        }
        return 0;
    }

    // ========== 每帧入口: GPU 帧 → 编码 → 发送 ==========
    int push_frame(AVFrame *gpu_frame, int64_t frame_pts) {
        // Step 1: 填充编码输入帧 (实际项目里这里是 GPU→CPU 或 Surface 零拷贝)
        av_frame_make_writable(enc_frame_);
        enc_frame_->pts = frame_pts;
        // ...填充 YUV 数据...

        // Step 2: 编码
        int ret = avcodec_send_frame(enc_ctx_, enc_frame_);
        if (ret < 0) return ret;

        while (ret >= 0) {
            AVPacket *pkt = av_packet_alloc();
            ret = avcodec_receive_packet(enc_ctx_, pkt);
            if (ret == AVERROR(EAGAIN)) { av_packet_free(&pkt); break; }
            if (ret < 0)                    { av_packet_free(&pkt); return ret; }

            av_packet_rescale_ts(pkt, enc_ctx_->time_base,
                                 out_vs_->time_base);
            pkt->stream_index = out_vs_->index;

            // Step 3: 发送 — flag 控制同步/异步
            if (cfg_.enable_async_send) {
                push_async(pkt);              // → 大厂
            } else {
                push_sync(pkt);               // → 小厂
            }

            // Step 4: ABR
            if (cfg_.enable_abr) {
                adjust_abr();
            }
        }
        return 0;
    }

private:
    // ======================================================
    // 小厂: 同步 av_interleaved_write_frame
    // ======================================================
    int push_sync(AVPacket *pkt) {
        // 直接写 → 阻塞等 RTMP 网络 IO
        // 编码线程卡在这里, 不能编下一帧!
        int ret = av_interleaved_write_frame(out_fmt_, pkt);
        av_packet_free(&pkt);
        return ret;
        // ⚠ 问题:
        //   av_interleaved_write_frame 内部会:
        //   1. 等上一次 TCP send 完成
        //   2. 发送当前包
        //   3. 等 TCP ACK
        //   整个过程可能 10~30ms, 编码线程完全被浪费
    }

    // ======================================================
    // 大厂: 异步入队 → 独立线程 av_interleaved_write_frame
    // ======================================================
    void push_async(AVPacket *pkt) {
        {
            std::lock_guard<std::mutex> lk(pkt_mtx_);
            pkt_queue_.push(pkt);      // 入队, 所有权转移
        }
        pkt_cv_.notify_one();
        // 立刻返回 → 编码线程继续编下一帧!
    }

    void mux_loop() {
        while (running_) {
            AVPacket *pkt = nullptr;
            {
                std::unique_lock<std::mutex> lk(pkt_mtx_);
                pkt_cv_.wait(lk, [this] {
                    return !pkt_queue_.empty() || !running_;
                });
                if (!running_ && pkt_queue_.empty()) break;
                pkt = pkt_queue_.front();
                pkt_queue_.pop();
            }
            // 网络阻塞在这里, 不影响编码器
            av_interleaved_write_frame(out_fmt_, pkt);
            av_packet_free(&pkt);
        }
    }

    // ======================================================
    // 大厂: ABR — 队列水位驱动, 不降分辨率
    // ======================================================
    void adjust_abr() {
        size_t water;
        {
            std::lock_guard<std::mutex> lk(pkt_mtx_);
            water = pkt_queue_.size();
        }

        if (water > cfg_.abr_high_water) {
            // 水位高 → 降码率保流畅 (不降分辨率!)
            int new_br = std::max((int)(enc_ctx_->bit_rate * 0.7),
                                  5'000'000);  // 下限 5Mbps
            if (new_br != enc_ctx_->bit_rate) {
                enc_ctx_->bit_rate = new_br;
                av_log(nullptr, AV_LOG_WARNING,
                       "ABR: bitrate ↓ %d kbps (water=%zu)\n",
                       new_br / 1000, water);
            }
        } else if (water < cfg_.abr_low_water &&
                   enc_ctx_->bit_rate < cfg_.target_bitrate) {
            // 水位低 → 逐步恢复码率
            int new_br = std::min(
                (int)(enc_ctx_->bit_rate * 1.1),
                cfg_.target_bitrate);
            if (new_br != enc_ctx_->bit_rate) {
                enc_ctx_->bit_rate = new_br;
                av_log(nullptr, AV_LOG_INFO,
                       "ABR: bitrate ↑ %d kbps (water=%zu)\n",
                       new_br / 1000, water);
            }
        }
        // 对比小厂做法:
        //   if (water > 60) switch_to_1080p();
        //   → 用户看到分辨率突变, 画面变糊, 体验断崖下降
    }
};

// ============================================================
// 使用示例
// ============================================================
// LiveCfg cfg;
// cfg.enable_async_send = true;    // 异步, 编码不等 RTMP
// cfg.enable_abr        = true;    // 动态码率
// cfg.target_bitrate    = 20'000'000;
//
// LiveStreamer ls;
// ls.init(cfg);
// for (;;) {
//     AVFrame *f = get_next_gpu_frame();
//     ls.push_frame(f, next_pts++);
// }
```

---

> **▸ 小节过渡**：Pipeline 优化解决的是「吞吐」——在理想网络下跑满硬件。
> 但实时音视频场景（如相机预览）的敌人不是吞吐，是**延时**。
> 而延时的根因往往不是「某一步太慢」，而是**调度模型把瞬时抖动固化成了永久偏移**。
> 这就是实时性问题：时钟域怎么对齐、缓冲怎么动态调节。

---

## 五、时钟与实时性类

| 小厂容易犯 | 大厂做法 | 我们的例子 | 面试关键词 |
|---|---|---|---|
| 用系统 wall clock 对齐媒体帧 | **媒体时钟 vs 单调时钟分离** | AJB `steady_clock` | 时钟域 |
| 首帧迟到把高延时「固化」 | **动态缓冲 / Resync** | X5 1.5s 预览 | 首帧锚点 |
| 为低延时直接删 wait，画面呼吸 | **AJB 快升慢降** | 预览 1.5s→0.5s | 延时 vs 平滑 |
| 队列无限堆积 | **水位线 + 丢帧/追赶** | AJB 追赶 + 直播队列上限 90 | 背压 |
| 直播/预览混用同一套调度 | **场景化调度器** | 直播偏吞吐、预览偏延时 | 工程权衡 |

**口述示例**：

> X5 预览 1.5s，解码不慢，是渲染等待策略把首帧高延时固化继承了。我注释 wait 验证后延时立刻到 0.5s，但不能简单删除——网络有抖动，所以做了 AJB：网络差快升缓冲，网络好慢降，兼顾低延时和平滑。

**相关文档**：[`核心-预览延时优化.md`](../核心-预览延时优化.md)

### 🔬 模拟推演：首帧延时怎么被「固化」，AJB 怎么打破它

这是面试中最有区分度的故事——它不是「改个参数就好了」，而是「模型设计错了」。

```
┌─────────────────────────────────────────────────────────┐
│ 场景: X5 相机 Wi-Fi 直连手机，实时预览                     │
│ 链路: 相机编码 → 网络发送 → 手机接收 → 硬解码 → 渲染上屏   │
│ 目标延时: < 0.5s（用户感知不到）                          │
│ 实际延时: 1.5s ← 用户明显感觉「画面慢半拍」                │
└─────────────────────────────────────────────────────────┘

▸ 第一步：定位 —— 延时到底卡在哪？

  全链路埋点打时间戳:
    相机曝光时间戳 (T0)
      ↓  ~30ms  编码
    编码完成 (T1)
      ↓  ~20ms  网络传输 (Wi-Fi 直连，RTT 很低)
    手机收到帧 (T2)
      ↓  ~10ms  硬解码
    解码完成 (T3)
      ↓  ???    等待渲染
    实际渲染上屏 (T4)

  打点结果:
    T0→T3 = 30+20+10 = 60ms  ← 网络+编解码合计才 60ms，很快！
    T3→T4 = 1440ms = 1.44s   ← 帧解码完了，在渲染队列里躺了 1.44 秒才上屏！

  结论: 延时不在传输，不在解码，在渲染等待策略。

▸ 第二步：根因 —— 首帧延时为什么被「固化」？

  渲染线程的策略（简化还原）:

  ┌─────────────────────────────────────────────────────┐
  │ 小厂的渲染调度逻辑（伪代码）:                          │
  │                                                       │
  │   first_frame_pts = 首帧的 PTS                        │
  │   first_wall_clock = 首帧到达时的系统时间               │
  │                                                       │
  │   for each 新帧:                                      │
  │       # 计算这帧「应该」在什么时间显示                   │
  │       target_wall_clock = first_wall_clock             │
  │                         + (帧PTS - first_frame_pts)    │
  │                                                       │
  │       now = 当前系统时间                               │
  │       if now < target_wall_clock:                     │
  │           sleep(target_wall_clock - now)  ← 等着       │
  │       渲染这一帧                                       │
  │                                                       │
  └─────────────────────────────────────────────────────┘

  问题出在 first_wall_clock。看实际发生了什么:

  T=0.0s   相机开始推流，首帧编码
  T=0.1s   首帧到达手机... 但恰好 Wi-Fi 有个瞬时 burst
           首帧实际到达 T=1.5s（网络抖动导致首帧迟到 1.4s）
           
  渲染线程:
    first_frame_pts  = 0ms     (首帧 PTS，以流开始为 0)
    first_wall_clock = 1500ms  (首帧到达的系统时间)
    
    第 2 帧: pts=33ms
      target = 1500 + (33-0) = 1533ms
      if now < 1533: sleep  ← 被强制等到 1533ms 才渲染
      
    第 3 帧: pts=66ms
      target = 1500 + (66-0) = 1566ms
      sleep 到 1566ms

    第 N 帧: target = 1500 + pts
      → 每一帧都比真实时间晚了 1.5s！

  核心问题:
    first_wall_clock 被首帧的实际到达时间「锚定」了。
    首帧因为网络抖动晚了 1.4s → 这个 1.4s 被永久写入了调度公式。
    后续帧即使解码完立刻就能渲染，也被公式强制 sleep 到 (1500ms + pts)。

  这就是「首帧延时固化」: 网络只抖了一下，渲染策略把这一下变成了永久。

▸ 第三步：验证 —— 注释 wait 试试

  临时把 sleep 注释掉 → 有帧就渲染，不等
  结果: 延时立刻从 1.5s 降到 0.5s  ← 证明根因就是渲染等待策略

  但直接删 wait 不行:
    - 网络抖动时，某帧可能迟到 100ms
    - 不等 → 上一帧显示超时 → 渲染线程没事干 → 画面短暂卡住
    - 下一帧突然到了 → 立刻渲染 → 画面「跳」了一下
    - 用户感受: 「画面呼吸」，忽快忽慢

▸ 第四步：正解 —— AJB（自适应抖动缓冲）

  大厂的思路: 不是「等 or 不等」的二选一，
  而是「动态决定等多少」。

  ┌─────────────────────────────────────────────────────┐
  │ AJB 的核心逻辑:                                       │
  │                                                       │
  │   target_delay = 50ms  # 目标缓冲（理想情况下的延时）   │
  │   current_delay = 测量到的实际延时                     │
  │                                                       │
  │   for each 帧:                                        │
  │       帧到达 → 计算这一帧「迟到」了多少                  │
  │                                                       │
  │       if 网络抖动增大 (连续多帧迟到):                   │
  │           target_delay ↑ 快升                          │
  │           # 牺牲延时保平滑，缓冲大了不怕抖               │
  │                                                       │
  │       if 网络恢复平稳 (连续多帧准时):                   │
  │           target_delay ↓ 慢降                          │
  │           # 逐步回收延时，但不跳帧                       │
  │                                                       │
  │       # 渲染决策:                                      │
  │       if current_delay > target_delay:                 │
  │           追赶模式: 轻微加速/丢非参考帧 → 缩小延时       │
  │       else:                                            │
  │           正常渲染                                     │
  │                                                       │
  └─────────────────────────────────────────────────────┘

  关键设计:

  1. 不再用首帧做锚点
     用 steady_clock（单调时钟，不受系统时间跳变影响）
     每个帧根据自己的 PTS 和当前缓冲深度独立决策

  2. 快升慢降
     ┌─ 网络差: target_delay 快速拉大 — 不黑屏不卡顿
     └─ 网络好: target_delay 慢慢缩小 — 用户无感回收延时

     小厂「快升快降」→ 延时下来了但画面跳 → 用户能感知
     大厂「快升慢降」→ 既保流畅又逐步优化延时

  3. 追赶 + 水位保护
     队列堆积超过阈值 → 丢非参考帧 / 轻微加速播放
     队列快空 → 放慢、等帧（宁可微卡不黑屏）

  效果:
    X5 预览延时: 1.5s → 0.5~0.6s, 下降 60%+
    网络抖动时: 不卡顿不黑屏，延时自动调控

┌─────────────────────────────────────────────────────────┐
│ 面试一句话总结:                                           │
│ 「首帧因为网络抖动迟到，渲染策略把它当锚点，                 │
│   1.5s 的延时被永久继承了。AJB 不设固定锚点，                │
│   而是动态测量→动态调节缓冲深度，                            │
│   在延时和平滑之间做工程上的最优权衡。」                     │
└─────────────────────────────────────────────────────────┘
```

### 💻 代码对比：一个 `enable_ajb` 控制走固定锚点还是动态缓冲 (FFmpeg C API)

```cpp
// =================================================================
// 预览渲染调度: 小厂固定锚点 vs 大厂 AJB (FFmpeg 时间体系)
// =================================================================

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>    // av_gettime_relative()
}

#include <deque>
#include <numeric>

// ============================================================
// 配置
// ============================================================
struct RenderCfg {
    bool  enable_ajb      = false;       // false=固定锚点, true=AJB
    int   target_delay_ms = 50;          // 目标缓冲 (AJB 用)
    int   max_delay_ms    = 500;         // 缓冲上限
    AVRational media_tb   = {1, 90000};  // 媒体时间基 (PTS 的 time_base)
};

// ============================================================
// 渲染调度器: flag 控制路径
// ============================================================
class RenderScheduler {
    RenderCfg cfg_;

    // ---- 小厂: 固定锚点 ----
    int64_t anchor_pts_      = AV_NOPTS_VALUE;
    int64_t anchor_wall_us_  = 0;  // 首帧到达时的 av_gettime_relative()

    // ---- 大厂: AJB ----
    int current_delay_ms_ = 50;
    std::deque<int64_t> recent_delays_;  // 滑动窗口 30 帧

public:
    void configure(const RenderCfg &cfg) { cfg_ = cfg; }

    // ========== 每帧入口 ==========
    // frame: 解码完的 AVFrame, 准备渲染
    void schedule(const AVFrame *frame,
                  std::function<void()> do_render) {
        if (cfg_.enable_ajb) {
            schedule_ajb(frame->pts, do_render);
        } else {
            schedule_fixed_anchor(frame->pts, do_render);
        }
    }

private:
    // ======================================================
    // 小厂: 首帧锚点固定延时
    // ======================================================
    void schedule_fixed_anchor(int64_t frame_pts,
                               std::function<void()> &render) {
        int64_t now_us = av_gettime_relative();  // 微秒, 单调时钟

        if (anchor_pts_ == AV_NOPTS_VALUE) {
            // ★ 首帧 → 锚定!
            //   frame_pts = 这个帧在流里的 PTS (如 0)
            //   now_us    = 实际到达时间
            //   如果首帧因网络抖动迟到了 1.4s (1,400,000us),
            //   这个 1.4s 就被永久写入了 anchor_wall_us_
            anchor_pts_      = frame_pts;
            anchor_wall_us_  = now_us;
            render();
            return;
        }

        // ★ 后续帧: 公式决定渲染时间
        //   target = anchor_wall + (frame_pts - anchor_pts)
        int64_t pts_delta_us = av_rescale_q(
            frame_pts - anchor_pts_,
            cfg_.media_tb,           // 如 1/90000
            AV_TIME_BASE_Q           // 1/1000000 (微秒)
        );

        int64_t target_us = anchor_wall_us_ + pts_delta_us;

        if (now_us < target_us) {
            av_usleep(target_us - now_us);
            // ⚠ 致命问题:
            //  anchor_wall_us_ 已经被首帧「锚」在了 1,500,000us
            //  第 N 帧: target = 1,500,000 + pts_delta
            //  → 每一帧都比真实时间晚 1.5s 才渲染
            //  → 首帧的延时被「固化」成了永久的系统延时
        }
        render();
    }

    // ======================================================
    // 大厂: AJB 动态缓冲 (快升慢降)
    // ======================================================
    void schedule_ajb(int64_t frame_pts,
                      std::function<void()> &render) {
        int64_t now_us = av_gettime_relative();

        // Step 1: 测量端到端延时 (PTS → 实际渲染时间)
        int64_t frame_pts_us = av_rescale_q(
            frame_pts, cfg_.media_tb, AV_TIME_BASE_Q);
        int64_t delay_us = now_us - frame_pts_us;
        int     delay_ms = (int)(delay_us / 1000);

        // Step 2: 滑动窗口 30 帧 (~1s) → 平滑瞬时抖动
        recent_delays_.push_back(delay_ms);
        if (recent_delays_.size() > 30) {
            recent_delays_.pop_front();
        }

        int64_t sum = std::accumulate(
            recent_delays_.begin(), recent_delays_.end(), 0LL);
        int avg_delay = (int)(sum / (int64_t)recent_delays_.size());

        // Step 3: 快升慢降
        if (avg_delay > cfg_.target_delay_ms + 30) {
            // ⬆ 连续抖动 → 快升缓冲 (保流畅, 牺牲延时)
            cfg_.target_delay_ms = std::min(
                cfg_.target_delay_ms + 20,   // 每次升 20ms
                cfg_.max_delay_ms
            );
            av_log(nullptr, AV_LOG_WARNING,
                   "AJB: delay↑ target=%dms (avg=%dms)\n",
                   cfg_.target_delay_ms, avg_delay);
        } else if (avg_delay < cfg_.target_delay_ms - 10) {
            // ⬇ 连续平稳 → 慢降缓冲 (逐步回收延时, 不跳帧)
            cfg_.target_delay_ms = std::max(
                cfg_.target_delay_ms - 5,    // 每次只降 5ms
                30                           // 最低 30ms
            );
            av_log(nullptr, AV_LOG_INFO,
                   "AJB: delay↓ target=%dms (avg=%dms)\n",
                   cfg_.target_delay_ms, avg_delay);
        }

        // Step 4: 追赶 — 延时远超目标 → 主动丢弃非参考帧
        current_delay_ms_ = delay_ms;
        if (current_delay_ms_ > cfg_.target_delay_ms + 50) {
            drop_non_ref_frames();   // 丢 P/B 帧, 保 I 帧
            current_delay_ms_ -= 33;
            return;  // 这帧跳过, 下一帧再来
        }

        // Step 5: 按动态缓冲深度等待 (不是按首帧锚点!)
        if (current_delay_ms_ < cfg_.target_delay_ms) {
            int wait_ms = cfg_.target_delay_ms - current_delay_ms_;
            av_usleep(wait_ms * 1000);
            current_delay_ms_ = cfg_.target_delay_ms;
        }
        render();
    }

    void drop_non_ref_frames() {
        // 遍历渲染队列, 跳过所有非 I 帧
        // 和 FFmpeg 的 key_frame 标记联动:
        //   if (!frame->key_frame) skip;
        //   else render;
    }
};

// ============================================================
// 关键: 为什么 anchor 方案在「首帧迟到」场景下必然出错
// ============================================================
//
//   时间轴 (微秒, av_gettime_relative):
//     T=0        相机开始推流
//     T=50000    首帧编码完成 (PTS=0), 开始网络发送
//     T=1500000  首帧到达手机! (Wi-Fi 抖动导致延迟 1.45s)
//     T=1533000  第 2 帧到达 (PTS=33000)
//     T=1566000  第 3 帧到达 (PTS=66000)
//
//   锚点状态:
//     anchor_pts_     = 0
//     anchor_wall_us_ = 1,500,000  ← 比真实流起点晚了 1.45s!
//
//   调度公式 (所有后续帧):
//     target = 1,500,000 + av_rescale_q(frame_pts - 0,
//                                        {1,90000}, {1,1000000})
//
//   第 2 帧: target = 1,500,000 +  33,000 = 1,533,000 ✅ (准时但晚)
//   第 3 帧: target = 1,500,000 +  66,000 = 1,566,000 ✅ (准时但晚)
//   第 N 帧: target = 1,500,000 + pts_us   → 永远比实时晚 1.45s
//
//   AJB 的做法:
//     不设锚点, 每帧独立计算 delay = now - (PTS → 微秒)
//     delay 大 → 慢降 buff → 逐步回收 → 最终稳定在 50ms

// ====== 使用示例 ======
// RenderCfg cfg;
// cfg.enable_ajb      = true;
// cfg.target_delay_ms = 50;
// cfg.media_tb        = {1, 90000};   // FFmpeg 标准时间基
//
// RenderScheduler sched;
// sched.configure(cfg);
//
// // 解码循环:
// AVFrame *frame = av_frame_alloc();
// while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
//     sched.schedule(frame, [&] {
//         glBindTexture(...);
//         glDrawArrays(...);   // 上屏渲染
//     });
//     av_frame_unref(frame);
// }
```

---

> **▸ 小节过渡**：时间模型、Pipeline、实时调度——这三类解决的是「代码怎么写对、怎么跑快」。
> 但 SDK 开发还有一个更隐蔽的维度：**你的优化怎么安全地交付给客户**。
> 优化代码写得再好，上线炸了就是零分。这就是工程化问题：隔离、灰度、回滚、可观测。

---

## 六、工程化与交付类

| 小厂容易犯 | 大厂做法 | 我们的例子 | 面试关键词 |
|---|---|---|---|
| 底层重构一刀切上线 | **参数开关 + 默认关闭** | `enableLiveZeroCopy` / `enableAsyncSendAndABR` | 向后兼容 |
| 第三方集成符号冲突才发现 | **依赖隔离**（静态链/隐藏符号） | libMNN.so 冲突 | ABI / 符号表 |
| 跨团队需求靠催 | **优先级对齐 + 降低对方成本** | 拉 PM + leader 协调插单 | 统筹思维 |
| 疑难问题一个人闷头搞 | **2 小时规则 + 结构化同步** | Smart Seek 同事点醒 | 及时暴露风险 |
| 踩坑不留文档 | **复盘沉淀成接入规范** | 3.0 算法接入 5 步流程 | 基础建设 |
| 只测 happy path | **控制变量法** | 本地 MP4 vs RTMP 推流 | 可证伪 |

**口述示例**：

> 直播零拷贝和 ABR 我都做成 Java 层布尔开关，JNI 透传，默认 false，旧业务零侵入。这比编译期宏隔离更安全，适合 SDK 灰度。

**相关文档**：[`面试记录.md`](../面试记录.md)、[`self-talk.md`](../../self-talk.md)

### 🔬 模拟推演：为什么「开关」比「宏」安全 —— SDK 灰度的工程逻辑

```
┌─────────────────────────────────────────────────────────┐
│ 场景: 零拷贝推流优化上线，涉及 MediaCodec Surface 输入    │
│ 改动: GPU 渲染目标从 Texture → AHardwareBuffer/Surface   │
│ 风险: 不同设备/Android 版本 Surface 行为不一致            │
└─────────────────────────────────────────────────────────┘

▸ 小厂做法: 直接改，编译期决定

  // sdk_config.h
  #define USE_ZERO_COPY 1  // 改了这个，全部客户都走新路径

  // 编译产物: libsdk.so 只有一个版本
  // → 所有接入方用的都是同一条代码路径
  // → 某台设备 Surface 不兼容 → 客户 App 黑屏 → 全量回滚

  时间线:
    D-0  16:00  合入主干，CI 通过（测试机只有 Pixel 和 小米旗舰）
    D-0  18:00  发版 3.2.0
    D+0  20:00  客户 A 的红米 Note 8 用户反馈「直播黑屏」
    D+0  21:00  客户 B 的 OPPO A 系列也出现黑屏
    D+0  22:00  紧急回滚到 3.1.0，发 hotfix 3.2.1
    D+1  全天   排查发现: MediaCodec Surface 输入在部分机型上
               需要额外配置 color format
    D+2  10:00  修好，再发 3.2.2

  代价: 两天时间 + 客户信任 + 线上事故记录

▸ 大厂做法: 运行时开关，默认关闭

  // Java 层
  public class LiveConfig {
      // 默认 false，新功能不影响存量客户
      boolean enableZeroCopy = false;
      boolean enableAsyncSend = false;
      boolean enableABR = false;
  }

  // JNI 层透传
  // native_live.cpp
  void configureLivePipeline(JNIEnv* env, jobject config) {
      bool zeroCopy = getBooleanField(env, config, "enableZeroCopy");
      bool asyncSend = getBooleanField(env, config, "enableAsyncSend");

      if (zeroCopy) {
          initSurfaceInputPipeline();   // 新路径
      } else {
          initTextureReadbackPipeline(); // 旧路径，稳定
      }
      // ...
  }

  时间线:
    D-0  10:00  合入，默认 false — 所有客户行为不变，CI 全绿
    D-0  14:00  发版 3.2.0（包含零拷贝代码，但不执行）
    D+0  16:00  内部测试 App 打开开关: enableZeroCopy=true
                在 20+ 款测试机上验证一周
    D+5        OPPO A 系列发现不兼容 → 加机型黑名单/修复
    D+7        内部全量验证通过
    D+10       客户 A 灰度: 1% 用户打开开关
    D+11       监控无异常 → 10% → 50% → 100%
    D+14       客户 A 全量，客户 B 开始灰度
    D+21       全量稳定运行

  关键差异:
    1. 默认关闭  → 发版零风险，旧客户行为完全不变
    2. 运行时开关 → 出问题关开关就行，不需要重新编译发版
    3. 灰度能力   → 逐客户、逐比例放量，问题影响面可控
    4. 机型过滤   → 已知不兼容机型自动关开关

┌─────────────────────────────────────────────────────────┐
│ 面试一句话总结:                                           │
│ 「编译期宏是二进制的选择，运行时开关是运营的选择。          │
│  SDK 的底线是: 新功能可以不开，但不能炸存量。               │
│  开关 + 灰度 = 把事故半径从『全量』缩小到『可控』。」        │
└─────────────────────────────────────────────────────────┘
```

---

> **▸ 小节过渡**：前面四类——时间模型、Pipeline、实时调度、工程交付——解决的是单点技术问题。
> 但作为 SDK 开发者，你的代码要跑在 iOS 和 Android 上，要集成第三方的 .so，
> 要应对跨团队协作的不确定性。这一节把跨平台和协作类坑点集中列出来，
> 作为前面四类的**横向补充**。

---

## 七、跨平台、可观测与协作（横向补充）

> 前面四类（三~六）是纵深——每类一个问题、一段模拟、一份代码。
> 下面两张表是**横向**——SDK 跨平台开发的常见坑、以及所有优化都依赖的可观测性基础。
> 面试时不需要逐条讲，挑一个你最有故事的做补充即可。

### 7.1 SDK / 跨平台

| 小厂容易犯 | 大厂做法 | 我们可关联 | 面试关键词 |
|---|---|---|---|
| iOS/Android 各写一套逻辑 | **C++ 核心 + 统一契约** | 全景剪辑统一渲染层 | 双端一致 |
| JNI/OC 回调生命周期乱 | **shared_ptr 值捕获、Ref 规范** | 直播 async queue Lambda | 生命周期 |
| 算法 SO 版本和宿主不一致 | **依赖内嵌 / 符号隐藏** | libMNN 静态链进 SDK | 第三方集成 |
| 新算法接入没有标准流程 | **打点→精度→Seek→并行→内存** | 算法接入 5 步 | 基建 |
| 对外 SDK 巨型类 | **聚合根 + 子域拆分** | KMP 重构设计 | 可扩展性 |

### 7.2 可观测性（小厂最晚补、大厂最早做）

| 小厂容易犯 | 大厂做法 | 我们的例子 | 面试关键词 |
|---|---|---|---|
| 「感觉卡」争论不休 | **先定义指标** | 预览端到端 1.5s vs 0.5s | 可量化 |
| 日志散落 println | **全链路分段耗时** | `SFT_SCOPE`、节点时间戳 | Trace |
| 优化完无法回归 | **基准场景 + 前后对比** | 22fps→30fps、出片数倍 | 数据说话 |
| 线上问题难复现 | **关键状态可 dump** | ABR 队列水位、码率告警 | 可诊断 |

**面试金句**：

> 大厂不是更聪明，是**更早把「测量」当成开发流程的一部分**，不是优化完才补打点。

---

## 八、按项目映射（一张表讲清楚你的经历）

| 项目 | 踩的坑类型 | 大厂范式 | 可讲的「小厂→大厂」转变 |
|---|---|---|---|
| **自动剪辑 3.0** | 精度 + 吞吐 | frameId + Smart Seek + 异构 Pipeline | 从 float 时间到整数索引；从串行到流水线 |
| **4K 直播** | 吞吐 + 弱网 | 零拷贝 + 异步发送 + 水位 ABR | 从同步阻塞到背压感知架构 |
| **车载预览** | 实时 + 时钟 | AJB + 单调时钟 + Resync | 从删 wait 到场景化调度 |
| **libMNN 集成** | 依赖 + 协作 | 依赖隔离 + 优先级对齐 | 从「不配合」到「排期不对齐」 |

---

## 九、值得提前准备的「同类坑」（未必全踩过，面试常问）

| 坑 | 小厂表现 | 大厂预防 |
|---|---|---|
| **B 帧 DTS≠PTS** | 切点/Seek 算错 | 全程 pts 整数，理解 decode vs presentation order |
| **旋转/裁剪 metadata** | 抽帧尺寸和算法输入不一致 | 统一 EXIF/rotation 处理链 |
| **硬解输出格式碎片化** | NV12/P010 路径不统一 | 格式协商 + 统一中间格式 |
| **解码器非线程安全** | 多路抽帧 crash | 每路独立 decoder 或加锁策略 |
| **fMP4 vs 普通 MP4** | 直播/点播容器选错 | 按场景选容器（见 Doc/ffmpeg/05） |
| **音画 time_base 不同** | 合成 rescale 错误 | `av_rescale_q` 统一到输出流 tb |
| **Seek 后不解 flush** | 花屏/PTS 错乱 | 经典 Seek 必 flush，Smart Seek 同 GOP 例外 |
| **Global Header / extradata** | 硬解黑屏 | MP4 avcC、Annex-B 转换（bsf） |

---

## 十、面试怎么「打包讲」（2 分钟版）

> 我在影石做 SDK 这几年，发现小厂和大厂差距 often 不在会不会 FFmpeg，而在**工程契约**上。  
>
> 第一类是**时间模型**。自动剪辑 2.0 用 float 毫秒和 frameId 互转，边界帧会偏；3.0 改成 frameId 主索引 + pts 整数 Seek，这是大厂标准做法。  
>
> 第二类是**Pipeline 吞吐**。4K 直播不是编码器慢，是 CPU 拷贝和网络同步阻塞；我用零拷贝、异步队列、水位 ABR 做架构级改造。  
>
> 第三类是**实时调度**。预览延时不是解码慢，是首帧延时被渲染策略固化；我用 AJB 在延时和平滑间做权衡。  
>
> 第四类是**协作和基建**。libMNN 冲突让我学会优先级对齐；3.0 我把算法接入流程沉淀成团队规范。  
>
> 这些问题的共性是：**看起来是业务 Bug，根因是底层设计没对齐**。我的习惯是先测量、再定契约、最后做架构，并且沉淀成可复用流程。

---

## 十一、和 JD 的对齐话术

### 剪映 / 小红书 / 视频编辑类

- 强调：**frameIndex、Smart Seek、异构 Pipeline、分段打点**——和剪辑 SDK 内核思路一致。
- 弱项诚实补：**时间线/Track/转场**不是你负责的，但 **抽帧精度和 Pipeline 吞吐**是强相关底层能力。

### 直播 / RTC 类

- 强调：**零拷贝、异步发送、ABR、背压、队列水位**。
- 延伸：AJB 经验可类比 **Jitter Buffer / 拥塞控制** 的工程权衡。

### 通用 C++ / 音视频 SDK

- 强调：**向后兼容开关、生命周期安全、跨团队推进、文档沉淀**。

---

## 十二、自检清单（面试前过一遍）

- [ ] 能举 **4 类坑**各一个真实例子（精度/吞吐/实时/协作）
- [ ] 每例子都能说 **小厂会怎么做、大厂会怎么做、我怎么改的**
- [ ] 能解释 **为什么自动剪辑是「契约问题」不是「算法慢」**
- [ ] 能对比 **2.0 vs 3.0** 和 **本地 MP4 vs 推流** 两个「控制变量」故事
- [ ] 不会空泛说「大厂更好」——能落到 **pts、Pipeline、ABR、AJB** 具体概念

---

## 十三、相关文档索引

| 文档 | 内容 |
|---|---|
| [`自动剪辑-帧ID与时间戳Seek精度方案.md`](./自动剪辑-帧ID与时间戳Seek精度方案.md) | 精度 + Seek + FFmpeg 流程详解 |
| [`核心-自动剪辑性能优化.md`](../核心-自动剪辑性能优化.md) | 项目 STAR + Smart Seek + 并行 |
| [`核心-直播性能优化.md`](../核心-直播性能优化.md) | 零拷贝 + 异步 + ABR |
| [`核心-预览延时优化.md`](../核心-预览延时优化.md) | AJB + 首帧固化 |
| [`面试记录.md`](../面试记录.md) | 基建贡献 + 算法接入 5 步 |
| [`self-talk.md`](../../self-talk.md) | 自我介绍 + 缺点 + 方法论 |
| [`Doc/ffmpeg/00-FFmpeg全景导读.md`](../../../Doc/ffmpeg/00-FFmpeg全景导读.md) | FFmpeg 时间体系、Pipeline 主线 |
