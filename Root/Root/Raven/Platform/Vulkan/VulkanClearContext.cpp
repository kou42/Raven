#include "VulkanClearContext.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

namespace Raven
{
VulkanClearContext::~VulkanClearContext()
{
    Shutdown();
}

bool VulkanClearContext::Init(Window& window)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::Vulkan || window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    // WindowsWindow生成後なのでGLFWは初期化済みです。
    if (m_Instance.Init() == false ||
        m_Surface.Init(m_Instance.GetHandle(), static_cast<GLFWwindow*>(window.GetNativeWindow())) == false ||
        m_SwapChain.Init(m_Instance.GetDevice(), m_Surface.GetHandle(),
            window.GetWidth(), window.GetHeight(), window.IsVSync()) == false ||
        m_CommandBuffer.Init(m_Instance.GetDevice()) == false ||
        m_FrameSync.Init(m_Instance.GetDevice()) == false)
    {
        Shutdown();
        return false;
    }

    m_VSync = window.IsVSync();
    return true;
}

bool VulkanClearContext::DrawClearFrame(const VkClearColorValue& clearColor)
{
    return m_FrameRenderer.DrawClearFrame(
        m_Instance.GetDevice(), m_SwapChain, m_CommandBuffer, m_FrameSync, clearColor);
}

bool VulkanClearContext::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    return m_SwapChain.Recreate(width, height, m_VSync);
}

void VulkanClearContext::Shutdown()
{
    if (m_Instance.IsValid() == true)
    {
        m_Instance.GetDevice().WaitIdle();
    }
    // 子Resourceから順に解放します。
    m_FrameSync.Shutdown();
    m_CommandBuffer.Shutdown();
    m_SwapChain.Shutdown();
    m_Surface.Shutdown();
    m_Instance.Shutdown();
}
} // namespace Raven
