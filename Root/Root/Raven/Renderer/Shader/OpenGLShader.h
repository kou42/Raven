#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Core/Base.h"

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
    OpenGLShader(const std::string& vertexFilePath, const std::string& fragFilePath);
    ~OpenGLShader();

    virtual void Bind() const override;
    virtual void Unbind() const override;

    virtual void SetInt(const std::string& name, int value) override;
    virtual void SetFloat(const std::string& name, float value) override;
    virtual void SetFloat3(const std::string& name, float x, float y, float z) override;
    virtual void SetFloat4(const std::string& name, float x, float y, float z, float w) override;
    virtual void SetVec2(const std::string& name, const math::Vec2& vec2) override;
    virtual void SetVec3(const std::string& name, const math::Vec3& vec3) override;
    virtual void SetVec4(const std::string& name, const math::Vec4& vec4) override;
    virtual void SetMat4(const std::string& name, const math::Mat4& mat4) override;

private:

    std::string ReadFile(const std::string& filepath);
    std::unordered_map<GLuint, std::string> PreProcess(const std::string& source);
    std::unordered_map<GLuint, std::string> PreProcess(const std::string& vertexFilePath, const std::string& fragFilePath);
    std::pair<std::string, std::string> ParseShaderFile(const std::string& filepath);
    void Compile(const std::unordered_map<GLuint, std::string>& shaderSources);
    unsigned int CompileShader(unsigned int type, const std::string& source);
    unsigned int CreateProgram(const std::string& vertexSource, const std::string& fragmentSource);

private:
    unsigned int m_RendererID = 0;
};

}