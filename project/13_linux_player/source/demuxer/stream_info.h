#pragma once

#include <cstdint>
#include <string>

namespace player {

enum class MediaType {
    Unknown,
    Video,
    Audio,
    Subtitle,
    Data,
    Attachment
};

struct StreamInfo {
    int index = -1;
    MediaType type = MediaType::Unknown;
    int codec_id = 0;
    std::string codec_name;
    int width = 0;
    int height = 0;
    int sample_rate = 0;
    int channels = 0;
    int64_t bitrate = 0;
    int64_t duration = 0; // in microseconds (AV_NOPTS_VALUE if unknown)
};

const char* mediaTypeToString(MediaType type);

} // namespace player
