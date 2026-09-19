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

    // Windowの論理サイズと実Framebufferサイズは高DPI環境で異なる場合があります。
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(window.GetNativeWindow()),
        &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return false;
    }

    // WindowsWindow生成後なのでGLFWは初期化済みです。
    if (m_Instance.Init() == false ||
        m_Surface.Init(m_Instance.GetHandle(), static_cast<GLFWwindow*>(window.GetNativeWindow())) == false ||
        m_SwapChain.Init(m_Instance.GetDevice(), m_Surface.GetHandle(),
            static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight),
            window.IsVSync()) == false ||
        m_CommandBuffer.Init(m_Instance.GetDevice()) == false ||
        m_FrameSync.Init(m_Instance.GetDevice(), static_cast<uint32_t>(m_SwapChain.GetImages().size())) == false)
    {
        Shutdown();
        return false;
    }

    m_VSync = window.IsVSync();
    return true;
}

VulkanFrameResult VulkanClearContext::DrawClearFrame(const VkClearColorValue& clearColor)
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
    if (m_SwapChain.Recreate(width, height, m_VSync) == false)
    {
        return false;
    }

    // Resize後はImage数が変化するため、Present Semaphore配列も作り直します。
    // SwapChain::RecreateはDeviceWaitIdle済みなので旧同期Resourceを安全に解放できます。
    return m_FrameSync.Init(m_Instance.GetDevice(),
        static_cast<uint32_t>(m_SwapChain.GetImages().size()));
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
