// Raven/Character/CharacterController.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"

namespace Raven
{
class Scene;

namespace Gltf
{
class SkinnedBlendTreeRuntime;
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

    bool Run = false;
    bool Jump = false;
};

// ============================================================================
// CharacterControllerConfig
// ============================================================================
struct CharacterControllerConfig
{
    float WalkSpeed = 1.8f;
    float RunSpeed = 5.5f;

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
//         -> Capsule Cast / Step Up / Wall Slide
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

    // ========================================================================
    // Legacy Ground Update
    // ========================================================================
    // PhysicsWorldを持たない既存呼び出し互換用です。
    // GroundHeightの水平Planeを床として扱います。新規コードでは下のScene版を優先します。
    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    // ========================================================================
    // Physics Character Update
    // ========================================================================
    // PhysicsWorld::GroundQuery()で床を取得し、PhysicsWorld::CapsuleCast()で壁貫通を防ぎます。
    // 衝突後の残り水平変位は接触面へ投影してSlideさせるため、斜め入力で壁へ入った場合も
    // 完全停止せず壁沿いの成分を維持します。低い障害物はMaxStepHeight以内ならStep Upします。
    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    // Raven標準Keyboard入力(WASD / Left Shift / Space)をDevice非依存入力へ変換します。
    // Input Mapping System導入後はこの関数だけを置き換え、運動計算は維持できます。
    static CharacterControllerInput ReadDefaultKeyboardInput();

    // Stage 4 BlendTreeとの接続用Helperです。
    // Controller自身はAnimation Runtimeを所有せず、実際の水平速度だけをParameterとして渡します。
    bool UpdateLocomotionAnimation(
        Gltf::SkinnedBlendTreeRuntime& animationRuntime,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr) const;

    // ========================================================================
    // Ragdoll -> Character Controller State Restore
    // ========================================================================
    // Dynamic RagdollからKinematic Character Controllerへ制御を戻す瞬間に使用します。
    // 通常のUpdate()を1回通して位置を合わせるのではなく、Ragdoll最終Poseから決定した
    // World Position / Yaw / Velocityを原子的にController Stateへ反映します。
    //
    // Pitch / RollはRagdollの倒れ姿勢をKinematic Controllerへ持ち越さず0へ戻します。
    // grounded=trueの場合は下向き速度を0へClampし、復帰直後に床へ潜ることを防ぎます。
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

    // 最後にPhysics Ground Queryで採用した床Normalです。
    // Legacy UpdateやAirborne中はWorld Upを返します。
    const math::Vec3& GetGroundNormal() const { return m_GroundNormal; }

private:
    bool ValidateConfig(std::string* errorMessage) const;

    // 共通運動ロジックです。scene == nullptrならLegacy GroundHeight、SceneありならPhysics Queryを使います。
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

    // 水平変位をCapsule Castし、最初の接触まで移動した後、残り変位を接触面へ投影してSlideします。
    // Velocityの壁へ向かう水平成分も同時に除去し、次Frameで同じ壁へ押し込み続けないようにします。
    bool ResolvePhysicsMovement(
        Scene& scene,
        const math::Vec3& horizontalDisplacement,
        TransformComponent& transform,
        std::string* errorMessage);

    // 低い障害物へ当たったときだけ、MaxStepHeight上から同じ変位を再Castして上面へ着地できるか調べます。
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
};

} // namespace Raven
