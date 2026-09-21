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

    unsigned int GetWidth() const override { return m_Data.Width; }
    unsigned int GetHeight() const override { return m_Data.Height; }
    RHIBackend GetBackend() const override { return m_Data.Backend; }
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
        RHIBackend Backend = RHIBackend::OpenGL;
        bool VSync = false;

        EventCallbackFn EventCallback;
        IMECaretPositionFn IMECaretPositionCallback;
    };

    WindowData m_Data;

    // OpenGLだけがGLFW OpenGL Contextを所有します。
    // Vulkan/DX12はSwapChain実装前なのでNo-API Windowとしてイベント処理のみ行います。
    Scope<GraphicsContext> m_Context;
    Scope<Input> m_Input;
};

} // namespace Raven
