// Raven/Character/CharacterRagdollRecoveryRuntime.h
#pragma once

#include <string>

#include "Raven/Character/CharacterController.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{
class Scene;

namespace Gltf
{
class SkinnedRagdollRuntime;
}

// ============================================================================
// CharacterRagdollRecoveryOrientation
// ============================================================================
// Ragdoll終了時にCharacterがどの向きで倒れているかを表します。
// 現段階ではAnimation Clip選択をこのRuntime自身では行わず、呼び出し側が
// FaceUp / FaceDown / Sideを見て適切なGet-Up Animationへ切り替えられるよう公開します。
enum class CharacterRagdollRecoveryOrientation
{
    FaceUp,
    FaceDown,
    Side
};

// ============================================================================
// CharacterRagdollRecoveryConfig
// ============================================================================
struct CharacterRagdollRecoveryConfig
{
    // Character Controller位置・向き・倒れ方向を決める基準Boneです。
    // Humanoid glTFではHips / PelvisなどAssetごとに名前が異なるため設定可能にします。
    std::string ReferenceBoneName = "Hips";

    // Frozen Ragdoll Pose -> Animation PoseへBlendする秒数です。
    // 0なら次Updateで即座にAnimation Poseへ戻します。
    float BlendDuration = 0.35f;

    // Reference BoneのYをそのまま使うとPelvis高さへController Rootが浮くため、既定では
    // PhysicsWorld::GroundQuery()でReference Body直下の実床面へControllerを復元します。
    bool SnapControllerToGround = true;

    // Pelvis / HipsなどのReference Bodyから下方向へ床を探す最大距離です。
    // Character通常更新のGroundSnapDistanceより長く取り、横倒ししたPelvisからでも床へ届くようにします。
    float GroundProbeDistance = 3.0f;

    // Physics Ground Queryで床が見つからなかった場合に、既存GroundHeightへfallbackするかです。
    // SceneにまだFloor Colliderが無い既存Sampleとの互換を維持しつつ、新規Sceneでは実Colliderを優先します。
    bool FallbackToControllerGroundHeight = true;

    // Characterの「顔が向く方向」を表すReference Boneローカル軸です。
    // world +YとのDotを使ってFaceUp / FaceDownを判定します。
    math::Vec3 FaceAxisLocal{ 0.0f, 0.0f, 1.0f };

    // FaceAxisがほぼ鉛直でXZへ投影できない場合、Yaw復元に使う補助軸です。
    // Pelvis/Boneの+Yが身体の長軸に近いRigでは、倒れた身体の頭側方向を復元できます。
    math::Vec3 YawFallbackAxisLocal{ 0.0f, 1.0f, 0.0f };

    // abs(dot(FaceAxisWorld, WorldUp))がこの値以上ならFaceUp / FaceDownと判定します。
    // 小さい場合は横向き(Side)として扱います。
    float FaceUpDownThreshold = 0.45f;

    // Ragdoll最後の水平速度をCharacter Controllerへどの程度引き継ぐかです。
    // 0なら完全停止、1ならReference Bodyの水平速度をそのまま使用します。
    float InheritedHorizontalVelocityScale = 0.0f;

    // 衝突直後の大きなRagdoll速度をKinematic Controllerへそのまま渡さないための上限です。
    float MaxInheritedHorizontalSpeed = 3.0f;
};

// ============================================================================
// CharacterRagdollRecoveryRuntime
// ============================================================================
// Dynamic RagdollからKinematic Character Controllerへ制御を戻すためのオーケストレーターです。
//
// 責務:
// 1. PhysicsWorldの最終Ragdoll PoseをRagdollRuntimeへ取り込む
// 2. Reference BoneからCharacter ControllerのWorld XZ / Yawを復元する
// 3. Physics Ground QueryでReference Body直下の実床面へController Rootを復元する
// 4. FaceUp / FaceDown / Sideを判定し、Get-Up Animation選択情報を公開する
// 5. Physics Bodyを破棄してDynamic制御を終了する
// 6. Frozen Ragdoll Poseから現在Animation Poseへ一定時間Blendする
//
// Animation Clipの選択・再生自体はAnimator / BlendTree側の責務として残します。
// Update()を呼ぶFrameでは、先にAnimation Runtimeを評価してDeformerへ「復帰先Pose」を
// 書き込んでから本Runtimeを呼ぶことで、そのPoseへRagdoll Weightを減衰させてBlendできます。
class CharacterRagdollRecoveryRuntime
{
public:
    // Physics駆動Ragdollの終了を開始します。
    // 成功後はPhysics Bodyが破棄予約され、Character ControllerがReference Bone直下の床へ復元されます。
    bool Begin(
        Scene& scene,
        Gltf::SkinnedRagdollRuntime& ragdollRuntime,
        CharacterController& characterController,
        TransformComponent& characterTransform,
        const CharacterRagdollRecoveryConfig& config = {},
        std::string* errorMessage = nullptr);

    // Animation評価後に毎Frame呼びます。
    // Frozen Ragdoll Poseをweight=1 -> 0へ減衰させ、最終的にAnimationだけのPoseへ戻します。
    bool Update(
        Gltf::SkinnedRagdollRuntime& ragdollRuntime,
        float deltaTime,
        std::string* errorMessage = nullptr);

    // Scene切替やCharacter破棄など、Blendを最後まで進めずStateだけ破棄する場合に使用します。
    void Reset();

    bool IsRecovering() const
    {
        return m_IsRecovering;
    }

    float GetRagdollBlendWeight() const
    {
        return m_RagdollBlendWeight;
    }

    CharacterRagdollRecoveryOrientation GetOrientation() const
    {
        return m_Orientation;
    }

private:
    bool ValidateConfig(
        const CharacterRagdollRecoveryConfig& config,
        std::string* errorMessage) const;

private:
    CharacterRagdollRecoveryConfig m_Config{};
    float m_ElapsedTime = 0.0f;
    float m_RagdollBlendWeight = 0.0f;
    CharacterRagdollRecoveryOrientation m_Orientation = CharacterRagdollRecoveryOrientation::Side;
    bool m_IsRecovering = false;
};

} // namespace Raven
