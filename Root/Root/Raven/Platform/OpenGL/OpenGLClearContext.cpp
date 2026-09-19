#include "OpenGLClearContext.h"

#include "Raven/Core/Window.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Raven
{
bool OpenGLClearContext::Init(Window& window)
{
    Shutdown();
    if (window.GetBackend() != RHIBackend::OpenGL || window.GetNativeWindow() == nullptr)
    {
        return false;
    }

    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(window.GetNativeWindow());
    // WindowsWindow::Initで生成・初期化済みのOpenGL Contextを使用します。
    // GLADが初期化されていない場合はGL呼び出しを行いません。
    if (glad_glClear == nullptr || glad_glViewport == nullptr)
    {
        return false;
    }

    m_Window = nativeWindow;
    glfwMakeContextCurrent(m_Window);
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    if (width <= 0 || height <= 0)
    {
        Shutdown();
        return false;
    }
    return Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

RHIFrameResult OpenGLClearContext::DrawClearFrame(const float clearColor[4])
{
    // 従来のClear Demo入口を維持し、段階別のFrame APIへ委譲します。
    RHIFrameResult result = BeginFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }
    result = ClearFrame(clearColor);
    if (result != RHIFrameResult::Success)
    {
        // 不正な描画要求ではPresentせず、次Frameを開始できる状態へ戻します。
        m_FrameActive = false;
        m_FrameEnded = false;
        return result;
    }
    result = EndFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }
    return Present();
}

RHIFrameResult OpenGLClearContext::BeginFrame()
{
    if (m_Window == nullptr || m_FrameActive == true)
    {
        return RHIFrameResult::FatalError;
    }

    // OpenGLはSwapChain Image Acquireを持たないため、ContextをCurrentにするだけです。
    glfwMakeContextCurrent(m_Window);
    m_FrameActive = true;
    m_FrameEnded = false;
    return RHIFrameResult::Success;
}

RHIFrameResult OpenGLClearContext::ClearFrame(const float clearColor[4])
{
    if (m_FrameActive == false || m_FrameEnded == true || clearColor == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    return RHIFrameResult::Success;
}

RHIFrameResult OpenGLClearContext::EndFrame()
{
    if (m_FrameActive == false || m_FrameEnded == true)
    {
        return RHIFrameResult::FatalError;
    }

    // OpenGL命令は現在のContextへ発行済み。明示的なQueue Submitは不要です。
    m_FrameEnded = true;
    return RHIFrameResult::Success;
}

RHIFrameResult OpenGLClearContext::Present()
{
    if (m_Window == nullptr || m_FrameActive == false || m_FrameEnded == false)
    {
        return RHIFrameResult::FatalError;
    }

    // Window::OnUpdate側ではSwapしない。Clear DemoのPresentはここだけで行います。
    glfwSwapBuffers(m_Window);
    m_FrameActive = false;
    m_FrameEnded = false;
    return RHIFrameResult::Success;
}

bool OpenGLClearContext::Resize(uint32_t width, uint32_t height)
{
    if (m_Window == nullptr || width == 0 || height == 0 || m_FrameActive == true)
    {
        return false;
    }

    // Windowの論理サイズではなく、共通Demoが取得したFramebuffer実寸法を適用します。
    glfwMakeContextCurrent(m_Window);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return true;
}

void OpenGLClearContext::Shutdown()
{
    // OpenGL Contextの破棄はWindow所有者に任せます。
    m_FrameActive = false;
    m_FrameEnded = false;
    m_Window = nullptr;
}
} // namespace Raven
