// Raven/Character/CharacterControllerInput.cpp
#include "Raven/Character/CharacterController.h"

#include <algorithm>
#include <cmath>

#include "Raven/Core/Input.h"

namespace Raven
{
namespace
{

math::Vec2 ApplyCircularDeadZone(const math::Vec2& stick, float deadZone)
{
    // 不正な設定値が渡されても入力変換が破綻しないよう、Dead Zoneは0..1未満へ制限します。
    // 1.0ちょうどでは再マッピング時の除算が0になるため、上限は僅かに小さい値にします。
    const float clampedDeadZone = std::clamp(deadZone, 0.0f, 0.999f);
    const float lengthSquared = stick.x * stick.x + stick.y * stick.y;
    const float deadZoneSquared = clampedDeadZone * clampedDeadZone;

    if (lengthSquared <= deadZoneSquared)
    {
        return math::Vec2{ 0.0f, 0.0f };
    }

    const float length = std::sqrt(lengthSquared);
    if (length <= 1.0e-6f)
    {
        return math::Vec2{ 0.0f, 0.0f };
    }

    // 実機Stickは斜め方向で長さが1を僅かに超える場合があるため、Magnitudeを先にClampします。
    // Dead Zone境界を0、最大傾きを1へ線形再マッピングすることで、境界直後の速度ジャンプを防ぎます。
    const float clampedLength = std::min(length, 1.0f);
    const float remappedLength = (clampedLength - clampedDeadZone) / (1.0f - clampedDeadZone);
    const float scale = remappedLength / length;

    return math::Vec2{ stick.x * scale, stick.y * scale };
}

math::Vec2 ClampMoveLength(const math::Vec2& move)
{
    const float lengthSquared = move.x * move.x + move.y * move.y;
    if (lengthSquared <= 1.0f)
    {
        return move;
    }

    const float length = std::sqrt(lengthSquared);
    if (length <= 1.0e-6f)
    {
        return math::Vec2{ 0.0f, 0.0f };
    }

    return math::Vec2{ move.x / length, move.y / length };
}

} // namespace

CharacterControllerInput CharacterController::ReadDefaultGamepadInput(
    int gamepadIndex,
    float stickDeadZone,
    float runTriggerThreshold)
{
    CharacterControllerInput input{};
    GamepadState gamepadState{};

    if (Input::GetGamepadState(gamepadIndex, gamepadState) == false)
    {
        return input;
    }

    // WindowsInput側でGLFWのY軸をRavenのForward(+Y)へ変換済みなので、
    // Character側ではDevice固有の符号を意識せず、そのままMoveへ渡せます。
    input.Move = ApplyCircularDeadZone(
        math::Vec2{ gamepadState.LeftStickX, gamepadState.LeftStickY },
        stickDeadZone);

    input.Jump = gamepadState.ButtonA;

    const float clampedRunThreshold = std::clamp(runTriggerThreshold, 0.0f, 1.0f);
    input.Run = gamepadState.RightTrigger >= clampedRunThreshold;
    return input;
}

CharacterControllerInput CharacterController::ReadDefaultPlayerInput(
    int gamepadIndex,
    float stickDeadZone,
    float runTriggerThreshold)
{
    const CharacterControllerInput keyboard = ReadDefaultKeyboardInput();
    const CharacterControllerInput gamepad = ReadDefaultGamepadInput(
        gamepadIndex,
        stickDeadZone,
        runTriggerThreshold);

    CharacterControllerInput input{};

    // KeyboardとGamepadを同時に触った場合も斜め速度がsqrt(2)倍にならないよう、
    // 合成後のMoveを単位円内へClampします。
    input.Move = ClampMoveLength(math::Vec2{
        keyboard.Move.x + gamepad.Move.x,
        keyboard.Move.y + gamepad.Move.y
    });

    input.Run = keyboard.Run || gamepad.Run;
    input.Jump = keyboard.Jump || gamepad.Jump;
    return input;
}

} // namespace Raven
