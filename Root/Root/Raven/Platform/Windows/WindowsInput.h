#pragma once

#include "Raven/Core/Input.h"

struct GLFWwindow;

namespace Raven
{

class WindowsInput final : public Input
{
public:
    explicit WindowsInput(GLFWwindow* window);

protected:
    virtual bool IsKeyPressedImpl(int keycode) override;
    virtual bool IsMousePressedImpl(int button) override;
    virtual std::pair<float, float> GetMousePositionImpl() override;
    virtual bool IsGamepadConnectedImpl(int gamepadIndex) override;
    virtual bool GetGamepadStateImpl(int gamepadIndex, GamepadState& outState) override;

private:
    GLFWwindow* m_Window = nullptr;
};

}
