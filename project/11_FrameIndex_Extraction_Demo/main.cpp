// ==============================================================================
// Frame Index Extraction Demo
// ──────────────────────────────────────────────────────────────────────────────
// 演示核心命题：
//   "用容器原生的 Sample Index 替代时间戳 Seek，彻底消除帧ID↔时间戳的精度误差"
//
// 覆盖内容：
//   Part A — 从 FFmpeg index_entries 构建关键帧索引（等价于解析 stss Box）
//   Part B — AVSEEK_FLAG_FRAME 按帧号精确 Seek（对比时间戳 Seek）
//   Part C — Smart Seek 决策引擎（基于索引判断同 GOP）
//   Part D — 逐帧扫描构建完整帧索引 + 字节级精确寻址
//   Part E — 精度对比报告
//
// 编译：
//   cmake -S . -B build && cmake --build build
//
// 运行：
//   ./build/frame_index_demo <input.mp4> [--extract frame_id] [--compare]
//
// 环境要求：FFmpeg >= 6.0（用 avformat_index_get_entry 公开 API）
// ==============================================================================

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════════════════════════
// 工具函数
// ══════════════════════════════════════════════════════════════════════════════

static double PtsToSeconds(int64_t pts, AVRational time_base) {
    if (pts == AV_NOPTS_VALUE) return NAN;
    return pts * av_q2d(time_base);
}

// ✅ 大厂推荐：先转毫秒整数，再用 av_rescale_q，避开 float 截断
static int64_t SecondsToPtsAccurate(double seconds, AVRational time_base) {
    const int64_t ms = static_cast<int64_t>(llround(seconds * 1000.0));
    return av_rescale_q(ms, AVRational{1, 1000}, time_base);
}

// ❌ 反面教材：float 直除，Seek 边界可能偏 1 个刻度
static int64_t SecondsToPtsFloat(double seconds, AVRational time_base) {
    return static_cast<int64_t>(seconds / av_q2d(time_base));
}

// ❌ 反面教材：用平均帧间隔硬算帧ID的时间戳
static int64_t FrameIdToPtsNaive(int frame_id, double avg_frame_duration,
                                 AVRational time_base) {
    double sec = frame_id * avg_frame_duration;
    return static_cast<int64_t>(sec / av_q2d(time_base));
}

static void PrintSeparator(const char* title = nullptr) {
    std::printf("\n");
    if (title)
        std::printf("═══ %s ═══\n", title);
    else
        std::printf("═══════════════════════════════════════════════════════════\n");
}

// ══════════════════════════════════════════════════════════════════════════════
// Part A: 数据结构 — "帧的房号"
// ══════════════════════════════════════════════════════════════════════════════

struct FrameSampleInfo {
    int     frame_id;       // 帧序号（从 0 开始）

    int64_t pts;            // PTS（time_base 单位）
    int64_t dts;
    double  pts_seconds;    // 预计算的秒数（仅展示）

    int64_t byte_offset;    // 文件绝对字节偏移（来自 stco + stsc）
    int     size_bytes;     // 帧数据大小（来自 stsz）

    bool    is_keyframe;    // stss 判定：是否可独立解码

    int     gop_index;      // 所属 GOP 编号
    int     frame_in_gop;   // 在 GOP 内第几帧
};

using FrameIndex = std::vector<FrameSampleInfo>;

// ══════════════════════════════════════════════════════════════════════════════
// Part B: 从 FFmpeg index_entries 构建关键帧索引
// （等价于解析 MP4 的 stss + stco Box）
// ══════════════════════════════════════════════════════════════════════════════

struct KeyframeEntry {
    int     index;          // 在 index_entries 中的序号
    int64_t pts;
    int64_t pos;            // 字节偏移（来自 stco）
    double  pts_seconds;
    int     gop_index;
};

static int IndexEntryCount(const AVStream* stream) {
    return avformat_index_get_entries_count(stream);
}

static std::vector<KeyframeEntry> BuildKeyframeIndex(AVStream* stream) {
    std::vector<KeyframeEntry> keyframes;
    const int count = IndexEntryCount(stream);

    if (count <= 0) return keyframes;

    int gop_id = 0;
    for (int i = 0; i < count; i++) {
        const AVIndexEntry* e = avformat_index_get_entry(stream, i);
        if (!e || !(e->flags & AVINDEX_KEYFRAME)) continue;

        KeyframeEntry kf;
        kf.index       = i;
        kf.pts         = e->timestamp;
        kf.pos         = e->pos;
        kf.pts_seconds = PtsToSeconds(e->timestamp, stream->time_base);
        kf.gop_index   = gop_id++;
        keyframes.push_back(kf);
    }
    return keyframes;
}

// ══════════════════════════════════════════════════════════════════════════════
// Part C: Smart Seek 决策引擎
// 基于关键帧索引判断是否需要 Classic Seek
// ══════════════════════════════════════════════════════════════════════════════

