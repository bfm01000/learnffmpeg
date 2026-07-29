#include "render/video/opengl_renderer/opengl_renderer.h"
#include <GL/gl.h>
#include <cstdio>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

namespace player {

OpenGLRenderer::OpenGLRenderer() = default;
OpenGLRenderer::~OpenGLRenderer() { destroy(); }

int OpenGLRenderer::init(const RenderConfig& cfg) {
  int w=cfg.width>0?cfg.width:1280;
  int h=cfg.height>0?cfg.height:720;
  if(!m_gl.create(w,h,cfg.title?cfg.title:"Player SDK")) return -1;
  if(!m_shader.compile(nullptr,nullptr)) return -1;
  setupQuad_();
  m_inited=true;
  return 0;
}

void OpenGLRenderer::setupQuad_() {
  // fullscreen quad: pos + texcoord
  float v[]={ -1,-1, 0,1,  1,-1, 1,1,  1,1, 1,0,  -1,-1, 0,1,  1,1, 1,0,  -1,1, 0,0 };
  glGenVertexArrays(1,&m_vao); glGenBuffers(1,&m_vbo);
  glBindVertexArray(m_vao);
  glBindBuffer(GL_ARRAY_BUFFER,m_vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_STATIC_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}

int OpenGLRenderer::render(AVFrame* frame) {
  if(!m_inited||!frame||!frame->data[0]) return -1;
  if(m_gl.shouldClose()) return -2;

  glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);

  // Upload YUV420P
  int w=frame->width, h=frame->height;
  const uint8_t* y=frame->data[0]; const uint8_t* u=frame->data[1]; const uint8_t* v=frame->data[2];
  int yStride=frame->linesize[0], uStride=frame->linesize[1], vStride=frame->linesize[2];

  if(!m_tex.uploadYUV420P(y,yStride,u,uStride,v,vStride,w,h)) return -1;

  // Draw
  m_shader.use();
  m_shader.setUniform1i("uTexY",0); m_shader.setUniform1i("uTexU",1); m_shader.setUniform1i("uTexV",2);
  m_tex.bind(0,1,2);

  glBindVertexArray(m_vao);
  glDrawArrays(GL_TRIANGLES,0,6);
  glBindVertexArray(0);

  m_gl.swapBuffers();
  m_gl.pollEvents();
  return 0;
}

void OpenGLRenderer::resize(int w, int h) {
  glViewport(0,0,w,h);
}

void OpenGLRenderer::destroy() {
  if(m_vao) glDeleteVertexArrays(1,&m_vao);
  if(m_vbo) glDeleteBuffers(1,&m_vbo);
  m_vao=m_vbo=0; m_inited=false;
  m_gl.destroy();
}

bool OpenGLRenderer::shouldClose() const { return m_gl.shouldClose(); }
}
