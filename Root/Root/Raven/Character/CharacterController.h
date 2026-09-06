// Raven/Character/CharacterController.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Raven/Character/CharacterDashAction.h"
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

// ============================================================================
// CharacterControllerInput
// ============================================================================
// Character Controllerが必要とする入力をDevice非依存の値へ変換した構造体です。
// Keyboard / GamepadをController内部で直接読むと、将来Input MappingやAI操作へ切り替える際に
// 移動ロジックまで変更する必要が出るため、入力取得と運動計算を明確に分離します。
struct CharacterControllerInput
{
    // X: Right(+1) / Left(-1)
    // Y: Forward(+1) / Backward(-1)
    math::Vec2 Move{ 0.0f, 0.0f };

    // Run / Sprint / Jump / Dashを個別要求として保持します。
    // Dashは通常移動速度の別名ではなく、CharacterDashActionへ渡す一時Action要求です。
    bool Run = false;
    bool Sprint = false;
    bool Jump = false;
    bool Dash = false;

    // Dash以外のGameplay Actionでも通常のWalk/Run/Sprint加減速を一時的に置き換えられる入口です。
    // trueのFrameはHorizontalVelocityOverrideをWorld XZ速度としてそのまま使用しますが、
    // 実際の移動は従来どおりCharacterControllerのCapsule Cast / Step / Wall Slideを通します。
    bool HasHorizontalVelocityOverride = false;
    math::Vec2 HorizontalVelocityOverride{ 0.0f, 0.0f };
};

// ============================================================================
// CharacterControllerConfig
// ============================================================================
struct CharacterControllerConfig
{
    float WalkSpeed = 1.8f;
    float RunSpeed = 5.5f;
    float SprintSpeed = 8.0f;

    // 目標速度へ近付く水平加速度です。
    float Acceleration = 14.0f;
    float Deceleration = 18.0f;

    // rad/s。0以下なら向きを瞬時に移動方向へ合わせます。
    float TurnSpeed = 10.0f;

    float Gravity = -9.81f;
    float JumpSpeed = 4.5f;

    // ========================================================================
    // Character Capsule
    // ========================================================================
    // Physics対応Update()で使用するKinematic Capsuleです。
    // Transform::Positionを足元とし、全高は 2 * (CapsuleHalfLength + CapsuleRadius) です。
    // 既定値では Radius=0.35 / HalfLength=0.55 なので約1.8mのCharacterになります。
    float CapsuleRadius = 0.35f;
    float CapsuleHalfLength = 0.55f;

    // Shape Cast時だけCapsuleを僅かに膨らませる安全距離です。
    // 接触直後の浮動小数誤差で次Frameに壁内部から開始することを抑えます。
    float CollisionSkinWidth = 0.02f;

    // 1Frame中に複数面へ当たった場合のSlide反復上限です。
    // 角へ入った場合でも無限反復せず、壁沿いへ残り変位を投影します。
    uint32_t MaxSlideIterations = 3u;
    uint32_t MaxCapsuleCastSubsteps = 64u;

    // ========================================================================
    // Dynamic Body Interaction
    // ========================================================================
    // trueの場合、水平移動のCapsule CastでDynamic BodyもBlocking Hitとして扱い、
    // Character側は貫通せず、接触したDynamic BodyへImpulseを与えます。
    // Character自身はKinematicのままなので、Physics SolverにCharacter自由度を追加しません。
    bool EnableDynamicBodyInteraction = true;

    // Characterを「押す側の仮想質量」として扱う値です。
    // Dynamic Bodyとの接近速度から reduced mass を計算するために使います。
    // Bodyが重いほど同じImpulseでも速度変化が小さくなり、軽い箱ほど押しやすくなります。
    float DynamicBodyPushMass = 60.0f;

    // 物理式で求めたImpulseへ掛けるGameplay調整倍率です。
    // 0ならDynamic BodyはCharacterを遮りますが、押すImpulseは発生しません。
    float DynamicBodyPushScale = 1.0f;

    // 1回の接触で与えるImpulse上限です。0以下なら上限を設けません。
    // 高速移動や極端なMass設定による過大ImpulseをGameplay側で抑えるために使用します。
    float MaxDynamicBodyPushImpulse = 120.0f;

    // ========================================================================
    // Crush Detection
    // ========================================================================
    // Dynamic Bodyから押されている代表速度がこの値未満ならCrush判定対象にしません。
    // 微小なSolver振動や静止直前の速度を「押し潰し」と誤判定しないための閾値です。
    float CrushMinIncomingSpeed = 0.15f;

