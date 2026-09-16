#include "Raven/Renderer/Buffer/VertexBuffer.h"

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Buffer/OpenGLVertexBuffer.h"

namespace Raven
{

Ref<VertexBuffer> VertexBuffer::Create(const float* vertices, uint32_t size)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLVertexBuffer>(vertices, size);

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

}