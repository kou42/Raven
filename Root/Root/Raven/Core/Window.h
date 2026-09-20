#pragma once
#include <string>
#include <functional>
#include <memory>

#include "Raven/Core/Event.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

struct WindowProps
{
    std::string Title;
    unsigned int Width;
    unsigned int Height;
    RHIBackend Backend;

    WindowProps(
        const std::string& title = "My Engine",
        unsigned int width = 1920,
        unsigned int height = 1080,
        RHIBackend backend = GetRHIBackend()
    )
        : Title(title), Width(width), Height(height), Backend(backend)
    {
    }
};

class Window
{
public:
    using EventCallbackFn = std::function<void(Event&)>;

    virtual ~Window() = default;

    // 互換入口。新しいApplication経路ではPollEvents/Presentを明示的に呼びます。
    virtual void OnUpdate() = 0;
    virtual void PollEvents() = 0;
    virtual void Present() = 0;

    virtual unsigned int GetWidth() const = 0;
    virtual unsigned int GetHeight() const = 0;
    virtual RHIBackend GetBackend() const = 0;

    // GLFWwindowはVulkan Surface生成で必要になるため従来どおり公開します。
    // DX12側のHWNDはPlatformWindowHandleから取得し、Core層へWin32型を漏らしません。
    virtual void* GetNativeWindow() const = 0;
    virtual void* GetPlatformWindowHandle() const = 0;

    virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
    virtual void SetVSync(bool enabled) = 0;
    virtual bool IsVSync() const = 0;

    static std::unique_ptr<Window> Create(const WindowProps& props = WindowProps());
};

} // namespace Raven
