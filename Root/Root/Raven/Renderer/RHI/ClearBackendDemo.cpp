#include "ClearBackendDemo.h"

#include "Raven/Core/Event.h"
#include "Raven/Core/Window.h"
#include "Raven/Platform/DirectX12/DX12ClearContext.h"
#include "Raven/Platform/Vulkan/VulkanClearContext.h"

#include <GLFW/glfw3.h>

#include <iostream>

namespace Raven
{
int RunClearBackendDemo(RHIBackend backend)
{
    if (backend != RHIBackend::Vulkan && backend != RHIBackend::DirectX12)
    {
        return 1;
    }

    // Contextより先にWindowを作り、Contextを先にShutdownしてからWindowを破棄します。
    auto window = Window::Create(WindowProps("Raven Clear Backend Demo", 1280, 720, backend));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());
    bool running = true;
    bool resizePending = false;
    window->SetEventCallback([&running, &resizePending](Event& event)
    {
        if (event.GetEventType() == EventType::WindowClose)
        {
            running = false;
        }
        else if (event.GetEventType() == EventType::WindowResize)
        {
            // Callback内ではGPU Resourceを触らず、Frame境界でResizeします。
            resizePending = true;
        }
    });

    VulkanClearContext vulkan;
    DX12ClearContext dx12;
    const bool initialized = backend == RHIBackend::Vulkan
        ? vulkan.Init(*window) : dx12.Init(*window);
    if (initialized == false)
    {
        std::cerr << "Failed to initialize Clear Backend Demo.\n";
        return 1;
    }

    int result = 0;
    while (running == true && glfwWindowShouldClose(glfwWindow) == GLFW_FALSE)
    {
        // WindowsWindow::OnUpdateはGLFWイベント処理のみ（No-APIではSwapBuffersしません）。
        window->OnUpdate();
        if (running == false || glfwWindowShouldClose(glfwWindow) == GLFW_TRUE)
        {
            break;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(glfwWindow, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth == 0 || framebufferHeight == 0)
        {
            // Minimize中はAcquire/Presentしない。復帰時に最新Framebuffer寸法で再生成します。
            resizePending = true;
            glfwWaitEvents();
            continue;
        }

        if (resizePending == true)
        {
            const bool resized = backend == RHIBackend::Vulkan
                ? vulkan.Resize(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight))
                : dx12.Resize(static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight));
            if (resized == false)
            {
                std::cerr << "Failed to resize Clear Backend SwapChain.\n";
                result = 1;
                break;
            }
            resizePending = false;
        }

        bool drawn = false;
        bool vulkanResizeRequired = false;
        if (backend == RHIBackend::Vulkan)
        {
            VkClearColorValue clearColor{};
            clearColor.float32[0] = 0.08f;
            clearColor.float32[1] = 0.16f;
            clearColor.float32[2] = 0.28f;
            clearColor.float32[3] = 1.0f;
            const VulkanFrameResult frameResult = vulkan.DrawClearFrame(clearColor);
            drawn = frameResult == VulkanFrameResult::Success;
            vulkanResizeRequired = frameResult == VulkanFrameResult::ResizeRequired;
        }
        else
        {
            const float clearColor[4] = { 0.08f, 0.16f, 0.28f, 1.0f };
            drawn = dx12.DrawClearFrame(clearColor);
        }

        if (drawn == false)
        {
            if (vulkanResizeRequired == true)
            {
                // OUT_OF_DATE/SUBOPTIMALだけを再生成で復旧します。
                resizePending = true;
                continue;
            }
            std::cerr << "Failed to draw Clear Backend frame.\n";
            result = 1;
            break;
        }
    }

    vulkan.Shutdown();
    dx12.Shutdown();
    return result;
}
} // namespace Raven
