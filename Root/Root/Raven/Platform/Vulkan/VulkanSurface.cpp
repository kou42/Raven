#include "VulkanSurface.h"

#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Raven
{

VulkanSurface::~VulkanSurface()
{
    Shutdown();
}

bool VulkanSurface::Init(VkInstance instance, GLFWwindow* window)
{
    if (m_Surface != VK_NULL_HANDLE)
    {
        const bool sameInstance = m_Instance == instance;
        if (sameInstance == true)
        {
            return true;
        }

        Shutdown();
    }

    if (instance == VK_NULL_HANDLE)
    {
        std::cout << "Cannot create Vulkan Surface because VkInstance is null.\n";
        return false;
    }

    if (window == nullptr)
    {
        std::cout << "Cannot create Vulkan Surface because GLFWwindow is null.\n";
        return false;
    }

    // GLFWにPlatform固有Surface生成を委譲し、Vulkan層からWin32/X11等の差異を隔離します。
    const VkResult result = glfwCreateWindowSurface(
        instance,
        window,
        nullptr,
        &m_Surface);
    if (result != VK_SUCCESS)
    {
        m_Surface = VK_NULL_HANDLE;
        std::cout << "Failed to create Vulkan VkSurfaceKHR. VkResult = "
                  << static_cast<int>(result) << '\n';
        return false;
    }

    m_Instance = instance;
    std::cout << "Vulkan VkSurfaceKHR created successfully.\n";
    return true;
}

void VulkanSurface::Shutdown()
{
    if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    }

    m_Surface = VK_NULL_HANDLE;
    m_Instance = VK_NULL_HANDLE;
}

} // namespace Raven
