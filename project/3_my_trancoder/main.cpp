#include <iostream>

#include <string>


extern "C"{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    // #include <libavutil/avutil.h>
}


struct InputOptions {
    std::string input_path;
    std::string output_path;
    AVPixelFormat output_pixel_format = AV_PIX_FMT_YUV420P;
};


bool parse_args(int argc, char** argv, InputOptions& options);

int main(int argc, char** argv) {
    
    InputOptions options;
    if (!parse_args(argc, argv, options)) {
        std::cerr << "Failed to parse arguments" << std::endl;
        return -1;
    }

    AVFormatContext* fmt_ctx = nullptr;
    AVFormatContext* ofmt_ctx = nullptr;

    int ret = 0;

    ret = avformat_open_input(&fmt_ctx, options.input_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open input file" << std::endl;
        return -1;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    ret = avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, options.output_path.c_str());
    if (ret < 0) {
        std::cerr << "Failed to alloc output context" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    
    
    
  


    return 0;
}


bool parse_args(int argc, char** argv, InputOptions& options) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <input_path> <output_path> <output_pixel_format>" << std::endl;
        return false;
    }
    
    for (int i = 1; i < argc; i++) {
    
        std::string arg = argv[i];
        if (arg == "--input") {
            options.input_path = argv[++i];
        } else if (arg == "--output") {
            options.output_path = argv[++i];
        } else if (arg == "--output-pixel-format") {
            options.output_pixel_format = (AVPixelFormat)std::stoi(argv[++i]);
        }
    }

    if (options.input_path.empty() || options.output_path.empty()) {
        std::cerr << "Missing input or output path" << std::endl;
        return false;
    }

    if (options.output_pixel_format == AV_PIX_FMT_NONE) {
        std::cerr << "Missing output pixel format" << std::endl;
        return false;
    }

    return true;
}


