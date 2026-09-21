#include "Raven/UI/Rendering/UIRenderer.h"

#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/UI/Rendering/OpenGLUIRenderer.h"

namespace Raven
{

Scope<UIRenderer> UIRenderer::Create()
{
    return Create(GetRHIBackend());
}

Scope<UIRenderer> UIRenderer::Create(RHIBackend backend)
{
    switch (backend)
    {
    case RHIBackend::OpenGL:
        return CreateScope<OpenGLUIRenderer>();

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

} // namespace Raven
