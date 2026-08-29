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

// Character Controllerが必要とするDevice非依存入力です。
struct CharacterControllerInput
{
    // X: Right(+1) / Left(-1), Y: Forward(+1) / Backward(-1)
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
    float CapsuleRadius = 0.35f;
    float CapsuleHalfLength = 0.55f;
    float CollisionSkinWidth = 0.02f;
    uint32_t MaxSlideIterations = 3u;
    uint32_t MaxCapsuleCastSubsteps = 64u;
    bool EnableDynamicBodyInteraction = true;
    float DynamicBodyPushMass = 60.0f;
    float DynamicBodyPushScale = 1.0f;
    float MaxDynamicBodyPushImpulse = 120.0f;
    float CrushMinIncomingSpeed = 0.15f;
    float CrushBlockedMovementRatio = 0.25f;
    float CrushOpposingVelocityRatio = 0.25f;
    float MaxStepHeight = 0.30f;
    float GroundProbeStartOffset = 0.15f;
    float GroundSnapDistance = 0.30f;
    float MaxGroundSlopeRadians = 0.872664626f;
    float GroundHeight = 0.0f;
};

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

    bool Update(const CharacterControllerInput& input, float deltaTime, TransformComponent& transform, std::string* errorMessage = nullptr);
    bool Update(const CharacterControllerInput& input, float deltaTime, Scene& scene, TransformComponent& transform, std::string* errorMessage = nullptr);
    bool UpdateWithMovingPlatforms(const CharacterControllerInput& input, float deltaTime, Scene& scene, TransformComponent& transform, float jumpPlatformHorizontalVelocityScale = 1.0f, std::string* errorMessage = nullptr);

    void ResetMovingPlatformTracking();
    void ResetCrushTracking();

    // Raven標準Keyboard入力(WASD / Left Shift / Space)をDevice非依存入力へ変換します。
    static CharacterControllerInput ReadDefaultKeyboardInput();

    // Raven標準Gamepad入力をDevice非依存入力へ変換します。
    // 左Stick: Move / A: Jump / RT: Run。
    // Dead Zone外は0..1へ再マッピングするため、Dead Zone境界で速度が急に跳ねません。
    static CharacterControllerInput ReadDefaultGamepadInput(
        int gamepadIndex = 0,
        float stickDeadZone = 0.15f,
        float runTriggerThreshold = 0.25f);

    // KeyboardとGamepadを統合した標準Player入力です。
    // 移動は両Deviceを加算後に長さ1へClampし、Jump/Runはどちらか一方が有効なら有効にします。
    static CharacterControllerInput ReadDefaultPlayerInput(
        int gamepadIndex = 0,
        float stickDeadZone = 0.15f,
        float runTriggerThreshold = 0.25f);

    bool UpdateLocomotionAnimation(Gltf::SkinnedBlendTreeRuntime& animationRuntime, std::size_t skinIndex, std::string* errorMessage = nullptr) const;

    bool RestoreAfterRagdoll(const math::Vec3& worldPosition, float yawRadians, const math::Vec3& inheritedVelocity, bool grounded, TransformComponent& transform, std::string* errorMessage = nullptr);

    const math::Vec3& GetVelocity() const { return m_Velocity; }
    float GetHorizontalSpeed() const;
    bool IsGrounded() const { return m_Grounded; }
    const math::Vec3& GetGroundNormal() const { return m_GroundNormal; }
    bool IsOnMovingPlatform() const { return m_HasMovingPlatform; }
    const math::Vec3& GetMovingPlatformVelocity() const { return m_MovingPlatformVelocity; }
    Entity GetMovingPlatformEntity() const { return m_MovingPlatformEntity; }
    bool IsCrushed() const { return m_IsCrushed; }
    float GetCrushStrength() const { return m_CrushStrength; }
    float GetCrushDuration() const { return m_CrushDuration; }
    float GetCrushExposure() const { return m_CrushExposure; }

private:
    bool ValidateConfig(std::string* errorMessage) const;
    bool UpdateInternal(const CharacterControllerInput& input, float deltaTime, Scene* scene, TransformComponent& transform, std::string* errorMessage);
    bool TrySnapToPhysicsGround(Scene& scene, TransformComponent& transform, bool allowSnap, std::string* errorMessage);

    // Capsule Cast後のWall Slide / Dynamic Pushを含む既存の水平移動解決処理です。
    bool ResolvePhysicsMovement(Scene& scene, const math::Vec3& horizontalDisplacement, TransformComponent& transform, std::string* errorMessage);

    // Hit EntityがDynamic Bodyなら水平Impulseを与えます。
    bool TryPushDynamicBody(Scene& scene, const ph::PhysicsCapsuleCastHit& hit);

    bool TryStepUp(Scene& scene, const math::Vec3& horizontalDisplacement, TransformComponent& transform, std::string* errorMessage);

private:
    CharacterControllerConfig m_Config{};
    math::Vec3 m_Velocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 m_GroundNormal{ 0.0f, 1.0f, 0.0f };
    bool m_Grounded = false;

    bool m_IsCrushed = false;
    float m_CrushStrength = 0.0f;
    float m_CrushDuration = 0.0f;
    float m_CrushExposure = 0.0f;

    Entity m_MovingPlatformEntity{};
    math::Vec3 m_MovingPlatformPosition{};
    math::Vec3 m_MovingPlatformVelocity{};
    bool m_HasMovingPlatform = false;
};

} // namespace Raven
