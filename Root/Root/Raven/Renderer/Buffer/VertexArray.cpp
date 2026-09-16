#include "Raven/Renderer/Buffer/VertexArray.h"

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Platform/OpenGL/OpenGLVertexArray.h"

namespace Raven
{

Ref<VertexArray> VertexArray::Create()
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLVertexArray>();

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

}