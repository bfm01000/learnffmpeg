#pragma once
#include <string>
namespace player {
class ShaderProgram {
public:
  ~ShaderProgram();
  bool compile(const char* vsSrc, const char* fsSrc);
  void use() const;
  int  uniformLoc(const char* name) const;
  void setUniform1i(const char* name, int v) const;
  unsigned id() const { return m_id; }
private:
  unsigned m_id=0;
  unsigned compileShader_(unsigned type, const char* src);
};
}
