// Raven/Gltf/SkinnedBlendTreeRuntimeOneShot.cpp
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"

#include <cmath>
#include <string>

namespace Raven
{
namespace Gltf
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

} // namespace

bool SkinnedBlendTreeRuntime::PlayOneShotAnimation(
    std::size_t skinIndex,
    const std::string& animationName,
    float crossFadeDuration,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (animationName.empty())
    {
        return SetError(errorMessage, "one-shot Animation名は空にできません");
    }
    if (std::isfinite(crossFadeDuration) == false || crossFadeDuration < 0.0f)
    {
        return SetError(errorMessage, "one-shot CrossFade時間は0以上の有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }
    if (state->Configured == false || state->LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "one-shot再生前にLocomotion BlendTreeのConfigureが必要です");
    }

    const RuntimeClip* runtimeClip = FindClip(*state, animationName);
    if (runtimeClip == nullptr || runtimeClip->Clip == nullptr)
    {
        return SetError(errorMessage, "one-shot Animationが見つかりません: " + animationName);
    }

    if (state->OneShotActive == true)
    {
        return SetError(errorMessage, "one-shot Animationは既に再生中です");
    }

    Animator& animator = state->AnimatorInstance;
    const bool previousLoop = animator.IsLooping();

    // ========================================================================
    // Locomotion -> Non-loop Clip
    // ========================================================================
    // Animator::CrossFade()はBlendTree -> Clipも同じState経路で処理できるため、Get-Up専用の
    // Pose Blend実装は作りません。SetLoop(false)は遷移先Clipへも伝播し、終端でIsFinished()を
    // 取得できる状態にします。
    animator.SetLoop(false);
    if (animator.CrossFade(
            runtimeClip->Clip,
            crossFadeDuration,
            true) == false)
    {
        // CrossFade中の再割り込みなどで失敗した場合、元のLoop設定を復元します。
        animator.SetLoop(previousLoop);
        return SetError(errorMessage, "one-shot AnimationへのCrossFadeを開始できません");
    }

    state->OneShotActive = true;
    return true;
}

bool SkinnedBlendTreeRuntime::ReturnToLocomotion(
    std::size_t skinIndex,
    float movementSpeed,
    float crossFadeDuration,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(movementSpeed) == false || movementSpeed < 0.0f)
    {
        return SetError(errorMessage, "Locomotion復帰Movement Speedは0以上の有限値である必要があります");
    }
    if (std::isfinite(crossFadeDuration) == false || crossFadeDuration < 0.0f)
    {
        return SetError(errorMessage, "Locomotion復帰CrossFade時間は0以上の有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }
    if (state->Configured == false || state->LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "Locomotionへ戻すBlendTreeがConfigureされていません");
    }
    if (state->OneShotActive == false)
    {
        // 既にLocomotion中ならParameterだけ最新値へ合わせます。
        return SetMovementSpeed(skinIndex, movementSpeed, errorMessage);
    }

    Animator& animator = state->AnimatorInstance;
    const bool previousLoop = animator.IsLooping();

    // ========================================================================
    // Non-loop Get-Up -> Locomotion BlendTree
    // ========================================================================
    // Ragdoll突入前のMovementSpeedを残したまま戻すと、Get-Up完了直後にRun Poseへ飛ぶ可能性があります。
    // そのため復帰時点のCharacter Controller速度をここでStateへ保存し、その値を遷移先BlendTreeへ
    // 直接渡します。
    state->MovementSpeed = movementSpeed;

    // Get-Up Clipの終端NormalizedTime=1を歩行周期へ持ち込む意味はないためrestart=trueとし、
    // 現在MovementSpeedに対応したIdle / Walk / Run Poseから新しい周期を開始します。
    animator.SetLoop(true);
    if (animator.CrossFadeBlendTree(
            state->LocomotionTree,
            state->MovementSpeed,
            crossFadeDuration,
            true) == false)
    {
        animator.SetLoop(previousLoop);
        return SetError(errorMessage, "one-shot AnimationからLocomotionへ戻せません");
    }

    state->OneShotActive = false;
    return true;
}

bool SkinnedBlendTreeRuntime::IsOneShotAnimationFinished(
    std::size_t skinIndex,
    bool& outFinished,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    outFinished = false;

    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }

    if (state->OneShotActive == true
        && state->AnimatorInstance.IsFinished() == true)
    {
        outFinished = true;
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
