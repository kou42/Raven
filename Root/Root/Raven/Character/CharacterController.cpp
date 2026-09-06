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
        || std::isfinite(m_Config.SprintSpeed) == false
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
        || m_Config.SprintSpeed < m_Config.RunSpeed
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
    input.Sprint = Input::IsKeyPressed(GLFW_KEY_LEFT_CONTROL);
    input.Jump = Input::IsKeyPressed(GLFW_KEY_SPACE);
    input.Dash = Input::IsKeyPressed(GLFW_KEY_LEFT_ALT);
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
    if (scene.GetPhysicsWorld().GroundQuery(
            scene,
            probeOrigin,
            settings,
            groundHit) == false)
    {
        return false;
    }

    transform.Position.y = groundHit.Point.y;
    m_Velocity.y = 0.0f;
    m_Grounded = true;
    m_GroundNormal = groundHit.Normal;
    return true;
}

bool CharacterController::TryStepUp(
    Scene& scene,
    const math::Vec3& horizontalDisplacement,
    TransformComponent& transform,
    std::string* errorMessage)
{
    static_cast<void>(errorMessage);

    if (m_Grounded == false
        || m_Config.MaxStepHeight <= math::Epsilon
        || horizontalDisplacement.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    ph::PhysicsCapsuleCastSettings castSettings{};
    castSettings.Radius = m_Config.CapsuleRadius;
    castSettings.HalfLength = m_Config.CapsuleHalfLength;
    castSettings.SkinWidth = m_Config.CollisionSkinWidth;
    castSettings.MaxSubsteps = m_Config.MaxCapsuleCastSubsteps;
    castSettings.BinarySearchIterations = 10u;
    castSettings.IncludeStatic = true;
    castSettings.IncludeKinematic = true;
    castSettings.IncludeDynamic = false;
    castSettings.IncludePlanes = true;
    castSettings.IncludeTriggers = false;

    const math::Vec3 raisedStart = transform.Position
        + math::Vec3{ 0.0f, m_Config.MaxStepHeight, 0.0f };

    ph::PhysicsCapsuleCastHit raisedHit{};
    if (scene.GetPhysicsWorld().CapsuleCast(
            scene,
            raisedStart,
            horizontalDisplacement,
            castSettings,
            raisedHit) == true)
    {
        return false;
    }

    const math::Vec3 raisedDestination = raisedStart + horizontalDisplacement;

    ph::PhysicsGroundQuerySettings groundSettings{};
    groundSettings.MaxDistance = m_Config.GroundProbeStartOffset
        + m_Config.MaxStepHeight
        + m_Config.GroundSnapDistance;
    groundSettings.MaxSlopeRadians = m_Config.MaxGroundSlopeRadians;
    groundSettings.IncludeStatic = true;
    groundSettings.IncludeKinematic = true;
    groundSettings.IncludeDynamic = false;
    groundSettings.IncludePlanes = true;

    const math::Vec3 groundProbeOrigin = raisedDestination
        + math::Vec3{ 0.0f, m_Config.GroundProbeStartOffset, 0.0f };

    ph::PhysicsGroundQueryHit groundHit{};
    if (scene.GetPhysicsWorld().GroundQuery(
            scene,
            groundProbeOrigin,
            groundSettings,
            groundHit) == false)
    {
        return false;
    }

    const float stepHeight = groundHit.Point.y - transform.Position.y;
    if (stepHeight > m_Config.MaxStepHeight + 1.0e-4f
        || stepHeight < -m_Config.GroundSnapDistance - 1.0e-4f)
    {
        return false;
    }

    transform.Position.x = raisedDestination.x;
    transform.Position.y = groundHit.Point.y;
    transform.Position.z = raisedDestination.z;
    m_Velocity.y = 0.0f;
    m_Grounded = true;
    m_GroundNormal = groundHit.Normal;
    return true;
}

bool CharacterController::TryPushDynamicBody(
    Scene& scene,
    const ph::PhysicsCapsuleCastHit& hit)
{
    if (m_Config.EnableDynamicBodyInteraction == false)
    {
        return false;
    }

    if (scene.IsEntityAlive(hit.HitEntity) == false)
    {
        return false;
    }

    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(hit.HitEntity.GetIndex());
    if (rigidBody == nullptr
        || rigidBody->Type != BodyType::Dynamic
        || rigidBody->InverseMass <= 0.0f)
    {
        return false;
    }

    math::Vec3 horizontalNormal{ hit.Normal.x, 0.0f, hit.Normal.z };
    const float horizontalNormalLengthSquared = horizontalNormal.LengthSq();
    if (horizontalNormalLengthSquared <= 1.0e-10f)
    {
        return true;
    }
    horizontalNormal /= std::sqrt(horizontalNormalLengthSquared);

    const math::Vec3 characterHorizontalVelocity{ m_Velocity.x, 0.0f, m_Velocity.z };
    const math::Vec3 bodyVelocity = scene.GetPhysicsWorld().GetLinearVelocity(scene, hit.HitEntity);
    const math::Vec3 bodyHorizontalVelocity{ bodyVelocity.x, 0.0f, bodyVelocity.z };
    const math::Vec3 relativeVelocity = characterHorizontalVelocity - bodyHorizontalVelocity;
    const float velocityIntoBody = math::Vec3::Dot(relativeVelocity, horizontalNormal);
    if (velocityIntoBody >= 0.0f || m_Config.DynamicBodyPushScale <= 0.0f)
    {
        return true;
    }

    const float inverseCharacterPushMass = 1.0f / m_Config.DynamicBodyPushMass;
    const float effectiveInverseMass = inverseCharacterPushMass + rigidBody->InverseMass;
    if (effectiveInverseMass <= math::Epsilon)
    {
        return true;
    }

    const float closingSpeed = -velocityIntoBody;
    float impulseMagnitude = (closingSpeed / effectiveInverseMass) * m_Config.DynamicBodyPushScale;

    if (m_Config.MaxDynamicBodyPushImpulse > 0.0f)
    {
        impulseMagnitude = std::min(impulseMagnitude, m_Config.MaxDynamicBodyPushImpulse);
    }

    if (impulseMagnitude <= math::Epsilon)
    {
        return true;
    }

    const math::Vec3 impulse = -horizontalNormal * impulseMagnitude;
    scene.GetPhysicsWorld().AddImpulseAtPoint(
        scene,
        hit.HitEntity,
        impulse,
        hit.Point);

    return true;
}

bool CharacterController::ResolvePhysicsMovement(
    Scene& scene,
    const math::Vec3& horizontalDisplacement,
    TransformComponent& transform,
    std::string* errorMessage)
{
    static_cast<void>(errorMessage);

    if (horizontalDisplacement.LengthSq() <= 1.0e-12f)
    {
        return true;
    }

    ph::PhysicsCapsuleCastSettings castSettings{};
    castSettings.Radius = m_Config.CapsuleRadius;
    castSettings.HalfLength = m_Config.CapsuleHalfLength;
    castSettings.SkinWidth = m_Config.CollisionSkinWidth;
    castSettings.MaxSubsteps = m_Config.MaxCapsuleCastSubsteps;
    castSettings.BinarySearchIterations = 10u;
    castSettings.IncludeStatic = true;
    castSettings.IncludeKinematic = true;
    castSettings.IncludeDynamic = m_Config.EnableDynamicBodyInteraction;
    castSettings.IncludePlanes = true;
    castSettings.IncludeTriggers = false;

    math::Vec3 remainingDisplacement = horizontalDisplacement;

    for (uint32_t iteration = 0u; iteration < m_Config.MaxSlideIterations; ++iteration)
    {
        if (remainingDisplacement.LengthSq() <= 1.0e-10f)
        {
            break;
        }

        ph::PhysicsCapsuleCastHit hit{};
        if (scene.GetPhysicsWorld().CapsuleCast(
                scene,
                transform.Position,
                remainingDisplacement,
                castSettings,
                hit) == false)
        {
            transform.Position += remainingDisplacement;
            remainingDisplacement = math::Vec3{};
            break;
        }

        const bool hitDynamicBody = TryPushDynamicBody(scene, hit);
        if (hitDynamicBody == false)
        {
            if (TryStepUp(
                    scene,
                    remainingDisplacement,
                    transform,
                    errorMessage) == true)
            {
                remainingDisplacement = math::Vec3{};
                break;
            }
        }

        const float safeFraction = std::clamp(hit.Fraction - 1.0e-4f, 0.0f, 1.0f);
        transform.Position += remainingDisplacement * safeFraction;

        math::Vec3 remainingAfterHit = remainingDisplacement * (1.0f - safeFraction);
        const float intoSurface = math::Vec3::Dot(remainingAfterHit, hit.Normal);
        if (intoSurface < 0.0f)
        {
            remainingAfterHit -= hit.Normal * intoSurface;
        }
        else
        {
            remainingAfterHit = math::Vec3{};
        }

        math::Vec3 horizontalNormal{ hit.Normal.x, 0.0f, hit.Normal.z };
        const float horizontalNormalLengthSquared = horizontalNormal.LengthSq();
        if (horizontalNormalLengthSquared > 1.0e-10f)
        {
            horizontalNormal /= std::sqrt(horizontalNormalLengthSquared);
            const math::Vec3 horizontalVelocity{ m_Velocity.x, 0.0f, m_Velocity.z };
            const float velocityIntoSurface = math::Vec3::Dot(horizontalVelocity, horizontalNormal);
            if (velocityIntoSurface < 0.0f)
            {
                const math::Vec3 correctedVelocity = horizontalVelocity
                    - horizontalNormal * velocityIntoSurface;
                m_Velocity.x = correctedVelocity.x;
                m_Velocity.z = correctedVelocity.z;
            }
        }

        remainingDisplacement = remainingAfterHit;
    }

    return true;
}

bool CharacterController::UpdateInternal(
    const CharacterControllerInput& input,
    float deltaTime,
    Scene* scene,
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
    if (input.HasHorizontalVelocityOverride == true
        && (std::isfinite(input.HorizontalVelocityOverride.x) == false
            || std::isfinite(input.HorizontalVelocityOverride.y) == false))
    {
        return SetError(errorMessage, "HorizontalVelocityOverrideは有限値である必要があります");
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

    float targetSpeed = m_Config.WalkSpeed;
    if (input.Sprint == true)
    {
        targetSpeed = m_Config.SprintSpeed;
    }
    else if (input.Run == true)
    {
        targetSpeed = m_Config.RunSpeed;
    }

    math::Vec3 desiredVelocity{ 0.0f, 0.0f, 0.0f };
    if (input.HasHorizontalVelocityOverride == true)
    {
        desiredVelocity.x = input.HorizontalVelocityOverride.x;
        desiredVelocity.z = input.HorizontalVelocityOverride.y;
    }
    else if (hasMoveInput)
    {
        desiredVelocity.x = moveInput.x * targetSpeed;
        desiredVelocity.z = moveInput.y * targetSpeed;
    }

    // Dash等のAction Override中は通常のAcceleration/Decelerationを通すとDash立ち上がりが鈍るため、
    // Gameplay側が指定した水平速度を即座に採用します。ただしその後の位置解決は既存Capsule Castへ流します。
    if (input.HasHorizontalVelocityOverride == true)
    {
        m_Velocity.x = desiredVelocity.x;
        m_Velocity.z = desiredVelocity.z;
    }
    else
    {
        const float horizontalRate = hasMoveInput ? m_Config.Acceleration : m_Config.Deceleration;
        const float maxHorizontalDelta = horizontalRate * deltaTime;
        m_Velocity.x = MoveTowards(m_Velocity.x, desiredVelocity.x, maxHorizontalDelta);
        m_Velocity.z = MoveTowards(m_Velocity.z, desiredVelocity.z, maxHorizontalDelta);
    }

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

    m_Grounded = false;
    m_GroundNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };

    if (scene != nullptr)
    {
        TrySnapToPhysicsGround(
            *scene,
            transform,
            m_Velocity.y <= 0.0f,
            errorMessage);
    }
    else if (transform.Position.y <= m_Config.GroundHeight + 1.0e-4f
        && m_Velocity.y <= 0.0f)
    {
        transform.Position.y = m_Config.GroundHeight;
        m_Velocity.y = 0.0f;
        m_Grounded = true;
    }

    if (input.Jump && m_Grounded)
    {
        m_Velocity.y = m_Config.JumpSpeed;
        m_Grounded = false;
        m_GroundNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };
    }
    else if (m_Grounded == false)
    {
        m_Velocity.y += m_Config.Gravity * deltaTime;
    }

    const math::Vec3 horizontalDisplacement{
        m_Velocity.x * deltaTime,
        0.0f,
        m_Velocity.z * deltaTime
    };

    if (scene != nullptr)
    {
        if (ResolvePhysicsMovement(
                *scene,
                horizontalDisplacement,
                transform,
                errorMessage) == false)
        {
            return false;
        }
    }
    else
    {
        transform.Position += horizontalDisplacement;
    }

    const float verticalDisplacement = m_Velocity.y * deltaTime;
    if (scene != nullptr && verticalDisplacement > 0.0f)
    {
        ph::PhysicsCapsuleCastSettings castSettings{};
        castSettings.Radius = m_Config.CapsuleRadius;
        castSettings.HalfLength = m_Config.CapsuleHalfLength;
        castSettings.SkinWidth = m_Config.CollisionSkinWidth;
        castSettings.MaxSubsteps = m_Config.MaxCapsuleCastSubsteps;
        castSettings.BinarySearchIterations = 10u;
        castSettings.IncludeStatic = true;
        castSettings.IncludeKinematic = true;
        castSettings.IncludeDynamic = false;
        castSettings.IncludePlanes = true;
        castSettings.IncludeTriggers = false;

        const math::Vec3 upwardDisplacement{ 0.0f, verticalDisplacement, 0.0f };
        ph::PhysicsCapsuleCastHit ceilingHit{};
        if (scene->GetPhysicsWorld().CapsuleCast(
                *scene,
                transform.Position,
                upwardDisplacement,
                castSettings,
                ceilingHit) == true)
        {
            const float safeFraction = std::clamp(ceilingHit.Fraction - 1.0e-4f, 0.0f, 1.0f);
            transform.Position += upwardDisplacement * safeFraction;
            m_Velocity.y = 0.0f;
        }
        else
        {
            transform.Position += upwardDisplacement;
        }
    }
    else
    {
        transform.Position.y += verticalDisplacement;
    }

    if (scene != nullptr)
    {
        if (m_Velocity.y <= 0.0f)
        {
            TrySnapToPhysicsGround(*scene, transform, true, errorMessage);
        }
    }
    else if (transform.Position.y < m_Config.GroundHeight
        && m_Velocity.y <= 0.0f)
    {
        transform.Position.y = m_Config.GroundHeight;
        m_Velocity.y = 0.0f;
        m_Grounded = true;
    }

    return true;
}

bool CharacterController::RestoreAfterRagdoll(
    const math::Vec3& worldPosition,
    float yawRadians,
    const math::Vec3& inheritedVelocity,
    bool grounded,
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
    if (IsFinite(worldPosition) == false
        || IsFinite(inheritedVelocity) == false
        || std::isfinite(yawRadians) == false)
    {
        return SetError(errorMessage, "Ragdoll復帰Stateに非有限値が含まれています");
    }

    transform.Position = worldPosition;
    transform.Rotation.x = 0.0f;
    transform.Rotation.y = NormalizeAngle(yawRadians);
    transform.Rotation.z = 0.0f;

    m_Velocity = inheritedVelocity;
    m_Grounded = grounded;
    m_GroundNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };

    if (m_Grounded && m_Velocity.y < 0.0f)
    {
        m_Velocity.y = 0.0f;
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
    return animationRuntime.SetMovementSpeedSprintAware(
        skinIndex,
        GetHorizontalSpeed(),
        errorMessage);
}

} // namespace Raven
