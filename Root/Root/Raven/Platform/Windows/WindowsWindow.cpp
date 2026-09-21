#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <iostream>
#include <algorithm>
#include <utility>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Raven/Platform/Windows/WindowsWindow.h"
#include "Raven/Core/Base.h"
#include "Raven/Platform/OpenGL/OpenGLContext.h"
#include "Raven/Platform/Windows/WindowsInput.h"


#include <windows.h>
#include <imm.h>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "imm32.lib")

namespace Raven
{

namespace
{
constexpr wchar_t kIMEBridgeProperty[] = L"Raven.Win32IMEBridge";

// IMM32が返すUTF-16の位置をUI Coreのcodepoint indexへ変換します。
// サロゲートペアを一文字と数えるため、補助平面の文字でもCaretがずれません。
std::size_t CountIMECodepoints(std::wstring_view text, std::size_t utf16Offset)
{
    const std::size_t end = std::min(utf16Offset, text.size());
    std::size_t count = 0u;
    for (std::size_t i = 0u; i < end; ++i)
    {
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF &&
            i + 1u < end && text[i + 1u] >= 0xDC00 && text[i + 1u] <= 0xDFFF)
        {
            ++i;
        }
        ++count;
    }
    return count;
}

std::string ToIMEUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }
    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        output.data(), required, nullptr, nullptr);
    return output;
}

std::wstring ReadIMEString(HIMC context, DWORD kind)
{
    const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
    if (bytes <= 0 || bytes % sizeof(wchar_t) != 0)
    {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
    if (ImmGetCompositionStringW(context, kind, text.data(), static_cast<DWORD>(bytes)) < 0)
    {
        return {};
    }
    return text;
}
} // namespace

struct Win32IMEBridge
{
    HWND Handle = nullptr;
    WNDPROC Previous = nullptr;
    Window::EventCallbackFn* Callback = nullptr;
    Window::IMECaretPositionFn* CaretCallback = nullptr;
    GLFWwindow* GLFWHandle = nullptr;
    bool OwnedByRavenUI = false;
    std::size_t SuppressIMEChars = 0u;

    bool Send(IMECompositionEventType type, std::string text = {},
        std::size_t cursor = 0u)
    {
        if (Callback == nullptr || static_cast<bool>(*Callback) == false)
        {
            return false;
        }
        IMECompositionEvent event(type, std::move(text), cursor);
        (*Callback)(event);
        return event.Handled;
    }

    void CancelComposition()
    {
        if (Handle == nullptr || OwnedByRavenUI == false)
        {
            return;
        }

        // UIの置換対象が変わった後にOSの古い変換が確定されないよう、
        // IME自身へ取消を依頼します。同期的に届くEND通知は既存経路で処理します。
        HIMC context = ImmGetContext(Handle);
        if (context != nullptr)
        {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(Handle, context);
        }
        OwnedByRavenUI = false;
        SuppressIMEChars = 0u;
    }

