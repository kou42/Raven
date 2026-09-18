#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Raven
{

class VulkanDevice;

// VkSurfaceKHRの能力を調べ、表示用Imageを所有するVkSwapchainKHRを生成します。
// RenderPass/Framebufferはまだ持たせず、⑩ではPresentation Image取得までに責務を限定します。
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
    void Shutdown();

    VkSwapchainKHR GetHandle() const { return m_SwapChain; }
    VkFormat GetImageFormat() const { return m_ImageFormat; }
    VkExtent2D GetExtent() const { return m_Extent; }
    const std::vector<VkImage>& GetImages() const { return m_Images; }
    bool IsValid() const
    {
        return m_SwapChain != VK_NULL_HANDLE && m_Images.empty() == false;
    }

private:
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    std::vector<VkImage> m_Images;
};

} // namespace Raven
