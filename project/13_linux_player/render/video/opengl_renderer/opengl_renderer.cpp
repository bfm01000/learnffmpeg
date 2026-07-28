#include "opengl_renderer.h"

#include <thread>
#include <chrono>

namespace player {

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
    destroy();
}

int OpenGLRenderer::init(const RenderConfig& cfg) {
    // TODO: Create GLContext with cfg.width, cfg.height, cfg.title.
    //       Initialize GLEW.
    //       Create ShaderProgram, load YUV->RGB shaders.
    //       Create TextureManager.
    //       Call setupQuad() to build fullscreen quad VBO/VAO.
    //       Store window_w_ / window_h_.
    return 0;
}

int OpenGLRenderer::render(AVFrame* frame) {
    // TODO:
    //   1. Calculate PTS delay from frame->pts and last_pts_.
    //   2. If delay > 0, wait (std::this_thread::sleep_for or busy-wait).
    //   3. Call uploadFrame(frame) to push data to GPU.
    //   4. Call glClear(), then drawFrame().
    //   5. Swap buffers with gl_context_->swapBuffers().
    //   6. Poll events with glfwPollEvents().
    //   7. Update last_pts_ = frame->pts.
    return 0;
}

void OpenGLRenderer::resize(int w, int h) {
    // TODO: Update window_w_ / window_h_, call glViewport().
}

void OpenGLRenderer::destroy() {
    // TODO: Delete VAO/VBO, destroy TextureManager, ShaderProgram, GLContext.
}

int OpenGLRenderer::setupQuad() {
    // TODO: Create VAO and VBO for a fullscreen quad (two triangles).
    //       Vertex format: position (vec2) + texcoord (vec2).
    return 0;
}

int OpenGLRenderer::compileShaders() {
    // TODO: Optionally embed or load YUV-to-RGB vertex/fragment shaders
    //       via shader_program_->loadFromFile().
    return 0;
}

int OpenGLRenderer::uploadFrame(AVFrame* frame) {
    // TODO: Based on frame->format, call texture_manager_->uploadYUV420P()
    //       or uploadNV12().
    return 0;
}

void OpenGLRenderer::drawFrame() {
    // TODO: glBindVertexArray(vao_);
    //       glActiveTexture + glBindTexture for each plane,
    //       set shader uniforms, glDrawArrays(GL_TRIANGLE_STRIP, 0, 4).
}

void OpenGLRenderer::syncPts(double pts) {
    // TODO: Compute delay = (pts - last_pts_) * time_base.
    //       If delay > 0, sleep for the calculated duration.
    //       Update clock_reference_ if needed.
}

} // namespace player
