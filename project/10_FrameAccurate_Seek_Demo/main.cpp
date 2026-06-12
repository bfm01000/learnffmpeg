// Frame-Accurate Seek Demo
// 演示：用户输入 2.0s → av_rescale_q 转 pts → BACKWARD Seek → flush → 顺解到目标帧
//
// 用法:
//   ./frame_accurate_seek_demo <input.mp4> <seek_seconds> [second_seek_seconds]
//
// 示例:
//   ./frame_accurate_seek_demo video.mp4 2.0
//   ./frame_accurate_seek_demo video.mp4 2.0 2.5   # 第二次尝试 Smart Seek（同 GOP 内）

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────
// 时间换算：UI 秒 ↔ 内部 pts
// ─────────────────────────────────────────────────────────────

static double PtsToSeconds(int64_t pts, AVRational timeBase) {
    if (pts == AV_NOPTS_VALUE) {
        return NAN;
    }
    return pts * av_q2d(timeBase);
}

// ✅ 大厂推荐：整数 rescale，避免 float 边界误差
static int64_t SecondsToPtsAccurate(double seconds, AVRational timeBase) {
    const int64_t ms = static_cast<int64_t>(llround(seconds * 1000.0));
    return av_rescale_q(ms, AVRational{1, 1000}, timeBase);
}

// ❌ 小厂常见写法：直接 float 除法（demo 里对比用，不要在生产 Seek 边界用）
static int64_t SecondsToPtsFloat(double seconds, AVRational timeBase) {
    return static_cast<int64_t>(seconds / av_q2d(timeBase));
}

static void PrintRational(const char *label, AVRational r) {
    std::printf("    %s = %d/%d (≈ %.9f 秒/刻度)\n", label, r.num, r.den, av_q2d(r));
}

// ─────────────────────────────────────────────────────────────
// index_entries：关键帧索引（MP4 stss → FFmpeg 内存表）
// FFmpeg 6+ 使用 avformat_index_get_* API（index_entries 不再公开）
// ─────────────────────────────────────────────────────────────

static int IndexEntryCount(const AVStream *stream) {
    return avformat_index_get_entries_count(stream);
}

static int64_t FindPrevKeyframePts(AVStream *stream, int64_t targetPts) {
    const AVIndexEntry *entry = avformat_index_get_entry_from_timestamp(
        stream, targetPts, AVSEEK_FLAG_BACKWARD);
    if (!entry || !(entry->flags & AVINDEX_KEYFRAME)) {
        return AV_NOPTS_VALUE;
    }
    return entry->timestamp;
}

static bool HasKeyframeBetween(AVStream *stream, int64_t loPts, int64_t hiPts) {
    if (loPts == AV_NOPTS_VALUE) {
        return false;
    }
    const int count = IndexEntryCount(stream);
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const AVIndexEntry *entry = avformat_index_get_entry(stream, i);
        if (!entry) {
            continue;
        }
        if (entry->timestamp <= loPts) {
            continue;
        }
        if (entry->timestamp >= hiPts) {
            break;
        }
        if (entry->flags & AVINDEX_KEYFRAME) {
            return true;
        }
    }
    return false;
}

static void PrintKeyframeSummary(AVStream *stream, int maxPrint) {
    const int count = IndexEntryCount(stream);
    if (count <= 0) {
        std::printf("    (无 index，可能尚未 build index)\n");
        return;
    }

    int printed = 0;
    for (int i = 0; i < count && printed < maxPrint; ++i) {
        const AVIndexEntry *entry = avformat_index_get_entry(stream, i);
        if (!entry || !(entry->flags & AVINDEX_KEYFRAME)) {
            continue;
        }
        std::printf("    keyframe[%d] pts=%lld (≈ %.3fs)\n",
                    printed,
                    static_cast<long long>(entry->timestamp),
                    PtsToSeconds(entry->timestamp, stream->time_base));
        ++printed;
    }
}

// ─────────────────────────────────────────────────────────────
// 解码器打开
// ─────────────────────────────────────────────────────────────

