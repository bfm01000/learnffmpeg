#include "color_converter.h"

namespace player {

ColorConverter::ColorConverter() = default;

ColorConverter::~ColorConverter() {
    close();
}

int ColorConverter::init(int in_w, int in_h, AVPixelFormat in_fmt,
                          int out_w, int out_h, AVPixelFormat out_fmt) {
    m_srcW = in_w;
    m_srcH = in_h;
    m_srcFmt = in_fmt;
    m_dstW = out_w;
    m_dstH = out_h;
    m_dstFmt = out_fmt;

    // TODO: Create SwsContext with sws_getContext() for the CPU path.
    //       For the GPU shader path, compile GLSL color-conversion shaders
    //       and set m_useGpuPath = true.

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
    // TODO: Free m_swsCtx with sws_freeContext().
    //       Destroy GPU shader resources if the GPU path was used.
}

} // namespace player
