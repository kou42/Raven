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

    if (s_GLFWInitialized == false)
    {
        int success = glfwInit();

        if (success == false)
        {
            std::cerr << "Failed to initialize GLFW\n";
            return;
        }

        s_GLFWInitialized = true;
    }

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

    if (m_Window == nullptr)
    {
        std::cerr << "Failed to create GLFW window\n";
        return;
    }

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

    glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (focused == GLFW_TRUE)
            {
                WindowFocusGainedEvent event;
                data.EventCallback(event);
            }
            else
            {
                WindowFocusLostEvent event;
                data.EventCallback(event);
            }
        }
    );

    // Key callbackのkey/mods/repeatをそのままCore Eventへsnapshotし、Application以降でpollingしません。
    glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            static_cast<void>(scancode);

            if (action == GLFW_PRESS || action == GLFW_REPEAT)
            {
                KeyPressedEvent event(key, mods, action == GLFW_REPEAT);
                data.EventCallback(event);
            }
            else if (action == GLFW_RELEASE)
            {
                KeyReleasedEvent event(key, mods);
                data.EventCallback(event);
            }
        }
    );

    glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
            data.EventCallback(event);
        }
    );

    glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            static_cast<void>(mods);

            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            if (action == GLFW_PRESS)
            {
                MouseButtonPressedEvent event(button, static_cast<float>(mouseX), static_cast<float>(mouseY));
                data.EventCallback(event);
            }
            else if (action == GLFW_RELEASE)
            {
                MouseButtonReleasedEvent event(button, static_cast<float>(mouseX), static_cast<float>(mouseY));
                data.EventCallback(event);
            }
        }
    );

    glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double offsetX, double offsetY)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            MouseScrolledEvent event(
                static_cast<float>(offsetX),
                static_cast<float>(offsetY),
                static_cast<float>(mouseX),
                static_cast<float>(mouseY));
            data.EventCallback(event);
        }
    );
}

void WindowsWindow::Shutdown()
{
    if (m_Window != nullptr)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
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