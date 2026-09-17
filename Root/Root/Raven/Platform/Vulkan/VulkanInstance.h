#pragma once

#include <vulkan/vulkan.h>

#include "VulkanPhysicalDevice.h"

namespace Raven
{

// Vulkan APIを利用する最初の入口となるVkInstanceの所有クラスです。
// VkInstanceの生成と破棄を同じクラスへ閉じ込めることで、後続のPhysicalDeviceや
// Device実装からInstanceのLifetimeを明確に参照できるようにします。
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
    bool IsValid() const { return m_Instance != VK_NULL_HANDLE; }

    const VulkanPhysicalDevice& GetPhysicalDevices() const { return m_PhysicalDevices; }

private:
    VkInstance m_Instance = VK_NULL_HANDLE;
    VulkanPhysicalDevice m_PhysicalDevices;
};

} // namespace Raven
