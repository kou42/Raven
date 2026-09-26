#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"
#include "Raven/Renderer/RHI/RHILegacyBackendFactory.h"

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


Scope<RHIDevice> RHILegacyBackendFactory::CreateDevice(RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::OpenGL:
        return CreateScope<OpenGLRHIDevice>();
    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        // Explicit APIはScene RuntimeがContextとDeviceを同じ所有境界で管理します。
        // Legacy Deviceをここで別生成するとnative Deviceが二重化するため、明示的に未対応とします。
        return nullptr;
    }
}

Scope<RHICommandList> RHILegacyBackendFactory::CreateCommandList(RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::OpenGL:
        return CreateScope<OpenGLRHICommandList>();
    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        // DX12/VulkanはRHISceneCommandListへ記録するためLegacy CommandListを生成しません。
        return nullptr;
    }
}

} // namespace Raven