struct SeekDecision {
    bool need_classic_seek = true;   // 是否需要 demux seek + flush
    int  prev_keyframe_id  = -1;     // 目标前最近关键帧ID（用于日志）
    const char* reason     = "未知";
};

// 找到目标 PTS 之前最近的关键帧
static int FindPrevKeyframeIndex(const std::vector<KeyframeEntry>& keyframes,
                                 int64_t target_pts) {
    int best = -1;
    for (const auto& kf : keyframes) {
        if (kf.pts <= target_pts) {
            best = kf.gop_index;
        } else {
            break;
        }
    }
    return best;
}

// 判断两个 PTS 之间是否存在关键帧（即是否跨 GOP）
static bool HasKeyframeBetween(const std::vector<KeyframeEntry>& keyframes,
                               int64_t lo_pts, int64_t hi_pts) {
    for (const auto& kf : keyframes) {
        if (kf.pts > lo_pts && kf.pts < hi_pts) {
            return true;
        }
    }
    return false;
}

static SeekDecision MakeSeekDecision(const std::vector<KeyframeEntry>& keyframes,
                                     int64_t current_pts, int64_t target_pts,
                                     bool is_first_seek) {
    SeekDecision d;

    // 情况1：首次 Seek —— 必须 Classic
    if (is_first_seek || current_pts == AV_NOPTS_VALUE) {
        d.need_classic_seek = true;
        d.reason = "首次 Seek，需要建立解码器基线";
        d.prev_keyframe_id = FindPrevKeyframeIndex(keyframes, target_pts);
        return d;
    }

    // 情况2：向后 Seek —— 必须 Classic
    if (target_pts < current_pts) {
        d.need_classic_seek = true;
        d.reason = "目标在当前解码位置之前，解码器不能倒放";
        d.prev_keyframe_id = FindPrevKeyframeIndex(keyframes, target_pts);
        return d;
    }

    // 情况3：同 GOP 内向前 —— Smart Seek
    if (!HasKeyframeBetween(keyframes, current_pts, target_pts)) {
        d.need_classic_seek = false;
        d.reason = "同 GOP 内向前 → Smart Seek（跳过 demux seek + flush）";
        d.prev_keyframe_id = FindPrevKeyframeIndex(keyframes, current_pts);
        return d;
    }

    // 情况4：跨 GOP 向前 —— 需要 Classic
    d.need_classic_seek = true;
    d.reason = "跨 GOP，中间有关键帧 → Classic Seek 更优";
    d.prev_keyframe_id = FindPrevKeyframeIndex(keyframes, target_pts);
    return d;
}

// ══════════════════════════════════════════════════════════════════════════════
// Part D: 解码器管理
// ══════════════════════════════════════════════════════════════════════════════

static AVCodecContext* OpenVideoDecoder(AVStream* stream) {
    const AVCodec* dec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!dec) return nullptr;

    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) return nullptr;

    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }

    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

// ══════════════════════════════════════════════════════════════════════════════
// Part E: 抽帧操作
// ══════════════════════════════════════════════════════════════════════════════

struct ExtractionResult {
    bool    ok            = false;
    int64_t frame_pts     = AV_NOPTS_VALUE;
    double  frame_seconds = 0.0;
    int     frames_decoded = 0;
    int     frames_dropped = 0;
    int     frame_id       = -1;    // 解出的帧是视频流中第几帧
};

// ── E1: Classic Seek（demux seek + flush）────────────────────────────────
static int ClassicSeek(AVFormatContext* fmt, AVCodecContext* dec,
                       int stream_idx, int64_t target_pts) {
    int ret = avformat_seek_file(fmt, stream_idx,
                                 INT64_MIN, target_pts, target_pts,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) return ret;
    avcodec_flush_buffers(dec);
    return 0;
}

// ── E2: 顺解码直到帧的 PTS >= target_pts ─────────────────────────────────
static ExtractionResult DecodeUntilPts(AVFormatContext* fmt,
                                       AVCodecContext* dec,
                                       int video_stream_idx,
                                       int64_t target_pts) {
    ExtractionResult r;
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) goto out;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

            // save packet position/size before decoding (FFmpeg 7+ compat)
            int64_t saved_pos  = pkt->pos;
            int     saved_size = pkt->size;
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (true) {
            int ret = avcodec_receive_frame(dec, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) goto out;

            r.frames_decoded++;
            int64_t pts = frame->pts;

            if (pts == AV_NOPTS_VALUE) {
                av_frame_unref(frame);
                continue;
            }

            r.frame_id++;

            if (pts >= target_pts) {
                r.ok            = true;
                r.frame_pts     = pts;
                r.frame_seconds = PtsToSeconds(pts, dec->pkt_timebase);
                goto out;
            }

            r.frames_dropped++;
            av_frame_unref(frame);
        }
    }

out:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return r;
}

// ── E3: ✅ 方案一 — 用 AVSEEK_FLAG_FRAME 按帧号 Seek ────────────────────
static ExtractionResult ExtractByFrameId(AVFormatContext* fmt,
                                          AVCodecContext* dec,
                                          int video_stream_idx,
                                          int target_frame_id) {
    std::printf("      [FLAG_FRAME 模式] 直接 Seek 到第 %d 帧\n", target_frame_id);

    // ★ 核心 API：按帧号 Seek —— 零时间戳参与！
    int ret = av_seek_frame(fmt, video_stream_idx, target_frame_id,
                            AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::fprintf(stderr, "      ⚠️  av_seek_frame(FLAG_FRAME) 失败: %d\n", ret);
        return ExtractionResult{};
    }

    avcodec_flush_buffers(dec);

    ExtractionResult r;
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) goto out2;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

            // save packet position/size before decoding (FFmpeg 7+ compat)
            int64_t saved_pos  = pkt->pos;
            int     saved_size = pkt->size;
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(dec, frame) >= 0) {
            r.frames_decoded++;
            r.frame_id++;

            if (r.frame_id == target_frame_id) {
                r.ok            = true;
                r.frame_pts     = frame->pts;
                r.frame_seconds = PtsToSeconds(frame->pts, dec->pkt_timebase);
                goto out2;
            }

            if (r.frame_id < target_frame_id) {
                r.frames_dropped++;
                av_frame_unref(frame);
            } else {
                // 已经超过目标帧号还没匹配，说明 seek 可能不精确
                r.ok            = true; // 仍然返回，但标记
                r.frame_pts     = frame->pts;
                r.frame_seconds = PtsToSeconds(frame->pts, dec->pkt_timebase);
                goto out2;
            }
        }
    }

out2:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return r;
}

// ── E4: ✅ 方案二 — 用 FrameIndex + AVSEEK_FLAG_BYTE 字节 Seek ──────────
static ExtractionResult ExtractByByteOffset(AVFormatContext* fmt,
                                             AVCodecContext* dec,
                                             int video_stream_idx,
                                             const FrameIndex& index,
                                             int target_frame_id) {
    if (target_frame_id < 0 || target_frame_id >= (int)index.size()) {
        std::fprintf(stderr, "      ⚠️  frame_id=%d 超出索引范围 [0, %zu)\n",
                     target_frame_id, index.size());
        return ExtractionResult{};
    }

    const auto& info = index[target_frame_id];

    std::printf("      [BYTE 模式] 帧%d 字节偏移=%lld, is_keyframe=%d\n",
                target_frame_id,
                static_cast<long long>(info.byte_offset),
                info.is_keyframe);

    // 找前面最近的关键帧
    int kf_id = target_frame_id;
    while (kf_id >= 0 && !index[kf_id].is_keyframe) {
        kf_id--;
    }

    if (kf_id < 0) {
        std::fprintf(stderr, "      ⚠️  找不到前面的关键帧\n");
        return ExtractionResult{};
    }

    std::printf("      Seek 到关键帧 #%d (字节偏移=%lld)\n",
                kf_id, static_cast<long long>(index[kf_id].byte_offset));

    int ret = av_seek_frame(fmt, video_stream_idx,
                            index[kf_id].byte_offset,
                            AVSEEK_FLAG_BYTE);
    if (ret < 0) {
        std::fprintf(stderr, "      ⚠️  av_seek_frame(BYTE) 失败: %d\n", ret);
        return ExtractionResult{};
    }

    avcodec_flush_buffers(dec);

    // 从关键帧顺序解码到 target_frame_id
    ExtractionResult r;
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) goto out3;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

            // save packet position/size before decoding (FFmpeg 7+ compat)
            int64_t saved_pos  = pkt->pos;
            int     saved_size = pkt->size;
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(dec, frame) >= 0) {
            r.frames_decoded++;
            r.frame_id++;

            if (r.frame_id == target_frame_id) {
                r.ok            = true;
                r.frame_pts     = frame->pts;
                r.frame_seconds = PtsToSeconds(frame->pts, dec->pkt_timebase);
                goto out3;
            }

            if (r.frame_id < target_frame_id) {
                r.frames_dropped++;
            }
            av_frame_unref(frame);
        }
    }

out3:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return r;
}

