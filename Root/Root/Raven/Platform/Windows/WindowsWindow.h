#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Core/Base.h"
#include "Raven/Renderer/GraphicsContext.h"
#include "Raven/Core/Input.h"

#include <GLFW/glfw3.h>

namespace Raven
{

struct Win32IMEBridge;

class WindowsWindow : public Window
{
public:
    WindowsWindow(const WindowProps& props);
    ~WindowsWindow() override;

    void OnUpdate() override;
    void PollEvents() override;
    void Present() override;
    bool MakeContextCurrent() override;

    unsigned int GetWidth() const override { return m_Data.Width; }
    unsigned int GetHeight() const override { return m_Data.Height; }
    unsigned int GetFramebufferWidth() const override { return m_Data.FramebufferWidth; }
    unsigned int GetFramebufferHeight() const override { return m_Data.FramebufferHeight; }
    RHIBackend GetBackend() const override { return m_Data.Backend; }
    WindowState GetState() const override;
    void SetTitle(const std::string& title) override;
    void SetSize(unsigned int width, unsigned int height) override;
    void SetPosition(int x, int y) override;
    void Minimize() override;
    void Maximize() override;
    void Restore() override;
    void SetFullscreen(bool enabled) override;
    void Show() override;
    void Hide() override;
    void Focus() override;
    void* GetNativeWindow() const override { return m_Window; }
    void* GetPlatformWindowHandle() const override;

    void SetEventCallback(const EventCallbackFn& callback) override
    {
        m_Data.EventCallback = callback;
    }

    void SetIMECaretPositionCallback(IMECaretPositionFn callback) override;
    void CancelIMEComposition() override;
    void SetVSync(bool enabled) override;
    bool IsVSync() const override;

private:
    void Init(const WindowProps& props);
    void Shutdown();

private:
    GLFWwindow* m_Window = nullptr;
    // GLFWのWin32 WndProcへIME通知を追加するAdapter。Window破棄前に復元します。
    std::unique_ptr<Win32IMEBridge> m_IMEBridge;

    struct WindowData
    {
        std::string Title;
        unsigned int Width;
        unsigned int Height;
        unsigned int FramebufferWidth = 0;
        unsigned int FramebufferHeight = 0;
        RHIBackend Backend = RHIBackend::OpenGL;
        bool VSync = false;

        EventCallbackFn EventCallback;
        IMECaretPositionFn IMECaretPositionCallback;
    };

    WindowData m_Data;
    // Fullscreen解除時に元のWindow配置へ戻すための保存値です。
    int m_WindowedX = 0;
    int m_WindowedY = 0;
    int m_WindowedWidth = 0;
    int m_WindowedHeight = 0;

    // OpenGLだけがGLFW OpenGL Contextを所有します。
    // Vulkan/DX12はSwapChain実装前なのでNo-API Windowとしてイベント処理のみ行います。
    Scope<GraphicsContext> m_Context;
    Scope<Input> m_Input;
};

} // namespace Raven
