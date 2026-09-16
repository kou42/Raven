#include "Raven/Renderer/Buffer/IndexBuffer.h"

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/Buffer/OpenGLIndexBuffer.h"

namespace Raven
{

Ref<IndexBuffer> IndexBuffer::Create(const uint32_t* indices, uint32_t count)
{
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLIndexBuffer>(indices, count);

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

}