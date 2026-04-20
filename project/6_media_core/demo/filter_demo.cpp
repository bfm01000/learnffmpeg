#include "media_core/pipeline/transcode_pipeline.h"
#include <iostream>
#include <string>

using namespace media_core;

int main(int argc, char **argv) {
    // if (argc < 3) {
    //     std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [enable_watermark: 0/1] [enable_color: 0/1]\n";
    //     return -1;
    // }

    VideoTranscodeConfig config;
    config.input_path = "/home/bfm01000/workspace/source/cat.mp4";
    config.output_path = "/home/bfm01000/workspace/source/filter.mp4";
    config.output_format = "mp4";
#ifdef __APPLE__
    config.video_encoder_name = "h264_videotoolbox";
    config.preferred_hw_device = "videotoolbox";
#else
    config.video_encoder_name = "mpeg4";
#endif
    config.target_width = 640;
    config.target_height = 360;
    config.target_fps = 30;
    config.video_bitrate = 1000 * 1000; // 1Mbps
    config.enable_hw_decode = false;

    bool enable_watermark = true;
    bool enable_color = false;

    std::string filter_desc;
    
    // 调色: 提高亮度、对比度和饱和度
    if (enable_color) {
        filter_desc += "eq=brightness=0.06:contrast=1.2:saturation=1.5";
    }

    // 水印: 左上角打上 "create by dml + pts"
    if (enable_watermark) {
        if (!filter_desc.empty()) filter_desc += ",";
        // 注意：drawtext 依赖于 FFmpeg 编译时启用了 libfreetype
        filter_desc += "drawtext=text='create by dml + %{pts\\:hms}':x=10:y=10:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.5";
    }

    config.filter_desc = filter_desc;

    if (!config.filter_desc.empty()) {
        std::cout << "Using filter: " << config.filter_desc << std::endl;
    } else {
        std::cout << "No filter enabled." << std::endl;
    }

    TranscodePipeline pipeline;
    Status st = pipeline.Run(config);
    if (!st.ok()) {
        std::cerr << "Transcode failed: " << st.message << std::endl;
        return -1;
    }

    std::cout << "Filter and transcode success!" << std::endl;
    return 0;
}
