// Raven/Gltf/SkinnedAnimationRuntimeLocomotion.cpp
#include "Raven/Gltf/SkinnedAnimationRuntime.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Raven
{
namespace Gltf
{
namespace
{

bool SetLocomotionError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

LocomotionMode SelectLocomotionMode(
    const LocomotionClipSet& clipSet,
    float movementSpeed)
{
    if (movementSpeed < clipSet.WalkSpeedThreshold)
    {
        return LocomotionMode::Idle;
    }

    if (movementSpeed < clipSet.RunSpeedThreshold)
    {
        return LocomotionMode::Walk;
    }

    return LocomotionMode::Run;
}

const std::string& GetLocomotionAnimationName(
    const LocomotionClipSet& clipSet,
    LocomotionMode mode)
{
    switch (mode)
    {
    case LocomotionMode::Idle:
        return clipSet.IdleAnimationName;
    case LocomotionMode::Walk:
        return clipSet.WalkAnimationName;
    case LocomotionMode::Run:
        return clipSet.RunAnimationName;
    }

    // LocomotionModeは上記3値だけですが、将来enumを拡張した際にも参照切れを起こさないよう、
    // fallbackとしてIdle名を返します。
    return clipSet.IdleAnimationName;
}

bool IsWalkRunTransition(LocomotionMode from, LocomotionMode to)
{
    return (from == LocomotionMode::Walk && to == LocomotionMode::Run)
        || (from == LocomotionMode::Run && to == LocomotionMode::Walk);
}

} // namespace

SkinnedAnimationRuntime::LocomotionState* SkinnedAnimationRuntime::FindLocomotionState(
    std::size_t skinIndex)
{
    const auto it = std::find_if(
        m_LocomotionStates.begin(),
        m_LocomotionStates.end(),
        [skinIndex](const LocomotionState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_LocomotionStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

const SkinnedAnimationRuntime::LocomotionState* SkinnedAnimationRuntime::FindLocomotionState(
    std::size_t skinIndex) const
{
    const auto it = std::find_if(
        m_LocomotionStates.begin(),
        m_LocomotionStates.end(),
        [skinIndex](const LocomotionState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_LocomotionStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

const RuntimeAnimationClip* SkinnedAnimationRuntime::FindRuntimeClip(
    const SkinAnimationState& state,
    const std::string& animationName) const
{
    const auto it = std::find_if(
        state.Clips.begin(),
        state.Clips.end(),
        [&animationName](const RuntimeAnimationClip& clip)
        {
            return clip.Name == animationName;
        });

    if (it == state.Clips.end())
    {
        return nullptr;
    }

    return &(*it);
}

bool SkinnedAnimationRuntime::CrossFade(
    std::size_t skinIndex,
    const std::string& animationName,
    float duration,
    bool restart,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(duration) == false || duration < 0.0f)
    {
        return SetLocomotionError(errorMessage, "CrossFade Durationは0以上の有限値である必要があります");
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetLocomotionError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    const RuntimeAnimationClip* runtimeClip = FindRuntimeClip(*state, animationName);
    if (runtimeClip == nullptr)
    {
        return SetLocomotionError(errorMessage, "指定Animation名がありません: " + animationName);
    }
    if (runtimeClip->Clip == nullptr)
    {
        return SetLocomotionError(errorMessage, "CrossFade対象AnimationClipがnullptrです");
    }

    // Current Motionがまだ無い場合はCrossFade元Poseが無いためAnimator::CrossFade()側が
    // Playへフォールバックします。呼び出し側は初回/遷移中を分岐せず同じAPIを利用できます。
    if (state->AnimatorInstance.CrossFade(runtimeClip->Clip, duration, restart) == false)
    {
        return SetLocomotionError(
            errorMessage,
            "Animator::CrossFade()に失敗しました。CrossFade中の再遷移は現段階では未対応です");
    }

    return true;
}

bool SkinnedAnimationRuntime::ConfigureLocomotion(
    std::size_t skinIndex,
    const LocomotionClipSet& clipSet,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* animationState = FindSkinState(skinIndex);
    if (animationState == nullptr)
    {
        return SetLocomotionError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    if (clipSet.IdleAnimationName.empty()
        || clipSet.WalkAnimationName.empty()
        || clipSet.RunAnimationName.empty())
    {
        return SetLocomotionError(errorMessage, "Idle / Walk / RunのAnimation名は空にできません");
    }

    if (std::isfinite(clipSet.WalkSpeedThreshold) == false
        || std::isfinite(clipSet.RunSpeedThreshold) == false
        || clipSet.WalkSpeedThreshold < 0.0f
        || clipSet.RunSpeedThreshold <= clipSet.WalkSpeedThreshold)
    {
        return SetLocomotionError(
            errorMessage,
            "Locomotion速度閾値は 0 <= WalkSpeedThreshold < RunSpeedThreshold を満たす必要があります");
    }

    if (std::isfinite(clipSet.CrossFadeDuration) == false
        || clipSet.CrossFadeDuration < 0.0f)
    {
        return SetLocomotionError(errorMessage, "Locomotion CrossFadeDurationは0以上の有限値である必要があります");
    }

    // Configure時に3 Clipをすべて解決します。
    // 実際に走り始めてからRunだけ見つからない、といった遅延エラーを避けるためです。
    const RuntimeAnimationClip* idleClip = FindRuntimeClip(*animationState, clipSet.IdleAnimationName);
    const RuntimeAnimationClip* walkClip = FindRuntimeClip(*animationState, clipSet.WalkAnimationName);
    const RuntimeAnimationClip* runClip = FindRuntimeClip(*animationState, clipSet.RunAnimationName);

    if (idleClip == nullptr)
    {
        return SetLocomotionError(errorMessage, "Idle Animationが見つかりません: " + clipSet.IdleAnimationName);
    }
    if (walkClip == nullptr)
    {
        return SetLocomotionError(errorMessage, "Walk Animationが見つかりません: " + clipSet.WalkAnimationName);
    }
    if (runClip == nullptr)
    {
        return SetLocomotionError(errorMessage, "Run Animationが見つかりません: " + clipSet.RunAnimationName);
    }
    if (idleClip->Clip == nullptr || walkClip->Clip == nullptr || runClip->Clip == nullptr)
    {
        return SetLocomotionError(errorMessage, "Locomotion用AnimationClipにnullptrが含まれています");
    }

    LocomotionState* locomotionState = FindLocomotionState(skinIndex);
    if (locomotionState == nullptr)
    {
        LocomotionState state{};
        state.SkinIndex = skinIndex;
        state.ClipSet = clipSet;
        state.CurrentMode = LocomotionMode::Idle;
        state.MovementSpeed = 0.0f;
        state.Configured = true;
        state.Started = false;
        m_LocomotionStates.emplace_back(std::move(state));
    }
    else
    {
        locomotionState->ClipSet = clipSet;
        locomotionState->CurrentMode = LocomotionMode::Idle;
        locomotionState->MovementSpeed = 0.0f;
        locomotionState->Configured = true;
        locomotionState->Started = false;
    }

    // Locomotionは周期Motionとして使うためLoopを明示的に有効化します。
    animationState->AnimatorInstance.SetLoop(true);
    return true;
}

bool SkinnedAnimationRuntime::SetLocomotionSpeed(
    std::size_t skinIndex,
    float movementSpeed,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(movementSpeed) == false || movementSpeed < 0.0f)
    {
        return SetLocomotionError(errorMessage, "Locomotion Speedは0以上の有限値である必要があります");
    }

    SkinAnimationState* animationState = FindSkinState(skinIndex);
    if (animationState == nullptr)
    {
        return SetLocomotionError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    LocomotionState* locomotionState = FindLocomotionState(skinIndex);
    if (locomotionState == nullptr || locomotionState->Configured == false)
    {
        return SetLocomotionError(errorMessage, "指定SkinIndexのLocomotionがConfigureされていません");
    }

    locomotionState->MovementSpeed = movementSpeed;
    const LocomotionMode targetMode = SelectLocomotionMode(
        locomotionState->ClipSet,
        movementSpeed);

    // 初回入力ではBlend元Motionが無いので直接Playします。
    // Idleで開始するとは限らず、Spawn直後から既に移動速度を持つケースにも対応します。
    if (locomotionState->Started == false)
    {
        const std::string& animationName = GetLocomotionAnimationName(
            locomotionState->ClipSet,
            targetMode);

        if (Play(skinIndex, animationName, true, errorMessage) == false)
        {
            return false;
        }

        locomotionState->CurrentMode = targetMode;
        locomotionState->Started = true;
        return true;
    }

    if (targetMode == locomotionState->CurrentMode)
    {
        // 同じ状態ならPlay/CrossFadeを呼び直しません。
        // 毎Frame速度を渡してもAnimation周期が0へ戻らないことが重要です。
        return true;
    }

    // Animatorは現段階でCrossFade中の割り込みを受け付けません。
    // 速度入力自体は最新値へ更新しておき、Fade完了後の次Frameで改めてtargetModeを評価します。
    if (animationState->AnimatorInstance.IsCrossFading())
    {
        return true;
    }

    const std::string& animationName = GetLocomotionAnimationName(
        locomotionState->ClipSet,
        targetMode);

    // Walk <-> Runは歩行周期が対応していることを期待できるためrestart=falseで位相を維持します。
    // Idleを跨ぐ遷移は異なる周期Motionなので先頭から開始し、停止Poseからの不自然な中間位相を避けます。
    const bool preservePhase = IsWalkRunTransition(
        locomotionState->CurrentMode,
        targetMode);
    const bool restart = preservePhase == false;

    if (CrossFade(
            skinIndex,
            animationName,
            locomotionState->ClipSet.CrossFadeDuration,
            restart,
            errorMessage) == false)
    {
        return false;
    }

    locomotionState->CurrentMode = targetMode;
    return true;
}

bool SkinnedAnimationRuntime::GetLocomotionMode(
    std::size_t skinIndex,
    LocomotionMode& outMode,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const LocomotionState* state = FindLocomotionState(skinIndex);
    if (state == nullptr || state->Configured == false)
    {
        return SetLocomotionError(errorMessage, "指定SkinIndexのLocomotionがConfigureされていません");
    }

    outMode = state->CurrentMode;
    return true;
}

} // namespace Gltf
} // namespace Raven
