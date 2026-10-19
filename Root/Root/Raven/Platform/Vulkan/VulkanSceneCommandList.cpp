#include "VulkanSceneCommandList.h"

namespace Raven
{

bool VulkanSceneCommandList::BeginOffscreenRenderTarget(
    const Ref<RHIOffscreenRenderTarget>& renderTarget,
    const float clearColor[4])
{
    return m_Context.BeginOffscreenRenderTarget(renderTarget, clearColor);
}

bool VulkanSceneCommandList::EndOffscreenRenderTarget(
    const Ref<RHIOffscreenRenderTarget>& renderTarget)
{
    return m_Context.EndOffscreenRenderTarget(renderTarget);
}

} // namespace Raven
