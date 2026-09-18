#include "VulkanSwapChain.h"

#include "VulkanDevice.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

namespace Raven
{
namespace
{

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& presentModes,
    bool vsync)
{
    if (vsync == false)
    {
        for (const VkPresentModeKHR presentMode : presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return presentMode;
            }
        }

        for (const VkPresentModeKHR presentMode : presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                return presentMode;
            }
        }
    }

    // FIFOはVulkan仕様上必ず利用可能で、VSync相当の安全なFallbackになります。
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32_t width,
    uint32_t height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{};
    extent.width = std::clamp(
        width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    extent.height = std::clamp(
        height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);
    return extent;
}

} // namespace

VulkanSwapChain::~VulkanSwapChain()
{
    Shutdown();
}

bool VulkanSwapChain::Init(
    const VulkanDevice& device,
    VkSurfaceKHR surface,
    uint32_t width,
    uint32_t height,
    bool vsync)
{
    Shutdown();

    if (device.IsValid() == false || surface == VK_NULL_HANDLE)
    {
        std::cout << "Cannot create Vulkan SwapChain because Device or Surface is invalid.\n";
        return false;
    }

    VkBool32 presentSupported = VK_FALSE;
    VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
        device.GetPhysicalDeviceHandle(),
        device.GetGraphicsQueueFamilyIndex(),
        surface,
        &presentSupported);
    if (result != VK_SUCCESS || presentSupported == VK_FALSE)
    {
        // 現在の学習実装ではGraphics/Presentを同一Queueへ集約します。
        // GPUによってQueue Familyが分離される場合は、後続でPresent Queueを独立管理します。
        std::cout << "Selected Vulkan Graphics Queue cannot present to this Surface.\n";
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device.GetPhysicalDeviceHandle(),
        surface,
        &capabilities);
    if (result != VK_SUCCESS)
    {
        std::cout << "Failed to query Vulkan Surface capabilities. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        device.GetPhysicalDeviceHandle(),
        surface,
        &formatCount,
        nullptr);
    if (result != VK_SUCCESS || formatCount == 0)
    {
        std::cout << "No Vulkan Surface format is available.\n";
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        device.GetPhysicalDeviceHandle(),
        surface,
        &formatCount,
        formats.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }
    formats.resize(formatCount);

    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(
        device.GetPhysicalDeviceHandle(),
        surface,
        &presentModeCount,
        nullptr);
    if (result != VK_SUCCESS || presentModeCount == 0)
    {
        std::cout << "No Vulkan Present Mode is available.\n";
        return false;
    }

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(
        device.GetPhysicalDeviceHandle(),
        surface,
        &presentModeCount,
        presentModes.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }
    presentModes.resize(presentModeCount);

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = ChoosePresentMode(presentModes, vsync);
    const VkExtent2D extent = ChooseExtent(capabilities, width, height);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    result = vkCreateSwapchainKHR(
        device.GetHandle(),
        &createInfo,
        nullptr,
        &m_SwapChain);
    if (result != VK_SUCCESS)
    {
        m_SwapChain = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan VkSwapchainKHR. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    m_Device = device.GetHandle();
    m_ImageFormat = surfaceFormat.format;
    m_Extent = extent;

    uint32_t swapChainImageCount = 0;
    result = vkGetSwapchainImagesKHR(
        m_Device,
        m_SwapChain,
        &swapChainImageCount,
        nullptr);
    if (result != VK_SUCCESS || swapChainImageCount == 0)
    {
        Shutdown();
        return false;
    }

    m_Images.resize(swapChainImageCount, VK_NULL_HANDLE);
    result = vkGetSwapchainImagesKHR(
        m_Device,
        m_SwapChain,
        &swapChainImageCount,
        m_Images.data());
    if (result != VK_SUCCESS)
    {
        Shutdown();
        return false;
    }
    m_Images.resize(swapChainImageCount);

    std::cout << "Vulkan SwapChain created successfully.\n";
    std::cout << "  Images : " << m_Images.size() << '\n';
    std::cout << "  Extent : " << m_Extent.width << " x " << m_Extent.height << '\n';
    return true;
}

void VulkanSwapChain::Shutdown()
{
    m_Images.clear();

    if (m_SwapChain != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
    }

    m_SwapChain = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_ImageFormat = VK_FORMAT_UNDEFINED;
    m_Extent = {};
}

} // namespace Raven
