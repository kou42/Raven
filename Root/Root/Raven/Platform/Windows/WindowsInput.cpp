#include "WindowsInput.h"

#include <GLFW/glfw3.h>

// WindowsWindow.cpp‚Ìg_MainWindow‚ªŽÀ‘Ì
extern GLFWwindow* g_MainWindow;

bool Input::IsKeyPressed(int keycode)
{
    auto state = glfwGetKey(g_MainWindow, keycode);

    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

bool Input::IsMouseButtonPressed(int button)
{
    auto state = glfwGetMouseButton(g_MainWindow, button);

    return state == GLFW_PRESS;
}

std::pair<float, float> Input::GetMousePosition()
{
    double xpos, ypos;

    glfwGetCursorPos(g_MainWindow, &xpos, &ypos);

    return { (float)xpos, (float)ypos };
}

float Input::GetMouseX()
{
    return GetMousePosition().first;
}

float Input::GetMouseY()
{
    return GetMousePosition().second;
}