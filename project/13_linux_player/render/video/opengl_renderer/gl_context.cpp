#include "render/video/opengl_renderer/gl_context.h"
#include <GLFW/glfw3.h>
#include <cstdio>
namespace player {
static int g_glfwRef=0;
GLContext::GLContext() { if(g_glfwRef++==0) glfwInit(); }
GLContext::~GLContext() { destroy(); if(--g_glfwRef==0) glfwTerminate(); }
bool GLContext::create(int w,int h,const char* title) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  m_window=glfwCreateWindow(w,h,title,nullptr,nullptr);
  if(!m_window) return false;
  m_width=w; m_height=h; makeCurrent(); glfwSwapInterval(1); return true;
}
void GLContext::destroy() { if(m_window){glfwDestroyWindow(m_window);m_window=nullptr;} }
void GLContext::makeCurrent() { glfwMakeContextCurrent(m_window); }
void GLContext::swapBuffers() { glfwSwapBuffers(m_window); }
bool GLContext::shouldClose() const { return glfwWindowShouldClose(m_window); }
void GLContext::pollEvents() { glfwPollEvents(); }
void GLContext::setSwapInterval(int i) { glfwSwapInterval(i); }
}
