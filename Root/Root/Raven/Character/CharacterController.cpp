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
        || m_Config.GroundProbeStartOffset < 0.0f
        || m_Config.GroundSnapDistance < 0.0f
        || m_Config.MaxGroundSlopeRadians < 0.0f
        || m_Config.MaxGroundSlopeRadians > HalfPi)
    {
        return SetError(errorMessage, "CharacterController Configの速度/加速度/Ground Query値が不正です");
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
    // 新しい標準経路ではSceneのPhysicsWorldへGround Queryを行います。
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

    // ========================================================================
    // Root Foot Point -> Ground Probe Origin
    // ========================================================================
    // Transform::Positionは現在のKinematic Controllerでは足元Rootとして扱います。
    // 少し上からQueryすることで、床へ数mm潜った状態や小段差の上面も安定して検出できます。
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

    // HitがProbe開始位置から近すぎても、GroundQuery側で上向きWalkable Normalを検証済みです。
    // Root Yを実際のContact Pointへ合わせることで、水平GroundHeight固定では不可能だった
    // 上り坂 / 下り坂 / 高さの異なる床へ追従できます。
    transform.Position.y = groundHit.Point.y;
    m_Velocity.y = 0.0f;
    m_Grounded = true;
    m_GroundNormal = groundHit.Normal;
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
    // Character本体はKinematicなので、床のNormalに合わせてPitch/Rollを傾けず直立を維持します。
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
    // 上昇中は下に床があってもSnapするとJumpを即座に打ち消してしまうため、Ground Probeは
    // 鉛直速度が0以下のFrameだけ許可します。
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
        // Legacy fallback: PhysicsWorldを渡さない呼び出しだけ固定水平Groundを使います。
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

    transform.Position.x += m_Velocity.x * deltaTime;
    transform.Position.y += m_Velocity.y * deltaTime;
    transform.Position.z += m_Velocity.z * deltaTime;

    // ========================================================================
    // End-of-frame Ground Snap
    // ========================================================================
    // 水平移動後の新しいXZで再Queryすることで、坂や小段差を降りたFrameにも床へ追従します。
    // GroundProbeStartOffsetの範囲内なら小さな上り段差も上面を検出してRootを持ち上げられます。
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
        // 大きなdeltaTimeでLegacy Groundを突き抜けた場合もFrame末尾で必ずClampします。
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

    // ========================================================================
    // Dynamic Ragdoll -> Kinematic Controller
    // ========================================================================
    // Ragdoll中のCharacter本体TransformはPhysics Boneと独立しているため、復帰時には
    // Reference Boneから解決したWorld位置へController Rootを明示的に移動させます。
    // 倒れていたPitch / Rollを残すと次のKinematic UpdateでもCharacter全体が傾いたままになるため、
    // Controllerが責任を持つYawだけを維持して直立状態へ戻します。
    transform.Position = worldPosition;
    transform.Rotation.x = 0.0f;
    transform.Rotation.y = NormalizeAngle(yawRadians);
    transform.Rotation.z = 0.0f;

    m_Velocity = inheritedVelocity;
    m_Grounded = grounded;
    m_GroundNormal = math::Vec3{ 0.0f, 1.0f, 0.0f };

    // Grounded復帰時にRagdoll最後の下向き速度を残すと、次UpdateのGround判定前後で
    // Characterが一瞬床へ潜る可能性があります。水平慣性は保持しつつ鉛直方向だけ安全に止めます。
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
    // BlendTreeへ渡すのは入力値ではなく「実際の現在水平速度」です。
    // 加減速中のCharacter見た目も物理的な速度へ追従するため、Inputを離した瞬間に
    // AnimationだけIdleへ飛ぶことを防げます。
    return animationRuntime.SetMovementSpeed(
        skinIndex,
        GetHorizontalSpeed(),
        errorMessage);
}

} // namespace Raven
