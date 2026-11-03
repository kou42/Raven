#include "VulkanSwapChain.h"

#include "VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace Raven
{
namespace
{
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

bool VulkanSwapChain::SelectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats,
    bool mutableFormatSupported, VkSurfaceFormatKHR& surfaceFormat, VkFormat& colorAttachmentFormat,
    VkFormat preferredSurfaceFormat)
{
    surfaceFormat = {};
    colorAttachmentFormat = VK_FORMAT_UNDEFINED;
    // 現行Scene/UIの値は表示用SDR値で、GL/DX12もUNORM上でBlendしています。
    // sRGB Attachmentの自動encodeを重ねず、全Surfaceで同じUNORM描画契約に揃えます。
    // 不具合時はVulkanだけB8G8R8A8_SRGBを描画先にも使い、Fragment出力をlinear値として
    // ハードウェアがsRGB encodeしていました。例えば表示値0.5が約0.735（8bitで約188）に
    // なり、UNORMへ約128を保存するGL/DX12よりSceneとUIの両方が明るく見えていました。
    // 同じShader/頂点色でもAttachment形式で結果が変わるため、Shader側のTint補正では直しません。
    // colorSpaceのSRGB_NONLINEARはPresent先の解釈で、Attachmentの自動encodeとは別の指定です。
    // これを維持したままUNORMへ表示用の値を書き、Presentまで余分なencodeを挟まない契約にします。
    // これは現行Backend間の互換性のための方針です。将来linear lighting/HDRへ移行する場合は、
    // Scene出力・UI合成・最終出力の変換を全Backendで一緒に設計し直す必要があります。
    const VkFormat candidates[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
    for (VkFormat candidate : candidates)
    {
        if (preferredSurfaceFormat != VK_FORMAT_UNDEFINED && preferredSurfaceFormat != candidate)
        {
            continue;
        }
        const bool srgb = candidate == VK_FORMAT_B8G8R8A8_SRGB || candidate == VK_FORMAT_R8G8B8A8_SRGB;
        if (srgb == true && mutableFormatSupported == false)
        {
            continue;
        }
        for (const auto& format : formats)
        {
            if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (format.format == candidate || (formats.size() == 1u && format.format == VK_FORMAT_UNDEFINED)))
            {
                surfaceFormat = {candidate, format.colorSpace};
                colorAttachmentFormat = candidate == VK_FORMAT_B8G8R8A8_SRGB ? VK_FORMAT_B8G8R8A8_UNORM :
                    candidate == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : candidate;
                return true;
            }
        }
    }
    // sRGBしかなくmutable非対応の場合も、誤った色で成功扱いにせず初期化失敗を返します。
    // HDR/10bitは別の色出力契約が必要なので、先頭候補を無条件には採用しません。
    return false;
}

VkClearColorValue VulkanSwapChain::PrepareTransferClearValue(VkFormat nativeFormat, const VkClearColorValue& displayColor)
{
    VkClearColorValue value = displayColor;
    if (nativeFormat == VK_FORMAT_B8G8R8A8_SRGB || nativeFormat == VK_FORMAT_R8G8B8A8_SRGB)
    {
        // vkCmdClearColorImageはsRGB Imageへのfloat値をlinearとして扱います。
        // 表示用SDR値を一度だけdecodeし、native encode後に元のbit値へ戻します。Alphaは変換しません。
        // RenderPassのUNORM Viewとは異なり、このAPIはnative Image形式を直接使います。
        // 例えば0.5を約0.214へ戻して渡すとnative側のencode後に0.5となります。
        // この補正をUNORM AttachmentのclearやUI Shaderにも適用すると、そちらはencodeされないため
        // 逆に暗くなります。raw transfer clear専用の境界として、この関数へ分離しています。
        for (int channel = 0; channel < 3; ++channel)
        {
            const float encoded = std::clamp(value.float32[channel], 0.0f, 1.0f);
            value.float32[channel] = encoded <= 0.04045f ? encoded / 12.92f :
                std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }
    }
    return value;
}

bool VulkanSwapChain::Init(const VulkanDevice& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync,
    VkFormat preferredSurfaceFormat)
{
    Shutdown();
    if (device.IsValid() == false || surface == VK_NULL_HANDLE || width == 0 || height == 0) { return false; }

    m_PhysicalDevice = device.GetPhysicalDeviceHandle();
    m_Device = device.GetHandle();
    m_Surface = surface;
    m_GraphicsQueueFamilyIndex = device.GetGraphicsQueueFamilyIndex();
    m_MutableFormatSupported = device.SupportsMutableSwapChainFormat();
    m_PreferredSurfaceFormat = preferredSurfaceFormat;
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
        m_ColorAttachmentFormat = VK_FORMAT_UNDEFINED;
        m_ColorReadback = false;
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

    VkSurfaceFormatKHR surfaceFormat{};
    VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
    if (SelectSurfaceFormat(formats, m_MutableFormatSupported, surfaceFormat, colorAttachmentFormat,
        m_PreferredSurfaceFormat) == false)
    {
        std::cerr << "Vulkan Surface has no supported SDR UNORM output (sRGB fallback requires mutable format).\n";
        return false;
    }
    VkFormatProperties colorProperties{};
    vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, colorAttachmentFormat, &colorProperties);
    const VkFormatFeatureFlags requiredColorFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if ((colorProperties.optimalTilingFeatures & requiredColorFeatures) != requiredColorFeatures)
    {
        return false;
    }
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
    // Capture/readbackは対応Surfaceだけに追加し、通常表示にTRANSFER_SRC対応を強制しません。
    const bool colorReadback = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (colorReadback == true)
    {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    const VkFormat viewFormats[] = {surfaceFormat.format, colorAttachmentFormat};
    VkImageFormatListCreateInfoKHR formatList{};
    if (surfaceFormat.format != colorAttachmentFormat)
    {
        // Native sRGBのbit列をUNORM Viewで書くため、Blend/Clearにも自動色変換を入れません。
        // SRGB Surfaceしかない環境でも表示契約を保つためのfallbackです。native形式自体は
        // Surfaceが許すSRGBのままにし、実際のAttachment Viewだけを互換UNORMへ変えます。
        // Shaderの出力だけをdecodeする方法では、sRGB Attachment上のBlendもGL/DX12と異なるため、
        // 半透明UIまで揃えるにはView/RenderPass/Pipelineを同じUNORM形式にする必要があります。
        // Image生成時点でmutableフラグと両形式を宣言し、後のView生成を合法にします。
        formatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
        formatList.viewFormatCount = 2;
        formatList.pViewFormats = viewFormats;
        createInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        createInfo.pNext = &formatList;
    }
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
    m_ColorAttachmentFormat = colorAttachmentFormat;
    m_ColorReadback = colorReadback;
    std::cout << "Vulkan SDR output: surface=" << static_cast<int>(m_ImageFormat)
        << " attachment=" << static_cast<int>(m_ColorAttachmentFormat) << '\n';
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
    m_ColorAttachmentFormat = VK_FORMAT_UNDEFINED;
    m_PreferredSurfaceFormat = VK_FORMAT_UNDEFINED;
    m_MutableFormatSupported = false;
    m_ColorReadback = false;
    m_Extent = {};
}

} // namespace Raven
