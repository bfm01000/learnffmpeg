#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/packet.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include <cstddef>
#include <iostream>
#include <string>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libswscale/swscale.h>
}

struct InputOptions {
    std::string input_path;
    std::string output_path;

    int in_video_index;
    int in_audio_index;

    int out_video_index;
    int out_audio_index;

    // 如果为-1，则保留原始宽高
    int target_width = -1;
    int target_height = -1;
    int target_fps = 30;

    AVPixelFormat target_pix_fmt = AV_PIX_FMT_YUV420P;

    std::string out_format;
};

static std::string ff_err2str(int errnum) {
    // FFmpeg 很多 API 失败时返回的是一个负数错误码，直接打印这个整数可读性很差。
    // 这里先准备一个字符缓冲区，用来接收“错误码 -> 可读错误字符串”的转换结果。
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};

    // av_make_error_string 的作用：
    // 1. 读取 FFmpeg 返回的错误码 errnum
    // 2. 把它翻译成更容易读懂的错误描述
    // 3. 把结果写到 buf 中
    //
    // 参数含义：
    // - buf         : 目标缓冲区，保存转换后的错误字符串
    // - sizeof(buf) : 缓冲区大小，防止写越界
    // - errnum      : FFmpeg 的错误码（通常是某个 API 返回的 ret）
    //
    // 它和 strerror() 的思路类似，但更适合 FFmpeg 自己定义的错误码。
    av_make_error_string(buf, sizeof(buf), errnum);

    // 最后把 C 风格字符串封装成 std::string，方便在 C++ 里直接返回和打印。
    return std::string(buf);
}

bool parse_args(int argc, char **argv, InputOptions &options);


InputOptions input_options;




