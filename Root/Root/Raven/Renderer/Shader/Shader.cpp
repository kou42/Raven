#include "Raven/Renderer/Shader/Shader.h"

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Shader/OpenGLShader.h"

#include <iostream>

namespace Raven
{

Ref<Shader> Shader::Create(const std::string& filepath)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLShader>(filepath);

    case RHIBackend::DirectX11:
        // return CreateRef<DirectX11Shader>(filepath);
        return nullptr;
    case RHIBackend::DirectX12:
        // return CreateRef<DirectX12Shader>(filepath);
        return nullptr;
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

Ref<Shader> Shader::Create(const std::string& vertFilePath, const std::string& fragFilePath)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLShader>(vertFilePath, fragFilePath);

    case RHIBackend::DirectX11:
        // return CreateRef<DirectX11Shader>(vertFilePath, fragFilePath);
        return nullptr;
    case RHIBackend::DirectX12:
        // return CreateRef<DirectX12Shader>(vertFilePath, fragFilePath);
        return nullptr;
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
{
    if (shader == nullptr)
    {
        std::cerr << "ShaderLibrary::Add failed. Shader is null: " << name << std::endl;
        return;
    }

    if (Exists(name))
    {
        std::cerr << "Shader already exists: " << name << std::endl;
        return;
    }

    m_Shaders[name] = shader;
}

Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
{
    Ref<Shader> shader = Shader::Create(filepath);
    Add(name, shader);
    return shader;
}

Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& vertexFilePath, const std::string& fragmentFilePath)
{
    Ref<Shader> shader = Shader::Create(vertexFilePath, fragmentFilePath);
    Add(name, shader);
    return shader;
}

Ref<Shader> ShaderLibrary::Get(const std::string& name)
{
    auto it = m_Shaders.find(name);
    if (it == m_Shaders.end())
    {
        std::cerr << "Shader not found: " << name << std::endl;
        return nullptr;
    }

    return it->second;
}

bool ShaderLibrary::Exists(const std::string& name) const
{
    return m_Shaders.find(name) != m_Shaders.end();
}

}