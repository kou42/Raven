// Raven/Gltf/SkinnedBlendTreeRuntime.h
#pragma once

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
// Thresholdは状態切り替え境界ではなく「そのClipが100%になる速度」を表します。
struct LocomotionBlendTreeConfig
{
    std::string IdleAnimationName;
    std::string WalkAnimationName;
    std::string RunAnimationName;

    float IdleThreshold = 0.0f;
    float WalkThreshold = 1.8f;
    float RunThreshold = 5.5f;
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
//
// この構成によりWalk -> Runで足運びの位相を保ったまま連続Pose Blendできます。
class SkinnedBlendTreeRuntime
{
public:
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    bool Configure(
        std::size_t skinIndex,
        const LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    // Character Controllerから毎Frame渡す速度Parameterです。
    bool SetMovementSpeed(
        std::size_t skinIndex,
        float movementSpeed,
        std::string* errorMessage = nullptr);

    bool SetPlaybackSpeed(
        std::size_t skinIndex,
        float playbackSpeed,
        std::string* errorMessage = nullptr);

    bool GetDebugInfo(
        std::size_t skinIndex,
        BlendTree1DDebugInfo& outInfo,
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
        bool Configured = false;
    };

    SkinState* FindSkinState(std::size_t skinIndex);
    const SkinState* FindSkinState(std::size_t skinIndex) const;

    const RuntimeClip* FindClip(
        const SkinState& state,
        const std::string& animationName) const;

    bool ApplyPoseToSkin(
        const SkinState& state,
        std::string* errorMessage);

private:
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::vector<SkinState> m_SkinStates;
};

} // namespace Gltf
} // namespace Raven
