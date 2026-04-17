#pragma once

#include "media_core/common/status.h"

extern "C" {
#include <libavutil/rational.h>
}

struct AVFrame;

namespace media_core {

class IVideoFilter {
public:
    virtual ~IVideoFilter() = default;

    // Initialize the filter graph
    virtual Status Init(const char* filter_desc, 
                        int in_width, int in_height, 
                        int in_pix_fmt, AVRational in_tb, AVRational in_sar) = 0;

    // Send a decoded frame to the filter graph
    virtual Status SendFrame(AVFrame* frame) = 0;

    // Receive a filtered frame from the filter graph
    virtual Status ReceiveFrame(AVFrame* frame, bool* again, bool* eof) = 0;
};

}  // namespace media_core
