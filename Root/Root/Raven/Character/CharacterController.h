// Raven/Character/CharacterController.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;

namespace Gltf
{
class SkinnedBlendTreeRuntime;
}

namespace ph
{
struct PhysicsCapsuleCastHit;
}

struct CharacterControllerInput
{
    // X: Right(+1) / Left(-1)
    // Y: Forward(+1) / Backward(-1)
    math::Vec2 Move{ 0.0f, 0.0f };
    bool Run = false;
    bool Jump = false;
};

struct CharacterControllerConfig
{
    float WalkSpeed = 1.8f;
    float RunSpeed = 5.5f;
    float Acceleration = 14.0f;
    float Deceleration = 18.0f;
    float TurnSpeed = 10.0f;
    float Gravity = -9.81f;
    float JumpSpeed = 4.5f;

    // ========================================================================
    // Character Capsule
    // ========================================================================
    // Physics対応Update()で使用するKinematic Capsuleです。
    // Transform::Positionを足元とし、全高は 2 * (CapsuleHalfLength + CapsuleRadius) です。
    float CapsuleRadius = 0.35f;
    float CapsuleHalfLength = 0.55f;
    float CollisionSkinWidth = 0.02f;
    uint32_t MaxSlideIterations = 3u;
    uint32_t MaxCapsuleCastSubsteps = 64u;

    // ========================================================================
    // Dynamic Body Interaction
    // ========================================================================
    // Character自身はKinematicのまま、Dynamic BodyをBlocking Hitとして扱います。
    bool EnableDynamicBodyInteraction = true;
    float DynamicBodyPushMass = 60.0f;
    float DynamicBodyPushScale = 1.0f;
    float MaxDynamicBodyPushImpulse = 120.0f;

    // ========================================================================
    // Crush Detection
    // ========================================================================
    // Dynamic Bodyから押されている速度がこの値未満ならCrush判定対象にしません。
    // 微小なSolver振動や静止直前の速度を「押し潰し」と誤判定しないための閾値です。
    float CrushMinIncomingSpeed = 0.15f;

    // 要求された押し返し変位に対し、実際にCharacterが移動できた割合がこの値以下なら
    // 壁などに逃げ道を塞がれた候補とします。0.25なら要求変位の25%以下しか逃げられない状態です。
    float CrushBlockedMovementRatio = 0.25f;

    // 反対方向から複数Bodyに押された場合、各押し返し速度の平均が最大速度に対して
    // この割合以下まで相殺されたら「両側から挟まれている」と判定します。
    float CrushOpposingVelocityRatio = 0.25f;

    // ========================================================================
    // Step Up / Down
    // ========================================================================
    float MaxStepHeight = 0.30f;

    // ========================================================================
    // Ground Probe
    // ========================================================================
    float GroundProbeStartOffset = 0.15f;
    float GroundSnapDistance = 0.30f;
    float MaxGroundSlopeRadians = 0.872664626f;
    float GroundHeight = 0.0f;
};

// ============================================================================
// CharacterController
// ============================================================================
// Character自身をDynamic RigidBodyにせず、ゲームロジックが位置を決定するKinematic Controllerです。
class CharacterController
{
public:
    CharacterController() = default;
    explicit CharacterController(const CharacterControllerConfig& config)
        : m_Config(config)
    {
    }

    void SetConfig(const CharacterControllerConfig& config) { m_Config = config; }
    const CharacterControllerConfig& GetConfig() const { return m_Config; }

    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    // 前Frameに接地していたKinematic Groundの差分を適用し、Dynamic Bodyからの押し返しも処理します。
    bool UpdateWithMovingPlatforms(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        float jumpPlatformHorizontalVelocityScale = 1.0f,
        std::string* errorMessage = nullptr);

    void ResetMovingPlatformTracking();
    static CharacterControllerInput ReadDefaultKeyboardInput();

    bool UpdateLocomotionAnimation(
        Gltf::SkinnedBlendTreeRuntime& animationRuntime,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr) const;

    bool RestoreAfterRagdoll(
        const math::Vec3& worldPosition,
        float yawRadians,
        const math::Vec3& inheritedVelocity,
        bool grounded,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    const math::Vec3& GetVelocity() const { return m_Velocity; }
    float GetHorizontalSpeed() const;
    bool IsGrounded() const { return m_Grounded; }
    const math::Vec3& GetGroundNormal() const { return m_GroundNormal; }
    bool IsOnMovingPlatform() const { return m_HasMovingPlatform; }
    const math::Vec3& GetMovingPlatformVelocity() const { return m_MovingPlatformVelocity; }
    Entity GetMovingPlatformEntity() const { return m_MovingPlatformEntity; }

    // ========================================================================
    // Crush Detection Result
    // ========================================================================
    // UpdateWithMovingPlatforms()の直近Frameで、Dynamic Bodyによる押し込みに対して
    // Characterが十分に逃げられなかった、または反対方向から挟まれた場合にtrueになります。
    // Damage / Death / Ragdoll遷移などのGameplay判断はController外でこの状態を参照してください。
    bool IsCrushed() const { return m_IsCrushed; }

    // Crushを引き起こしているDynamic Bodyの代表的な水平速度[m/s]です。
    // Damage量などへ利用できますが、Controller自身はDamage処理を行いません。
    float GetCrushStrength() const { return m_CrushStrength; }

private:
    bool ValidateConfig(std::string* errorMessage) const;

    bool UpdateInternal(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene* scene,
        TransformComponent& transform,
        std::string* errorMessage);

    bool TrySnapToPhysicsGround(
        Scene& scene,
        TransformComponent& transform,
        bool allowSnap,
        std::string* errorMessage);

    bool ResolvePhysicsMovement(
        Scene& scene,
        const math::Vec3& horizontalDisplacement,
        TransformComponent& transform,
        std::string* errorMessage);

    bool TryPushDynamicBody(
        Scene& scene,
        const ph::PhysicsCapsuleCastHit& hit);

    bool TryStepUp(
        Scene& scene,
        const math::Vec3& horizontalDisplacement,
        TransformComponent& transform,
        std::string* errorMessage);

private:
    CharacterControllerConfig m_Config{};
    math::Vec3 m_Velocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 m_GroundNormal{ 0.0f, 1.0f, 0.0f };
    bool m_Grounded = false;

    // ========================================================================
    // Crush Detection State
    // ========================================================================
    // Frameを跨いでLatchしません。UpdateWithMovingPlatforms()開始時に必ずResetし、そのFrameの
    // Dynamic Interaction結果だけを公開します。これによりBodyが離れた後もCrush状態が残りません。
    bool m_IsCrushed = false;
    float m_CrushStrength = 0.0f;

    // ========================================================================
    // Moving Platform Tracking
    // ========================================================================
    // Entity HandleはGenerationを含むため、Platform破棄後に同じIndexが再利用されても古い追跡状態を
    // 新Entityへ誤適用しません。Positionは前FrameのKinematic Ground Transform位置です。
    Entity m_MovingPlatformEntity{};
    math::Vec3 m_MovingPlatformPosition{};
    math::Vec3 m_MovingPlatformVelocity{};
    bool m_HasMovingPlatform = false;
};

} // namespace Raven