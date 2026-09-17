#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

#include "VulkanPhysicalDevice.h"

namespace Raven
{

// VkDeviceとGraphics QueueのLifetimeを管理するLogical Deviceラッパーです。
// Queue Family選択はVulkanPhysicalDevice側で行い、このクラスは選択済み情報から
// Device/Queueを生成する責務だけを持たせます。
class VulkanDevice
{
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    bool Init(const VulkanPhysicalDevice::DeviceInfo& physicalDevice);
    bool WaitIdle() const;
    void Shutdown();

    bool IsValid() const
    {
        return m_PhysicalDevice != VK_NULL_HANDLE &&
               m_Device != VK_NULL_HANDLE &&
               m_GraphicsQueue != VK_NULL_HANDLE &&
               m_GraphicsQueueFamilyIndex != VulkanPhysicalDevice::InvalidQueueFamilyIndex;
    }

    VkPhysicalDevice GetPhysicalDeviceHandle() const { return m_PhysicalDevice; }
    VkDevice GetHandle() const { return m_Device; }
    VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
    uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = VulkanPhysicalDevice::InvalidQueueFamilyIndex;
};

} // namespace Raven
