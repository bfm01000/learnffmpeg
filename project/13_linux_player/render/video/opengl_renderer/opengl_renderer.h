#pragma once

#include <memory>

#include "render/i_renderer.h"
#include "gl_context.h"
#include "shader_program.h"
#include "texture_manager.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace player {

/// @brief OpenGL-based video renderer implementing IRenderer.
///        Manages the VSync render loop, PTS-based timing,
///        YUV-to-RGB shader, and fullscreen quad drawing.
class OpenGLRenderer : public IRenderer {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    OpenGLRenderer(const OpenGLRenderer&) = delete;
    OpenGLRenderer& operator=(const OpenGLRenderer&) = delete;
    OpenGLRenderer(OpenGLRenderer&&) = delete;
    OpenGLRenderer& operator=(OpenGLRenderer&&) = delete;

    // IRenderer interface
    int init(const RenderConfig& cfg) override;
    int render(AVFrame* frame) override;
    void resize(int w, int h) override;
    void destroy() override;

private:
    std::unique_ptr<GLContext>   gl_context_;
    std::unique_ptr<ShaderProgram> shader_program_;
    std::unique_ptr<TextureManager> texture_manager_;

    // Screen quad geometry (VBO/VAO)
    GLuint vao_{0};
    GLuint vbo_{0};

    // Timing
    double last_pts_{0.0};
    double clock_reference_{0.0};

    // Dimension cache
    int window_w_{0};
    int window_h_{0};

    // Internal helpers
    int setupQuad();
    int compileShaders();
    int uploadFrame(AVFrame* frame);
    void drawFrame();
    void syncPts(double pts);
};

} // namespace player
