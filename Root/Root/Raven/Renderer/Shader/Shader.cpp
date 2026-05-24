#include "Shader.h"

#include "../RendererAPI.h"
#include "OpenGLShader.h"

namespace Raven
{

Ref<Shader> Shader::Create(const std::string& filepath)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLShader>(filepath);

    case RendererAPI::API::DirectX11:
        // return CreateRef<DirectX11Shader>(filepath);
        return nullptr;
    case RendererAPI::API::DirectX12:
        // return CreateRef<DirectX12Shader>(filepath);
        return nullptr;
    }

    return nullptr;
}

Ref<Shader> Shader::Create(const std::string& vertFilePath, const std::string& fragFilePath)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLShader>(vertFilePath, fragFilePath);

    case RendererAPI::API::DirectX11:
        // return CreateRef<DirectX11Shader>(vertFilePath, fragFilePath);
        return nullptr;
    case RendererAPI::API::DirectX12:
        // return CreateRef<DirectX12Shader>(vertFilePath, fragFilePath);
        return nullptr;
    }

    return nullptr;
}

}