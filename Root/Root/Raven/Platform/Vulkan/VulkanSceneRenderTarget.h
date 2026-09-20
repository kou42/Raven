#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{
class VulkanSwapChain;

// SwapChainのColor Attachmentを所有しないScene描画用RenderPass/Framebufferです。
// ImageView/FramebufferはSwapChain再生成前に破棄し、再生成後にInitし直します。
class VulkanSceneRenderTarget
{
public:
    VulkanSceneRenderTarget() = default;
    ~VulkanSceneRenderTarget();

    VulkanSceneRenderTarget(const VulkanSceneRenderTarget&) = delete;
    VulkanSceneRenderTarget& operator=(const VulkanSceneRenderTarget&) = delete;

    bool Init(VkDevice device, const VulkanSwapChain& swapChain);
    bool Begin(VkCommandBuffer commandBuffer, uint32_t imageIndex,
        const VkClearColorValue& clearColor) const;
    void End(VkCommandBuffer commandBuffer) const;
    void Shutdown();

    VkRenderPass GetRenderPass() const { return m_RenderPass; }
    bool IsValid() const
    {
        return m_Device != VK_NULL_HANDLE &&
            m_RenderPass != VK_NULL_HANDLE && m_Framebuffers.empty() == false;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkExtent2D m_Extent{};
    std::vector<VkImageView> m_ImageViews;
    std::vector<VkFramebuffer> m_Framebuffers;
};
} // namespace Raven
