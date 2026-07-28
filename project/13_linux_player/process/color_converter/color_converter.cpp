#include "color_converter.h"

namespace player {

ColorConverter::ColorConverter() = default;

ColorConverter::~ColorConverter() {
    close();
}

int ColorConverter::init(int in_w, int in_h, AVPixelFormat in_fmt,
                          int out_w, int out_h, AVPixelFormat out_fmt) {
    src_w_ = in_w;
    src_h_ = in_h;
    src_fmt_ = in_fmt;
    dst_w_ = out_w;
    dst_h_ = out_h;
    dst_fmt_ = out_fmt;

    // TODO: Create SwsContext with sws_getContext() for the CPU path.
    //       For the GPU shader path, compile GLSL color-conversion shaders
    //       and set use_gpu_path_ = true.

    return 0;
}

AVFrame* ColorConverter::convert(AVFrame* in_frame) {
    // TODO: Allocate output AVFrame.
    //       If CPU path: call sws_scale().
    //       If GPU path: upload frame data to a texture and run the
    //       shader-based color conversion pass, then read back the result.
    return nullptr;
}

void ColorConverter::close() {
    // TODO: Free sws_ctx_ with sws_freeContext().
    //       Destroy GPU shader resources if the GPU path was used.
}

} // namespace player
