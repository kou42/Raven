#include "Raven/Platform/Windows/WindowsInput.h"
#include <GLFW/glfw3.h>

namespace Raven
{

WindowsInput::WindowsInput(GLFWwindow* window)
{
    m_Window = window;
    s_Instance = this;
}

bool WindowsInput::IsKeyPressedImpl(int keycode)
{
    if (!m_Window) {
        return false;
    }

    auto state = glfwGetKey(m_Window, keycode);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}


bool WindowsInput::IsMousePressedImpl(int button)
{
    if (!m_Window) {
        return false;
    }

    auto state = glfwGetMouseButton(m_Window, button);

    return state == GLFW_PRESS;
}

std::pair<float, float> WindowsInput::GetMousePositionImpl()
{
    double xpos = 0;
    double ypos = 0;

    if (!m_Window) {
        return { xpos, ypos };
    }

    glfwGetCursorPos(m_Window, &xpos, &ypos);

    return { static_cast<float>(xpos), static_cast<float>(ypos) };

}

}