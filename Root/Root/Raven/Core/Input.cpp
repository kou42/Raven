#include "Input.h"

#include <GLFW/glfw3.h>

namespace Raven
{

Input* Input::s_Instance = nullptr;

bool Input::IsKeyPressed(int keycode)
{
    return s_Instance && s_Instance->IsKeyPressedImpl(keycode);
}

bool Input::IsMouseButtonPressed(int button)
{
    return s_Instance && s_Instance->IsMouseButtonPressed(button);
}

std::pair<float, float> Input::GetMousePosition()
{
    if (!s_Instance) {
        return { 0.f, 0.f };
    }
    return s_Instance->GetMousePositionImpl();
}

float Input::GetMouseX()
{
    return GetMousePosition().first;
}
float Input::GetMouseY()
{
    return GetMousePosition().second;
}

#if 0
// WindowsWindow.cppのg_MainWindowが実体
struct GLFWwindow;
//OpenGLContextで前方宣言してるため、ビルドエラーになってしまっている

extern GLFWwindow* g_MainWindow = nullptr;

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
#endif

}