static AVCodecContext *OpenVideoDecoder(AVStream *stream) {
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return nullptr;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(decoder);
    if (!ctx) {
        return nullptr;
    }

    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
        avcodec_open2(ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

// ─────────────────────────────────────────────────────────────
// Classic Seek：avformat_seek_file + flush
// ─────────────────────────────────────────────────────────────

static int ClassicSeek(AVFormatContext *fmt,
                       AVCodecContext *dec,
                       AVStream *stream,
                       int64_t targetPts) {
    std::printf("\n  [Classic Seek] avformat_seek_file(BACKWARD) → pts=%lld\n",
                static_cast<long long>(targetPts));

    const int ret = avformat_seek_file(
        fmt,
        stream->index,
        INT64_MIN,
        targetPts,
        targetPts,
        AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::fprintf(stderr, "  avformat_seek_file 失败: %d\n", ret);
        return ret;
    }

    avcodec_flush_buffers(dec);
    std::printf("  avcodec_flush_buffers() 完成\n");
    return 0;
}

// ─────────────────────────────────────────────────────────────
// 顺解码直到 frame->pts >= targetPts
// ─────────────────────────────────────────────────────────────

struct SeekResult {
    bool ok = false;
    int64_t framePts = AV_NOPTS_VALUE;
    int decodedFrames = 0;
    int droppedFrames = 0;
};

static SeekResult DecodeUntilPts(AVFormatContext *fmt,
                                 AVCodecContext *dec,
                                 int videoStreamIndex,
                                 int64_t targetPts,
                                 AVFrame *outFrame) {
    SeekResult result;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return result;
    }

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(dec, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (true) {
            const int ret = avcodec_receive_frame(dec, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                av_packet_free(&packet);
                av_frame_free(&frame);
                return result;
            }

            ++result.decodedFrames;
            const int64_t pts = frame->pts;

            if (pts == AV_NOPTS_VALUE) {
                av_frame_unref(frame);
                continue;
            }

            if (pts >= targetPts) {
                av_frame_ref(outFrame, frame);
                result.ok = true;
                result.framePts = pts;
                av_frame_unref(frame);
                av_packet_free(&packet);
                av_frame_free(&frame);
                return result;
            }

            ++result.droppedFrames;
            av_frame_unref(frame);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    return result;
}

// Smart Seek：同 GOP 内不 demux seek、不 flush，直接顺解（复用 DecodeUntilPts）
static bool CanSmartForwardSeek(int64_t currentPts, int64_t targetPts, AVStream *stream) {
    if (currentPts == AV_NOPTS_VALUE || targetPts < currentPts) {
        return false;
    }
    return !HasKeyframeBetween(stream, currentPts, targetPts);
}

// ─────────────────────────────────────────────────────────────
// 可选：保存命中帧为 PPM，方便肉眼确认
// ─────────────────────────────────────────────────────────────

static int SaveFrameAsPpm(const AVFrame *frame, const char *path) {
    SwsContext *sws = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        return -1;
    }

    AVFrame *rgb = av_frame_alloc();
    const int rgbBufSize = av_image_get_buffer_size(
        AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
    uint8_t *rgbBuf = static_cast<uint8_t *>(av_malloc(rgbBufSize));
    if (!rgb || !rgbBuf) {
        sws_freeContext(sws);
        av_frame_free(&rgb);
        av_free(rgbBuf);
        return -1;
    }

    av_image_fill_arrays(
        rgb->data, rgb->linesize, rgbBuf, AV_PIX_FMT_RGB24,
        frame->width, frame->height, 1);

    sws_scale(
        sws, frame->data, frame->linesize, 0, frame->height,
        rgb->data, rgb->linesize);

    FILE *fp = std::fopen(path, "wb");
    if (!fp) {
        sws_freeContext(sws);
        av_frame_free(&rgb);
        av_free(rgbBuf);
        return -1;
    }

    std::fprintf(fp, "P6\n%d %d\n255\n", frame->width, frame->height);
    for (int y = 0; y < frame->height; ++y) {
        std::fwrite(rgb->data[0] + y * rgb->linesize[0], 1, frame->width * 3, fp);
    }
    std::fclose(fp);

    sws_freeContext(sws);
    av_frame_free(&rgb);
    av_free(rgbBuf);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// 一次完整 Seek 流程（用户秒 → pts → seek → decode）
// ─────────────────────────────────────────────────────────────

static int RunSeekFlow(AVFormatContext *fmt,
                       AVCodecContext *dec,
                       AVStream *stream,
                       int videoStreamIndex,
                       double userSeconds,
                       bool useSmartSeek,
                       int64_t *outCurrentPts) {
    std::printf("\n════════════════════════════════════════════════════════\n");
    std::printf("  用户输入: %.3f 秒\n", userSeconds);
    std::printf("════════════════════════════════════════════════════════\n");

    // Step 1: 秒 → pts
    const int64_t ptsAccurate = SecondsToPtsAccurate(userSeconds, stream->time_base);
    const int64_t ptsFloat = SecondsToPtsFloat(userSeconds, stream->time_base);

    std::printf("\n[Step 1] 秒 → pts 换算\n");
    PrintRational("stream->time_base", stream->time_base);
    std::printf("    ✅ av_rescale_q  : pts = %lld (≈ %.6f s)\n",
                static_cast<long long>(ptsAccurate),
                PtsToSeconds(ptsAccurate, stream->time_base));
    std::printf("    ❌ float 直接换算: pts = %lld (≈ %.6f s)\n",
                static_cast<long long>(ptsFloat),
                PtsToSeconds(ptsFloat, stream->time_base));
    if (ptsAccurate != ptsFloat) {
        std::printf("    ⚠️  两种换算结果不同！Seek 边界应使用 av_rescale_q\n");
    }

    const int64_t targetPts = ptsAccurate;

    // Step 2: 查关键帧索引
    std::printf("\n[Step 2] 查 index_entries（MP4 stss 的内存化结果）\n");
    PrintKeyframeSummary(stream, 5);
    const int64_t keyframePts = FindPrevKeyframePts(stream, targetPts);
    if (keyframePts != AV_NOPTS_VALUE) {
        std::printf("    目标 pts 之前最近关键帧: %lld (≈ %.3fs)\n",
                    static_cast<long long>(keyframePts),
                    PtsToSeconds(keyframePts, stream->time_base));
    }

    // Step 3: Seek 决策
    std::printf("\n[Step 3] Seek 策略决策\n");
    const bool smart = useSmartSeek && CanSmartForwardSeek(*outCurrentPts, targetPts, stream);
    if (smart) {
        std::printf("    同 GOP 内、目标在当前位置之后 → Smart Seek\n");
    } else {
        if (*outCurrentPts != AV_NOPTS_VALUE && targetPts < *outCurrentPts) {
            std::printf("    目标在当前位置之前 → 必须 Classic Seek\n");
        } else if (HasKeyframeBetween(stream, *outCurrentPts, targetPts)) {
            std::printf("    区间内有新关键帧 → Classic Seek 更优\n");
        } else {
            std::printf("    首次 Seek 或需重置解码器 → Classic Seek\n");
        }
    }

    // Step 4: 执行 Seek
    std::printf("\n[Step 4] 执行 Seek\n");
    if (!smart) {
        if (ClassicSeek(fmt, dec, stream, targetPts) < 0) {
            return -1;
        }
    }

    // Step 5: 顺解到目标帧
    std::printf("\n[Step 5] 顺解码直到 frame->pts >= target_pts\n");
    if (smart) {
        std::printf("  [Smart Seek] 跳过 demux seek / flush，直接顺解\n");
    }
    AVFrame *hitFrame = av_frame_alloc();
    SeekResult result = DecodeUntilPts(fmt, dec, videoStreamIndex, targetPts, hitFrame);

    if (!result.ok) {
        std::fprintf(stderr, "  未能解码到目标帧\n");
        av_frame_free(&hitFrame);
        return -1;
    }

    std::printf("    解码帧数=%d, 丢弃中间帧=%d\n",
                result.decodedFrames, result.droppedFrames);
    std::printf("    命中帧 pts=%lld (≈ %.6f s)\n",
                static_cast<long long>(result.framePts),
                PtsToSeconds(result.framePts, stream->time_base));
    std::printf("    尺寸=%dx%d  format=%s\n",
                hitFrame->width, hitFrame->height,
                av_get_pix_fmt_name(static_cast<AVPixelFormat>(hitFrame->format)));

    *outCurrentPts = result.framePts;

    char ppmPath[256];
    std::snprintf(ppmPath, sizeof(ppmPath), "seek_%.3fs.ppm", userSeconds);
    if (SaveFrameAsPpm(hitFrame, ppmPath) == 0) {
        std::printf("    已保存预览图: %s\n", ppmPath);
    }

    av_frame_free(&hitFrame);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "用法: %s <input.mp4> <seek_seconds> [second_seek_seconds]\n\n"
                     "示例:\n"
                     "  %s video.mp4 2.0\n"
                     "  %s video.mp4 2.0 2.5    # 演示第二次 Smart Seek\n",
                     argv[0], argv[0], argv[0]);
        return 1;
    }

    const char *inputPath = argv[1];
    const double firstSeekSec = std::atof(argv[2]);
    const bool hasSecondSeek = argc >= 4;
    const double secondSeekSec = hasSecondSeek ? std::atof(argv[3]) : 0.0;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, inputPath, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "打不开文件: %s\n", inputPath);
        return 1;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        std::fprintf(stderr, "读不到流信息\n");
        avformat_close_input(&fmt);
        return 1;
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        std::fprintf(stderr, "找不到视频流\n");
        avformat_close_input(&fmt);
        return 1;
    }

    AVStream *videoStream = fmt->streams[videoIndex];
    AVCodecContext *dec = OpenVideoDecoder(videoStream);
    if (!dec) {
        std::fprintf(stderr, "打不开视频解码器\n");
        avformat_close_input(&fmt);
        return 1;
    }

    std::printf("✅ 打开: %s\n", inputPath);
    std::printf("   视频流 index=%d  codec=%s  %dx%d\n",
                videoIndex,
                avcodec_get_name(videoStream->codecpar->codec_id),
                videoStream->codecpar->width,
                videoStream->codecpar->height);
    PrintRational("   time_base", videoStream->time_base);
    std::printf("   index 条目数量: %d\n", IndexEntryCount(videoStream));

    int64_t currentPts = AV_NOPTS_VALUE;

    if (RunSeekFlow(fmt, dec, videoStream, videoIndex, firstSeekSec, false, &currentPts) < 0) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return 1;
    }

    if (hasSecondSeek) {
        if (RunSeekFlow(fmt, dec, videoStream, videoIndex, secondSeekSec, true, &currentPts) < 0) {
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            return 1;
        }
    }

    std::printf("\n✅ Demo 完成。流程回顾:\n");
    std::printf("   UI 秒 → av_rescale_q → pts → index 查关键帧 → Seek+flush → 顺解到目标帧\n");

    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 0;
}
