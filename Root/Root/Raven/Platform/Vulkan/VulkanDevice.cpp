#include "VulkanDevice.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace Raven
{

VulkanDevice::~VulkanDevice()
{
    Shutdown();
}

bool VulkanDevice::SupportsRequiredExtensions(
    const VulkanPhysicalDevice::DeviceInfo& physicalDevice) const
{
    uint32_t extensionCount = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(
        physicalDevice.Handle,
        nullptr,
        &extensionCount,
        nullptr);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    result = vkEnumerateDeviceExtensionProperties(
        physicalDevice.Handle,
        nullptr,
        &extensionCount,
        extensions.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }

    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        {
            return true;
        }
    }

    return false;
}

bool VulkanDevice::Init(const VulkanPhysicalDevice::DeviceInfo& physicalDevice)
{
    if (m_Device != VK_NULL_HANDLE)
    {
        const bool samePhysicalDevice = m_PhysicalDevice == physicalDevice.Handle;
        const bool sameGraphicsQueueFamily =
            m_GraphicsQueueFamilyIndex == physicalDevice.GraphicsQueueFamilyIndex;

        if (samePhysicalDevice == true &&
            sameGraphicsQueueFamily == true &&
            IsValid() == true)
        {
            return true;
        }

        Shutdown();
    }

    if (physicalDevice.Handle == VK_NULL_HANDLE)
    {
        std::cout << "Cannot create Vulkan logical device because PhysicalDevice is invalid.\n";
        return false;
    }

    if (physicalDevice.HasGraphicsQueue() == false)
    {
        std::cout << "Cannot create Vulkan logical device because Graphics Queue is unavailable.\n";
        return false;
    }

    if (SupportsRequiredExtensions(physicalDevice) == false)
    {
        std::cout << "Cannot create Vulkan logical device because VK_KHR_swapchain is unavailable.\n";
        return false;
    }

    const uint32_t queueFamilyCount =
        static_cast<uint32_t>(physicalDevice.QueueFamilies.size());
    if (physicalDevice.GraphicsQueueFamilyIndex >= queueFamilyCount)
    {
        std::cout << "Cannot create Vulkan logical device because Graphics Queue Family index is out of range.\n";
        return false;
    }

    const VkQueueFamilyProperties& graphicsQueueFamily =
        physicalDevice.QueueFamilies[physicalDevice.GraphicsQueueFamilyIndex];
    const bool hasQueue = graphicsQueueFamily.queueCount > 0;
    const bool supportsGraphics =
        (graphicsQueueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

    if (hasQueue == false)
    {
        std::cout << "Cannot create Vulkan logical device because selected Queue Family has no queues.\n";
        return false;
    }

    if (supportsGraphics == false)
    {
        std::cout << "Cannot create Vulkan logical device because selected Queue Family cannot execute graphics commands.\n";
        return false;
    }

    constexpr float graphicsQueuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = physicalDevice.GraphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &graphicsQueuePriority;

    VkPhysicalDeviceFeatures enabledFeatures{};

    const char* requiredExtensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &enabledFeatures;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = requiredExtensions;
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

    if (IsValid() == false)
    {
        std::cout << "Failed to initialize a complete Vulkan Graphics Device state.\n";
        Shutdown();
        return false;
    }

    std::cout << "Vulkan VkDevice created successfully.\n";
    std::cout << "  Graphics Queue Family : " << m_GraphicsQueueFamilyIndex << '\n';
    std::cout << "  VK_KHR_swapchain : enabled\n";
    return true;
}

bool VulkanDevice::WaitIdle() const
{
    if (m_Device == VK_NULL_HANDLE)
    {
        return true;
    }

    const VkResult result = vkDeviceWaitIdle(m_Device);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to wait for Vulkan device idle. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    return true;
}

void VulkanDevice::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        WaitIdle();
        vkDestroyDevice(m_Device, nullptr);
    }

    m_Device = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_GraphicsQueueFamilyIndex = VulkanPhysicalDevice::InvalidQueueFamilyIndex;
    m_PhysicalDevice = VK_NULL_HANDLE;
}

} // namespace Raven
