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
        const bool samePhysicalDevice = m_PhysicalDevice == physicalDevice.Handle;
        const bool sameGraphicsQueueFamily =
            m_GraphicsQueueFamilyIndex == physicalDevice.GraphicsQueueFamilyIndex;
        const bool hasGraphicsQueue = m_GraphicsQueue != VK_NULL_HANDLE;

        if (samePhysicalDevice == true &&
            sameGraphicsQueueFamily == true &&
            hasGraphicsQueue == true)
        {
            return true;
        }

        // 別GPU/Queue Familyで再初期化する場合は、既存Deviceを安全に破棄して作り直します。
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
        // 正常終了ではGPU処理完了を待ちます。Device lost等でWaitIdleが失敗した場合も、
        // Shutdownを停止してHandleを残さないため、そのままDevice破棄処理へ進みます。
        WaitIdle();
        vkDestroyDevice(m_Device, nullptr);
    }

    // 親Deviceを破棄した後で、Device由来のQueueと選択情報をまとめて無効化します。
    m_Device = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_GraphicsQueueFamilyIndex = VulkanPhysicalDevice::InvalidQueueFamilyIndex;
    m_PhysicalDevice = VK_NULL_HANDLE;
}

} // namespace Raven
