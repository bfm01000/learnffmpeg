#pragma once

#include <cstdint>

extern "C" {
#include <libavutil/frame.h>
}

namespace player {

/// @brief Configuration passed to each renderer at init time.
struct RenderConfig {
    int         width{0};          ///< Desired output width (pixels).
    int         height{0};         ///< Desired output height (pixels).
    const char* title{nullptr};    ///< Window title (video renderers).
    void*       window_handle{nullptr}; ///< Optional external window handle.
};

/// @brief Abstract interface for all renderers (video, audio, subtitle).
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// @brief Initialize the renderer with a config.
    /// @param cfg  RenderConfig struct.
    /// @return 0 on success, non-zero on failure.
    virtual int init(const RenderConfig& cfg) = 0;

    /// @brief Render a single frame.
    /// @param frame  AVFrame to render (ownership remains with caller).
    /// @return 0 on success, non-zero on failure.
    virtual int render(AVFrame* frame) = 0;

    /// @brief Handle window resize events.
    /// @param w  New width.
    /// @param h  New height.
    virtual void resize(int w, int h) = 0;

    /// @brief Tear down all resources acquired by the renderer.
    virtual void destroy() = 0;
};

} // namespace player
