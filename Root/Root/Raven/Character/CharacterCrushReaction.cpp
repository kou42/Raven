// Raven/Character/CharacterCrushReaction.cpp
#include "Raven/Character/CharacterCrushReaction.h"

#include <cmath>

#include "Raven/Character/CharacterController.h"
#include "Raven/Gltf/SkinnedRagdollRuntime.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace
{

bool SetCrushReactionError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool ValidateCrushReactionConfig(
    const CharacterCrushReactionConfig& config,
    std::string* errorMessage)
{
    if (std::isfinite(config.DamageDelay) == false
        || std::isfinite(config.RagdollDurationThreshold) == false
        || std::isfinite(config.RagdollExposureThreshold) == false)
    {
        return SetCrushReactionError(errorMessage, "Crush Reaction Configに非有限値が含まれています");
    }

    if (config.DamageDelay < 0.0f)
    {
        return SetCrushReactionError(errorMessage, "Crush DamageDelayは0以上である必要があります");
    }

    return true;
}

} // namespace

bool EvaluateCharacterCrushReaction(
    const CharacterController& characterController,
    const CharacterCrushReactionConfig& config,
    CharacterCrushReactionState& outState,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    outState = CharacterCrushReactionState{};

    if (ValidateCrushReactionConfig(config, errorMessage) == false)
    {
        return false;
    }

    // Controllerは「物理的にCrushされているか」だけを計測し、この層で初めてGameplay上の
    // Damage / Ragdoll要求へ変換します。これによりCharacterControllerをHP Systemや
    // Animation/Ragdoll所有関係から独立させたまま保てます。
    if (characterController.IsCrushed() == false)
    {
        return true;
    }

    outState.Strength = characterController.GetCrushStrength();
    outState.Duration = characterController.GetCrushDuration();
    outState.Exposure = characterController.GetCrushExposure();
    outState.ShouldApplyDamage = outState.Duration >= config.DamageDelay;

    const bool durationEnabled = config.RagdollDurationThreshold > 0.0f;
    const bool exposureEnabled = config.RagdollExposureThreshold > 0.0f;
    const bool durationReached = durationEnabled == true
        && outState.Duration >= config.RagdollDurationThreshold;
    const bool exposureReached = exposureEnabled == true
        && outState.Exposure >= config.RagdollExposureThreshold;

    // 両方有効な場合はOR条件です。強いCrushならExposureで早く倒れ、弱いCrushでも
    // 長時間逃げられなければDurationでRagdollへ移行できます。
    outState.ShouldEnterRagdoll = durationReached == true || exposureReached == true;
    return true;
}

bool EnterCharacterRagdollFromCrush(
    Scene& scene,
    Gltf::SkinnedRagdollRuntime& ragdollRuntime,
    CharacterController& characterController,
    const std::string& entityNamePrefix,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (entityNamePrefix.empty())
    {
        return SetCrushReactionError(errorMessage, "Crush RagdollのEntity名Prefixは空にできません");
    }

    // ========================================================================
    // Kinematic Character -> Dynamic Ragdoll
    // ========================================================================
    // Animation評価後にSampleAnimationMotion()を継続している場合、EnterRagdoll()はその最新Poseと
    // 計測済みVelocityを維持します。Samplingしていない従来経路でも現在PoseをCaptureできます。
    if (ragdollRuntime.EnterRagdoll(errorMessage) == false)
    {
        return false;
    }

    // Ragdoll開始PoseをJoint Rest Poseとして採用してからBodyを生成します。
    // この順番を崩すと、開始直後にConstraintが別Poseへ引き戻す原因になります。
    if (ragdollRuntime.InitializeConstraints(errorMessage) == false)
    {
        return false;
    }

    if (ragdollRuntime.CreatePhysicsBodies(scene, entityNamePrefix, errorMessage) == false)
    {
        return false;
    }

    // Character側に残ったPlatform差分やCrush Exposureを、Ragdoll終了後のControllerへ
    // 持ち越さないよう遷移成功後に明示的に破棄します。
    characterController.ResetMovingPlatformTracking();
    characterController.ResetCrushTracking();
    return true;
}

} // namespace Raven
