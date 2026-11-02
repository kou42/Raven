#include "Raven/Renderer/RHI/IExplicitSceneRuntime.h"

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

namespace Raven
{

bool IExplicitSceneRuntime::CreateAuxiliaryUIWindow(Window& window)
{
    (void)window;
    return false;
}

void IExplicitSceneRuntime::DestroyAuxiliaryUIWindow(Window& window)
{
    (void)window;
}

bool IExplicitSceneRuntime::RenderAuxiliaryUIWindow(Window& window, const UIDrawList& drawList)
{
    (void)window;
    (void)drawList;
    return false;
}

bool IExplicitSceneRuntime::SupportsOffscreenSceneRendering() const
{
    return false;
}

Ref<RHIOffscreenRenderTarget>
IExplicitSceneRuntime::CreateOffscreenRenderTarget(
    const RHIOffscreenRenderTargetSpecification& specification)
{
    (void)specification;
    return nullptr;
}

bool IExplicitSceneRuntime::CaptureMainSceneFrame()
{
    return false;
}

bool IExplicitSceneRuntime::CaptureOffscreenSceneFrame(
    const Ref<RHIOffscreenRenderTarget>& renderTarget)
{
    (void)renderTarget;
    return false;
}

} // namespace Raven
