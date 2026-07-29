#pragma once
#include <cstdint>
namespace player {
class TextureManager {
public:
  ~TextureManager();
  bool uploadYUV420P(const uint8_t* y, int yStride, const uint8_t* u, int uStride,
                     const uint8_t* v, int vStride, int w, int h);
  void bind(int yUnit=0, int uUnit=1, int vUnit=2) const;
private:
  void ensureTextures_(int w, int h);
  unsigned m_texY=0, m_texU=0, m_texV=0;
  int m_texW=0, m_texH=0;
};
}
