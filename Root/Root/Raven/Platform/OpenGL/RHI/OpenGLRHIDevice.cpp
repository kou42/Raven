#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIBuffer.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHITexture.h"

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

Ref<RHITexture> OpenGLRHIDevice::CreateTexture(
    const RHITextureSpecification& specification,
    const void* initialData,
    std::size_t initialDataSize)
{
    if (specification.Width == 0
        || specification.Height == 0
        || specification.Format == RHITextureFormat::None
        || specification.Usage == RHITextureUsage::None)
    {
        return nullptr;
    }

    // Resource生成をDeviceへ集約することで、上位層はOpenGLTexture objectを直接生成せず、
    // 将来D3D12/Vulkan backendへ同じSpecificationを渡せる境界を維持します。
    return CreateRef<OpenGLRHITexture>(specification, initialData, initialDataSize);
}

} // namespace Raven
