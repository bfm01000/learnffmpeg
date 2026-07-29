#include "render/video/opengl_renderer/texture_manager.h"
#include <GL/gl.h>
#include <cstdio>
#include <cstring>

namespace player {

TextureManager::~TextureManager() {
  if(m_texY) glDeleteTextures(1,&m_texY);
  if(m_texU) glDeleteTextures(1,&m_texU);
  if(m_texV) glDeleteTextures(1,&m_texV);
}

void TextureManager::ensureTextures_(int w, int h) {
  if(m_texY && m_texW==w && m_texH==h) return; // size unchanged
  if(m_texY) glDeleteTextures(1,&m_texY);
  if(m_texU) glDeleteTextures(1,&m_texU);
  if(m_texV) glDeleteTextures(1,&m_texV);
  m_texW=w; m_texH=h;

  auto createTex=[&](unsigned& tex, int tw, int th, GLenum fmt) {
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,fmt,tw,th,0,fmt,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  };
  createTex(m_texY, w,   h,   GL_RED);
  createTex(m_texU, w/2, h/2, GL_RED);
  createTex(m_texV, w/2, h/2, GL_RED);
}

bool TextureManager::uploadYUV420P(
    const uint8_t* y, int yStride, const uint8_t* u, int uStride,
    const uint8_t* v, int vStride, int w, int h) {
  if(!y||!u||!v) return false;
  ensureTextures_(w,h);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, yStride);
  glBindTexture(GL_TEXTURE_2D,m_texY);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RED,GL_UNSIGNED_BYTE,y);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, uStride);
  glBindTexture(GL_TEXTURE_2D,m_texU);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w/2,h/2,GL_RED,GL_UNSIGNED_BYTE,u);
  glBindTexture(GL_TEXTURE_2D,m_texV);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w/2,h/2,GL_RED,GL_UNSIGNED_BYTE,v);

  glPixelStorei(GL_UNPACK_ROW_LENGTH,0); // reset
  return true;
}

void TextureManager::bind(int yUnit, int uUnit, int vUnit) const {
  auto bindTex=[&](unsigned tex, int unit) {
    glActiveTexture(GL_TEXTURE0+unit);
    glBindTexture(GL_TEXTURE_2D,tex);
  };
  bindTex(m_texY,yUnit); bindTex(m_texU,uUnit); bindTex(m_texV,vUnit);
}
}
