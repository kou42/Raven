// Raven/Gltf/SkinnedBlendTreeRuntime.h
#pragma once

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
// Idle / Walk / RunをSpeed Parameter上へ配置する設定です。
// Thresholdは状態切り替え境界ではなく「そのClipが100%になるGameplay速度」を表します。
//
// AuthoredMotionSpeedはClipを1.0倍速で再生したときに見た目上想定している移動速度です。
// Thresholdと分離することで、Gameplay速度を変えずに足滑りだけをPlayback Speedで補正できます。
// これらはAnimation Assetごとの設定なので、CharacterControllerのWalk / Run目標速度を既定値として
// 流用しません。Configure()の呼び出し側がProfile等の正規の設定元から全値を明示します。
struct LocomotionBlendTreeConfig
{
    std::string IdleAnimationName;
    std::string WalkAnimationName;
    std::string RunAnimationName;

    float IdleThreshold = 0.0f;
    float WalkThreshold = 0.0f;
    float RunThreshold = 0.0f;

    float WalkAuthoredMotionSpeed = 0.0f;
    float RunAuthoredMotionSpeed = 0.0f;

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
// Stage 4用の1D Locomotion BlendTree Runtimeです。
//
// 重要:
// - BlendTree1D自身は時間を持たない
// - AnimatorがNormalizedTimeを1つだけ進める
// - Idle / Walk / Runは同じNormalizedTimeでSampleする
// - Speed変更時は再生をRestartせずParameterだけ更新する
// - Gameplay実速度とClip想定速度の差はAnimator Playback Speedで吸収する
//
// この構成によりWalk -> Runで足運びの位相を保ったまま連続Pose Blendできます。
class SkinnedBlendTreeRuntime
{
public:
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    // Attach済みSkinで利用可能なAnimation名をglTF animations[]順で返します。
    // Character側で固定名を推測せず、Assetが実際に持つClip名を診断・解決するための入口です。
    // Clip本体を外部へ公開しないことで、Animation所有権とAnimator状態はRuntime内部へ閉じたまま維持します。
    bool GetAnimationNames(
        std::size_t skinIndex,
        std::vector<std::string>& outNames,
        std::string* errorMessage = nullptr) const;

    bool Configure(
        std::size_t skinIndex,
        const LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    // Character Controllerから毎Frame渡す速度Parameterです。
    // Parameter更新と同時に現在のBlend WeightからReference Motion Speedを求め、
    // Animator Playback Speedも更新してFoot Slidingを補正します。
    bool SetMovementSpeed(
        std::size_t skinIndex,
        float movementSpeed,
        std::string* errorMessage = nullptr);

    // Runtime調整UIからAuthored Motion Speedだけを更新する入口です。
    // BlendTreeの再Configureや再生Restartは行わず、現在Parameterに対する補正倍率だけを即座に再計算します。
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

    // 汎用の明示Playback Speed設定です。
    // Locomotion中はSetMovementSpeed()が毎Frame補正値を書き戻すため、手動値は一時的なOverrideになります。
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
    // Locomotion BlendTreeからGet-Up / Hit Reactionなどの単発Clipへ遷移します。
    // Animatorの既存CrossFade経路を利用し、Clipは非Loopで再生します。
    //
    // one-shot再生中は通常のSetMovementSpeed()を停止し、終了後ReturnToLocomotion()へ
    // Character Controllerの現在速度を渡してLocomotion Parameterを再同期します。
    bool PlayOneShotAnimation(
        std::size_t skinIndex,
        const std::string& animationName,
        float crossFadeDuration = 0.10f,
        std::string* errorMessage = nullptr);

    // one-shot Clipから設定済みIdle / Walk / Run BlendTreeへ戻します。
    // movementSpeedを明示的に受け取ることで、Ragdoll突入前の古い走行速度ではなく
    // Get-Up完了時点のCharacter Controller速度へ直接復帰できます。
    bool ReturnToLocomotion(
        std::size_t skinIndex,
        float movementSpeed,
        float crossFadeDuration = 0.15f,
        std::string* errorMessage = nullptr);

    // one-shotが非Loop終端へ到達したかを取得します。
    // one-shot状態でない場合はoutFinished=falseを返します。
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

        // Locomotion Playback Speed補正用のAsset側速度メタデータです。
        // Idleは移動距離0として扱い、Walk / Runだけ明示値を保持します。
        // Configure()成功時にProfile由来の値で初期化し、未設定状態では中立値を維持します。
        float WalkAuthoredMotionSpeed = 0.0f;
        float RunAuthoredMotionSpeed = 0.0f;
        float MinLocomotionPlaybackSpeed = 0.50f;
        float MaxLocomotionPlaybackSpeed = 2.00f;
        float ReferenceMotionSpeed = 0.0f;
        float LocomotionPlaybackSpeed = 1.0f;

        bool Configured = false;

        // Locomotion以外の非Loop Clipを一時再生しているかをRuntime側で追跡します。
        // Animator::IsFinished()だけでは「Locomotion停止」と「one-shot完了」を区別できないため、
        // 呼び出し側がGet-Up終了を安全に判定できるよう明示Stateを持ちます。
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

} // namespace Gltf
} // namespace Raven
