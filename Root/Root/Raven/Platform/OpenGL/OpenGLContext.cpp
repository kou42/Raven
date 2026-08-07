#include "OpenGLContext.h"

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Raven
{

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