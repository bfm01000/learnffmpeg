#include <iostream>
#include <string>
#include <vector>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>

void parse_args(int argc, char** argv, std::string& video_path, int& n, std::string& output_path) {

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " --input <video_path> --n <n>\n";
        std::exit(101);
    }

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--input") {
            video_path = arg;
        } else if (arg == "--n") {
            n = std::stoi(arg);
        } else if (arg == "--output") {
            output_path = arg;
        }   
    }
    if (video_path.empty()) {
        std::cerr << "Missing input file\n";
        std::exit(101);
    }
    if (n <= 0) {
        std::cerr << "n must be greater than 0\n";
        std::exit(101);
    }
    if (output_path.empty()) {
        std::cerr << "Missing output file\n";
        std::exit(101);
    }
}

int main(int argc, char** argv) {

    std::string video_path;
    int n;
    std::string output_path;
    parse_args(argc, argv, video_path, n, output_path);

    AVFormatContext* fmt_ctx = nullptr;
    
    int ret = avformat_open_input(&fmt_ctx, video_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open input file: " << video_path << "\n";
        return ret;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info: " << video_path << "\n";
        return ret;
    }

    int video_stream_index = -1;

    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            std::cout << "Video stream found at index: " << i << "\n";
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        std::cout << "No video stream found\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_STREAM_NOT_FOUND;
    }

    // 拿到视频流参数
    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    // 拿到解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Failed to find decoder\n";
        avformat_close_input(&fmt_ctx);
        return AVERROR_DECODER_NOT_FOUND;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    // 读取视频帧
    return 0;
}