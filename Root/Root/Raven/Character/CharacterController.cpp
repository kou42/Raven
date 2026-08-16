// Raven/Character/CharacterController.cpp
#include "Raven/Character/CharacterController.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <GLFW/glfw3.h>

#include "Raven/Core/Input.h"
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"

namespace Raven
{
namespace
{

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

float MoveTowards(float current, float target, float maxDelta)
{
    const float delta = target - current;
    if (std::fabs(delta) <= maxDelta)
    {
        return target;
    }

    return current + (delta > 0.0f ? maxDelta : -maxDelta);
}

float NormalizeAngle(float angle)
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = Pi * 2.0f;

    while (angle > Pi)
    {
        angle -= TwoPi;
    }
    while (angle < -Pi)
    {
        angle += TwoPi;
    }

    return angle;
}

} // namespace

bool CharacterController::ValidateConfig(std::string* errorMessage) const
{
    if (std::isfinite(m_Config.WalkSpeed) == false
        || std::isfinite(m_Config.RunSpeed) == false
        || std::isfinite(m_Config.Acceleration) == false
        || std::isfinite(m_Config.Deceleration) == false
        || std::isfinite(m_Config.TurnSpeed) == false
        || std::isfinite(m_Config.Gravity) == false
        || std::isfinite(m_Config.JumpSpeed) == false
        || std::isfinite(m_Config.GroundHeight) == false)
    {
        return SetError(errorMessage, "CharacterController Configに非有限値が含まれています");
    }

    if (m_Config.WalkSpeed < 0.0f
        || m_Config.RunSpeed < m_Config.WalkSpeed
        || m_Config.Acceleration < 0.0f
        || m_Config.Deceleration < 0.0f
        || m_Config.JumpSpeed < 0.0f)
    {
        return SetError(errorMessage, "CharacterController Configの速度/加速度値が不正です");
    }

    return true;
}

CharacterControllerInput CharacterController::ReadDefaultKeyboardInput()
{
    CharacterControllerInput input{};

    if (Input::IsKeyPressed(GLFW_KEY_D))
    {
        input.Move.x += 1.0f;
    }
    if (Input::IsKeyPressed(GLFW_KEY_A))
    {
        input.Move.x -= 1.0f;
    }
    if (Input::IsKeyPressed(GLFW_KEY_W))
    {
        input.Move.y += 1.0f;
    }
    if (Input::IsKeyPressed(GLFW_KEY_S))
    {
        input.Move.y -= 1.0f;
    }

    input.Run = Input::IsKeyPressed(GLFW_KEY_LEFT_SHIFT);
    input.Jump = Input::IsKeyPressed(GLFW_KEY_SPACE);
    return input;
}

bool CharacterController::Update(
    const CharacterControllerInput& input,
    float deltaTime,
    TransformComponent& transform,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (ValidateConfig(errorMessage) == false)
    {
        return false;
    }
    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    math::Vec2 moveInput = input.Move;
    const float inputLengthSquared = moveInput.x * moveInput.x + moveInput.y * moveInput.y;
    if (inputLengthSquared > 1.0f)
    {
        const float inverseLength = 1.0f / std::sqrt(inputLengthSquared);
        moveInput.x *= inverseLength;
        moveInput.y *= inverseLength;
    }

    const bool hasMoveInput = (moveInput.x * moveInput.x + moveInput.y * moveInput.y) > 1.0e-6f;
    const float targetSpeed = input.Run ? m_Config.RunSpeed : m_Config.WalkSpeed;

    math::Vec3 desiredVelocity{ 0.0f, 0.0f, 0.0f };
    if (hasMoveInput)
    {
        desiredVelocity.x = moveInput.x * targetSpeed;
        desiredVelocity.z = moveInput.y * targetSpeed;
    }

    // ========================================================================
    // Horizontal acceleration / deceleration
    // ========================================================================
    // 入力がある間はAcceleration、入力を離した後はDecelerationで0へ戻します。
    // これによりKey入力を直接Positionへ足す実装より、Character Controllerらしい慣性を持たせます。
    const float horizontalRate = hasMoveInput ? m_Config.Acceleration : m_Config.Deceleration;
    const float maxHorizontalDelta = horizontalRate * deltaTime;

    m_Velocity.x = MoveTowards(m_Velocity.x, desiredVelocity.x, maxHorizontalDelta);
    m_Velocity.z = MoveTowards(m_Velocity.z, desiredVelocity.z, maxHorizontalDelta);

    // ========================================================================
    // Facing rotation
    // ========================================================================
    // CharacterのForwardを+Zとして、移動方向へYawだけを向けます。
    // Pitch/Rollは地形傾斜対応を入れるまでは既存値を維持します。
    const float horizontalSpeedSquared = m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z;
    if (horizontalSpeedSquared > 1.0e-6f)
    {
        const float targetYaw = std::atan2(m_Velocity.x, m_Velocity.z);
        const float deltaYaw = NormalizeAngle(targetYaw - transform.Rotation.y);

        if (m_Config.TurnSpeed <= 0.0f)
        {
            transform.Rotation.y = targetYaw;
        }
        else
        {
            const float maxYawDelta = m_Config.TurnSpeed * deltaTime;
            const float appliedYawDelta = std::clamp(deltaYaw, -maxYawDelta, maxYawDelta);
            transform.Rotation.y = NormalizeAngle(transform.Rotation.y + appliedYawDelta);
        }
    }

    // ========================================================================
    // Grounded / Gravity / Jump
    // ========================================================================
    // Stage 5ではまず水平PlaneをGroundとして扱います。
    // 次工程でPhysicsWorldのCollision Queryへ置き換えられるよう、接地判定をこの区画へ隔離します。
    if (transform.Position.y <= m_Config.GroundHeight + 1.0e-4f && m_Velocity.y <= 0.0f)
    {
        transform.Position.y = m_Config.GroundHeight;
        m_Velocity.y = 0.0f;
        m_Grounded = true;
    }
    else
    {
        m_Grounded = false;
    }

    if (input.Jump && m_Grounded)
    {
        m_Velocity.y = m_Config.JumpSpeed;
        m_Grounded = false;
    }
    else if (m_Grounded == false)
    {
        m_Velocity.y += m_Config.Gravity * deltaTime;
    }

    transform.Position.x += m_Velocity.x * deltaTime;
    transform.Position.y += m_Velocity.y * deltaTime;
    transform.Position.z += m_Velocity.z * deltaTime;

    // 大きなdeltaTimeでGroundを突き抜けた場合もFrame末尾で必ずClampします。
    if (transform.Position.y < m_Config.GroundHeight && m_Velocity.y <= 0.0f)
    {
        transform.Position.y = m_Config.GroundHeight;
        m_Velocity.y = 0.0f;
        m_Grounded = true;
    }

    return true;
}

float CharacterController::GetHorizontalSpeed() const
{
    return std::sqrt(m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z);
}

bool CharacterController::UpdateLocomotionAnimation(
    Gltf::SkinnedBlendTreeRuntime& animationRuntime,
    std::size_t skinIndex,
    std::string* errorMessage) const
{
    // BlendTreeへ渡すのは入力値ではなく「実際の現在水平速度」です。
    // 加減速中のCharacter見た目も物理的な速度へ追従するため、Inputを離した瞬間に
    // AnimationだけIdleへ飛ぶことを防げます。
    return animationRuntime.SetMovementSpeed(
        skinIndex,
        GetHorizontalSpeed(),
        errorMessage);
}

} // namespace Raven
