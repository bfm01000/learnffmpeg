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
    m_windowW = window_w;
    m_windowH = window_h;
    return 0;
}

int SubtitleRenderer::renderSubtitle(AVFrame* frame) {
    // TODO: Extract subtitle rectangles from frame (subtitle rects / ASS data).
    //       For bitmap subtitles: convert to RGBA and store in m_rects.
    //       For ASS text: use libass or a simple bitmap font renderer.
    //       Upload the composed subtitle bitmap(s) to m_overlayTexture.
    (void)frame;
    return 0;
}

void SubtitleRenderer::drawOverlay() {
    // TODO: If m_overlayTexture is valid, bind it and draw a blended quad
    //       that covers the subtitle region(s). Enable GL_BLEND.
}

void SubtitleRenderer::clearOverlay() {
    // TODO: Clear m_rects vector.
    //       Optionally clear the overlay texture to transparent.
}

void SubtitleRenderer::destroy() {
    // TODO: glDeleteTextures() for m_overlayTexture.
}

} // namespace player
