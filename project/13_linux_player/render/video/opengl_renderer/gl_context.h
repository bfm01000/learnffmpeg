#pragma once

#include <string>

struct GLFWwindow;

namespace player {

/// @brief Manages a GLFW window and its associated OpenGL context.
class GLContext {
public:
    GLContext();
    ~GLContext();

    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;
    GLContext(GLContext&&) = delete;
    GLContext& operator=(GLContext&&) = delete;

    /// @brief Create the GLFW window and make its OpenGL context current.
    /// @param width  Window width in screen coordinates.
    /// @param height Window height in screen coordinates.
    /// @param title  Window title string.
    /// @return 0 on success, non-zero on failure.
    int create(int width, int height, const std::string& title);

    /// @brief Make this context current on the calling thread.
    void makeCurrent();

    /// @brief Swap the front and back buffers (double-buffering).
    void swapBuffers();

    /// @brief Query whether the window has received a close request.
    bool shouldClose();

    /// @brief Destroy the window and terminate GLFW.
    void destroy();

    /// @brief Get the native GLFWwindow pointer.
    GLFWwindow* window() const { return window_; }

private:
    GLFWwindow* window_{nullptr};
};

} // namespace player
