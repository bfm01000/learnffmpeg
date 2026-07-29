#include "render/video/opengl_renderer/shader_program.h"
#include <GL/gl.h>
#include <cstdio>
#include <cstring>

namespace player {

static const char* kYuvToRgbVs = R"(#version 330 core
layout(location=0) in vec2 aPos; layout(location=1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main(){ gl_Position=vec4(aPos,0.0,1.0); vTexCoord=aTexCoord; })";

static const char* kYuvToRgbFs = R"(#version 330 core
in vec2 vTexCoord; out vec4 fragColor;
uniform sampler2D uTexY,uTexU,uTexV;
void main(){
  float y=texture(uTexY,vTexCoord).r;
  float u=texture(uTexU,vTexCoord).r-0.5;
  float v=texture(uTexV,vTexCoord).r-0.5;
  float r=y+1.402*v; float g=y-0.344*u-0.714*v; float b=y+1.772*u;
  fragColor=vec4(r,g,b,1.0); })";

ShaderProgram::~ShaderProgram() { if(m_id) glDeleteProgram(m_id); }

bool ShaderProgram::compile(const char* vsSrc, const char* fsSrc) {
  unsigned vs=compileShader_(GL_VERTEX_SHADER, vsSrc?vsSrc:kYuvToRgbVs);
  unsigned fs=compileShader_(GL_FRAGMENT_SHADER, fsSrc?fsSrc:kYuvToRgbFs);
  if(!vs||!fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return false; }
  m_id=glCreateProgram();
  glAttachShader(m_id,vs); glAttachShader(m_id,fs);
  glLinkProgram(m_id);
  GLint ok; glGetProgramiv(m_id,GL_LINK_STATUS,&ok);
  if(!ok){ char log[512]; glGetProgramInfoLog(m_id,512,nullptr,log); fprintf(stderr,"Shader link: %s\n",log); }
  glDeleteShader(vs); glDeleteShader(fs);
  return ok;
}

void ShaderProgram::use() const { glUseProgram(m_id); }
int  ShaderProgram::uniformLoc(const char* n) const { return glGetUniformLocation(m_id,n); }
void ShaderProgram::setUniform1i(const char* n, int v) const { glUniform1i(uniformLoc(n),v); }

unsigned ShaderProgram::compileShader_(unsigned type, const char* src) {
  unsigned s=glCreateShader(type);
  glShaderSource(s,1,&src,nullptr);
  glCompileShader(s);
  GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[512]; glGetShaderInfoLog(s,512,nullptr,log); fprintf(stderr,"Shader %u: %s\n",type,log); glDeleteShader(s); return 0; }
  return s;
}
}
