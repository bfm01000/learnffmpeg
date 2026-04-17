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

    // Trim options in microseconds (us)
    int64_t trim_start_us = -1;
    int64_t trim_end_us = -1;

    // Filter description (e.g. "drawtext=text='create by dml + %{pts\\:hms}':x=10:y=10:fontsize=24:fontcolor=white")
    std::string filter_desc;
};

}  // namespace media_core
