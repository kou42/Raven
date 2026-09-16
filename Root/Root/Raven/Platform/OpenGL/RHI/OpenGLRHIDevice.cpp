#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"

#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"

namespace Raven
{

RHIBackend OpenGLRHIDevice::GetBackend() const
{
    return RHIBackend::OpenGL;
}

Ref<RHIBuffer> OpenGLRHIDevice::CreateBuffer(
    const RHIBufferSpecification& specification,
    const void* initialData)
{
    if (specification.Size == 0 || specification.Usage == RHIBufferUsage::None)
    {
        return nullptr;
    }

    return CreateRef<OpenGLRHIBuffer>(specification, initialData);
}

} // namespace Raven
