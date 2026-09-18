#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{

class VulkanDevice;

// 1 Frame分のImage取得・描画完了を同期するSemaphore/Fenceを所有します。
// FenceはSignaledで生成し、最初のFrameでも待機が即完了するようにします。
class VulkanFrameSync
{
public:
    VulkanFrameSync() = default;
    ~VulkanFrameSync();

    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;
    VulkanFrameSync(VulkanFrameSync&&) = delete;
    VulkanFrameSync& operator=(VulkanFrameSync&&) = delete;

    bool Init(const VulkanDevice& device);
    bool WaitForFrame() const;
    bool ResetFence() const;
    void Shutdown();

    VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphore; }
    VkSemaphore GetRenderFinishedSemaphore() const { return m_RenderFinishedSemaphore; }
    VkFence GetInFlightFence() const { return m_InFlightFence; }
    bool IsValid() const
    {
        return m_Device != VK_NULL_HANDLE &&
               m_ImageAvailableSemaphore != VK_NULL_HANDLE &&
               m_RenderFinishedSemaphore != VK_NULL_HANDLE &&
               m_InFlightFence != VK_NULL_HANDLE;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;
};

} // namespace Raven
