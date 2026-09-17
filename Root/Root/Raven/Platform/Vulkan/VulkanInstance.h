#pragma once

#include <vulkan/vulkan.h>

#include "VulkanDevice.h"
#include "VulkanPhysicalDevice.h"

namespace Raven
{

// Vulkan APIを利用する最初の入口となるVkInstanceの所有クラスです。
// VkInstance -> PhysicalDevice -> Logical DeviceのLifetime順序をこのクラスで束ね、
// 後続のSurface / SwapChain / Command実装が有効なDeviceを参照できるようにします。
class VulkanInstance
{
public:
    VulkanInstance() = default;
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&&) = delete;
    VulkanInstance& operator=(VulkanInstance&&) = delete;

    bool Init();
    void Shutdown();

    VkInstance GetHandle() const { return m_Instance; }
    bool IsValid() const
    {
        return m_Instance != VK_NULL_HANDLE && m_Device.IsValid() == true;
    }

    const VulkanPhysicalDevice& GetPhysicalDevices() const { return m_PhysicalDevices; }
    const VulkanPhysicalDevice::DeviceInfo* GetGraphicsDevice() const
    {
        return m_PhysicalDevices.FindFirstGraphicsDevice();
    }

    VulkanDevice& GetDevice() { return m_Device; }
    const VulkanDevice& GetDevice() const { return m_Device; }

private:
    VkInstance m_Instance = VK_NULL_HANDLE;
    VulkanPhysicalDevice m_PhysicalDevices;
    VulkanDevice m_Device;
};

} // namespace Raven
