#include "Raven/Renderer/RHI/RHILegacyBackendFactory.h"

#include "Raven/Platform/OpenGL/RHI/OpenGLRHICommandList.h"
#include "Raven/Platform/OpenGL/RHI/OpenGLRHIDevice.h"

namespace Raven
{

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
