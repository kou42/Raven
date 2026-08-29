#include "Input.h"

#include <GLFW/glfw3.h>

namespace Raven
{

Input* Input::s_Instance = nullptr;

bool Input::IsKeyPressed(int keycode)
{
    if (s_Instance == nullptr)
    {
        return false;
    }

    return s_Instance->IsKeyPressedImpl(keycode);
}

bool Input::IsMouseButtonPressed(int button)
{
    if (s_Instance == nullptr)
    {
        return false;
    }

    return s_Instance->IsMousePressedImpl(button);
}

std::pair<float, float> Input::GetMousePosition()
{
    if (s_Instance == nullptr)
    {
        return { 0.0f, 0.0f };
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

bool Input::IsGamepadConnected(int gamepadIndex)
{
    if (s_Instance == nullptr)
    {
        return false;
    }

    return s_Instance->IsGamepadConnectedImpl(gamepadIndex);
}

bool Input::GetGamepadState(int gamepadIndex, GamepadState& outState)
{
    // 取得失敗時に前Frameの入力が残ると、切断後もCharacterが移動し続けるため、
    // Platform実装を呼ぶ前に必ずNeutral状態へ戻します。
    outState = GamepadState{};

    if (s_Instance == nullptr)
    {
        return false;
    }

    return s_Instance->GetGamepadStateImpl(gamepadIndex, outState);
}

}
