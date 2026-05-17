#include "OpenGLShader.h"

#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>

namespace Raven
{

OpenGLShader::OpenGLShader(const std::string& filepath)
{
    auto [vertexSrc, fragmentSrc] = ParseShaderFile(filepath);
    m_RendererID = CreateProgram(vertexSrc, fragmentSrc);
}

OpenGLShader::~OpenGLShader()
{
    glDeleteProgram(m_RendererID);
}

void OpenGLShader::Bind() const
{
    glUseProgram(m_RendererID);
}

void OpenGLShader::Unbind() const
{
    glUseProgram(0);
}

std::string OpenGLShader::ReadFile(const std::string& filepath)
{
    std::ifstream file(filepath);

    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::pair<std::string, std::string> OpenGLShader::ParseShaderFile(const std::string& filepath)
{
    std::string source = ReadFile(filepath);

    const char* vertexToken = "#type vertex";
    const char* fragmentToken = "#type fragment";

    size_t vertexPos = source.find(vertexToken);
    size_t fragmentPos = source.find(fragmentToken);

    if (vertexPos == std::string::npos || fragmentPos == std::string::npos)
    {
        std::cerr << "Shader file must contain #type vertex and #type fragment" << std::endl;
        return {};
    }

    size_t vertexStart = source.find('\n', vertexPos) + 1;
    size_t fragmentStart = source.find('\n', fragmentPos) + 1;

    std::string vertexSource =
        source.substr(vertexStart, fragmentPos - vertexStart);

    std::string fragmentSource =
        source.substr(fragmentStart);

    return { vertexSource, fragmentSource };
}

unsigned int OpenGLShader::CompileShader(unsigned int type, const std::string& source)
{
    unsigned int shader = glCreateShader(type);

    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        int length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        std::string message(length, ' ');
        glGetShaderInfoLog(shader, length, &length, message.data());

        std::cerr << "Shader compile error:\n" << message << std::endl;

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

unsigned int OpenGLShader::CreateProgram(
    const std::string& vertexSource,
    const std::string& fragmentSource)
{
    unsigned int program = glCreateProgram();

    unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);

    unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (!success)
    {
        int length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        std::string message(length, ' ');
        glGetProgramInfoLog(program, length, &length, message.data());

        std::cerr << "Shader link error:\n" << message << std::endl;

        glDeleteProgram(program);
        return 0;
    }

    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void OpenGLShader::SetInt(const std::string& name, int value)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform1i(location, value);
}

void OpenGLShader::SetFloat(const std::string& name, float value)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform1f(location, value);
}

void OpenGLShader::SetFloat3(const std::string& name, float x, float y, float z)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform3f(location, x, y, z);
}

void OpenGLShader::SetFloat4(const std::string& name, float x, float y, float z, float w)
{
    GLint location = glGetUniformLocation(m_RendererID, name.c_str());
    glUniform4f(location, x, y, z, w);
}

}