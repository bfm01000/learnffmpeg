#pragma once

#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "stream_info.h"

namespace player {

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual int open(const char* url) = 0;
    virtual std::shared_ptr<AVPacket> readPacket() = 0;
    virtual int seekTo(int64_t pos_ms) = 0;
    virtual std::vector<StreamInfo> getStreams() const = 0;
    virtual int close() = 0;
};

} // namespace player
