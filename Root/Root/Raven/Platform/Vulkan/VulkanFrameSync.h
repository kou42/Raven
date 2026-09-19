#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{
class VulkanDevice;

// CPU側は1 Frame in Flight。Present待機SemaphoreだけはSwapChain Image単位に保持します。
// Submit Fence完了だけではPresentation EngineによるSemaphore待機完了を保証できません。
class VulkanFrameSync
{
public:
    VulkanFrameSync() = default;
    ~VulkanFrameSync();
    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;
    VulkanFrameSync(VulkanFrameSync&&) = delete;
    VulkanFrameSync& operator=(VulkanFrameSync&&) = delete;

    bool Init(const VulkanDevice& device, uint32_t imageCount);
    bool WaitForFrame() const;
    bool ResetFence() const;
    void Shutdown();

    VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphore; }
    VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const;
    VkFence GetInFlightFence() const { return m_InFlightFence; }
    bool IsValid() const
    {
        return m_Device != VK_NULL_HANDLE &&
            m_ImageAvailableSemaphore != VK_NULL_HANDLE &&
            m_RenderFinishedSemaphores.empty() == false &&
            m_InFlightFence != VK_NULL_HANDLE;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    VkFence m_InFlightFence = VK_NULL_HANDLE;
};
} // namespace Raven
