#include "shader_program.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace player {

ShaderProgram::ShaderProgram() = default;

ShaderProgram::~ShaderProgram() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (program_) glDeleteProgram(program_);
        program_ = other.program_;
        other.program_ = 0;
    }
    return *this;
}

int ShaderProgram::loadFromFile(const std::string& vs_path, const std::string& fs_path) {
    // TODO: Read sources with readFile(), compile each with compileShader(),
    //       create program with glCreateProgram(), attach shaders, link,
    //       check link status, detach and delete shader objects.
    return 0;
}

void ShaderProgram::use() {
    // TODO: Call glUseProgram(program_).
}

void ShaderProgram::setUniform(const std::string& name, const float* mat4_ptr) {
    // TODO: Call glUniformMatrix4fv(location, 1, GL_FALSE, mat4_ptr).
}

GLint ShaderProgram::getAttribLocation(const std::string& name) {
    // TODO: Return glGetAttribLocation(program_, name.c_str()).
    return -1;
}

GLuint ShaderProgram::compileShader(GLenum type, const std::string& source) {
    // TODO: Call glCreateShader(), glShaderSource(), glCompileShader(),
    //       check compile status, return shader id.
    return 0;
}

std::string ShaderProgram::readFile(const std::string& path) {
    // TODO: Open file, read into std::string, return.
    return {};
}

} // namespace player
