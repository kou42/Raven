#pragma once
#include "../../Core/Window.h"

#include <GLFW/glfw3.h>

class WindowsWindow : public Window
{
public:
    WindowsWindow(const WindowProps& props);
    ~WindowsWindow() override;

    void OnUpdate() override;

    unsigned int GetWidth() const override { return m_Data.Width; }
    unsigned int GetHeight() const override { return m_Data.Height; }

    void SetEventCallback(const EventCallbackFn& callback) override
    {
        m_Data.EventCallback = callback;
    }

    void SetVSync(bool enabled) override;
    bool IsVSync() const override;

private:
    void Init(const WindowProps& props);
    void Shutdown();

private:
    GLFWwindow* m_Window = nullptr;

    struct WindowData
    {
        std::string Title;
        unsigned int Width;
        unsigned int Height;
        bool VSync = false;

        EventCallbackFn EventCallback;
    };

    WindowData m_Data;
};