#pragma once

#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

#include <vulkan/vulkan.h>
#include <vector>

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
        const RHIOffscreenRenderTargetSpecification& specification,
        VkRenderPass pickingRenderPass, uint32_t frameSlotCount);
    bool Begin(VkCommandBuffer commandBuffer, const float clearColor[4]);
    bool End(VkCommandBuffer commandBuffer);
    void Shutdown();

    const RHIOffscreenRenderTargetSpecification& GetSpecification() const override;
    const Ref<RHITexture>& GetColorAttachment() const override;
    bool IsValid() const override;

    bool SupportsEntityPicking() const override;
    bool RequestEntityPicking(uint32_t x, uint32_t y, std::uint64_t& outSequence) override;
    bool ConsumeEntityPickingResult(std::uint64_t& outSequence, std::int32_t& outEntityID) override;
    bool TakePendingEntityPickingRequest(RHIEntityPickingRequest& outRequest);
    bool BeginEntityPickingPass(VkCommandBuffer commands, const RHIEntityPickingRequest& request);
    bool EndEntityPickingPass(VkCommandBuffer commands, uint32_t slot,
        const RHIEntityPickingRequest& request);
    // Contextが該当slotのFence待機を完了した後だけ呼びます。
    bool ResolveEntityPickingReadback(uint32_t slot);
    VkDevice GetOwnerDevice() const;

private:
    bool InitEntityPicking(const VulkanDevice& device, VkRenderPass renderPass,
        const RHIOffscreenRenderTargetSpecification& specification, uint32_t frameSlotCount);
    struct PickingReadbackSlot
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        std::uint64_t Sequence = 0u;
    };
    VkImage m_PickingImage = VK_NULL_HANDLE;
    VkDeviceMemory m_PickingMemory = VK_NULL_HANDLE;
    VkImageView m_PickingView = VK_NULL_HANDLE;
    VkFramebuffer m_PickingFramebuffer = VK_NULL_HANDLE;
    VkRenderPass m_PickingRenderPass = VK_NULL_HANDLE;
    std::vector<PickingReadbackSlot> m_PickingReadbacks;
    RHIEntityPickingReadbackState m_PickingState;
    RHIEntityPickingRequest m_ActivePickingRequest{};
    bool m_PickingPassActive = false;
    bool m_ColorPassEnded = false;
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
