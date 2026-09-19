#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{
class VulkanDevice;

// CPU側は最大2 Frames in Flight。Present待機SemaphoreはSwapChain Image単位に保持します。
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

    bool Init(const VulkanDevice& device, uint32_t imageCount, uint32_t frameCount = 2);
    bool WaitForFrame() const;
    bool ResetFence() const;
    void AdvanceFrame();
    uint32_t GetCurrentFrameIndex() const { return m_CurrentFrame; }
    uint32_t GetFrameCount() const { return static_cast<uint32_t>(m_InFlightFences.size()); }
    void Shutdown();

    VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphores[m_CurrentFrame]; }
    VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const;
    VkFence GetInFlightFence() const { return m_InFlightFences[m_CurrentFrame]; }
    bool IsValid() const
    {
        return m_Device != VK_NULL_HANDLE &&
            m_ImageAvailableSemaphores.empty() == false &&
            m_RenderFinishedSemaphores.empty() == false &&
            m_InFlightFences.empty() == false;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    std::vector<VkFence> m_InFlightFences;
    uint32_t m_CurrentFrame = 0;
};
} // namespace Raven
