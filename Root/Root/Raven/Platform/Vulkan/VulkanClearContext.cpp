#include "VulkanClearContext.h"

#include "Raven/Core/Window.h"

#include <GLFW/glfw3.h>

#include <iostream>

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
        m_FrameSync.Init(m_Instance.GetDevice(), static_cast<uint32_t>(m_SwapChain.GetImages().size())) == false)
    {
        Shutdown();
        return false;
    }

    for (uint32_t frame = 0; frame < m_FrameSync.GetFrameCount(); ++frame)
    {
        auto commandBuffer = std::make_unique<VulkanCommandBuffer>();
        if (commandBuffer->Init(m_Instance.GetDevice()) == false)
        {
            Shutdown();
            return false;
        }
        m_CommandBuffers.push_back(std::move(commandBuffer));
    }

    m_VSync = window.IsVSync();
    return true;
}

RHIFrameResult VulkanClearContext::DrawClearFrame(const float clearColor[4])
{
    const uint32_t frame = m_FrameSync.GetCurrentFrameIndex();
    if (frame >= m_CommandBuffers.size() || m_CommandBuffers[frame] == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    if (clearColor == nullptr)
    {
        return RHIFrameResult::FatalError;
    }
    VkClearColorValue vulkanColor{};
    for (uint32_t component = 0; component < 4; ++component)
    {
        vulkanColor.float32[component] = clearColor[component];
    }
    const VulkanFrameResult result = m_FrameRenderer.DrawClearFrame(
        m_Instance.GetDevice(), m_SwapChain, *m_CommandBuffers[frame], m_FrameSync, vulkanColor);
    if (result == VulkanFrameResult::Success)
    {
        return RHIFrameResult::Success;
    }
    if (result == VulkanFrameResult::ResizeRequired)
    {
        return RHIFrameResult::ResizeRequired;
    }
    return RHIFrameResult::FatalError;
}

bool VulkanClearContext::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    // 要求サイズとSurfaceが実際に選択したExtentは異なる場合があるため両方記録します。
    const VkExtent2D oldExtent = m_SwapChain.GetExtent();
    std::cout << "[Vulkan Resize] Old Extent: " << oldExtent.width
              << " x " << oldExtent.height << ", Requested: "
              << width << " x " << height << '\n';
    if (m_SwapChain.Recreate(width, height, m_VSync) == false)
    {
        return false;
    }

    // Resize後はImage数が変化するため、Present Semaphore配列も作り直します。
    // SwapChain::RecreateはDeviceWaitIdle済みなので旧同期Resourceを安全に解放できます。
    if (m_FrameSync.Init(m_Instance.GetDevice(),
        static_cast<uint32_t>(m_SwapChain.GetImages().size())) == false)
    {
        std::cerr << "[Vulkan Resize] FrameSync recreation failed.\n";
        return false;
    }
    const VkExtent2D newExtent = m_SwapChain.GetExtent();
    std::cout << "[Vulkan Resize] Recreated Extent: " << newExtent.width
              << " x " << newExtent.height << '\n';
    return true;
}

void VulkanClearContext::Shutdown()
{
    if (m_Instance.IsValid() == true)
    {
        m_Instance.GetDevice().WaitIdle();
    }
    // 子Resourceから順に解放します。
    m_FrameSync.Shutdown();
    // ContextのWaitIdle後に全Frame SlotのCommandPoolを破棄します。
    m_CommandBuffers.clear();
    m_SwapChain.Shutdown();
    m_Surface.Shutdown();
    m_Instance.Shutdown();
}
} // namespace Raven
