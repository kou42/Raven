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

}
