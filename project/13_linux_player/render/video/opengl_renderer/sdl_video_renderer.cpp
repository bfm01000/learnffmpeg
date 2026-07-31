/// SDL2 video renderer — YUV420P direct upload.

#include "render/video/opengl_renderer/sdl_video_renderer.h"
#include <cstdio>
extern "C" {
#include <libavutil/frame.h>
}

namespace player {

SDLVideoRenderer::SDLVideoRenderer() {}
SDLVideoRenderer::~SDLVideoRenderer() { destroy(); }

int SDLVideoRenderer::init(const RenderConfig& cfg) {
  int w = cfg.width  > 0 ? cfg.width  : 1280;
  int h = cfg.height > 0 ? cfg.height : 720;
  m_winW = w; m_winH = h;
  m_window = SDL_CreateWindow(
      cfg.title ? cfg.title : "Player SDK",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      w, h, SDL_WINDOW_RESIZABLE);
  if (!m_window) {
    fprintf(stderr, "SDL window: %s\n", SDL_GetError());
    return -1;
  }
  m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
  if (!m_renderer) {
    fprintf(stderr, "SDL renderer: %s\n", SDL_GetError());
    return -1;
  }
  return 0;
}

int SDLVideoRenderer::render(AVFrame* frame) {
  if (!m_window || !frame || !frame->data[0]) return -1;

  int w = frame->width, h = frame->height;
  if (!m_texture || w != m_winW || h != m_winH) {
    if (m_texture) SDL_DestroyTexture(m_texture);
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    m_winW = w; m_winH = h;
    if (!m_texture) return -1;
  }

  SDL_UpdateYUVTexture(m_texture, nullptr,
      frame->data[0], frame->linesize[0],
      frame->data[1], frame->linesize[1],
      frame->data[2], frame->linesize[2]);

  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
  SDL_RenderPresent(m_renderer);
  return 0;
}

bool SDLVideoRenderer::pollEvents() {
  if (!m_window) return false;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      destroy();
      return false;
    }
  }
  return true;
}

void SDLVideoRenderer::resize(int w, int h) { m_winW = w; m_winH = h; }

void SDLVideoRenderer::destroy() {
  if (m_texture)  { SDL_DestroyTexture(m_texture);   m_texture  = nullptr; }
  if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
  if (m_window)   { SDL_DestroyWindow(m_window);     m_window   = nullptr; }
  // SDL_QuitSubSystem handled centrally by PlayerController
}

bool SDLVideoRenderer::shouldClose() const { return m_window == nullptr; }

}
