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
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_make_error_string(buf, sizeof(buf), errnum);
    return std::string(buf);
}

bool parse_args(int argc, char **argv, InputOptions &options);


InputOptions input_options;




int main(int argc, char **argv) {
    // 1. 解析参数
    input_options.input_path = "";
    input_options.output_path = "";

    // 2. 打开AVFormatContext
    AVFormatContext * in_fmt_ctx = nullptr;
    AVFormatContext * out_fmt_ctx = nullptr;

    int ret = 0;

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

    // 5.1视频编码器初始化
    video_encoder = avcodec_find_encoder_by_name("libx264");
    if (!video_encoder) {
        std::cerr << "no video encoder found" << std::endl;
        return -1;
    }

    out_video_enc_ctx = avcodec_alloc_context3(video_encoder);
    if (!out_video_enc_ctx) {
        std::cerr << "failed to allocate video encoder context" << std::endl;
        return -1;
    }

    // 5.2获取输入视频的帧率

    if (input_options.target_width == -1) {
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

    out_audio_enc_ctx->sample_rate = in_audio_dec_ctx->sample_rate > 0 ? in_audio_dec_ctx->sample_rate : 4800;
    out_audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&out_audio_enc_ctx->ch_layout, &in_audio_dec_ctx->ch_layout);
    out_audio_enc_ctx->time_base = AVRational{1, in_audio_dec_ctx->sample_rate};


    AVPacket *pkt = av_packet_alloc();

    while (av_read_frame(in_fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == input_options.in_video_index) {
            ret = avcodec_send_packet(in_video_dec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "failed to send packet to video decoder" << std::endl;
                return -1;
            }

            // while (ret >= 0) {
            //     ret = avcodec_receive_frame(in_video_dec_ctx, frame);
            //     if (ret < 0) {
            //         std::cerr << "failed to receive frame from video decoder" << std::endl;
            //         return -1;
            //     }
            // }

        } else if (pkt->stream_index == input_options.in_audio_index) {
            ret = avcodec_send_packet(in_audio_dec_ctx, pkt);
        
        }

    }
    

    // 6. 循环读取AVPacket，解码成AVFrame, 转成特定分辨率以及数据格式

    // 7. 送入编码器

    // 8. 循环读取AVFrame，编码成AVPacket，写入文件

    // 9. 释放资源
    return 0;
}