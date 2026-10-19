#include "VulkanSceneRHIDevice.h"

#include "VulkanOffscreenRenderTarget.h"

namespace Raven
{

Ref<RHIOffscreenRenderTarget>
VulkanSceneRHIDevice::CreateOffscreenRenderTarget(
    const RHIOffscreenRenderTargetSpecification& specification)
{
    if (m_Context.GetDevice().IsValid() == false ||
        m_Context.GetActiveCommandBuffer() != VK_NULL_HANDLE)
    {
        return nullptr;
    }

    auto renderTarget = CreateRef<VulkanOffscreenRenderTarget>();
    if (renderTarget->Init(m_Context.GetDevice(), m_Context.GetRenderPass(),
        m_Context.GetColorFormat(), specification) == false)
    {
        return nullptr;
    }
    const auto color = std::dynamic_pointer_cast<VulkanSceneRHITexture>(
        renderTarget->GetColorAttachment());
    if (color == nullptr)
    {
        return nullptr;
    }

    // Texture descriptorとFramebuffer借用RenderPassの両方をContextの終了順へ登録します。
    m_Context.RegisterTexture(color);
    m_Context.RegisterOffscreenRenderTarget(renderTarget);
    return renderTarget;
}

} // namespace Raven
