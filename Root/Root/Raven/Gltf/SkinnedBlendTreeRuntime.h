// Raven/Gltf/SkinnedBlendTreeRuntime.h
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Raven/Animation/Animator.h"
#include "Raven/Animation/BlendTree1D.h"
#include "Raven/Gltf/GltfDocument.h"

namespace Raven
{
namespace Gltf
{

class SkinnedMeshRuntimeAsset;

// ============================================================================
// LocomotionBlendTreeConfig
// ============================================================================
// Idle / Walk / Run / SprintをSpeed Parameter上へ配置する設定です。
// Thresholdは状態切り替え境界ではなく「そのClipが100%になるGameplay速度」を表します。
//
// AuthoredMotionSpeedはClipを1.0倍速で再生したときに見た目上想定している移動速度です。
// Thresholdと分離することで、Gameplay速度を変えずに足滑りだけをPlayback Speedで補正できます。
// これらはAnimation Assetごとの設定なので、CharacterControllerのGameplay目標速度を既定値として
// 流用しません。Configure() / ConfigureSprint()の呼び出し側がProfile等から明示します。
struct LocomotionBlendTreeConfig
{
    std::string IdleAnimationName;
    std::string WalkAnimationName;
    std::string RunAnimationName;
    std::string SprintAnimationName;

    float IdleThreshold = 0.0f;
    float WalkThreshold = 0.0f;
    float RunThreshold = 0.0f;
    float SprintThreshold = 0.0f;

    float WalkAuthoredMotionSpeed = 0.0f;
    float RunAuthoredMotionSpeed = 0.0f;
    float SprintAuthoredMotionSpeed = 0.0f;

    // 誤ったAsset設定や極端な速度変化でAnimationが停止/高速化し過ぎないための安全範囲です。
    // IdleそのものはReference Speedが0なので1.0倍速を維持します。
    float MinLocomotionPlaybackSpeed = 0.50f;
    float MaxLocomotionPlaybackSpeed = 2.00f;
};

// ============================================================================
// LocomotionPlaybackDebugInfo
// ============================================================================
// Foot Sliding補正の診断値です。
// ReferenceMotionSpeedは現在のBlend Weightで補間した「Clip側の想定移動速度」、
// PlaybackSpeedは Actual Movement Speed / Reference Motion Speed を安全範囲へClampした値です。
struct LocomotionPlaybackDebugInfo
{
    float MovementSpeed = 0.0f;
    float ReferenceMotionSpeed = 0.0f;
    float PlaybackSpeed = 1.0f;
};

// ============================================================================
// SkinnedBlendTreeRuntime
// ============================================================================
// 1D Locomotion BlendTree Runtimeです。
//
// 重要:
// - BlendTree1D自身は時間を持たない
// - AnimatorがNormalizedTimeを1つだけ進める
// - Locomotion Childは同じNormalizedTimeでSampleする
// - Speed変更時は再生をRestartせずParameterだけ更新する
// - Gameplay実速度とClip想定速度の差はAnimator Playback Speedで吸収する
//
// 既存3段階APIは互換用として維持し、Sprintを使うCharacterだけ4段階APIを選択します。
class SkinnedBlendTreeRuntime
{
public:
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    bool GetAnimationNames(
        std::size_t skinIndex,
        std::vector<std::string>& outNames,
        std::string* errorMessage = nullptr) const;

    // 既存 Idle / Walk / Run の3段階Locomotionです。
    bool Configure(
        std::size_t skinIndex,
        const LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    // Idle / Walk / Run / Sprintの4段階Locomotionです。
    // 既存Configure()を置き換えず別入口にすることで、Sprint未対応Assetの挙動を変更しません。
    bool ConfigureSprint(
        std::size_t skinIndex,
        const LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    bool SetMovementSpeed(
        std::size_t skinIndex,
        float movementSpeed,
        std::string* errorMessage = nullptr);

    // 4 ChildのAuthored Motion Speedを用いてFoot Sliding補正まで行うSprint対応版です。
    bool SetMovementSpeedSprintAware(
        std::size_t skinIndex,
        float movementSpeed,
        std::string* errorMessage = nullptr);

    bool SetLocomotionThresholds(
        std::size_t skinIndex,
        float idleThreshold,
        float walkThreshold,
        float runThreshold,
        std::string* errorMessage = nullptr);

    bool SetLocomotionThresholds(
        std::size_t skinIndex,
        float idleThreshold,
        float walkThreshold,
        float runThreshold,
        float sprintThreshold,
        std::string* errorMessage = nullptr);

    bool SetLocomotionAuthoredMotionSpeeds(
        std::size_t skinIndex,
        float walkAuthoredMotionSpeed,
        float runAuthoredMotionSpeed,
        std::string* errorMessage = nullptr)
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }

        if (std::isfinite(walkAuthoredMotionSpeed) == false
            || std::isfinite(runAuthoredMotionSpeed) == false
            || walkAuthoredMotionSpeed <= 0.0f
            || runAuthoredMotionSpeed <= walkAuthoredMotionSpeed)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Authored Motion Speedは 0 < Walk < Run を満たす必要があります";
            }
            return false;
        }

