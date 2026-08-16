// Raven/Character/CharacterController.cpp
#include "Raven/Character/CharacterController.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <GLFW/glfw3.h>

#include "Raven/Core/Input.h"
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Scene/Scene.h"

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

bool IsFinite(const math::Vec3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
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
        || std::isfinite(m_Config.CapsuleRadius) == false
        || std::isfinite(m_Config.CapsuleHalfLength) == false
        || std::isfinite(m_Config.CollisionSkinWidth) == false
        || std::isfinite(m_Config.DynamicBodyPushMass) == false
        || std::isfinite(m_Config.DynamicBodyPushScale) == false
        || std::isfinite(m_Config.MaxDynamicBodyPushImpulse) == false
        || std::isfinite(m_Config.MaxStepHeight) == false
        || std::isfinite(m_Config.GroundProbeStartOffset) == false
        || std::isfinite(m_Config.GroundSnapDistance) == false
        || std::isfinite(m_Config.MaxGroundSlopeRadians) == false
        || std::isfinite(m_Config.GroundHeight) == false)
    {
        return SetError(errorMessage, "CharacterController Configに非有限値が含まれています");
    }

    constexpr float HalfPi = 1.57079632679489661923f;
    if (m_Config.WalkSpeed < 0.0f
        || m_Config.RunSpeed < m_Config.WalkSpeed
        || m_Config.Acceleration < 0.0f
        || m_Config.Deceleration < 0.0f
        || m_Config.JumpSpeed < 0.0f
        || m_Config.CapsuleRadius <= 0.0f
        || m_Config.CapsuleHalfLength < 0.0f
        || m_Config.CollisionSkinWidth < 0.0f
        || m_Config.MaxSlideIterations == 0u
        || m_Config.MaxCapsuleCastSubsteps == 0u
        || m_Config.DynamicBodyPushMass <= 0.0f
        || m_Config.DynamicBodyPushScale < 0.0f
        || m_Config.MaxDynamicBodyPushImpulse < 0.0f
        || m_Config.MaxStepHeight < 0.0f
        || m_Config.GroundProbeStartOffset < 0.0f
        || m_Config.GroundSnapDistance < 0.0f
        || m_Config.MaxGroundSlopeRadians < 0.0f
        || m_Config.MaxGroundSlopeRadians > HalfPi)
    {
        return SetError(errorMessage, "CharacterController Configの速度/衝突/Push/Step/Ground Query値が不正です");
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
    // PhysicsWorldを渡さない既存呼び出しはGroundHeight互換経路を使います。
    return UpdateInternal(input, deltaTime, nullptr, transform, errorMessage);
}

bool CharacterController::Update(
    const CharacterControllerInput& input,
    float deltaTime,
    Scene& scene,
    TransformComponent& transform,
    std::string* errorMessage)
{
    // 新しい標準経路ではSceneのPhysicsWorldへGround Query / Capsule Castを行います。
    return UpdateInternal(input, deltaTime, &scene, transform, errorMessage);
}

bool CharacterController::TrySnapToPhysicsGround(
    Scene& scene,
    TransformComponent& transform,
    bool allowSnap,
    std::string* errorMessage)
{
    static_cast<void>(errorMessage);

    if (allowSnap == false)
    {
        return false;
    }

    ph::PhysicsGroundQuerySettings settings{};
    settings.MaxDistance = m_Config.GroundProbeStartOffset + m_Config.GroundSnapDistance;
    settings.MaxSlopeRadians = m_Config.MaxGroundSlopeRadians;
    settings.IncludeStatic = true;
    settings.IncludeKinematic = true;
    settings.IncludeDynamic = false;
    settings.IncludePlanes = true;

    const math::Vec3 probeOrigin = transform.Position
        + math::Vec3{ 0.0f, m_Config.GroundProbeStartOffset, 0.0f };

    ph::PhysicsGroundQueryHit groundHit{};
    if (scene.GetPhysicsWorld().GroundQuery(scene, probeOrigin, settings, groundHit) == false)
    {
        return false;
    }

    transform.Position.y = groundHit.Point.y;
    m_Velocity.y = 0.0f;
    m_Grounded = true;
    m_GroundNormal = groundHit.Normal;
    return true;
}

// NOTE: このファイルは直前の正常Blobから復元する必要があります。
// GitHub Contents APIの単一ファイル更新制約上、以降の完全内容は次コミットで復元します。
} // namespace Raven
