// Raven/Character/CharacterRagdollRecoveryRuntime.cpp
#include "Raven/Character/CharacterRagdollRecoveryRuntime.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Raven/Gltf/SkinnedRagdollRuntime.h"
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"
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

float HorizontalLengthSquared(const math::Vec3& value)
{
    return value.x * value.x + value.z * value.z;
}

math::Vec3 ClampHorizontalVelocity(
    const math::Vec3& sourceVelocity,
    float scale,
    float maxSpeed)
{
    math::Vec3 result{
        sourceVelocity.x * scale,
        0.0f,
        sourceVelocity.z * scale
    };

    const float speedSquared = HorizontalLengthSquared(result);
    const float maxSpeedSquared = maxSpeed * maxSpeed;
    if (speedSquared > maxSpeedSquared && speedSquared > math::Epsilon)
    {
        const float speed = std::sqrt(speedSquared);
        const float multiplier = maxSpeed / speed;
        result.x *= multiplier;
        result.z *= multiplier;
    }

    return result;
}

CharacterRagdollRecoveryOrientation ClassifyOrientation(
    const math::Quat& referenceRotation,
    const math::Vec3& faceAxisLocal,
    float threshold)
{
    const math::Vec3 normalizedFaceAxis = faceAxisLocal.Normalized();
    const math::Vec3 faceAxisWorld = referenceRotation.Rotate(normalizedFaceAxis);

    // WorldUp=(0,1,0)とのDotなのでY成分だけで判定できます。
    // Characterの顔方向が上を向いていればFaceUp、下を向いていればFaceDownです。
    const float upDot = faceAxisWorld.y;
    if (upDot >= threshold)
    {
        return CharacterRagdollRecoveryOrientation::FaceUp;
    }
    if (upDot <= -threshold)
    {
        return CharacterRagdollRecoveryOrientation::FaceDown;
    }

    return CharacterRagdollRecoveryOrientation::Side;
}

float ResolveRecoveryYaw(
    const math::Quat& referenceRotation,
    const math::Vec3& faceAxisLocal,
    const math::Vec3& fallbackAxisLocal,
    float fallbackYaw)
{
    // まずCharacterの正面軸をXZ Planeへ投影します。
    // 横倒しではこの値から自然なYawを得られますが、FaceUp / FaceDownでは正面軸が
    // ほぼWorldUp方向を向いて投影長が0へ近付くため、その場合だけ補助軸へ切り替えます。
    math::Vec3 worldAxis = referenceRotation.Rotate(faceAxisLocal.Normalized());
    float horizontalLengthSquared = HorizontalLengthSquared(worldAxis);

    if (horizontalLengthSquared <= 1.0e-6f)
    {
        worldAxis = referenceRotation.Rotate(fallbackAxisLocal.Normalized());
        horizontalLengthSquared = HorizontalLengthSquared(worldAxis);
    }

    if (horizontalLengthSquared <= 1.0e-6f)
    {
        return fallbackYaw;
    }

    return std::atan2(worldAxis.x, worldAxis.z);
}

} // namespace

bool CharacterRagdollRecoveryRuntime::ValidateConfig(
    const CharacterRagdollRecoveryConfig& config,
    std::string* errorMessage) const
{
    if (config.ReferenceBoneName.empty())
    {
        return SetError(errorMessage, "Ragdoll復帰Reference Bone名は空にできません");
    }
    if (std::isfinite(config.BlendDuration) == false
        || std::isfinite(config.FaceUpDownThreshold) == false
        || std::isfinite(config.InheritedHorizontalVelocityScale) == false
        || std::isfinite(config.MaxInheritedHorizontalSpeed) == false)
    {
        return SetError(errorMessage, "Ragdoll復帰Configに非有限値が含まれています");
    }
    if (IsFinite(config.FaceAxisLocal) == false
        || IsFinite(config.YawFallbackAxisLocal) == false)
    {
        return SetError(errorMessage, "Ragdoll復帰Axisに非有限値が含まれています");
    }
    if (config.BlendDuration < 0.0f
        || config.FaceUpDownThreshold < 0.0f
        || config.FaceUpDownThreshold > 1.0f
        || config.InheritedHorizontalVelocityScale < 0.0f
        || config.MaxInheritedHorizontalSpeed < 0.0f)
    {
        return SetError(errorMessage, "Ragdoll復帰Configの範囲が不正です");
    }
    if (config.FaceAxisLocal.LengthSq() <= math::Epsilon
        || config.YawFallbackAxisLocal.LengthSq() <= math::Epsilon)
    {
        return SetError(errorMessage, "Ragdoll復帰Axisは0ベクトルにできません");
    }

    return true;
}

