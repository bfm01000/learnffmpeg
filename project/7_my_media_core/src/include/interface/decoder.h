#pragma once

#include "common/status.h"


extern "C" {
    #include <libavcodec/avcodec.h>
}

namespace my_media_core {

class IDecoder {
    public:
        virtual ~IDecoder() = 0;
        virtual Status Open(AVCodecContext *ctx, bool enable_hw_decode, const char *preferred_hw_device) = 0; 

};


}