    // 要求された押し返し変位に対し、実際にCharacterが移動できた割合がこの値以下なら
    // 壁などに逃げ道を塞がれた候補とします。既定0.25なら25%以下しか逃げられない状態です。
    float CrushBlockedMovementRatio = 0.25f;

    // 反対方向から複数Bodyに押された場合、合成速度が最強の押し速度に対してこの割合以下まで
    // 相殺されたら「両側から挟まれている」と判定します。
    float CrushOpposingVelocityRatio = 0.25f;

    // ========================================================================
    // Step Up / Down
    // ========================================================================
    // 正面Capsule Castが低い障害物へ当たった場合、これだけ足元を持ち上げた位置から同じ水平変位を
    // 再Castします。上側が空いていて、その先にWalkable Groundが見つかれば段差として乗り越えます。
    float MaxStepHeight = 0.30f;

    // GroundSnapDistanceは下り段差のStep Down上限も兼ねます。
    // 水平移動後にこの距離以内の床へSnapするため、小さな階段を下るFrameでAirborneになりません。

    // ========================================================================
    // Ground Probe
    // ========================================================================
    // PhysicsWorldを渡すUpdate()では、Character Rootより少し上から下向きへGroundQueryします。
    // Root位置そのものからRayを始めると、床へ僅かにめり込んだFrameでRay始点がShape内部になり
    // 法線やfraction=0の扱いが不安定になりやすいため、ProbeStartOffsetだけ上から開始します。
    float GroundProbeStartOffset = 0.15f;

    // 現在Root位置よりこの距離以内にWalkable Groundがあれば床へSnapします。
    // 小さな段差を降りる際に毎FrameAirborneへ切り替わることを防ぎます。
    float GroundSnapDistance = 0.30f;

    // Walkableとみなす最大斜面角度[rad]です。既定50度。
    float MaxGroundSlopeRadians = 0.872664626f;

    // Legacy / PhysicsWorldを渡さないUpdate()用の水平Ground高さです。
    // 既存呼び出し互換を維持するため残しますが、新しいCharacter実装ではPhysics Ground Query版
    // Update()を優先してください。
    float GroundHeight = 0.0f;
};

// ============================================================================
// CharacterController
// ============================================================================
// Kinematic Character Controllerです。
//
// Update順:
//   Input -> Desired Horizontal Velocity
//         -> Acceleration / Deceleration
//         -> Facing Rotation
//         -> Physics Ground Query / Gravity / Jump
//         -> Capsule Cast / Dynamic Push / Step Up / Wall Slide
//         -> Vertical Integration
//         -> Ground Snap / Step Down
//
// Character自身をDynamic RigidBodyにすると入力移動とImpulse Solverが同じ自由度を奪い合うため、
// 現段階ではゲームロジックが位置を決定するKinematic Controllerとして実装します。
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

    bool UpdateWithMovingPlatforms(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        float jumpPlatformHorizontalVelocityScale = 1.0f,
        std::string* errorMessage = nullptr);

    void ResetMovingPlatformTracking();
    void ResetCrushTracking();

    static CharacterControllerInput ReadDefaultKeyboardInput();
    static CharacterControllerInput ReadDefaultGamepadInput(
        int gamepadIndex = 0,
        float stickDeadZone = 0.15f,
        float runTriggerThreshold = 0.25f);
    static CharacterControllerInput ReadDefaultPlayerInput(
        int gamepadIndex = 0,
        float stickDeadZone = 0.15f,
        float runTriggerThreshold = 0.25f);

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

    // Dash開始FrameはRoll One-Shot等のAnimation Triggerへ利用できます。
    bool IsDashing() const { return m_DashAction.IsActive(); }
    bool WasDashStartedThisFrame() const { return m_DashAction.WasStartedThisFrame(); }
    const CharacterDashAction& GetDashAction() const { return m_DashAction; }
    CharacterDashAction& GetDashAction() { return m_DashAction; }

    bool IsOnMovingPlatform() const { return m_HasMovingPlatform; }
    const math::Vec3& GetMovingPlatformVelocity() const { return m_MovingPlatformVelocity; }
    Entity GetMovingPlatformEntity() const { return m_MovingPlatformEntity; }

    bool IsCrushed() const { return m_IsCrushed; }
    float GetCrushStrength() const { return m_CrushStrength; }
    float GetCrushDuration() const { return m_CrushDuration; }
    float GetCrushExposure() const { return m_CrushExposure; }

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
    CharacterDashAction m_DashAction{};
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