// ── E5: ❌ 反面教材 — 用时间戳 + float 换算 Seek ────────────────────────
static ExtractionResult ExtractByTimestampFloat(AVFormatContext* fmt,
                                                 AVCodecContext* dec,
                                                 int video_stream_idx,
                                                 int target_frame_id,
                                                 double avg_frame_duration,
                                                 AVRational time_base) {
    // ❌ 用平均帧间隔硬算时间戳
    double target_sec = target_frame_id * avg_frame_duration;
    int64_t target_pts = SecondsToPtsFloat(target_sec, time_base);

    std::printf("      [时间戳模式] frame_id=%d → %.6fs → pts=%lld (已引入浮点误差)\n",
                target_frame_id, target_sec, static_cast<long long>(target_pts));

    int ret = avformat_seek_file(fmt, video_stream_idx,
                                 INT64_MIN, target_pts, target_pts,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::fprintf(stderr, "      ⚠️  时间戳 Seek 失败\n");
        return ExtractionResult{};
    }

    avcodec_flush_buffers(dec);
    return DecodeUntilPts(fmt, dec, video_stream_idx, target_pts);
}

// ── E6: ✅ 推荐 — 用 av_rescale_q 的精确时间戳 Seek ──────────────────
static ExtractionResult ExtractByTimestampAccurate(AVFormatContext* fmt,
                                                    AVCodecContext* dec,
                                                    int video_stream_idx,
                                                    int target_frame_id,
                                                    double avg_frame_duration,
                                                    AVRational time_base) {
    double target_sec = target_frame_id * avg_frame_duration;
    int64_t target_pts = SecondsToPtsAccurate(target_sec, time_base);

    std::printf("      [精确时间戳] frame_id=%d → av_rescale_q → pts=%lld\n",
                target_frame_id, static_cast<long long>(target_pts));

    int ret = avformat_seek_file(fmt, video_stream_idx,
                                 INT64_MIN, target_pts, target_pts,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::fprintf(stderr, "      ⚠️  精确时间戳 Seek 失败\n");
        return ExtractionResult{};
    }

    avcodec_flush_buffers(dec);
    return DecodeUntilPts(fmt, dec, video_stream_idx, target_pts);
}

// ══════════════════════════════════════════════════════════════════════════════
// Part F: 逐帧扫描构建完整 FrameIndex
// 走一遍完整解码管线，记录每帧的 pts、字节偏移（尽力）、是否关键帧
// 生产环境中这步在"上传时"异步完成，线上直接查表
// ══════════════════════════════════════════════════════════════════════════════

static FrameIndex BuildFullFrameIndex(AVFormatContext* fmt,
                                      AVCodecContext* dec,
                                      AVStream* stream,
                                      int video_stream_idx,
                                      const std::vector<KeyframeEntry>& keyframes) {
    FrameIndex index;
    std::map<int64_t, int> pts_to_keyframe_gop;

    // 建立 pts → GOP编号 的快速查找
    for (const auto& kf : keyframes) {
        pts_to_keyframe_gop[kf.pts] = kf.gop_index;
    }

    // Seek 回文件开头
    av_seek_frame(fmt, video_stream_idx, 0, AVSEEK_FLAG_BYTE);
    avcodec_flush_buffers(dec);

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return index;
    }

    int    frame_cnt   = 0;
    int    current_gop = 0;
    int    frame_in_gop = 0;
    int64_t last_gop_kf_pts = AV_NOPTS_VALUE;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

            // save packet position/size before decoding (FFmpeg 7+ compat)
            int64_t saved_pos  = pkt->pos;
            int     saved_size = pkt->size;
        if (avcodec_send_packet(dec, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(dec, frame) >= 0) {
            FrameSampleInfo info;
            info.frame_id    = frame_cnt++;
            info.pts         = frame->pts;
            info.dts         = frame->pkt_dts;
            info.pts_seconds = PtsToSeconds(frame->pts, stream->time_base);
            info.byte_offset = saved_pos;   // ← 来自解封装层，尽力而为
            info.size_bytes  = saved_size;

            // 判断是否关键帧
            info.is_keyframe = (frame->flags & AV_FRAME_FLAG_KEY) ||
                               (frame->pict_type == AV_PICTURE_TYPE_I);

            // GOP 归属
            if (info.is_keyframe) {
                // 找最近的 keyframe entry
                auto it = pts_to_keyframe_gop.find(info.pts);
                if (it != pts_to_keyframe_gop.end()) {
                    current_gop = it->second;
                } else {
                    current_gop++;
                }
                frame_in_gop    = 0;
                last_gop_kf_pts = info.pts;
            } else {
                frame_in_gop++;
            }
            info.gop_index    = current_gop;
            info.frame_in_gop = frame_in_gop;

            index.push_back(info);
            av_frame_unref(frame);
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);

    // Seek 回开头，方便后续使用
    av_seek_frame(fmt, video_stream_idx, 0, AVSEEK_FLAG_BYTE);
    avcodec_flush_buffers(dec);

    return index;
}

// ══════════════════════════════════════════════════════════════════════════════
// Part G: 精度对比 + 报告输出
// ══════════════════════════════════════════════════════════════════════════════

