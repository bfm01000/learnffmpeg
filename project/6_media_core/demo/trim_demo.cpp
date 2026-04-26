#include "media_core/pipeline/transcode_pipeline.h"
#include <iostream>

using namespace media_core;

int main(int argc, char **argv) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> <start_time_sec> <end_time_sec>\n";
        return -1;
    }

    VideoTranscodeConfig config;
    config.input_path = argv[1];
    config.output_path = argv[2];
    config.output_format = "mp4";
#ifdef __APPLE__
    config.video_encoder_name = "h264_videotoolbox";
    config.preferred_hw_device = "videotoolbox";
#else
    config.video_encoder_name = "h264_v4l2m2m";
#endif
    config.target_width = 640;
    config.target_height = 360;
    config.target_fps = 30;
    config.video_bitrate = 1000 * 1000; // 1Mbps
    config.enable_hw_decode = false;

    // Parse trim times
    double start_sec = std::stod(argv[3]);
    double end_sec = std::stod(argv[4]);
    config.trim_start_us = static_cast<int64_t>(start_sec * 1000000.0);
    config.trim_end_us = static_cast<int64_t>(end_sec * 1000000.0);

    std::cout << "Trimming from " << start_sec << "s to " << end_sec << "s\n";

    TranscodePipeline pipeline;
    Status st = pipeline.Run(config);



    
    if (!st.ok()) {
        std::cerr << "Transcode failed: " << st.message << std::endl;
        return -1;
    }

    std::cout << "Trim and transcode success!" << std::endl;
    return 0;
}
