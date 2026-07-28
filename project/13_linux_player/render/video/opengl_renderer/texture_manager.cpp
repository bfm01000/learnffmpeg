#include "texture_manager.h"

#include <cstring>

namespace player {

TextureManager::TextureManager() = default;

TextureManager::~TextureManager() {
    destroy();
}

void TextureManager::uploadYUV420P(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                    int w, int h) {
    // TODO: Create or update three GL_TEXTURE_2D objects for Y/U/V planes.
    //       Optionally use a PBO for async transfer: glBufferData() +
    //       glMapBufferRange() to write data without stalling the pipeline.
    width_ = w;
    height_ = h;
}

void TextureManager::uploadNV12(const uint8_t* data, int w, int h) {
    // TODO: Upload Y plane as one texture and interleaved UV as another.
    //       Same PBO strategy as YUV420P.
    width_ = w;
    height_ = h;
}

void TextureManager::destroy() {
    // TODO: glDeleteTextures() for y_tex_, u_tex_, v_tex_, texture_id_.
    //       glDeleteBuffers() for pbo_.
}

} // namespace player
