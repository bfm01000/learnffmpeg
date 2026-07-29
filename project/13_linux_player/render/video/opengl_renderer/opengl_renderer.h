#pragma once
#include "render/i_renderer.h"
#include "render/video/opengl_renderer/gl_context.h"
#include "render/video/opengl_renderer/shader_program.h"
#include "render/video/opengl_renderer/texture_manager.h"
#include <memory>
namespace player {
class OpenGLRenderer : public IRenderer {
public:
  OpenGLRenderer();
  ~OpenGLRenderer() override;
  int  init(const RenderConfig& cfg) override;
  int  render(AVFrame* frame) override;
  void resize(int w, int h) override;
  void destroy() override;
  bool shouldClose() const;
private:
  GLContext m_gl;
  ShaderProgram m_shader;
  TextureManager m_tex;
  unsigned m_vao=0, m_vbo=0;
  bool m_inited=false;
  void setupQuad_();
};
}
