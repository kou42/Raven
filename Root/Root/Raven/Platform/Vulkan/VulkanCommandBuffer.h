#pragma once

#include <vulkan/vulkan.h>

namespace Raven
{

class VulkanDevice;

// Graphics Queue用CommandPoolとPrimary CommandBufferを所有します。
// ⑪では「記録可能なCommandBuffer」を作る責務に限定し、Fence/Semaphoreは次段階で分離します。
class VulkanCommandBuffer
{
public:
    VulkanCommandBuffer() = default;
    ~VulkanCommandBuffer();

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&) = delete;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) = delete;

    bool Init(const VulkanDevice& device);
    bool Reset();
    bool Begin(VkCommandBufferUsageFlags usageFlags = 0);
    bool End();
    void Shutdown();

    VkCommandPool GetCommandPool() const { return m_CommandPool; }
    VkCommandBuffer GetHandle() const { return m_CommandBuffer; }
    bool IsValid() const
    {
        return m_Device != VK_NULL_HANDLE &&
               m_CommandPool != VK_NULL_HANDLE &&
               m_CommandBuffer != VK_NULL_HANDLE;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
};

} // namespace Raven