    void UpdateCandidatePosition()
    {
        if (OwnedByRavenUI == false || CaretCallback == nullptr ||
            static_cast<bool>(*CaretCallback) == false || GLFWHandle == nullptr)
        {
            return;
        }
        float x = 0.0f;
        float y = 0.0f;
        if ((*CaretCallback)(x, y) == false)
        {
            return;
        }

        // GLFWの論理Window座標をWin32 Client pixelへ変換します。
        // DPIが異なるMonitorへ移動しても候補位置をCaretへ追従させます。
        int glfwWidth = 0;
        int glfwHeight = 0;
        RECT client{};
        glfwGetWindowSize(GLFWHandle, &glfwWidth, &glfwHeight);
        if (glfwWidth <= 0 || glfwHeight <= 0 ||
            GetClientRect(Handle, &client) == FALSE)
        {
            return;
        }
        const float scaleX = static_cast<float>(client.right - client.left) / glfwWidth;
        const float scaleY = static_cast<float>(client.bottom - client.top) / glfwHeight;
        const POINT caret{
            static_cast<LONG>(x * scaleX),
            static_cast<LONG>(y * scaleY)
        };

        HIMC context = ImmGetContext(Handle);
        if (context == nullptr)
        {
            return;
        }
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = caret;
        ImmSetCompositionWindow(context, &composition);

        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_CANDIDATEPOS;
        candidate.ptCurrentPos = caret;
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(Handle, context);
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* bridge = static_cast<Win32IMEBridge*>(GetPropW(window, kIMEBridgeProperty));
        if (bridge == nullptr || bridge->Previous == nullptr)
        {
            return DefWindowProcW(window, message, wparam, lparam);
        }

        if (message == WM_IME_STARTCOMPOSITION)
        {
            // Raven UI以外（Dear ImGui等）が入力を所有する場合はGLFWへ従来どおり渡します。
            bridge->OwnedByRavenUI = bridge->Send(IMECompositionEventType::Begin);
            bridge->SuppressIMEChars = 0u;
            bridge->UpdateCandidatePosition();
        }
        else if (message == WM_IME_COMPOSITION && bridge->OwnedByRavenUI == true)
        {
            HIMC context = ImmGetContext(window);
            if (context != nullptr)
            {
                if ((lparam & GCS_RESULTSTR) != 0)
                {
                    const std::wstring result = ReadIMEString(context, GCS_RESULTSTR);
                    if (result.empty() == false)
                    {
                        if (bridge->Send(IMECompositionEventType::Commit, ToIMEUtf8(result)) == true)
                        {
                            // DefWindowProcが続けて発行するWM_IME_CHARを抑止し、
                            // GLFW char callbackから同じ確定結果が二重挿入されるのを防ぎます。
                            bridge->SuppressIMEChars += result.size();
                        }
                    }
                }
                // 同一通知にRESULTとCOMPがある場合は確定結果を優先し、
                // Commit直後に古い未確定文字列を再表示しません。
                if ((lparam & GCS_COMPSTR) != 0 && (lparam & GCS_RESULTSTR) == 0)
                {
                    const std::wstring composition = ReadIMEString(context, GCS_COMPSTR);
                    const LONG rawCursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
                    const std::size_t cursor = rawCursor < 0 ? 0u :
                        CountIMECodepoints(composition, static_cast<std::size_t>(rawCursor));
                    bridge->Send(IMECompositionEventType::Update, ToIMEUtf8(composition), cursor);
                }
                ImmReleaseContext(window, context);
                bridge->UpdateCandidatePosition();
            }
        }
        else if (message == WM_IME_CHAR && bridge->SuppressIMEChars > 0u)
        {
            --bridge->SuppressIMEChars;
            return 0;
        }
        else if (message == WM_IME_ENDCOMPOSITION)
        {
            if (bridge->OwnedByRavenUI == true)
            {
                bridge->Send(IMECompositionEventType::End);
            }
            bridge->OwnedByRavenUI = false;
            bridge->SuppressIMEChars = 0u;
        }
        else if (message == WM_KILLFOCUS)
        {
            if (bridge->OwnedByRavenUI == true)
            {
                bridge->Send(IMECompositionEventType::Cancel);
            }
            bridge->OwnedByRavenUI = false;
            bridge->SuppressIMEChars = 0u;
        }
        return CallWindowProcW(bridge->Previous, window, message, wparam, lparam);
    }

