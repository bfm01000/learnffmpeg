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
        // index_entries 是 avformat_find_stream_info() 时自动生成的
        // 包含每个关键帧的 (pos, pts, flags, size)
        void build_frame_index(AVStream *vs) {
            // 推算 frameId: CFR 下每帧时长 = time_base.den / (fps_num * time_base.num)
            // 或用 FFmpeg 内置 avg_frame_rate
            AVRational frame_dur = av_inv_q(vs->avg_frame_rate);
            // 转为 time_base 单位
            int64_t frame_dur_tb = av_rescale_q(1, frame_dur, vs->time_base);
    
            for (int i = 0; i < vs->nb_index_entries; i++) {
                const AVIndexEntry &e = vs->index_entries[i];
                // pts / 每帧时长 ≈ frameId
                int frame_id = (int)(e.timestamp / frame_dur_tb);
                frame_to_pts_[frame_id] = e.timestamp;
            }
            av_log(nullptr, AV_LOG_INFO,
                   "index: %d keyframes → %zu frameId entries\n",
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