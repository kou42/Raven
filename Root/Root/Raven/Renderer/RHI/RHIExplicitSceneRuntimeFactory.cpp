#include "Raven/Renderer/RHI/RHIExplicitSceneRuntimeFactory.h"

#include "Raven/Renderer/RHI/IExplicitSceneRuntime.h"
#include "Raven/Platform/DirectX12/DX12SceneRuntime.h"
#include "Raven/Platform/Vulkan/VulkanSceneRuntime.h"

namespace Raven
{

Scope<IExplicitSceneRuntime> RHIExplicitSceneRuntimeFactory::Create(RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::DirectX12:
        return CreateScope<DX12SceneRuntime>();
    case RHIBackend::Vulkan:
        return CreateScope<VulkanSceneRuntime>();
    case RHIBackend::OpenGL:
    case RHIBackend::DirectX11:
    case RHIBackend::None:
    default:
        // OpenGLはRenderCommand + OpenGLSceneFrameLifecycleのLegacy本流を維持します。
        // 未対応Backendを別APIへ暗黙fallbackさせないため、明示的にnullptrを返します。
        return nullptr;
    }
}

} // namespace Raven
