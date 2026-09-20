#include <iostream>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Raven/Platform/Windows/WindowsWindow.h"
#include "Raven/Core/Base.h"
#include "Raven/Platform/OpenGL/OpenGLContext.h"
#include "Raven/Platform/Windows/WindowsInput.h"

namespace Raven
{

static bool s_GLFWInitialized = false;

std::unique_ptr<Window> Window::Create(const WindowProps& props)
{
    return std::make_unique<WindowsWindow>(props);
}

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
    m_Data.Backend = props.Backend;

    if (s_GLFWInitialized == false)
    {
        const int success = glfwInit();
        if (success == GLFW_FALSE)
        {
            std::cerr << "Failed to initialize GLFW\n";
            return;
        }

        s_GLFWInitialized = true;
    }

    // GLFWのHintはProcess内で保持されるため、Window生成ごとに既定値へ戻してから
    // Backend固有Hintを設定します。これによりOpenGL WindowとNo-API Windowを連続生成できます。
    glfwDefaultWindowHints();

    if (m_Data.Backend == RHIBackend::OpenGL)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
    }
    else if (m_Data.Backend == RHIBackend::Vulkan || m_Data.Backend == RHIBackend::DirectX12)
    {
        // Vulkan/DX12はOpenGL Contextを作りません。
        // Vulkanは次段階でGLFWwindowからVkSurfaceKHRを作り、DX12はHWNDからSwapChainを作ります。
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    else
    {
        std::cerr << "Unsupported RHI backend for WindowsWindow.\n";
        return;
    }

    m_Window = glfwCreateWindow(
        static_cast<int>(props.Width),
        static_cast<int>(props.Height),
        props.Title.c_str(),
        nullptr,
        nullptr);

    if (m_Window == nullptr)
    {
        std::cerr << "Failed to create GLFW window\n";
        return;
    }

    if (m_Data.Backend == RHIBackend::OpenGL)
    {
        m_Context = CreateScope<OpenGLContext>(m_Window);
        m_Context->Init();
    }

    m_Input = CreateScope<WindowsInput>(m_Window);
    glfwSetWindowUserPointer(m_Window, &m_Data);

    SetVSync(true);

    glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == true)
            {
                WindowCloseEvent event;
                data.EventCallback(event);
            }
        });

    glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            data.Width = static_cast<unsigned int>(width);
            data.Height = static_cast<unsigned int>(height);

            if (static_cast<bool>(data.EventCallback) == true)
            {
                WindowResizeEvent event(data.Width, data.Height);
                data.EventCallback(event);
            }
        });

    glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }

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
        });

    glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            static_cast<void>(scancode);

            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }

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
        });

    glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == true)
            {
                MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
                data.EventCallback(event);
            }
        });

    glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            static_cast<void>(mods);

            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }

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
        });

    glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double offsetX, double offsetY)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }

            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            MouseScrolledEvent event(
                static_cast<float>(offsetX),
                static_cast<float>(offsetY),
                static_cast<float>(mouseX),
                static_cast<float>(mouseY));
            data.EventCallback(event);
        });
}

void WindowsWindow::Shutdown()
{
    // Graphics ContextはNative Windowより先に破棄します。
    m_Context.reset();
    m_Input.reset();

    if (m_Window != nullptr)
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
}

void WindowsWindow::OnUpdate()
{
    // 既存呼び出し元の互換性を維持します。Scene側は2段階を明示的に呼びます。
    PollEvents();
    Present();
}

void WindowsWindow::PollEvents()
{
    glfwPollEvents();
}

void WindowsWindow::Present()
{
    // Vulkan/DX12のPresentは各SwapChain側が所有します。
    // No-API WindowへglfwSwapBuffersを呼ばないようにします。
    if (m_Context != nullptr)
    {
        m_Context->SwapBuffers();
    }
}

void* WindowsWindow::GetPlatformWindowHandle() const
{
    if (m_Window == nullptr)
    {
        return nullptr;
    }

    return static_cast<void*>(glfwGetWin32Window(m_Window));
}

void WindowsWindow::SetVSync(bool enabled)
{
    m_Data.VSync = enabled;

    if (m_Data.Backend == RHIBackend::OpenGL)
    {
        glfwSwapInterval(enabled ? 1 : 0);
    }

    // Vulkan/DX12ではSwapChainのPresent Mode / Sync Intervalへ反映します。
    // 現段階では設定値だけ保持し、SwapChain実装時に利用します。
}

bool WindowsWindow::IsVSync() const
{
    return m_Data.VSync;
}

} // namespace Raven
