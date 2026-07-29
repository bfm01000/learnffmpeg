#pragma once
/// SDL2-based video renderer — 替代 OpenGL, 无需额外依赖.

#include "render/i_renderer.h"
#include <SDL2/SDL.h>
#include <memory>

struct AVFrame;

namespace player {

class SDLVideoRenderer : public IRenderer {
public:
  SDLVideoRenderer();
  ~SDLVideoRenderer() override;

  int  init(const RenderConfig& cfg) override;
  int  render(AVFrame* frame) override;
  void resize(int w, int h) override;
  void destroy() override;
  bool shouldClose() const;

private:
  SDL_Window*   m_window = nullptr;
  SDL_Renderer* m_renderer = nullptr;
  SDL_Texture*  m_texture = nullptr;
  int m_winW=0, m_winH=0;
};

}
