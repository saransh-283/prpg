#pragma once

#include <string>
#include <glad/glad.h>

// Load a text file into a string
std::string LoadShader(const char* path);

// Compile a shader from source text. Returns shader handle or 0 on failure.
GLuint CompileShader(GLenum type, const char* src);

// Load, compile and link a shader program from two file paths. Returns true on success and sets program.
bool LoadShaderProgram(const std::string& vertexPath, const std::string& fragmentPath, GLuint& program);