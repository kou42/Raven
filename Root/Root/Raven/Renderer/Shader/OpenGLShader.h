#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "Shader.h"
#include "../../Core/Base.h"

#include <glad/glad.h>

namespace Raven
{
    
class OpenGLShader : public Shader
{

public:

    enum ShaderType
    {
        SHADER_TYPE_VERTEXT,
        SHADER_TYPE_FLAGMENT,
        SHADER_TYPE_MAX,
    };

public:
    OpenGLShader(const std::string& filepath);
    ~OpenGLShader();

    void Bind() const;
    void Unbind() const;

    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetFloat3(const std::string& name, float x, float y, float z);
    void SetFloat4(const std::string& name, float x, float y, float z, float w);

private:

    // Ç±Ç±ÇÁï”Ç‡äÓíÍÇ≈êÈåæÇµÇΩÇŸÇ§Ç™Ç¢Ç¢ÇÃÇ©
    std::string ReadFile(const std::string& filepath);
    std::unordered_map<GLuint, std::string> PreProcess(const std::string& source);
    std::pair<std::string, std::string> ParseShaderFile(const std::string& filepath);
    void Compile(const std::unordered_map<GLuint, std::string>& shaderSources);
    unsigned int CompileShader(unsigned int type, const std::string& source);
    unsigned int CreateProgram(const std::string& vertexSource, const std::string& fragmentSource);

private:
    unsigned int m_RendererID = 0;
};

}