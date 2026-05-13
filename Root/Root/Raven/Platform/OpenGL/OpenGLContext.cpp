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
        // ‰Šú‰»Ž¸”s
        std::cout << "Failed to initialize GLAD\n";
        return;
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