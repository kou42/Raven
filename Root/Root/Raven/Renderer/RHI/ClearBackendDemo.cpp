#include "ClearBackendDemo.h"
#include "RHIClearContext.h"

#include "Raven/Core/Event.h"
#include "Raven/Core/Window.h"
#include "Raven/Platform/DirectX12/DX12ClearContext.h"
#include "Raven/Platform/Vulkan/VulkanClearContext.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <limits>
#include <memory>

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
    bool wasMinimized = false;
    const char* resizeReason = "Unknown";
    unsigned long totalResizeEvents = 0;
    unsigned long pendingResizeEvents = 0;
    window->SetEventCallback([&running, &resizePending, &resizeReason,
        &totalResizeEvents, &pendingResizeEvents](Event& event)
    {
        if (event.GetEventType() == EventType::WindowClose)
        {
            running = false;
        }
        else if (event.GetEventType() == EventType::WindowResize)
        {
            // Callback内ではGPU Resourceを触らず、Frame境界でResizeします。
            resizePending = true;
            resizeReason = "WindowResize event";
            // ドラッグ中の連続イベントは個別出力せず、次の再生成時にまとめて報告します。
            ++totalResizeEvents;
            ++pendingResizeEvents;
        }
    });

    // Backend選択は生成時のみ行い、Resize/描画/Shutdownは共通のFrame境界で実行します。
    std::unique_ptr<RHIClearContext> context;
    if (backend == RHIBackend::Vulkan)
    {
        context = std::make_unique<VulkanClearContext>();
    }
    else
    {
        context = std::make_unique<DX12ClearContext>();
    }
    const bool initialized = context->Init(*window);
    if (initialized == false)
    {
        std::cerr << "Failed to initialize Clear Backend Demo.\n";
        return 1;
    }

    // 自動Smoke Test用。未設定または不正値の場合は従来どおりWindowを閉じるまで実行します。
    // 0は無制限として扱い、正の整数だけをフレーム上限として採用します。
    unsigned long smokeFrameLimit = 0;
#if defined(_MSC_VER)
    // MSVCの安全なCRT APIで環境変数を複製し、解析後に必ず解放します。
    char* smokeFrames = nullptr;
    size_t smokeFramesLength = 0;
    const errno_t environmentResult = _dupenv_s(
        &smokeFrames, &smokeFramesLength, "RAVEN_RHI_SMOKE_FRAMES");
#else
    const char* smokeFrames = std::getenv("RAVEN_RHI_SMOKE_FRAMES");
#endif
    if (smokeFrames != nullptr && smokeFrames[0] != '\0')
    {
        char* end = nullptr;
        errno = 0;
        const unsigned long parsed = std::strtoul(smokeFrames, &end, 10);
        if (errno == 0 && end != smokeFrames && end != nullptr &&
            *end == '\0' &&
            parsed <= std::numeric_limits<unsigned int>::max())
        {
            smokeFrameLimit = parsed;
        }
        else
        {
            std::cerr << "[RHI Smoke] Invalid RAVEN_RHI_SMOKE_FRAMES; running interactively.\n";
        }
    }

#if defined(_MSC_VER)
    if (environmentResult != 0)
    {
        std::cerr << "[RHI Smoke] Failed to read RAVEN_RHI_SMOKE_FRAMES.\n";
    }
    std::free(smokeFrames);
#endif

    unsigned long completedFrames = 0;
    unsigned long completedResizes = 0;
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
            if (wasMinimized == false && backend == RHIBackend::Vulkan)
            {
                std::cout << "[Vulkan Window] Minimized (zero framebuffer); rendering suspended.\n";
            }
            wasMinimized = true;
            glfwWaitEvents();
            continue;
        }

        if (wasMinimized == true)
        {
            if (backend == RHIBackend::Vulkan)
            {
                std::cout << "[Vulkan Window] Restored; rendering will resume after resize.\n";
            }
            wasMinimized = false;
            resizeReason = "Restore after minimize";
        }

        if (resizePending == true)
        {
            if (backend == RHIBackend::Vulkan)
            {
                std::cout << "[Vulkan Resize] Reason: " << resizeReason
                          << ", WindowResize events since last recreation: "
                          << pendingResizeEvents << '\n';
            }
            const bool resized = context->Resize(
                static_cast<uint32_t>(framebufferWidth), static_cast<uint32_t>(framebufferHeight));
            if (resized == false)
            {
                std::cerr << "Failed to resize Clear Backend SwapChain.\n";
                result = 1;
                break;
            }
            ++completedResizes;
            std::cout << "[RHI Smoke] Resize completed: "
                      << framebufferWidth << " x " << framebufferHeight << '\n';
            resizePending = false;
            pendingResizeEvents = 0;
        }

        const float clearColor[4] = { 0.08f, 0.16f, 0.28f, 1.0f };
        const RHIFrameResult frameResult = context->DrawClearFrame(clearColor);
        if (frameResult != RHIFrameResult::Success)
        {
            if (frameResult == RHIFrameResult::ResizeRequired)
            {
                // BackendがSwapChain再生成を要求した場合だけ次のFrame境界で復旧します。
                resizePending = true;
                resizeReason = "Acquire/Present requested recreation (see Vulkan Resize log)";
                continue;
            }
            std::cerr << "Failed to draw Clear Backend frame.\n";
            result = 1;
            break;
        }
        ++completedFrames;
        if (smokeFrameLimit > 0 && completedFrames >= smokeFrameLimit)
        {
            std::cout << "[RHI Smoke] Completed " << completedFrames
                      << " frames successfully.\n";
            break;
        }
    }

    // 正常なWindow Closeとデバッガーの強制停止を区別できるよう、
    // Shutdown後にも対話モードの結果を出力します。
    context->Shutdown();
    std::cout << "[RHI Smoke] Shutdown completed. Frames: " << completedFrames
              << ", Resizes: " << completedResizes
              << ", WindowResize events: " << totalResizeEvents
              << ", Pending WindowResize events: " << pendingResizeEvents
              << ", Exit code: " << result << '\n';
    return result;
}
} // namespace Raven
