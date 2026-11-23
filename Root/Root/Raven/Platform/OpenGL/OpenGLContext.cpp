#include "OpenGLContext.h"

#include "Raven/Diagnostics/DiagnosticMeasurement.h"

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Raven
{

OpenGLFrameTimer::OpenGLFrameTimer(GLFWwindow* pWindow)
    : m_pWindow(pWindow)
{
}

OpenGLFrameTimer::~OpenGLFrameTimer()
{
    if (m_Samples.front().Queries.front() == 0)
    {
        return;
    }

    // 補助Windowが最後にCurrentだった場合も、Queryを生成したContextで解放します。
    GLFWwindow* pPrevious = glfwGetCurrentContext();
    glfwMakeContextCurrent(m_pWindow);
    if (glfwGetCurrentContext() == m_pWindow)
    {
        for (const Sample& sample : m_Samples)
        {
            glDeleteQueries(static_cast<GLsizei>(sample.Queries.size()), sample.Queries.data());
        }
    }
    glfwMakeContextCurrent(pPrevious);
}

void OpenGLFrameTimer::Begin()
{
    if (m_pActiveSample != nullptr || m_pWindow == nullptr || Diagnostics::IsMeasurementsEnabled() == false ||
        Diagnostics::IsDiagnosticCategoryEnabled(Diagnostics::DiagnosticCategory::RHI) == false ||
        glfwGetCurrentContext() != m_pWindow)
    {
        return;
    }
    if (m_HasCheckedSupport == false)
    {
        m_HasCheckedSupport = true;
        GLint bits = 0;
        if (glGetQueryiv != nullptr && glGenQueries != nullptr && glDeleteQueries != nullptr &&
            glQueryCounter != nullptr && glGetQueryObjectiv != nullptr && glGetQueryObjectui64v != nullptr)
        {
            glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        }
        m_IsSupported = bits > 0;
    }
    if (m_IsSupported == false)
    {
        return;
    }

    Collect();
    const auto frame = Diagnostics::GetDiagnosticFrameId();
    const auto session = Diagnostics::GetDiagnosticSessionId();
    if (frame == 0 || session == 0)
    {
        return;
    }
    for (Sample& sample : m_Samples)
    {
        if (sample.Frame != 0)
        {
            continue;
        }
        if (sample.Queries.front() == 0)
        {
            glGenQueries(static_cast<GLsizei>(sample.Queries.size()), sample.Queries.data());
            if (sample.Queries.front() == 0 || sample.Queries.back() == 0)
            {
                glDeleteQueries(static_cast<GLsizei>(sample.Queries.size()), sample.Queries.data());
                sample.Queries = {};
                return;
            }
        }
        sample.Frame = frame;
        sample.Session = session;
        m_pActiveSample = &sample;
        glQueryCounter(sample.Queries.front(), GL_TIMESTAMP);
        return;
    }
    Diagnostics::RecordCounter("OpenGL.Main.GPUTimestampSkipped", 1.0, Diagnostics::DiagnosticCategory::RHI);
}

void OpenGLFrameTimer::End()
{
    if (m_pActiveSample == nullptr)
    {
        return;
    }
    if (glfwGetCurrentContext() == m_pWindow)
    {
        glQueryCounter(m_pActiveSample->Queries.back(), GL_TIMESTAMP);
    }
    else
    {
        m_pActiveSample->Frame = 0;
    }
    m_pActiveSample = nullptr;
}

void OpenGLFrameTimer::Collect()
{
    for (Sample& sample : m_Samples)
    {
        if (sample.Frame == 0)
        {
            continue;
        }
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(sample.Queries.back(), GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready != GL_TRUE)
        {
            continue;
        }
        // 終了タイムスタンプが利用可能なら、同一Contextで先に発行した開始値も取得できます。
        GLuint64 begin = 0;
        GLuint64 end = 0;
        glGetQueryObjectui64v(sample.Queries.front(), GL_QUERY_RESULT, &begin);
        glGetQueryObjectui64v(sample.Queries.back(), GL_QUERY_RESULT, &end);
        if (end >= begin)
        {
            Diagnostics::DiagnosticMetric metric;
            metric.SetName("OpenGL.Main.GPUCommandSpanMilliseconds");
            metric.Value = static_cast<double>(end - begin) / 1000000.0;
            metric.FrameId = sample.Frame;
            Diagnostics::EmitMetric(metric, Diagnostics::DiagnosticCategory::RHI, sample.Session);
        }
        sample.Frame = 0;
    }
}

OpenGLContext::OpenGLContext()
{
    m_WindowHandle = nullptr;
}

OpenGLContext::OpenGLContext(GLFWwindow* window)
{
    m_WindowHandle = window;

    if (!m_WindowHandle)
    {
        std::cout << "GLFWwindow is null.\n";
    }

}

void OpenGLContext::Init()
{

    glfwMakeContextCurrent(m_WindowHandle);

    int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    if (!status)
    {
        // 初期化失敗
        std::cout << "Failed to initialize GLAD\n";
        return;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_WindowHandle, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0)
    {
        glViewport(0, 0, framebufferWidth, framebufferHeight);
    }

    std::cout << "OpenGL Info\n";
    std::cout << "  Vendor : " << glGetString(GL_VENDOR) << '\n';
    std::cout << "  Renderer : " << glGetString(GL_RENDERER) << '\n';
    std::cout << "  Version : " << glGetString(GL_VERSION) << '\n';
}

void OpenGLContext::SwapBuffers()
{
    glfwSwapBuffers(m_WindowHandle);
}

}
