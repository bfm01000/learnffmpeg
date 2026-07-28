#pragma once

#include <cstdint>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace player {

/// @brief Manages YUV/RGB texture planes for video rendering.
///        Supports YUV420P and NV12 input with optional PBO async upload.
class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    TextureManager(TextureManager&&) = delete;
    TextureManager& operator=(TextureManager&&) = delete;

    /// @brief Upload YUV420P planar data.
    /// @param y   Y plane data.
    /// @param u   U plane data.
    /// @param v   V plane data.
    /// @param w   Luma width in pixels.
    /// @param h   Luma height in pixels.
    void uploadYUV420P(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int w, int h);

    /// @brief Upload NV12 data (interleaved UV).
    /// @param data  Packed NV12 frame data (Y plane + interleaved UV).
    /// @param w     Width in pixels.
    /// @param h     Height in pixels.
    void uploadNV12(const uint8_t* data, int w, int h);

    /// @brief Get the final combined or primary texture ID.
    GLuint getTexture() const { return texture_id_; }

    /// @brief Delete all GL textures and PBOs.
    void destroy();

private:
    GLuint texture_id_{0};
    // YUV plane texture IDs (used for multi-texture YUV pipeline)
    GLuint y_tex_{0};
    GLuint u_tex_{0};
    GLuint v_tex_{0};

    // PBO handles for async pixel upload
    GLuint pbo_{0};

    // Cached dimensions
    int width_{0};
    int height_{0};
};

} // namespace player
