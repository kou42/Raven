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

}