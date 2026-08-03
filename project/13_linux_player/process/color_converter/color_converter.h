#pragma once

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace player {

/// @brief Converts pixel format and/or dimensions of video frames.
///        Supports both CPU (libswscale) and GPU shader-based paths.
class ColorConverter {
public:
    ColorConverter();
    ~ColorConverter();

    ColorConverter(const ColorConverter&) = delete;
    ColorConverter& operator=(const ColorConverter&) = delete;
    ColorConverter(ColorConverter&&) = delete;
    ColorConverter& operator=(ColorConverter&&) = delete;

    /// @brief Initialize the converter.
    /// @param in_w   Input width in pixels.
    /// @param in_h   Input height in pixels.
    /// @param in_fmt Input AVPixelFormat.
    /// @param out_w  Output width in pixels.
    /// @param out_h  Output height in pixels.
    /// @param out_fmt Output AVPixelFormat.
    /// @return 0 on success, negative AVERROR on failure.
    int init(int in_w, int in_h, AVPixelFormat in_fmt,
             int out_w, int out_h, AVPixelFormat out_fmt);

    /// @brief Convert a video frame.
    /// @param in_frame  Input AVFrame.
    /// @return A newly allocated AVFrame in the output format, or nullptr on error.
    AVFrame* convert(AVFrame* in_frame);

    /// @brief Release all converter resources.
    void close();

private:
    SwsContext* m_swsCtx{nullptr};

    int m_srcW{0};
    int m_srcH{0};
    AVPixelFormat m_srcFmt{AV_PIX_FMT_NONE};
    int m_dstW{0};
    int m_dstH{0};
    AVPixelFormat m_dstFmt{AV_PIX_FMT_NONE};

    // GPU shader-based path (future)
    bool m_useGpuPath{false};
};

} // namespace player
