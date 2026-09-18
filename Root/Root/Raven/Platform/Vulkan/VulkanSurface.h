#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace Raven
{

// GLFW WindowとVkInstanceを接続するVkSurfaceKHRの所有クラスです。
// SurfaceはSwapChainより長く、VkInstanceより短いLifetimeを持つため、
// Instanceとは別クラスで明示的に生成・破棄します。
class VulkanSurface
{
public:
    VulkanSurface() = default;
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;
    VulkanSurface(VulkanSurface&&) = delete;
    VulkanSurface& operator=(VulkanSurface&&) = delete;

    bool Init(VkInstance instance, GLFWwindow* window);
    void Shutdown();

    VkSurfaceKHR GetHandle() const { return m_Surface; }
    bool IsValid() const { return m_Surface != VK_NULL_HANDLE; }

private:
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
};

} // namespace Raven
