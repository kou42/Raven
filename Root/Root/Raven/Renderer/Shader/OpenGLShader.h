#pragma once

#include <string>
#include <memory>

#include "Shader.h"
#include "../../Core/Base.h"

namespace Raven
{
    
class OpenGLShader : public Shader
{
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

    // ‚±‚±‚ç•Ó‚àŠî’ê‚ÅéŒ¾‚µ‚½‚Ù‚¤‚ª‚¢‚¢‚Ì‚©
    std::string ReadFile(const std::string& filepath);
    std::pair<std::string, std::string> ParseShaderFile(const std::string& filepath);
    unsigned int CompileShader(unsigned int type, const std::string& source);
    unsigned int CreateProgram(const std::string& vertexSource, const std::string& fragmentSource);

private:
    unsigned int m_RendererID = 0;
};

}