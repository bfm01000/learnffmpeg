#include "subtitle_renderer.h"

#include <cstring>

namespace player {

SubtitleRenderer::SubtitleRenderer() = default;

SubtitleRenderer::~SubtitleRenderer() {
    destroy();
}

int SubtitleRenderer::init(int window_w, int window_h) {
    // TODO: Store window dimensions.
    //       Create a GL_TEXTURE_2D for the overlay with appropriate parameters
    //       (GL_RGBA, linear filtering, clamp-to-edge).
    window_w_ = window_w;
    window_h_ = window_h;
    return 0;
}

int SubtitleRenderer::renderSubtitle(AVFrame* frame) {
    // TODO: Extract subtitle rectangles from frame (subtitle rects / ASS data).
    //       For bitmap subtitles: convert to RGBA and store in rects_.
    //       For ASS text: use libass or a simple bitmap font renderer.
    //       Upload the composed subtitle bitmap(s) to overlay_texture_.
    (void)frame;
    return 0;
}

void SubtitleRenderer::drawOverlay() {
    // TODO: If overlay_texture_ is valid, bind it and draw a blended quad
    //       that covers the subtitle region(s). Enable GL_BLEND.
}

void SubtitleRenderer::clearOverlay() {
    // TODO: Clear rects_ vector.
    //       Optionally clear the overlay texture to transparent.
}

void SubtitleRenderer::destroy() {
    // TODO: glDeleteTextures() for overlay_texture_.
}

} // namespace player