    bool Install(HWND window, GLFWwindow* glfwWindow,
        Window::EventCallbackFn* callback, Window::IMECaretPositionFn* caretCallback)
    {
        if (window == nullptr || callback == nullptr)
        {
            return false;
        }
        Handle = window;
        Callback = callback;
        CaretCallback = caretCallback;
        GLFWHandle = glfwWindow;
        if (SetPropW(Handle, kIMEBridgeProperty, this) == FALSE)
        {
            Handle = nullptr;
            Callback = nullptr;
            return false;
        }
        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(Handle, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&Win32IMEBridge::Procedure));
        if (previous == 0 && GetLastError() != 0)
        {
            RemovePropW(Handle, kIMEBridgeProperty);
            Handle = nullptr;
            Callback = nullptr;
            return false;
        }
        Previous = reinterpret_cast<WNDPROC>(previous);
        return true;
    }

    void Uninstall()
    {
        if (Handle == nullptr)
        {
            return;
        }
        if (Previous != nullptr)
        {
            SetWindowLongPtrW(Handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Previous));
        }
        RemovePropW(Handle, kIMEBridgeProperty);
        Handle = nullptr;
        Previous = nullptr;
        Callback = nullptr;
        CaretCallback = nullptr;
        GLFWHandle = nullptr;
        OwnedByRavenUI = false;
        SuppressIMEChars = 0u;
    }

    ~Win32IMEBridge() { Uninstall(); }
};

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
    glfwWindowHint(GLFW_RESIZABLE, props.Resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, props.Decorated ? GLFW_TRUE : GLFW_FALSE);

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

    if (props.MinWidth > 0 && props.MinHeight > 0)
    {
        glfwSetWindowSizeLimits(m_Window, static_cast<int>(props.MinWidth),
            static_cast<int>(props.MinHeight), GLFW_DONT_CARE, GLFW_DONT_CARE);
    }
    SetVSync(props.VSync);
    if (props.Fullscreen == true)
    {
        SetFullscreen(true);
    }
    else if (props.Maximized == true)
    {
        Maximize();
    }

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

    // Window座標とFramebuffer Pixel数は高DPI環境で一致しないため、別Eventで通知します。
    glfwSetWindowPosCallback(m_Window, [](GLFWwindow* window, int x, int y)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == true)
            {
                WindowMovedEvent event(x, y);
                data.EventCallback(event);
            }
        });

    glfwSetWindowIconifyCallback(m_Window, [](GLFWwindow* window, int iconified)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }
            if (iconified == GLFW_TRUE)
            {
                WindowMinimizedEvent event;
                data.EventCallback(event);
            }
            else
            {
                WindowRestoredEvent event;
                data.EventCallback(event);
            }
        });

    glfwSetWindowMaximizeCallback(m_Window, [](GLFWwindow* window, int maximized)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == false)
            {
                return;
            }
            if (maximized == GLFW_TRUE)
            {
                WindowMaximizedEvent event;
                data.EventCallback(event);
            }
            else
            {
                WindowRestoredEvent event;
                data.EventCallback(event);
            }
        });

    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == true)
            {
                WindowFramebufferResizeEvent event(
                    static_cast<unsigned int>(width), static_cast<unsigned int>(height));
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

    // Key callbackは物理キー操作、char callbackは文字入力として別々に配送します。
    glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (static_cast<bool>(data.EventCallback) == true)
            {
                CharacterTypedEvent event(codepoint);
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
    // GLFWのWndProcが完成してからsubclassし、GLFWとDear ImGuiの既存callbackを保持します。
    m_IMEBridge = std::make_unique<Win32IMEBridge>();
    if (m_IMEBridge->Install(glfwGetWin32Window(m_Window), m_Window,
        &m_Data.EventCallback, &m_Data.IMECaretPositionCallback) == false)
    {
        m_IMEBridge.reset();
        std::cerr << "Failed to install Win32 IME bridge\n";
    }
}

void WindowsWindow::Shutdown()
{
    // Graphics ContextはNative Windowより先に破棄します。
    m_Context.reset();
    m_Input.reset();
    // HWNDが有効な間にGLFWの元WndProcへ戻します。
    m_IMEBridge.reset();

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

WindowState WindowsWindow::GetState() const
{
    if (m_Window == nullptr)
    {
        return WindowState::Normal;
    }
    if (glfwGetWindowMonitor(m_Window) != nullptr)
    {
        return WindowState::Fullscreen;
    }
    if (glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) == GLFW_TRUE)
    {
        return WindowState::Minimized;
    }
    if (glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE)
    {
        return WindowState::Maximized;
    }
    return WindowState::Normal;
}

void WindowsWindow::SetTitle(const std::string& title)
{
    m_Data.Title = title;
    if (m_Window != nullptr)
    {
        glfwSetWindowTitle(m_Window, title.c_str());
    }
}

void WindowsWindow::SetSize(unsigned int width, unsigned int height)
{
    if (m_Window != nullptr && width > 0 && height > 0)
    {
        glfwSetWindowSize(m_Window, static_cast<int>(width), static_cast<int>(height));
    }
}

void WindowsWindow::SetPosition(int x, int y)
{
    if (m_Window != nullptr)
    {
        glfwSetWindowPos(m_Window, x, y);
    }
}

void WindowsWindow::Minimize()
{
    if (m_Window != nullptr)
    {
        glfwIconifyWindow(m_Window);
    }
}

void WindowsWindow::Maximize()
{
    if (m_Window != nullptr)
    {
        glfwMaximizeWindow(m_Window);
    }
}

void WindowsWindow::Restore()
{
    if (m_Window != nullptr)
    {
        if (GetState() == WindowState::Fullscreen)
        {
            SetFullscreen(false);
        }
        glfwRestoreWindow(m_Window);
    }
}

void WindowsWindow::SetFullscreen(bool enabled)
{
    if (m_Window == nullptr || (GetState() == WindowState::Fullscreen) == enabled)
    {
        return;
    }
    if (enabled == true)
    {
        GLFWmonitor* monitor = glfwGetWindowMonitor(m_Window);
        if (monitor == nullptr)
        {
            monitor = glfwGetPrimaryMonitor();
        }
        if (monitor == nullptr)
        {
            return;
        }
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode == nullptr)
        {
            return;
        }
        // Fullscreen前の通常配置を保存し、解除時に元のサイズへ戻します。
        glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
        glfwGetWindowSize(m_Window, &m_WindowedWidth, &m_WindowedHeight);
        glfwSetWindowMonitor(m_Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else
    {
        glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY,
            m_WindowedWidth, m_WindowedHeight, GLFW_DONT_CARE);
    }
}

void WindowsWindow::Show()
{
    if (m_Window != nullptr)
    {
        glfwShowWindow(m_Window);
    }
}

void WindowsWindow::Hide()
{
    if (m_Window != nullptr)
    {
        glfwHideWindow(m_Window);
    }
}

void WindowsWindow::Focus()
{
    if (m_Window != nullptr)
    {
        glfwFocusWindow(m_Window);
    }
}

void WindowsWindow::CancelIMEComposition()
{
    if (m_IMEBridge != nullptr)
    {
        m_IMEBridge->CancelComposition();
    }
}

void WindowsWindow::SetIMECaretPositionCallback(IMECaretPositionFn callback)
{
    m_Data.IMECaretPositionCallback = std::move(callback);
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
