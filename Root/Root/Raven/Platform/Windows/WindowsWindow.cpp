#include <iostream>

#include "Raven/Platform/Windows/WindowsWindow.h"
#include "Raven/Core/Base.h"
#include "Raven/Platform/OpenGL/OpenGLContext.h"
#include "Raven/Platform/Windows/WindowsInput.h"

namespace Raven
{

static bool s_GLFWInitialized = false;

#if 0
GLFWwindow* g_MainWindow = nullptr;
#endif

#if 1
std::unique_ptr<Window> Window::Create(const WindowProps& props)
{
    return std::make_unique<WindowsWindow>(props);
}
#else
Scope<Window> Window::Create(const WindowProps& props)
{
    return CreateScope<Window>(props);
}
#endif

WindowsWindow::WindowsWindow(const WindowProps& props)
{
    Init(props);
}

WindowsWindow::~WindowsWindow()
{
    Shutdown();
}

void WindowsWindow::Init(const WindowProps& props)
{
    m_Data.Title = props.Title;
    m_Data.Width = props.Width;
    m_Data.Height = props.Height;

    if (!s_GLFWInitialized)
    {
        int success = glfwInit();

        if (!success)
        {
            std::cerr << "Failed to initialize GLFW\n";
            return;
        }

        s_GLFWInitialized = true;
    }

    // 実行環境差で既定値がぶれないよう、コンテキスト属性を明示します。
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    m_Window = glfwCreateWindow(
        static_cast<int>(props.Width),
        static_cast<int>(props.Height),
        props.Title.c_str(),
        nullptr,
        nullptr
    );

#if 0
    g_MainWindow = m_Window;
#endif

    if (!m_Window)
    {
        std::cerr << "Failed to create GLFW window\n";
        return;
    }

    // OpenGLContext::Init() の中でglfwMakeContextCurrent()を呼ぶためコメントアウト
#if 0
    glfwMakeContextCurrent(m_Window);
#endif

    m_Context = CreateScope<OpenGLContext>(m_Window);
    m_Context->Init();

    m_Input = CreateScope<WindowsInput>(m_Window);

    glfwSetWindowUserPointer(m_Window, &m_Data);

    SetVSync(true);

    glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            WindowCloseEvent event;
            data.EventCallback(event);
        }
    );

    glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            data.Width = static_cast<unsigned int>(width);
            data.Height = static_cast<unsigned int>(height);

            WindowResizeEvent event(data.Width, data.Height);
            data.EventCallback(event);
        }
    );
}

void WindowsWindow::Shutdown()
{
    glfwDestroyWindow(m_Window);
}

void WindowsWindow::OnUpdate()
{
    glfwPollEvents();

    m_Context->SwapBuffers();
#if 0
    glfwSwapBuffers(m_Window);
#endif
}

void WindowsWindow::SetVSync(bool enabled)
{
    glfwSwapInterval(enabled ? 1 : 0);
    m_Data.VSync = enabled;
}

bool WindowsWindow::IsVSync() const
{
    return m_Data.VSync;
}

}