int main(int argc, char **argv) {
    // 1. 解析参数
    input_options.input_path = "../../../source/cat.mp4";
    input_options.output_path = "../../../source/cat_output.mp4";

    int64_t next_video_pts = 0;
    int64_t next_audio_pts = 0;

    // 2. 打开AVFormatContext
    AVFormatContext * in_fmt_ctx = nullptr;
    AVFormatContext * out_fmt_ctx = nullptr;

    SwsContext *sws_ctx = nullptr;           // 视频像素格式/尺寸转换上下文（例如转为 yuv420p）
    SwsContext *swr_ctx = nullptr;           // 音频重采样上下文（例如从 48kHz 转 44.1kHz）


    int ret = 0;

    // 2.1 打开输入文件
    ret = avformat_open_input(&in_fmt_ctx, input_options.input_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "avformat_open_input failed: " << ff_err2str(ret) << std::endl;
        return -1;
    }

    ret = avformat_find_stream_info(in_fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "avformat_find_stream_info failed: " << ff_err2str(ret) << std::endl;
        return -1;
    }

    // 3. 选择一个视频流和音频流
    std::cout << "in_fmt_ctx->nb_streams: " << in_fmt_ctx->nb_streams << std::endl;
    for (int i = 0; i < in_fmt_ctx->nb_streams; i++){
        std::cout << "in_fmt_ctx->streams[i]->codecpar->codec_type: " << in_fmt_ctx->streams[i]->codecpar->codec_type << std::endl;
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            input_options.in_video_index = i;
            continue;
        }
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            input_options.in_audio_index = i;
            continue;
        }
    }
    std::cout << "input_options.in_video_index: " << input_options.in_video_index << std::endl;
    std::cout << "input_options.in_audio_index: " << input_options.in_audio_index << std::endl;

    if (input_options.in_video_index == -1 || input_options.in_audio_index == -1) {
        std::cerr << "no video or audio stream found" << std::endl;
        return -1;
    }

    // 3. 创建输出文件
    ret = avformat_alloc_output_context2(
        &out_fmt_ctx,
        nullptr,
        nullptr,
        input_options.output_path.c_str()
    );
    if (ret < 0) {
        std::cerr << "failed to alloc output context" << std::endl;
        return -1;
    }

    // 4. 初始化解码器
    AVCodecContext *in_video_dec_ctx = nullptr;
    AVCodecContext *in_audio_dec_ctx = nullptr;

    const AVCodec* video_decoder = nullptr;
    const AVCodec* audio_decoder = nullptr;

    // 4.0 查找解码器
    video_decoder = avcodec_find_decoder(in_fmt_ctx->streams[input_options.in_video_index]->codecpar->codec_id);
    audio_decoder = avcodec_find_decoder(in_fmt_ctx->streams[input_options.in_audio_index]->codecpar->codec_id);
    if (!video_decoder || !audio_decoder) {
        std::cerr << "no video or audio decoder found" << std::endl;
        return -1;
    }

    // 4. 分配解码器上下文
    in_video_dec_ctx = avcodec_alloc_context3(video_decoder);
    in_audio_dec_ctx = avcodec_alloc_context3(audio_decoder);
    if (!in_video_dec_ctx || !in_audio_dec_ctx) {
        std::cerr << "failed to allocate codec context" << std::endl;
        return -1;
    }

    // 4.1 拷贝参数到解码器上下文
    ret = avcodec_parameters_to_context(in_video_dec_ctx, in_fmt_ctx->streams[input_options.in_video_index]->codecpar);
    if (ret < 0) {
        std::cerr << "failed to copy codec parameters to context" << std::endl;
        return -1;
    }
    ret = avcodec_parameters_to_context(in_audio_dec_ctx, in_fmt_ctx->streams[input_options.in_audio_index]->codecpar);
    if (ret < 0) {
        std::cerr << "failed to copy codec parameters to context" << std::endl;
        return -1;
    }

    // 5. 打开解码器
    ret = avcodec_open2(in_video_dec_ctx, video_decoder, nullptr);
    if (ret < 0) {
        std::cerr << "failed to open video decoder" << std::endl;
        return -1;
    }
    ret = avcodec_open2(in_audio_dec_ctx, audio_decoder, nullptr);
    if (ret < 0) {
        std::cerr << "failed to open audio decoder" << std::endl;
        return -1;
    }

    // 5. 初始化编码器

    const AVCodec* video_encoder = nullptr;
    const AVCodec* audio_encoder = nullptr;
    
    AVCodecContext *out_video_enc_ctx = nullptr;
    AVCodecContext *out_audio_enc_ctx = nullptr;
    // const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");

    // 5.1 视频编码器初始化
    // 这句代码的含义是：按“编码器名字”去 FFmpeg 已注册的编码器列表里查找一个视频编码器。
    // 这里传入的 "libx264" 表示希望使用 x264 提供的 H.264 软件编码器。
    // 如果当前 FFmpeg 在编译时启用了 x264，这里就会返回对应的 AVCodec*；
    // 如果没有启用，或者本机 FFmpeg 不包含这个编码器，就会返回 nullptr。
    //
    // 为什么这里用 by_name：
    // - 你想明确指定“就用 x264”
    // - 而不是只说“给我一个 H.264 编码器”
    // 这样可以避免 FFmpeg 在存在多个 H.264 编码器时帮你做隐式选择。
    //
    // 常见视频编码器名字：
    // - "libx264"     : H.264 软件编码器，最常见、兼容性好
    // - "libx265"     : H.265/HEVC 软件编码器，压缩率更高但更慢
    // - "h264_nvenc"  : NVIDIA H.264 硬件编码器
    // - "hevc_nvenc"  : NVIDIA H.265 硬件编码器
    // - "h264_qsv"    : Intel Quick Sync H.264 编码器
    // - "hevc_qsv"    : Intel Quick Sync H.265 编码器
    // - "h264_vaapi"  : VAAPI H.264 编码器（Linux 常见）
    // - "hevc_vaapi"  : VAAPI H.265 编码器
    // - "libvpx"      : VP8 编码器
    // - "libvpx-vp9"  : VP9 编码器
    // - "libaom-av1"  : AV1 软件编码器
    // - "svtav1"      : AV1 编码器，工程里也很常见
    //
    // 常见音频编码器名字：
    // - "aac"         : AAC 编码器
    // - "libmp3lame"  : MP3 编码器
    // - "libopus"     : Opus 编码器
    // - "flac"        : FLAC 无损编码器
    //
    // 如果你不想按名字找，也可以按 codec id 查：
    // avcodec_find_encoder(AV_CODEC_ID_H264)
    // 这种写法更抽象，但不如 by_name 明确。
    video_encoder = avcodec_find_encoder_by_name("libx264");
    if (!video_encoder) {
        std::cerr << "no video encoder found" << std::endl;
        return -1;
    }

    // 这句代码的含义是：
    // 1. 为找到的 video_encoder 分配一个编码器上下文
    // 2. 把编码器上下文写到 out_video_enc_ctx 中
    //
    // 为什么必须有：
    // - 编码器需要知道如何处理视频数据
    // - 上下文里保存了编码器需要的各种参数
    out_video_enc_ctx = avcodec_alloc_context3(video_encoder);
    if (!out_video_enc_ctx) {
        std::cerr << "failed to allocate video encoder context" << std::endl;
        return -1;
    }

    // 5.2获取输入视频的帧率

    if (input_options.target_width <= 0 || input_options.target_height <= 0) {
        out_video_enc_ctx->width = in_fmt_ctx->streams[input_options.in_video_index]->codecpar->width;
        out_video_enc_ctx->height = in_fmt_ctx->streams[input_options.in_video_index]->codecpar->height;
        out_video_enc_ctx->time_base = av_inv_q(in_fmt_ctx->streams[input_options.in_video_index]->r_frame_rate);
    } else {
        AVRational fr = av_guess_frame_rate(in_fmt_ctx, in_fmt_ctx->streams[input_options.in_video_index], nullptr);
        if (fr.num <= 0 || fr.den <= 0) {
            fr = AVRational{input_options.target_fps, 1};
        }
        out_video_enc_ctx->width = input_options.target_width;
        out_video_enc_ctx->height = input_options.target_height;
        out_video_enc_ctx->time_base = av_inv_q(fr);
    }

    out_video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    // 保留原始的宽高比
    out_video_enc_ctx->sample_aspect_ratio = in_video_dec_ctx->sample_aspect_ratio;
    out_video_enc_ctx->framerate = AVRational{input_options.target_fps, 1};
    out_video_enc_ctx->gop_size = 50;
    out_video_enc_ctx->max_b_frames = 2;

    ret = avcodec_open2(out_video_enc_ctx, video_encoder, nullptr);
    if (ret < 0) {
        std::cerr << "failed to open video encoder" << std::endl;
        return -1;
    }
    
    // 5.3 初始化音频编码器
    
    audio_encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audio_encoder) {
        std::cerr << "no audio encoder found" << std::endl;
        return -1;
    }

    out_audio_enc_ctx = avcodec_alloc_context3(audio_encoder);
    if (!out_audio_enc_ctx) {
        std::cerr << "failed to allocate audio encoder context" << std::endl;
        return -1;
    }

    // sample_rate 表示音频采样率，也就是“每秒采多少个采样点”。
    // 这里优先沿用输入音频解码器的采样率；如果输入值异常，再退回一个兜底值。
    out_audio_enc_ctx->sample_rate = in_audio_dec_ctx->sample_rate > 0 ? in_audio_dec_ctx->sample_rate : 4800;

    // sample_fmt 表示音频采样格式。
    // AV_SAMPLE_FMT_FLTP 可以拆成两部分理解：
    // - FLT : 每个采样点使用 32 位 float
    // - P   : planar，平面存储
    // 例如双声道时，左声道数据放一块，右声道数据放一块，而不是 LRLR 交错存储。
    // 这里的含义是：告诉输出音频编码器，后面送给它的 PCM 数据将按 FLTP 格式组织。
    out_audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    // ch_layout 表示声道布局，不只是“有几个声道”，还包括“每个声道分别是什么位置”。
    // 例如 mono、stereo、5.1、7.1 都属于不同的声道布局。
    // 这里把输入音频解码器的声道布局拷贝给输出音频编码器，
    // 表示输出端沿用输入端的声道组织方式。
    av_channel_layout_copy(&out_audio_enc_ctx->ch_layout, &in_audio_dec_ctx->ch_layout);
    out_audio_enc_ctx->time_base = AVRational{1, in_audio_dec_ctx->sample_rate};


    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    
    while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == input_options.in_video_index) {
            ret = avcodec_send_packet(in_video_dec_ctx, pkt);
            av_packet_unref(pkt);
            if (ret < 0) {
                std::cerr << "failed to send packet to video decoder" << std::endl;
                return -1;
            }

            while ((ret = avcodec_receive_frame(in_video_dec_ctx, frame)) >= 0) {
                AVFrame *sw_frame = av_frame_alloc();
                if (!sw_frame) {
                    std::cerr << "failed to allocate sw frame" << std::endl;
                    return -1;
                }
                sw_frame->format = AV_PIX_FMT_YUV420P;
                sw_frame->width = out_video_enc_ctx->width;
                sw_frame->height = out_video_enc_ctx->height;

                ret = av_frame_get_buffer(sw_frame, 32);
                if (ret < 0) {
                    std::cerr << "failed to get buffer for sw frame" << std::endl;
                    av_frame_free(&sw_frame);
                    return AVERROR(EINVAL);
                }

                av_frame_make_writable(sw_frame);

                sws_ctx = sws_getCachedContext(
                    sws_ctx, 
                    in_video_dec_ctx->width, 
                    in_video_dec_ctx->height, 
                    in_video_dec_ctx->pix_fmt, 
                    out_video_enc_ctx->width, 
                    out_video_enc_ctx->height, 
                    out_video_enc_ctx->pix_fmt,
                    SWS_BILINEAR, 
                    nullptr, 
                    nullptr,
                    nullptr 
                );
                if (!sws_ctx){
                    std::cerr << "failed to get sws context" << std::endl;
                    return -1;
                }


                ret =sws_scale(
                    sws_ctx,
                    frame->data,
                    frame->linesize,
                    0,
                    frame->height,
                    sw_frame->data,
                    sw_frame->linesize);

                if (ret < 0) {
                    std::cerr << "failed to scale frame" << std::endl;
                    av_frame_free(&frame);
                    return AVERROR(EINVAL);
                }

                int64_t src_pts = frame->best_effort_timestamp;
                if (src_pts == AV_NOPTS_VALUE) {
                    src_pts = frame->pts;
                }
                if (src_pts != AV_NOPTS_VALUE) {
                    sw_frame->pts = av_rescale_q(src_pts, in_video_dec_ctx->time_base, out_video_enc_ctx->time_base);
                } else {
                    sw_frame->pts = next_video_pts++;
                }
                
                ret = avcodec_send_frame(out_video_enc_ctx, sw_frame);

                if (ret < 0) {
                    std::cerr << "failed to send frame to video encoder" << std::endl;
                    av_frame_free(&sw_frame);
                    return AVERROR(EINVAL);
                }

                av_frame_free(&sw_frame);

                AVPacket *out_pkt = av_packet_alloc();

                while ((ret = avcodec_receive_packet(out_video_enc_ctx, out_pkt) ) >= 0) {
                    AVStream *out_stream = out_fmt_ctx->streams[input_options.in_video_index];
                    av_packet_rescale_ts(out_pkt, out_video_enc_ctx->time_base, out_stream->time_base);
                    out_pkt->stream_index = input_options.out_video_index;
                    ret = av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                    if (ret < 0) {
                        std::cerr << "failed to write packet to output file" << std::endl;
                        av_packet_free(&out_pkt);
                        return AVERROR(EINVAL);
                    }
                    av_packet_unref(out_pkt);
                    av_packet_free(&out_pkt);
                }
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    ret = 0;
                }
                if (ret < 0) {
                    std::cerr << "failed to receive packet from video encoder" << std::endl;
                    av_packet_free(&out_pkt);
                    return AVERROR(EINVAL);
                }
                av_packet_free(&out_pkt);

            }

        } else if (pkt->stream_index == input_options.in_audio_index) {
            ret = avcodec_send_packet(in_audio_dec_ctx, pkt);
        
        }

    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_free(sws_ctx);
    av_free(swr_ctx);
    avcodec_free_context(&in_video_dec_ctx);
    avcodec_free_context(&in_audio_dec_ctx);
    avcodec_free_context(&out_video_enc_ctx);
    avcodec_free_context(&out_audio_enc_ctx);
    avformat_close_input(&in_fmt_ctx);
    return 0;
}