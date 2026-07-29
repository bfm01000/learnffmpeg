#pragma once
#include <string>
struct GLFWwindow;
namespace player {
class GLContext {
public:
  GLContext();
  ~GLContext();
  bool create(int w, int h, const char* title="Player SDK");
  void destroy();
  void makeCurrent();
  void swapBuffers();
  bool shouldClose() const;
  void pollEvents();
  void setSwapInterval(int interval);
  GLFWwindow* window() const { return m_window; }
  int width() const { return m_width; }
  int height() const { return m_height; }
private:
  GLFWwindow* m_window=nullptr;
  int m_width=0, m_height=0;
};
}
