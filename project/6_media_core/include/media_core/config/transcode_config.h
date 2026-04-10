#pragma once

#include <string>

namespace media_core {

struct VideoTranscodeConfig {
    std::string input_path;
    std::string output_path;
    std::string output_format;      // e.g. mp4/flv/mkv
    std::string video_encoder_name; // e.g. libx264, hevc_videotoolbox

    int target_width = -1;
    int target_height = -1;
    int target_fps = 30;
    int video_bitrate = 2 * 1000 * 1000;

    bool enable_hw_decode = true;
    std::string preferred_hw_device; // videotoolbox/nvdec/qsv/vaapi
};

}  // namespace media_core
