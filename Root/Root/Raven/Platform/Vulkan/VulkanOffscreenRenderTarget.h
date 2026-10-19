#pragma once

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

#include <vulkan/vulkan.h>

namespace Raven
{

class VulkanDevice;

// Vulkan Scene Contextと同じDevice／RenderPass互換形式を使うoff-screen描画先です。
// Color ImageはAttachment書き込み後にRaven UIのCombined Image Samplerから参照します。
class VulkanOffscreenRenderTarget final : public RHIOffscreenRenderTarget
{
public:
    ~VulkanOffscreenRenderTarget() override;

    bool Init(const VulkanDevice& device, VkRenderPass renderPass,
        VkFormat colorFormat,
        const RHIOffscreenRenderTargetSpecification& specification);
    bool Begin(VkCommandBuffer commandBuffer, const float clearColor[4]);
    bool End(VkCommandBuffer commandBuffer);
    void Shutdown();

    const RHIOffscreenRenderTargetSpecification& GetSpecification() const override;
    const Ref<RHITexture>& GetColorAttachment() const override;
    bool IsValid() const override;

    VkDevice GetOwnerDevice() const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    VkImage m_DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
    VkImageView m_DepthView = VK_NULL_HANDLE;
    VkImageLayout m_ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    RHIOffscreenRenderTargetSpecification m_Specification{};
    Ref<RHITexture> m_ColorAttachment;
    bool m_RenderPassActive = false;
};

} // namespace Raven