        SkinState* state = FindSkinState(skinIndex);
        if (state == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "指定SkinIndexのBlendTree Runtime Stateがありません";
            }
            return false;
        }
        if (state->Configured == false || state->LocomotionTree == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "指定SkinIndexのBlendTreeがConfigureされていません";
            }
            return false;
        }

        state->WalkAuthoredMotionSpeed = walkAuthoredMotionSpeed;
        state->RunAuthoredMotionSpeed = runAuthoredMotionSpeed;
        return UpdateLocomotionPlaybackSpeed(*state, errorMessage);
    }

    bool SetLocomotionAuthoredMotionSpeeds(
        std::size_t skinIndex,
        float walkAuthoredMotionSpeed,
        float runAuthoredMotionSpeed,
        float sprintAuthoredMotionSpeed,
        std::string* errorMessage = nullptr);

    bool SetPlaybackSpeed(
        std::size_t skinIndex,
        float playbackSpeed,
        std::string* errorMessage = nullptr);

    bool GetDebugInfo(
        std::size_t skinIndex,
        BlendTree1DDebugInfo& outInfo,
        std::string* errorMessage = nullptr) const;

    bool GetLocomotionPlaybackDebugInfo(
        std::size_t skinIndex,
        LocomotionPlaybackDebugInfo& outInfo,
        std::string* errorMessage = nullptr) const;

    // ========================================================================
    // Temporary One-Shot Animation
    // ========================================================================
    bool PlayOneShotAnimation(
        std::size_t skinIndex,
        const std::string& animationName,
        float crossFadeDuration = 0.10f,
        std::string* errorMessage = nullptr);

    bool ReturnToLocomotion(
        std::size_t skinIndex,
        float movementSpeed,
        float crossFadeDuration = 0.15f,
        std::string* errorMessage = nullptr);

    bool IsOneShotAnimationFinished(
        std::size_t skinIndex,
        bool& outFinished,
        std::string* errorMessage = nullptr) const;

    bool Update(float deltaTime, std::string* errorMessage = nullptr);

private:
    struct RuntimeClip
    {
        std::size_t SourceAnimationIndex = InvalidGltfIndex;
        std::string Name;
        std::shared_ptr<AnimationClip> Clip;
    };

    struct SkinState
    {
        std::size_t SkinIndex = InvalidGltfIndex;
        Animator AnimatorInstance;
        std::vector<RuntimeClip> Clips;
        std::shared_ptr<BlendTree1D> LocomotionTree;
        float MovementSpeed = 0.0f;

        // Idleは移動距離0として扱い、移動ClipだけAuthored Motion Speedを保持します。
        float WalkAuthoredMotionSpeed = 0.0f;
        float RunAuthoredMotionSpeed = 0.0f;
        float SprintAuthoredMotionSpeed = 0.0f;
        float MinLocomotionPlaybackSpeed = 0.50f;
        float MaxLocomotionPlaybackSpeed = 2.00f;
        float ReferenceMotionSpeed = 0.0f;
        float LocomotionPlaybackSpeed = 1.0f;

        bool Configured = false;
        bool OneShotActive = false;
    };

    SkinState* FindSkinState(std::size_t skinIndex);
    const SkinState* FindSkinState(std::size_t skinIndex) const;

    const RuntimeClip* FindClip(
        const SkinState& state,
        const std::string& animationName) const;

    bool UpdateLocomotionPlaybackSpeed(
        SkinState& state,
        std::string* errorMessage);

    bool ApplyPoseToSkin(
        const SkinState& state,
        std::string* errorMessage);

private:
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::vector<SkinState> m_SkinStates;
};

