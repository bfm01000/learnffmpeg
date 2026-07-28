#pragma once

#include <string>
#include <unordered_map>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace player {

/// @brief Compiles, links, and manages an OpenGL shader program.
class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) noexcept;
    ShaderProgram& operator=(ShaderProgram&&) noexcept;

    /// @brief Load vertex and fragment shaders from source files and link.
    /// @param vs_path  Path to vertex shader source file.
    /// @param fs_path  Path to fragment shader source file.
    /// @return 0 on success, non-zero on failure.
    int loadFromFile(const std::string& vs_path, const std::string& fs_path);

    /// @brief Activate (use) this program.
    void use();

    /// @brief Get the location of an attribute by name.
    /// @return Attribute location index, or -1 if not found.
    GLint getAttribLocation(const std::string& name);

    /// @brief Get the underlying GL program handle.
    GLuint id() const { return program_; }

    // -----------------------------------------------------------------------
    // Template setUniform implementations (must live in header)
    // -----------------------------------------------------------------------

    template <typename T>
    void setUniform(const std::string& name, T value);

    /// @brief Set a mat4 uniform by value.
    void setUniform(const std::string& name, const float* mat4_ptr);

private:
    GLuint program_{0};

    /// @brief Compile a single shader from source.
    GLuint compileShader(GLenum type, const std::string& source);

    /// @brief Read file contents into a string.
    std::string readFile(const std::string& path);
};

// ---------------------------------------------------------------------------
// Template specializations for setUniform (int, float)
// ---------------------------------------------------------------------------

template <>
inline void ShaderProgram::setUniform<int>(const std::string& name, int value) {
    // TODO: GLint loc = glGetUniformLocation(program_, name.c_str()); glUniform1i(loc, value);
}

template <>
inline void ShaderProgram::setUniform<float>(const std::string& name, float value) {
    // TODO: GLint loc = glGetUniformLocation(program_, name.c_str()); glUniform1f(loc, value);
}

} // namespace player
