#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{

class VulkanDevice;

class VulkanSwapChain
{
public:
    VulkanSwapChain() = default;
    ~VulkanSwapChain();

    VulkanSwapChain(const VulkanSwapChain&) = delete;
    VulkanSwapChain& operator=(const VulkanSwapChain&) = delete;
    VulkanSwapChain(VulkanSwapChain&&) = delete;
    VulkanSwapChain& operator=(VulkanSwapChain&&) = delete;

    // preferredSurfaceFormatはnative形式を明示する診断/互換確認用。未指定ならUNORMを優先します。
    bool Init(const VulkanDevice& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync,
        VkFormat preferredSurfaceFormat = VK_FORMAT_UNDEFINED);
    // CPUでSurface候補を検証できます。HDR等の未対応色空間へ暗黙にfallbackしません。
    static bool SelectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats, bool mutableFormatSupported,
        VkSurfaceFormatKHR& surfaceFormat, VkFormat& colorAttachmentFormat,
        VkFormat preferredSurfaceFormat = VK_FORMAT_UNDEFINED);
    // Transfer ClearはViewを使わずnative Image形式で変換されるため、sRGB時だけ入力を補償します。
    // UNORM AttachmentのClear/Shader/Blendには適用しません。
    static VkClearColorValue PrepareTransferClearValue(VkFormat nativeFormat, const VkClearColorValue& displayColor);
    bool Recreate(uint32_t width, uint32_t height, bool vsync);
    void Shutdown();

    VkSwapchainKHR GetHandle() const { return m_SwapChain; }
    VkFormat GetImageFormat() const { return m_ImageFormat; }
    VkFormat GetColorAttachmentFormat() const { return m_ColorAttachmentFormat; }
    bool SupportsColorReadback() const { return m_ColorReadback; }
    VkExtent2D GetExtent() const { return m_Extent; }
    const std::vector<VkImage>& GetImages() const { return m_Images; }
    VkImageLayout GetImageLayout(uint32_t imageIndex) const;
    void SetImageLayout(uint32_t imageIndex, VkImageLayout layout);
    bool IsValid() const
    {
        return m_SwapChain != VK_NULL_HANDLE && m_Images.empty() == false;
    }

private:
    bool Create(uint32_t width, uint32_t height, bool vsync, VkSwapchainKHR oldSwapChain);
    void DestroySwapChain(VkSwapchainKHR swapChain);

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    uint32_t m_GraphicsQueueFamilyIndex = UINT32_MAX;
    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_ColorAttachmentFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_PreferredSurfaceFormat = VK_FORMAT_UNDEFINED;
    bool m_MutableFormatSupported = false;
    bool m_ColorReadback = false;
    VkExtent2D m_Extent{};
    std::vector<VkImage> m_Images;
    std::vector<VkImageLayout> m_ImageLayouts;
};

} // namespace Raven
