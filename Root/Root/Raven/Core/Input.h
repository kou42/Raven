#pragma once

#include <utility>

namespace Raven
{

// ============================================================================
// GamepadState
// ============================================================================
// Platform固有のGamepad状態をGameplay側へ直接公開しないための共通状態です。
// CharacterControllerなどの上位層はGLFWgamepadstateへ依存せず、この構造体だけを参照します。
struct GamepadState
{
    float LeftStickX = 0.0f;
    float LeftStickY = 0.0f;
    float RightStickX = 0.0f;
    float RightStickY = 0.0f;
    float LeftTrigger = 0.0f;
    float RightTrigger = 0.0f;

    bool ButtonA = false;
    bool ButtonB = false;
    bool ButtonX = false;
    bool ButtonY = false;
    bool LeftBumper = false;
    bool RightBumper = false;
    bool Back = false;
    bool Start = false;
    bool LeftThumb = false;
    bool RightThumb = false;
};

class Input
{
public:
    virtual ~Input() = default;

    static bool IsKeyPressed(int keycode);
    static bool IsMouseButtonPressed(int button);
    static std::pair<float, float> GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();

    // GLFW_JOYSTICK_1などのGamepad indexを受け取ります。
    // Platform層で標準Gamepad Mappingとして認識できるDeviceだけを接続済みとみなします。
    static bool IsGamepadConnected(int gamepadIndex);

    // 接続済みGamepadの現在状態を取得します。
    // 取得失敗時はfalseを返し、outStateは既定値へ戻します。
    static bool GetGamepadState(int gamepadIndex, GamepadState& outState);

protected:
    virtual bool IsKeyPressedImpl(int keycode) = 0;
    virtual bool IsMousePressedImpl(int button) = 0;
    virtual std::pair<float, float> GetMousePositionImpl() = 0;
    virtual bool IsGamepadConnectedImpl(int gamepadIndex) = 0;
    virtual bool GetGamepadStateImpl(int gamepadIndex, GamepadState& outState) = 0;

protected:
    static Input* s_Instance;

#if 0
public:
    static bool IsKeyPressed(int keycode);

    static bool IsMouseButtonPressed(int button);

    static std::pair<float, float> GetMousePosition();

    static float GetMouseX();
    static float GetMouseY();
#endif
};

}
