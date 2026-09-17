#include "VulkanDevice.h"

#include <iostream>

namespace Raven
{

VulkanDevice::~VulkanDevice()
{
    Shutdown();
}

bool VulkanDevice::Init(const VulkanPhysicalDevice::DeviceInfo& physicalDevice)
{
    if (m_Device != VK_NULL_HANDLE)
    {
        return true;
    }

    if (physicalDevice.Handle == VK_NULL_HANDLE || physicalDevice.HasGraphicsQueue() == false)
    {
        std::cout << "Cannot create Vulkan logical device because PhysicalDevice/Graphics Queue is invalid.\n";
        return false;
    }

    constexpr float graphicsQueuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = physicalDevice.GraphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &graphicsQueuePriority;

    VkPhysicalDeviceFeatures enabledFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &enabledFeatures;

    // SwapChain Extension等はSurface/SwapChain実装時に追加します。
    // 現段階ではGraphics Queueを取得できる最小Logical Deviceを生成します。
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    const VkResult result = vkCreateDevice(
        physicalDevice.Handle,
        &createInfo,
        nullptr,
        &m_Device);

    if (result != VK_SUCCESS)
    {
        m_Device = VK_NULL_HANDLE;
        m_PhysicalDevice = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_GraphicsQueueFamilyIndex = VulkanPhysicalDevice::InvalidQueueFamilyIndex;

        std::cout << "Failed to create Vulkan VkDevice. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    m_PhysicalDevice = physicalDevice.Handle;
    m_GraphicsQueueFamilyIndex = physicalDevice.GraphicsQueueFamilyIndex;
    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &m_GraphicsQueue);

    if (m_GraphicsQueue == VK_NULL_HANDLE)
    {
        std::cout << "Failed to get Vulkan Graphics Queue.\n";
        Shutdown();
        return false;
    }

    std::cout << "Vulkan VkDevice created successfully.\n";
    std::cout << "  Graphics Queue Family : " << m_GraphicsQueueFamilyIndex << '\n';
    return true;
}

void VulkanDevice::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // 今後Command Buffer等を所有する場合は、それらの利用完了を保証した後でDeviceを破棄します。
        vkDestroyDevice(m_Device, nullptr);
    }

    // 親Deviceを破棄した後で、Device由来のQueueと選択情報をまとめて無効化します。
    m_Device = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_GraphicsQueueFamilyIndex = VulkanPhysicalDevice::InvalidQueueFamilyIndex;
    m_PhysicalDevice = VK_NULL_HANDLE;
}

} // namespace Raven
