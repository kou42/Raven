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
    if (m_Window == nullptr)
    {
        return false;
    }

    const auto state = glfwGetKey(m_Window, keycode);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

bool WindowsInput::IsMousePressedImpl(int button)
{
    if (m_Window == nullptr)
    {
        return false;
    }

    const auto state = glfwGetMouseButton(m_Window, button);
    return state == GLFW_PRESS;
}

std::pair<float, float> WindowsInput::GetMousePositionImpl()
{
    double xpos = 0.0;
    double ypos = 0.0;

    if (m_Window == nullptr)
    {
        return { 0.0f, 0.0f };
    }

    glfwGetCursorPos(m_Window, &xpos, &ypos);
    return { static_cast<float>(xpos), static_cast<float>(ypos) };
}

bool WindowsInput::IsGamepadConnectedImpl(int gamepadIndex)
{
    // Joystickとして接続されているだけでなく、GLFWの標準Gamepad Mappingを
    // 利用できるDeviceか確認します。これにより上位層はController固有のButton番号を
    // 意識せず、A/B/X/YやStickを共通レイアウトとして扱えます。
    if (glfwJoystickPresent(gamepadIndex) == GLFW_FALSE)
    {
        return false;
    }

    if (glfwJoystickIsGamepad(gamepadIndex) == GLFW_FALSE)
    {
        return false;
    }

    return true;
}

bool WindowsInput::GetGamepadStateImpl(int gamepadIndex, GamepadState& outState)
{
    outState = GamepadState{};

    if (IsGamepadConnectedImpl(gamepadIndex) == false)
    {
        return false;
    }

    GLFWgamepadstate glfwState{};
    if (glfwGetGamepadState(gamepadIndex, &glfwState) == GLFW_FALSE)
    {
        return false;
    }

    outState.LeftStickX = glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];

    // GLFWはStick上方向を負値として返します。
    // RavenのCharacterControllerInputではForwardを+Yとしているため、
    // Device境界でYだけ反転して上位層の座標規約を統一します。
    outState.LeftStickY = -glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    outState.RightStickX = glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
    outState.RightStickY = -glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];

    // GLFW Triggerは通常 -1(未入力) ～ +1(全押し) です。
    // Input共通APIでは扱いやすい0～1へ正規化して公開します。
    outState.LeftTrigger = (glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
    outState.RightTrigger = (glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;

    outState.ButtonA = glfwState.buttons[GLFW_GAMEPAD_BUTTON_A] == GLFW_PRESS;
    outState.ButtonB = glfwState.buttons[GLFW_GAMEPAD_BUTTON_B] == GLFW_PRESS;
    outState.ButtonX = glfwState.buttons[GLFW_GAMEPAD_BUTTON_X] == GLFW_PRESS;
    outState.ButtonY = glfwState.buttons[GLFW_GAMEPAD_BUTTON_Y] == GLFW_PRESS;
    outState.LeftBumper = glfwState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] == GLFW_PRESS;
    outState.RightBumper = glfwState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] == GLFW_PRESS;
    outState.Back = glfwState.buttons[GLFW_GAMEPAD_BUTTON_BACK] == GLFW_PRESS;
    outState.Start = glfwState.buttons[GLFW_GAMEPAD_BUTTON_START] == GLFW_PRESS;
    outState.LeftThumb = glfwState.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] == GLFW_PRESS;
    outState.RightThumb = glfwState.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] == GLFW_PRESS;

    return true;
}

}