// ============================================================================
// Sprint-aware inline extensions
// ============================================================================
inline bool SkinnedBlendTreeRuntime::ConfigureSprint(
    std::size_t skinIndex,
    const LocomotionBlendTreeConfig& config,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "指定SkinIndexのBlendTree Runtime Stateがありません";
        }
        return false;
    }

    if (config.IdleAnimationName.empty()
        || config.WalkAnimationName.empty()
        || config.RunAnimationName.empty()
        || config.SprintAnimationName.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Idle / Walk / Run / Sprint Animation名は空にできません";
        }
        return false;
    }

    if (std::isfinite(config.IdleThreshold) == false
        || std::isfinite(config.WalkThreshold) == false
        || std::isfinite(config.RunThreshold) == false
        || std::isfinite(config.SprintThreshold) == false
        || config.IdleThreshold < 0.0f
        || config.WalkThreshold <= config.IdleThreshold
        || config.RunThreshold <= config.WalkThreshold
        || config.SprintThreshold <= config.RunThreshold)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "BlendTree Thresholdは 0 <= Idle < Walk < Run < Sprint を満たす必要があります";
        }
        return false;
    }

    if (std::isfinite(config.WalkAuthoredMotionSpeed) == false
        || std::isfinite(config.RunAuthoredMotionSpeed) == false
        || std::isfinite(config.SprintAuthoredMotionSpeed) == false
        || config.WalkAuthoredMotionSpeed <= 0.0f
        || config.RunAuthoredMotionSpeed <= config.WalkAuthoredMotionSpeed
        || config.SprintAuthoredMotionSpeed <= config.RunAuthoredMotionSpeed
        || std::isfinite(config.MinLocomotionPlaybackSpeed) == false
        || std::isfinite(config.MaxLocomotionPlaybackSpeed) == false
        || config.MinLocomotionPlaybackSpeed <= 0.0f
        || config.MaxLocomotionPlaybackSpeed < config.MinLocomotionPlaybackSpeed)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Locomotion補正値は 0 < Walk < Run < Sprint かつ 0 < MinPlayback <= MaxPlayback を満たす必要があります";
        }
        return false;
    }

    const RuntimeClip* idleClip = FindClip(*state, config.IdleAnimationName);
    const RuntimeClip* walkClip = FindClip(*state, config.WalkAnimationName);
    const RuntimeClip* runClip = FindClip(*state, config.RunAnimationName);
    const RuntimeClip* sprintClip = FindClip(*state, config.SprintAnimationName);
    if (idleClip == nullptr || walkClip == nullptr || runClip == nullptr || sprintClip == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint Locomotionに必要なAnimation Clipが見つかりません";
        }
        return false;
    }
    if (idleClip->Clip == nullptr
        || walkClip->Clip == nullptr
        || runClip->Clip == nullptr
        || sprintClip->Clip == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTree用AnimationClipにnullptrが含まれています";
        }
        return false;
    }

    std::shared_ptr<BlendTree1D> blendTree = std::make_shared<BlendTree1D>();
    if (blendTree == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTree1Dの生成に失敗しました";
        }
        return false;
    }

    if (blendTree->AddChild(config.IdleThreshold, idleClip->Clip) == false
        || blendTree->AddChild(config.WalkThreshold, walkClip->Clip) == false
        || blendTree->AddChild(config.RunThreshold, runClip->Clip) == false
        || blendTree->AddChild(config.SprintThreshold, sprintClip->Clip) == false)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTree1D Childの登録に失敗しました";
        }
        return false;
    }

    state->LocomotionTree = std::move(blendTree);
    state->MovementSpeed = config.IdleThreshold;
    state->WalkAuthoredMotionSpeed = config.WalkAuthoredMotionSpeed;
    state->RunAuthoredMotionSpeed = config.RunAuthoredMotionSpeed;
    state->SprintAuthoredMotionSpeed = config.SprintAuthoredMotionSpeed;
    state->MinLocomotionPlaybackSpeed = config.MinLocomotionPlaybackSpeed;
    state->MaxLocomotionPlaybackSpeed = config.MaxLocomotionPlaybackSpeed;
    state->ReferenceMotionSpeed = 0.0f;
    state->LocomotionPlaybackSpeed = 1.0f;
    state->Configured = true;
    state->OneShotActive = false;

    state->AnimatorInstance.PlayBlendTree(state->LocomotionTree, state->MovementSpeed, true);
    state->AnimatorInstance.SetSpeed(1.0f);
    return true;
}

