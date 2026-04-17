#pragma  once

#include "libavutil/pixfmt.h"
#include <string>


extern "C" {
    #include <libavformat/avformat.h>
}


namespace my_media_core {
    struct InputConfig {
        std::string input_path ;
        std::string output_path;

        int width = -1;
        int height = -1;

        AVPixelFormat output_pixel_format = AV_PIX_FMT_YUV420P;
    };
}