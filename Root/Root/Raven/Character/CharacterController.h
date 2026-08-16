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
    // Dynamic Bodyへ当たった場合はCharacterを止めつつ、接触点へPush Impulseを与えます。
    bool Update(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        std::string* errorMessage = nullptr);

    // ========================================================================
    // Moving Platform対応Update
    // ========================================================================
    // 前Frameに接地していたKinematic Ground EntityのTransform差分をCharacterへ先に適用してから、
    // 通常のPhysics Character Updateを実行します。
    //
    // Platformの移動経路をPhysicsWorld::MovePosition()だけに限定せずTransform差分を直接追跡するため、
    // Animation / Script / Gameplay LogicからKinematic Platformを動かした場合も同じ仕組みで追従できます。
    // Jump時には直前FrameのPlatform水平速度をjumpPlatformHorizontalVelocityScale倍して継承します。
    bool UpdateWithMovingPlatforms(
        const CharacterControllerInput& input,
        float deltaTime,
        Scene& scene,
        TransformComponent& transform,
        float jumpPlatformHorizontalVelocityScale = 1.0f,
        std::string* errorMessage = nullptr);

    // Scene切替 / Teleport / Ragdoll切替など、前FrameのPlatform差分を次Frameへ持ち越してはいけない
    // 境界で呼びます。Crushの継続履歴も同じ境界では無効になるため合わせてResetします。
    void ResetMovingPlatformTracking();

    // Crushの連続時間・累積Exposureだけを明示的に破棄したい場合に使用します。
    // Scene切替やTeleportでは通常ResetMovingPlatformTracking()から同時に呼ばれます。
    void ResetCrushTracking();

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

    bool IsOnMovingPlatform() const { return m_HasMovingPlatform; }
    const math::Vec3& GetMovingPlatformVelocity() const { return m_MovingPlatformVelocity; }
    Entity GetMovingPlatformEntity() const { return m_MovingPlatformEntity; }

    // ========================================================================
    // Crush Detection Result
    // ========================================================================
    // 直近のUpdateWithMovingPlatforms()で、Dynamic Bodyによる押し込みに対して十分に逃げられなかった、
    // または反対方向から複数Bodyに挟まれた場合にtrueになります。
    // Damage / Death / Ragdoll遷移などのGameplay判断はController外で行います。
    bool IsCrushed() const { return m_IsCrushed; }

    // Crushを引き起こしているDynamic Bodyの代表的な水平速度[m/s]です。
    float GetCrushStrength() const { return m_CrushStrength; }

    // 現在のCrushが途切れず継続している時間[秒]です。
    // IsCrushed()==falseになったFrameで0へ戻るため、Gameplay側は例えば0.25秒以上なら
    // Damage開始、0.75秒以上ならRagdoll移行、のように時間閾値を自由に設定できます。
    float GetCrushDuration() const { return m_CrushDuration; }

    // 連続Crush中の strength * deltaTime の累積値です。
    // 単なる継続時間だけでなく「強い圧力ほど早く危険状態へ到達させたい」用途に使用します。
    // Crushが解除されたFrameで0へ戻るため、以前の接触履歴が後から誤発火することはありません。
    float GetCrushExposure() const { return m_CrushExposure; }

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
    // Dynamic Bodyへ当たった場合は接触点へImpulseを与え、Character側は同じBlocking Hitとして扱います。
    // Velocityの壁へ向かう水平成分も同時に除去し、次Frameで同じ壁へ押し込み続けないようにします。
    bool ResolvePhysicsMovement(
        Scene& scene,
        const math::Vec3& horizontalDisplacement,
        TransformComponent& transform,
        std::string* errorMessage);

    // Hit Entityが押せるDynamic Bodyなら接触点へ水平Impulseを与えます。
    // 戻り値は「Hit EntityがDynamic Bodyだったか」で、Impulseが0でもtrueを返します。
    // これによりDynamic Bodyを低いStatic Stepとして誤って乗り越えることを防ぎます。
    bool TryPushDynamicBody(
        Scene& scene,
        const ph::PhysicsCapsuleCastHit& hit);

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

    // ========================================================================
    // Crush Detection State
    // ========================================================================
    // m_IsCrushed / m_CrushStrength は現在Frameだけの瞬間状態です。
    // m_CrushDuration / m_CrushExposure はCrushが連続している間だけFrameを跨いで蓄積し、
    // Pressureが消えたFrameまたはResetCrushTracking()で0へ戻します。
    bool m_IsCrushed = false;
    float m_CrushStrength = 0.0f;
    float m_CrushDuration = 0.0f;
    float m_CrushExposure = 0.0f;

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