inline bool SkinnedBlendTreeRuntime::SetMovementSpeedSprintAware(
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
        if (errorMessage != nullptr)
        {
            *errorMessage = "Movement Speedは0以上の有限値である必要があります";
        }
        return false;
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false || state->LocomotionTree == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTreeがConfigureされていません";
        }
        return false;
    }

    state->MovementSpeed = movementSpeed;
    bool parameterUpdated = state->AnimatorInstance.SetCurrentBlendParameter(movementSpeed);
    if (state->AnimatorInstance.IsCrossFading() == true)
    {
        parameterUpdated = state->AnimatorInstance.SetNextBlendParameter(movementSpeed) || parameterUpdated;
    }
    if (parameterUpdated == false)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTree Parameterの更新に失敗しました";
        }
        return false;
    }

    BlendTree1DDebugInfo blendInfo{};
    if (state->LocomotionTree->GetDebugInfo(movementSpeed, blendInfo) == false)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint Locomotion Blend Weightの取得に失敗しました";
        }
        return false;
    }

    const auto resolveAuthoredMotionSpeed = [state](std::size_t childIndex)
    {
        if (childIndex == 0u) { return 0.0f; }
        if (childIndex == 1u) { return state->WalkAuthoredMotionSpeed; }
        if (childIndex == 2u) { return state->RunAuthoredMotionSpeed; }
        if (childIndex == 3u) { return state->SprintAuthoredMotionSpeed; }
        return 0.0f;
    };

    state->ReferenceMotionSpeed =
        resolveAuthoredMotionSpeed(blendInfo.LeftChildIndex) * blendInfo.LeftWeight
        + resolveAuthoredMotionSpeed(blendInfo.RightChildIndex) * blendInfo.RightWeight;

    constexpr float ReferenceSpeedEpsilon = 1.0e-5f;
    if (state->ReferenceMotionSpeed <= ReferenceSpeedEpsilon)
    {
        state->LocomotionPlaybackSpeed = 1.0f;
    }
    else
    {
        state->LocomotionPlaybackSpeed = std::clamp(
            movementSpeed / state->ReferenceMotionSpeed,
            state->MinLocomotionPlaybackSpeed,
            state->MaxLocomotionPlaybackSpeed);
    }

    state->AnimatorInstance.SetSpeed(state->LocomotionPlaybackSpeed);
    return true;
}

inline bool SkinnedBlendTreeRuntime::SetLocomotionThresholds(
    std::size_t skinIndex,
    float idleThreshold,
    float walkThreshold,
    float runThreshold,
    float sprintThreshold,
    std::string* errorMessage)
{
    if (std::isfinite(idleThreshold) == false
        || std::isfinite(walkThreshold) == false
        || std::isfinite(runThreshold) == false
        || std::isfinite(sprintThreshold) == false
        || idleThreshold < 0.0f
        || walkThreshold <= idleThreshold
        || runThreshold <= walkThreshold
        || sprintThreshold <= runThreshold)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "BlendTree Thresholdは 0 <= Idle < Walk < Run < Sprint を満たす必要があります";
        }
        return false;
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false || state->LocomotionTree == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTreeがConfigureされていません";
        }
        return false;
    }

    if (state->LocomotionTree->SetThresholds(
            { idleThreshold, walkThreshold, runThreshold, sprintThreshold }) == false)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTree ThresholdのRuntime更新に失敗しました";
        }
        return false;
    }

    return SetMovementSpeedSprintAware(skinIndex, state->MovementSpeed, errorMessage);
}

inline bool SkinnedBlendTreeRuntime::SetLocomotionAuthoredMotionSpeeds(
    std::size_t skinIndex,
    float walkAuthoredMotionSpeed,
    float runAuthoredMotionSpeed,
    float sprintAuthoredMotionSpeed,
    std::string* errorMessage)
{
    if (std::isfinite(walkAuthoredMotionSpeed) == false
        || std::isfinite(runAuthoredMotionSpeed) == false
        || std::isfinite(sprintAuthoredMotionSpeed) == false
        || walkAuthoredMotionSpeed <= 0.0f
        || runAuthoredMotionSpeed <= walkAuthoredMotionSpeed
        || sprintAuthoredMotionSpeed <= runAuthoredMotionSpeed)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Authored Motion Speedは 0 < Walk < Run < Sprint を満たす必要があります";
        }
        return false;
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false || state->LocomotionTree == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Sprint BlendTreeがConfigureされていません";
        }
        return false;
    }

    state->WalkAuthoredMotionSpeed = walkAuthoredMotionSpeed;
    state->RunAuthoredMotionSpeed = runAuthoredMotionSpeed;
    state->SprintAuthoredMotionSpeed = sprintAuthoredMotionSpeed;
    return SetMovementSpeedSprintAware(skinIndex, state->MovementSpeed, errorMessage);
}

} // namespace Gltf
} // namespace Raven
