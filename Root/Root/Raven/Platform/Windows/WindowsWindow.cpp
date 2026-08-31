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

    if (m_Window == nullptr)
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

    // Focusを失うとMouse UpがMain Windowへ戻らない場合があります。
    // Platform層ではFocus状態だけをCore Eventへ変換し、Capture終了判断はApplication/UIContextへ委譲します。
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

    // GLFW callbackで受け取ったMouse入力をCore Eventへ変換します。
    // UIContextをPlatform層から直接呼ばずApplication::OnEvent()へ集約することで、
    // UI以外のLayerも同じMouse Eventを利用でき、入力経路をWindow -> Application -> Consumerに統一します。
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
            (void)mods;

            // Button callbackには座標が直接渡されないため、Eventを生成するこの瞬間にGLFWから取得します。
            // Application側で後からpollingしないことで、Eventは発生時点の完全な入力snapshotになります。
            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            if (action == GLFW_PRESS)
            {
                MouseButtonPressedEvent event(
                    button,
                    static_cast<float>(mouseX),
                    static_cast<float>(mouseY));
                data.EventCallback(event);
            }
            else if (action == GLFW_RELEASE)
            {
                MouseButtonReleasedEvent event(
                    button,
                    static_cast<float>(mouseX),
                    static_cast<float>(mouseY));
                data.EventCallback(event);
            }
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