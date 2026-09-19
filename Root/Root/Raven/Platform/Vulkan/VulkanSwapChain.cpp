#include "VulkanSwapChain.h"

#include "VulkanDevice.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>
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

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
{
    if (vsync == false)
    {
        for (VkPresentModeKHR mode : modes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { return mode; }
        }
        for (VkPresentModeKHR mode : modes)
        {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) { return mode; }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    return {
        std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}
} // namespace

VulkanSwapChain::~VulkanSwapChain() { Shutdown(); }

bool VulkanSwapChain::Init(const VulkanDevice& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync)
{
    Shutdown();
    if (device.IsValid() == false || surface == VK_NULL_HANDLE || width == 0 || height == 0) { return false; }

    m_PhysicalDevice = device.GetPhysicalDeviceHandle();
    m_Device = device.GetHandle();
    m_Surface = surface;
    m_GraphicsQueueFamilyIndex = device.GetGraphicsQueueFamilyIndex();
    return Create(width, height, vsync, VK_NULL_HANDLE);
}

bool VulkanSwapChain::Recreate(uint32_t width, uint32_t height, bool vsync)
{
    if (m_Device == VK_NULL_HANDLE || m_PhysicalDevice == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE) { return false; }
    if (width == 0 || height == 0) { return false; }
    if (vkDeviceWaitIdle(m_Device) != VK_SUCCESS) { return false; }

    const VkSwapchainKHR oldSwapChain = m_SwapChain;

    m_SwapChain = VK_NULL_HANDLE;
    m_Images.clear();
    m_ImageLayouts.clear();

    if (Create(width, height, vsync, oldSwapChain) == false)
    {
        // vkCreateSwapchainKHRが成功した時点でoldSwapchainはretire済みの場合があります。
        // 旧SwapChainを描画可能と仮定せず、失敗後は明示的な再生成だけを許可します。
        DestroySwapChain(oldSwapChain);
        m_ImageFormat = VK_FORMAT_UNDEFINED;
        m_Extent = {};
        return false;
    }

    DestroySwapChain(oldSwapChain);
    return true;
}

bool VulkanSwapChain::Create(uint32_t width, uint32_t height, bool vsync, VkSwapchainKHR oldSwapChain)
{
    VkBool32 presentSupported = VK_FALSE;
    VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
        m_PhysicalDevice, m_GraphicsQueueFamilyIndex, m_Surface, &presentSupported);
    if (result != VK_SUCCESS || presentSupported == VK_FALSE) { return false; }

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);
    if (result != VK_SUCCESS ||
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0 ||
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) { return false; }

    uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr) != VK_SUCCESS ||
        formatCount == 0) { return false; }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data()) != VK_SUCCESS) { return false; }
    formats.resize(formatCount);

    uint32_t modeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &modeCount, nullptr) != VK_SUCCESS ||
        modeCount == 0) { return false; }
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &modeCount, modes.data()) != VK_SUCCESS) { return false; }
    modes.resize(modeCount);

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkExtent2D extent = ChooseExtent(capabilities, width, height);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) { imageCount = capabilities.maxImageCount; }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    // OPAQUEが非対応のSurfaceでは対応するCompositeAlphaを選びます。
    const VkCompositeAlphaFlagBitsKHR alphaCandidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for (VkCompositeAlphaFlagBitsKHR candidate : alphaCandidates)
    {
        if ((capabilities.supportedCompositeAlpha & candidate) != 0)
        {
            createInfo.compositeAlpha = candidate;
            break;
        }
    }
    if (createInfo.compositeAlpha == 0) { return false; }
    createInfo.presentMode = ChoosePresentMode(modes, vsync);
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapChain;

    VkSwapchainKHR newSwapChain = VK_NULL_HANDLE;
    result = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &newSwapChain);
    if (result != VK_SUCCESS) { return false; }

    uint32_t actualImageCount = 0;
    result = vkGetSwapchainImagesKHR(m_Device, newSwapChain, &actualImageCount, nullptr);
    if (result != VK_SUCCESS || actualImageCount == 0)
    {
        DestroySwapChain(newSwapChain);
        return false;
    }

    std::vector<VkImage> images(actualImageCount, VK_NULL_HANDLE);
    result = vkGetSwapchainImagesKHR(m_Device, newSwapChain, &actualImageCount, images.data());
    if (result != VK_SUCCESS)
    {
        DestroySwapChain(newSwapChain);
        return false;
    }
    images.resize(actualImageCount);

    m_SwapChain = newSwapChain;
    m_ImageFormat = surfaceFormat.format;
    m_Extent = extent;
    m_Images = std::move(images);
    m_ImageLayouts.assign(m_Images.size(), VK_IMAGE_LAYOUT_UNDEFINED);

    std::cout << "Vulkan SwapChain created/recreated : " << m_Extent.width << " x " << m_Extent.height << '\n';
    return true;
}

VkImageLayout VulkanSwapChain::GetImageLayout(uint32_t imageIndex) const
{
    if (imageIndex >= m_ImageLayouts.size()) { return VK_IMAGE_LAYOUT_UNDEFINED; }
    return m_ImageLayouts[imageIndex];
}

void VulkanSwapChain::SetImageLayout(uint32_t imageIndex, VkImageLayout layout)
{
    if (imageIndex < m_ImageLayouts.size()) { m_ImageLayouts[imageIndex] = layout; }
}

void VulkanSwapChain::DestroySwapChain(VkSwapchainKHR swapChain)
{
    if (swapChain != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE) { vkDestroySwapchainKHR(m_Device, swapChain, nullptr); }
}

void VulkanSwapChain::Shutdown()
{
    m_Images.clear();
    m_ImageLayouts.clear();
    DestroySwapChain(m_SwapChain);
    m_SwapChain = VK_NULL_HANDLE;
    m_Surface = VK_NULL_HANDLE;
    m_Device = VK_NULL_HANDLE;
    m_PhysicalDevice = VK_NULL_HANDLE;
    m_GraphicsQueueFamilyIndex = UINT32_MAX;
    m_ImageFormat = VK_FORMAT_UNDEFINED;
    m_Extent = {};
}

} // namespace Raven