bool CharacterRagdollRecoveryRuntime::Begin(
    Scene& scene,
    Gltf::SkinnedRagdollRuntime& ragdollRuntime,
    CharacterController& characterController,
    TransformComponent& characterTransform,
    const CharacterRagdollRecoveryConfig& config,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_IsRecovering == true)
    {
        return SetError(errorMessage, "Ragdoll復帰Blendは既に進行中です");
    }
    if (ValidateConfig(config, errorMessage) == false)
    {
        return false;
    }

    RagdollRuntime& ragdoll = ragdollRuntime.GetRagdoll();
    if (ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Ragdoll復帰にはBuild済みRagdollRuntimeが必要です");
    }

    // ========================================================================
    // Physics最終Stateを凍結
    // ========================================================================
    // Begin()はPhysics Step後に呼ぶ想定です。Physics Entityが存在する場合は、そのFrameの最終
    // Position / Orientation / Velocityを先にRagdollRuntimeへ同期してからBodyを破棄します。
    // これによりRecovery Blend中はPhysics Entityが無くても、最後の倒れPoseを固定元として使えます。
    if (ragdollRuntime.GetPhysicsBridge().IsCreated() == true)
    {
        if (ragdollRuntime.SyncPhysicsToRagdoll(scene, errorMessage) == false)
        {
            return false;
        }
    }

    const RagdollBodyState* referenceBody = ragdoll.FindBody(config.ReferenceBoneName);
    if (referenceBody == nullptr)
    {
        return SetError(
            errorMessage,
            "Ragdoll復帰Reference Boneに対応するBodyがありません: " + config.ReferenceBoneName);
    }
    if (IsFinite(referenceBody->Position) == false
        || IsFinite(referenceBody->LinearVelocity) == false
        || std::isfinite(referenceBody->Rotation.LengthSq()) == false
        || referenceBody->Rotation.LengthSq() <= math::Epsilon)
    {
        return SetError(errorMessage, "Ragdoll復帰Reference Body Stateが不正です");
    }

    const math::Quat referenceRotation = referenceBody->Rotation.Normalized();
    const CharacterRagdollRecoveryOrientation orientation = ClassifyOrientation(
        referenceRotation,
        config.FaceAxisLocal,
        config.FaceUpDownThreshold);

    const float recoveryYaw = ResolveRecoveryYaw(
        referenceRotation,
        config.FaceAxisLocal,
        config.YawFallbackAxisLocal,
        characterTransform.Rotation.y);

    math::Vec3 recoveryPosition = referenceBody->Position;
    if (config.SnapControllerToGround == true)
    {
        // Ragdoll BodyのPositionはPelvis等のBody中心です。
        // Character Controller RootをそのYへ置くと空中に浮くため、XZだけをRagdollから引き継ぎ、
        // Character Controller自身が持つGround基準面へYを戻します。
        recoveryPosition.y = characterController.GetConfig().GroundHeight;
    }

    const math::Vec3 inheritedVelocity = ClampHorizontalVelocity(
        referenceBody->LinearVelocity,
        config.InheritedHorizontalVelocityScale,
        config.MaxInheritedHorizontalSpeed);

    if (characterController.RestoreAfterRagdoll(
            recoveryPosition,
            recoveryYaw,
            inheritedVelocity,
            config.SnapControllerToGround,
            characterTransform,
            errorMessage) == false)
    {
        return false;
    }

    // Controller Stateを先に確定してからDynamic Bodyを破棄します。
    // 逆順にするとReference Body Entityを失った後に復帰位置を取得できなくなります。
    ragdollRuntime.DestroyPhysicsBodies(scene);

    m_Config = config;
    m_ElapsedTime = 0.0f;
    m_RagdollBlendWeight = 1.0f;
    m_Orientation = orientation;
    m_IsRecovering = true;
    return true;
}

bool CharacterRagdollRecoveryRuntime::Update(
    Gltf::SkinnedRagdollRuntime& ragdollRuntime,
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_IsRecovering == false)
    {
        // 呼び出し側のUpdate Loopを単純に保てるよう、非Recovery中は成功no-opとします。
        return true;
    }
    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "Ragdoll復帰deltaTimeは0以上の有限値である必要があります");
    }

    m_ElapsedTime += deltaTime;

    float progress = 1.0f;
    if (m_Config.BlendDuration > math::Epsilon)
    {
        progress = std::clamp(m_ElapsedTime / m_Config.BlendDuration, 0.0f, 1.0f);
    }

    // ========================================================================
    // Frozen Ragdoll Pose -> Current Animation Pose
    // ========================================================================
    // SkinnedRagdollRuntime::UpdateMesh()は現在Deformer PoseをAnimation側Blend元として使います。
    // したがって呼び出し側は、このUpdate()より先にAnimator / BlendTreeを評価しておく必要があります。
    // Ragdoll側Body StateはPhysics Body破棄時点のまま固定され、weightだけ1 -> 0へ減衰します。
    m_RagdollBlendWeight = 1.0f - progress;
    if (ragdollRuntime.UpdateMesh(
            deltaTime,
            m_RagdollBlendWeight,
            errorMessage) == false)
    {
        return false;
    }

    if (progress >= 1.0f)
    {
        // Blend完了後のAnimation Motion Samplingは、この復帰区間より前の履歴と比較してはいけません。
        // DestroyPhysicsBodies()でもResetしていますが、完了境界でも明示して次Frameを新しい基準にします。
        ragdollRuntime.ResetAnimationMotionHistory();
        m_IsRecovering = false;
        m_RagdollBlendWeight = 0.0f;
    }

    return true;
}

void CharacterRagdollRecoveryRuntime::Reset()
{
    m_Config = CharacterRagdollRecoveryConfig{};
    m_ElapsedTime = 0.0f;
    m_RagdollBlendWeight = 0.0f;
    m_Orientation = CharacterRagdollRecoveryOrientation::Side;
    m_IsRecovering = false;
}

} // namespace Raven