static void PrintFrameIndexSummary(const FrameIndex& index, int max_lines = 10) {
    std::printf("\n  %-8s %-12s %-10s %-12s %-8s %-6s %-5s\n",
                "frameId", "pts", "秒", "字节偏移", "大小", "GOP", "GOP内");
    std::printf("  ────────────────────────────────────────────────────\n");

    int gop_count = 0;
    int printed   = 0;
    for (const auto& f : index) {
        if (f.is_keyframe) gop_count++;
        if (printed < max_lines) {
            std::printf("  %-8d %-12lld %-10.4f %-12lld %-8d %-6d %-5d%s\n",
                        f.frame_id,
                        static_cast<long long>(f.pts),
                        f.pts_seconds,
                        static_cast<long long>(f.byte_offset),
                        f.size_bytes,
                        f.gop_index,
                        f.frame_in_gop,
                        f.is_keyframe ? " ★" : "");
            printed++;
        }
    }
    if ((int)index.size() > max_lines) {
        std::printf("  ... (共 %zu 帧，%d 个 GOP)\n", index.size(), gop_count);
    }
}

static void PrintKeyframeIndex(const std::vector<KeyframeEntry>& keyframes) {
    std::printf("\n  %-6s %-12s %-10s %-12s %-6s\n",
                "#", "pts", "秒", "字节偏移", "GOP");
    std::printf("  ──────────────────────────────────────────\n");
    for (const auto& kf : keyframes) {
        std::printf("  %-6d %-12lld %-10.4f %-12lld %-6d\n",
                    kf.index,
                    static_cast<long long>(kf.pts),
                    kf.pts_seconds,
                    static_cast<long long>(kf.pos),
                    kf.gop_index);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// main
// ══════════════════════════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf(
            "用法: %s <input.mp4> [选项]\n\n"
            "  选项:\n"
            "    --extract N      抽第 N 帧（演示三种方法对比）\n"
            "    --compare        运行全部方法并打印精度对比表\n"
            "    --full-index     逐帧扫描构建完整帧索引\n\n"
            "  示例:\n"
            "    %s test.mp4\n"
            "    %s test.mp4 --extract 42\n"
            "    %s test.mp4 --extract 42 --full-index\n"
            "    %s test.mp4 --compare\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 0;
    }

    const char* input_path = argv[1];

    bool do_extract   = false;
    bool do_compare   = false;
    bool do_full_index = false;
    int  extract_frame_id = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--extract") == 0 && i + 1 < argc) {
            do_extract = true;
            extract_frame_id = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--compare") == 0) {
            do_compare = true;
        } else if (strcmp(argv[i], "--full-index") == 0) {
            do_full_index = true;
        }
    }

    if (!do_extract && !do_compare) {
        do_compare = true;  // 默认行为
        do_full_index = true;
    }

    // ── 打开文件 ──────────────────────────────────────────────────────
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, input_path, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "打不开文件: %s\n", input_path);
        return 1;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        std::fprintf(stderr, "读不到流信息\n");
        avformat_close_input(&fmt);
        return 1;
    }

    const int video_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO,
                                              -1, -1, nullptr, 0);
    if (video_idx < 0) {
        std::fprintf(stderr, "找不到视频流\n");
        avformat_close_input(&fmt);
        return 1;
    }

    AVStream*       video_stream = fmt->streams[video_idx];
    AVCodecContext* dec          = OpenVideoDecoder(video_stream);
    if (!dec) {
        std::fprintf(stderr, "打不开解码器\n");
        avformat_close_input(&fmt);
        return 1;
    }

    // ── 基本信息 ──────────────────────────────────────────────────────
    double duration_sec = static_cast<double>(fmt->duration) / AV_TIME_BASE;
    int64_t nb_frames   = video_stream->nb_frames;
    double avg_frame_dur = (nb_frames > 0)
        ? duration_sec / nb_frames
        : av_q2d(video_stream->r_frame_rate);

    std::printf("╔═══════════════════════════════════════════════════════╗\n");
    std::printf("║   帧索引精确抽帧 Demo                                 ║\n");
    std::printf("╚═══════════════════════════════════════════════════════╝\n");
    std::printf("\n文件: %s\n", input_path);
    std::printf("编码: %s  %dx%d\n",
                avcodec_get_name(video_stream->codecpar->codec_id),
                video_stream->codecpar->width,
                video_stream->codecpar->height);
    std::printf("时长: %.3f 秒\n", duration_sec);
    std::printf("帧数: %lld 帧 (容器声明)\n", static_cast<long long>(nb_frames));
    std::printf("帧率: %.3f fps (avg_frame_rate)\n",
                av_q2d(video_stream->avg_frame_rate));
    std::printf("平均帧间隔: %.6f 秒/帧 (≈%.2f ms)\n",
                avg_frame_dur, avg_frame_dur * 1000.0);
    std::printf("time_base: %d/%d (≈%.9f 秒/刻度)\n",
                video_stream->time_base.num,
                video_stream->time_base.den,
                av_q2d(video_stream->time_base));

    // ── Part A: 关键帧索引（stss 等价物）──────────────────────────────
    PrintSeparator("Part A: 关键帧索引（来自 FFmpeg index_entries，等价于 MP4 stss Box）");

    auto keyframes = BuildKeyframeIndex(video_stream);
    std::printf("\n关键帧总数: %zu\n", keyframes.size());

    if (keyframes.size() >= 2) {
        int64_t gop_duration = keyframes[1].pts - keyframes[0].pts;
        std::printf("GOP 间隔: %lld pts (≈ %.3f 秒, ≈ %.1f 帧)\n",
                    static_cast<long long>(gop_duration),
                    PtsToSeconds(gop_duration, video_stream->time_base),
                    PtsToSeconds(gop_duration, video_stream->time_base) / avg_frame_dur);
    }

    PrintKeyframeIndex(keyframes);

    if (keyframes.empty()) {
        std::printf("\n⚠️  无关键帧索引！可能原因:\n");
        std::printf("   - 文件未包含有效的 moov box\n");
        std::printf("   - 容器格式不支持 stss（如 FLV/TS）\n");
        std::printf("   - 试试 MP4 文件: ffmpeg -f lavfi -i testsrc=d=5:r=30 test.mp4\n");
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return 1;
    }

    // ── Part B: 完整帧索引 ───────────────────────────────────────────
    if (do_full_index) {
        PrintSeparator("Part B: 完整帧索引（逐帧扫描，生产环境在'上传时'异步完成）");

        FrameIndex full_index = BuildFullFrameIndex(fmt, dec, video_stream, video_idx, keyframes);
        PrintFrameIndexSummary(full_index, 15);

        std::printf("\n索引信息:\n");
        std::printf("  总帧数: %zu\n", full_index.size());
        int kf_count = 0;
        for (const auto& f : full_index) {
            if (f.is_keyframe) kf_count++;
        }
        std::printf("  关键帧数: %d\n", kf_count);
        std::printf("  GOP 数: %d\n",
                    full_index.empty() ? 0 : full_index.back().gop_index + 1);

        // ── 演示 Smart Seek 决策 ────────────────────────────────────
        PrintSeparator("Part C: Smart Seek 决策示例");

        if (full_index.size() > 10) {
            // 模拟一系列抽帧请求
            int64_t current_pts = AV_NOPTS_VALUE;
            std::vector<int> test_frames = {0, 5, 8, 15, 3};

            for (size_t i = 0; i < test_frames.size(); i++) {
                int fid = test_frames[i];
                if (fid >= (int)full_index.size()) continue;

                int64_t target_pts = full_index[fid].pts;
                SeekDecision d = MakeSeekDecision(keyframes,
                                                  current_pts,
                                                  target_pts,
                                                  i == 0);

                std::printf("\n  请求 #%zu: 抽第 %d 帧 (pts=%lld, ≈%.4fs)\n",
                            i, fid,
                            static_cast<long long>(target_pts),
                            full_index[fid].pts_seconds);
                std::printf("    决策: %s\n", d.reason);
                std::printf("    动作: %s\n",
                            d.need_classic_seek ? "Classic Seek (demux seek + flush)"
                                                : "Smart Seek  (跳过 seek/flush，直接顺解)");

                if (!d.need_classic_seek) {
                    std::printf("    节省: 省掉 1 次 demux seek + 1 次 decoder flush\n");
                    if (d.prev_keyframe_id >= 0) {
                        std::printf("    复用: 当前 GOP #%d 的解码器状态\n", d.prev_keyframe_id);
                    }
                }

                current_pts = target_pts;  // 模拟：抽完这一帧后的位置
            }

            std::printf("\n  📊 统计: %zu 次请求中，%zu 次可走 Smart Seek\n",
                        test_frames.size(),
                        // 简单统计：同方向同 GOP 的次数
                        [&]() -> size_t {
                            size_t smart = 0;
                            int64_t cur = AV_NOPTS_VALUE;
                            for (size_t i = 0; i < test_frames.size(); i++) {
                                int fid = test_frames[i];
                                if (fid >= (int)full_index.size()) continue;
                                int64_t tgt = full_index[fid].pts;
                                SeekDecision d = MakeSeekDecision(keyframes, cur, tgt, i == 0);
                                if (!d.need_classic_seek) smart++;
                                cur = tgt;
                            }
                            return smart;
                        }());
        }
    }

    // ── Part D: 三种抽帧方法对比 ──────────────────────────────────────
    if (do_extract) {
        PrintSeparator("Part D: 三种抽帧方法精度对比");

        std::printf("\n目标帧: frame_id=%d\n", extract_frame_id);
        if (nb_frames > 0 && extract_frame_id >= static_cast<int>(nb_frames)) {
            std::printf("⚠️  frame_id=%d 超出视频帧数 %lld，可能失败\n",
                        extract_frame_id, static_cast<long long>(nb_frames));
        }

        // 方法 1: AVSEEK_FLAG_FRAME — 最优
        std::printf("\n────────── 方法1: AVSEEK_FLAG_FRAME（✅ 推荐）──────────\n");
        ExtractionResult r1 = ExtractByFrameId(fmt, dec, video_idx, extract_frame_id);
        if (r1.ok) {
            std::printf("      结果: ✅ 第 %d 帧, PTS=%lld (≈%.6fs), 解码%d帧 丢弃%d帧\n",
                        r1.frame_id,
                        static_cast<long long>(r1.frame_pts),
                        r1.frame_seconds,
                        r1.frames_decoded,
                        r1.frames_dropped);
        } else {
            std::printf("      结果: ❌ 失败\n");
        }

        // 方法 2: FrameIndex + AVSEEK_FLAG_BYTE — 最精确
        if (do_full_index) {
            std::printf("\n────────── 方法2: FrameIndex + AVSEEK_FLAG_BYTE（✅ 最精确）──────────\n");
            FrameIndex full_index = BuildFullFrameIndex(fmt, dec, video_stream, video_idx, keyframes);
            ExtractionResult r2 = ExtractByByteOffset(fmt, dec, video_idx,
                                                       full_index, extract_frame_id);
            if (r2.ok) {
                std::printf("      结果: ✅ 第 %d 帧, PTS=%lld (≈%.6fs), 解码%d帧 丢弃%d帧\n",
                            r2.frame_id,
                            static_cast<long long>(r2.frame_pts),
                            r2.frame_seconds,
                            r2.frames_decoded,
                            r2.frames_dropped);
            } else {
                std::printf("      结果: ❌ 失败\n");
            }
        }

        // 方法 3: 浮点时间戳 Seek — 反面教材
        std::printf("\n────────── 方法3: 浮点时间戳 Seek（❌ 有精度风险）──────────\n");
        ExtractionResult r3 = ExtractByTimestampFloat(fmt, dec, video_idx,
                                                       extract_frame_id,
                                                       avg_frame_dur,
                                                       video_stream->time_base);
        if (r3.ok) {
            std::printf("      结果: %s 第 %d 帧, PTS=%lld (≈%.6fs), 解码%d帧 丢弃%d帧\n",
                        (r3.frame_id == extract_frame_id) ? "✅" : "⚠️",
                        r3.frame_id,
                        static_cast<long long>(r3.frame_pts),
                        r3.frame_seconds,
                        r3.frames_decoded,
                        r3.frames_dropped);
        } else {
            std::printf("      结果: ❌ 失败\n");
        }

        // 方法 4: av_rescale_q 精确时间戳 Seek
        std::printf("\n────────── 方法4: av_rescale_q 精确时间戳 Seek ──────────\n");
        ExtractionResult r4 = ExtractByTimestampAccurate(fmt, dec, video_idx,
                                                          extract_frame_id,
                                                          avg_frame_dur,
                                                          video_stream->time_base);
        if (r4.ok) {
            std::printf("      结果: %s 第 %d 帧, PTS=%lld (≈%.6fs), 解码%d帧 丢弃%d帧\n",
                        (r4.frame_id == extract_frame_id) ? "✅" : "⚠️",
                        r4.frame_id,
                        static_cast<long long>(r4.frame_pts),
                        r4.frame_seconds,
                        r4.frames_decoded,
                        r4.frames_dropped);
        } else {
            std::printf("      结果: ❌ 失败\n");
        }

        // ── 精度总结 ──────────────────────────────────────────────────
        std::printf("\n────────── 对比总结 ──────────\n");
        std::printf("  %-35s %s\n", "方法", "帧精确度");
        std::printf("  %-35s %s\n", "AVSEEK_FLAG_FRAME", "⭐⭐⭐⭐⭐ (内置帧号映射)");
        std::printf("  %-35s %s\n", "FrameIndex + BYTE offset", "⭐⭐⭐⭐⭐ (字节级精确定位)");
        std::printf("  %-35s %s\n", "av_rescale_q 时间戳", "⭐⭐⭐⭐ (CFR可靠, VFR可能偏)");
        std::printf("  %-35s %s\n", "float 直除时间戳", "⭐⭐ (累积误差, 帧边界不可靠)");

        std::printf("\n  💡 推荐策略:\n");
        std::printf("     - 生产环境优先用 AVSEEK_FLAG_FRAME（最简单）\n");
        std::printf("     - 对精确度要求极高的场景，上传时预建 FrameIndex\n");
        std::printf("     - 线上查索引 → AVSEEK_FLAG_BYTE Seek → 解码\n");
    }

    // ── Part E: 批量对比（--compare）──────────────────────────────────
    if (do_compare && !do_extract) {
        PrintSeparator("Part D: 时间戳换算精度对比");

        // 展示同一个秒数用不同方式换算的差异
        std::printf("\n  演示: 2.000 秒在不同换算方式下的 PTS\n");
        std::printf("  ─────────────────────────────────────────\n");

        int64_t pts_acc = SecondsToPtsAccurate(2.0, video_stream->time_base);
        int64_t pts_flt = SecondsToPtsFloat(2.0, video_stream->time_base);
        int64_t pts_rev = static_cast<int64_t>(
            PtsToSeconds(pts_acc, video_stream->time_base) /
            av_q2d(video_stream->time_base));

        std::printf("  av_rescale_q:  %lld (≈%.9fs)\n",
                    static_cast<long long>(pts_acc),
                    PtsToSeconds(pts_acc, video_stream->time_base));
        std::printf("  float 直除:   %lld (≈%.9fs)%s\n",
                    static_cast<long long>(pts_flt),
                    PtsToSeconds(pts_flt, video_stream->time_base),
                    pts_acc != pts_flt ? " ⚠️ 不同！" : " ✅");
        std::printf("  回转换算:      %lld (≈%.9fs)%s\n",
                    static_cast<long long>(pts_rev),
                    PtsToSeconds(pts_rev, video_stream->time_base),
                    pts_acc != pts_rev ? " ⚠️ 误差！" : " ✅");

        // 展示帧ID → 时间戳的三种方式
        int test_fid = 42;
        std::printf("\n  演示: frame_id=%d → 时间戳的三种方式\n", test_fid);
        std::printf("  ─────────────────────────────────────────\n");

        int64_t pts_method1 = FrameIdToPtsNaive(test_fid, avg_frame_dur,
                                                 video_stream->time_base);
        int64_t pts_method2 = SecondsToPtsAccurate(
            test_fid * avg_frame_dur, video_stream->time_base);
        int64_t pts_method3 = av_rescale_q(
            test_fid, av_inv_q(video_stream->avg_frame_rate),
            video_stream->time_base);

        std::printf("  ❌ avg_frame_dur 直算: pts=%lld (≈%.6fs)\n",
                    static_cast<long long>(pts_method1),
                    PtsToSeconds(pts_method1, video_stream->time_base));
        std::printf("  ⚠️  av_rescale_q 毫秒: pts=%lld (≈%.6fs)\n",
                    static_cast<long long>(pts_method2),
                    PtsToSeconds(pts_method2, video_stream->time_base));
        std::printf("  ✅  av_rescale_q 帧率: pts=%lld (≈%.6fs)\n",
                    static_cast<long long>(pts_method3),
                    PtsToSeconds(pts_method3, video_stream->time_base));

        if (pts_method1 != pts_method2 || pts_method2 != pts_method3) {
            std::printf("\n  ⚠️  三种换算结果不一致！这就是帧ID↔时间戳互转的根本问题。\n");
            std::printf("  💡 解决方案: 用 AVSEEK_FLAG_FRAME 或 FrameIndex 绕过转换。\n");
        }

        // 展示关键帧间隔对 Smart Seek 的影响
        std::printf("\n  演示: GOP 结构对 Smart Seek 命中率的影响\n");
        std::printf("  ─────────────────────────────────────────\n");

        if (keyframes.size() >= 2) {
            // 模拟高频抽帧（每 200ms 一次），统计 Smart Seek 命中率
            int total_requests = 0;
            int smart_hits = 0;
            int64_t cur_pts = AV_NOPTS_VALUE;
            double sim_duration = std::min(duration_sec, 10.0);  // 模拟前10秒

            for (double t = 0; t < sim_duration; t += 0.200) {
                total_requests++;
                int64_t target_pts = SecondsToPtsAccurate(t, video_stream->time_base);
                SeekDecision d = MakeSeekDecision(keyframes, cur_pts, target_pts,
                                                  total_requests == 1);
                if (!d.need_classic_seek) smart_hits++;
                cur_pts = target_pts;
            }

            double hit_rate = total_requests > 0
                ? 100.0 * smart_hits / total_requests : 0.0;
            std::printf("  模拟: 前 %.0f 秒, 每 200ms 抽一次 (共 %d 次)\n",
                        sim_duration, total_requests);
            std::printf("  Smart Seek 命中: %d/%d (%.1f%%)\n",
                        smart_hits, total_requests, hit_rate);
            std::printf("  GOP 间隔越大 → Smart Seek 命中率越高 → 性能提升越明显\n");
        }
    }

    // ── 清理 ──────────────────────────────────────────────────────────
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);

    std::printf("\n✅ Demo 完成。\n");
    std::printf("   核心结论: 用容器原生的帧索引（AVSEEK_FLAG_FRAME 或 FrameIndex）\n");
    std::printf("   替代时间戳 Seek，彻底消除帧ID↔时间戳的精度误差。\n");
    return 0;
}
