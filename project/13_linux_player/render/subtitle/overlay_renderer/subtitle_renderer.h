#pragma once

#include <memory>
#include <string>
#include <vector>

#include <GL/gl.h>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {

/// @brief Renders text and bitmap subtitles as a GL texture overlay.
///        Composited on top of the video frame during the rendering pass.
class SubtitleRenderer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();

    SubtitleRenderer(const SubtitleRenderer&) = delete;
    SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;
    SubtitleRenderer(SubtitleRenderer&&) = delete;
    SubtitleRenderer& operator=(SubtitleRenderer&&) = delete;

    /// @brief Initialize the subtitle renderer.
    /// @param window_w  Video output width (pixels).
    /// @param window_h  Video output height (pixels).
    /// @return 0 on success, non-zero on failure.
    int init(int window_w, int window_h);

    /// @brief Upload a subtitle bitmap or render text to an overlay texture.
    /// @param frame  AVFrame containing subtitle data (AV_SUBTITLE_FMT_BITMAP
    ///               or AV_SUBTITLE_FMT_ASS text).
    /// @return 0 on success, non-zero on failure.
    int renderSubtitle(AVFrame* frame);

    /// @brief Draw the subtitle overlay (bind texture, draw blended quad).
    void drawOverlay();

    /// @brief Clear the current subtitle overlay.
    void clearOverlay();

    /// @brief Release all GL resources.
    void destroy();

private:
    GLuint overlay_texture_{0};
    int    window_w_{0};
    int    window_h_{0};

    struct SubtitleRect {
        int x{0}, y{0}, w{0}, h{0};
        std::vector<uint8_t> rgba_data;
    };
    std::vector<SubtitleRect> rects_;
};

} // namespace player
