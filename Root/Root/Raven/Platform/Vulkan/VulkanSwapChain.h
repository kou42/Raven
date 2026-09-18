#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{

class VulkanDevice;

// VkSurfaceKHRの能力を調べ、表示用Imageを所有するVkSwapchainKHRを管理します。
// Resize時はRecreateで安全に再生成し、各Imageの現在LayoutもSwapChain側で追跡します。
class VulkanSwapChain
{
public:
    VulkanSwapChain() = default;
    ~VulkanSwapChain();

    VulkanSwapChain(const VulkanSwapChain&) = delete;
    VulkanSwapChain& operator=(const VulkanSwapChain&) = delete;
    VulkanSwapChain(VulkanSwapChain&&) = delete;
    VulkanSwapChain& operator=(VulkanSwapChain&&) = delete;

    bool Init(
        const VulkanDevice& device,
        VkSurfaceKHR surface,
        uint32_t width,
        uint32_t height,
        bool vsync);
    bool Recreate(uint32_t width, uint32_t height, bool vsync);
    void Shutdown();

    VkSwapchainKHR GetHandle() const { return m_SwapChain; }
    VkFormat GetImageFormat() const { return m_ImageFormat; }
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
    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    std::vector<VkImage> m_Images;
    std::vector<VkImageLayout> m_ImageLayouts;
};

} // namespace Raven
