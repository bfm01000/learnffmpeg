#include "gl_context.h"

#include <GLFW/glfw3.h>

namespace player {

GLContext::GLContext() = default;

GLContext::~GLContext() {
    destroy();
}

int GLContext::create(int width, int height, const std::string& title) {
    // TODO: Initialize GLFW with glfwInit(), set GLFW window hints,
    //       create window with glfwCreateWindow(), make context current.
    return 0;
}

void GLContext::makeCurrent() {
    // TODO: Call glfwMakeContextCurrent(window_).
}

void GLContext::swapBuffers() {
    // TODO: Call glfwSwapBuffers(window_).
}

bool GLContext::shouldClose() {
    // TODO: Return glfwWindowShouldClose(window_).
    return false;
}

void GLContext::destroy() {
    // TODO: Destroy window with glfwDestroyWindow(), terminate with glfwTerminate().
}

} // namespace player
