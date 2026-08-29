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

struct LocomotionBlendTreeConfig
{
    std::string IdleAnimationName;
    std::string WalkAnimationName;
    std::string RunAnimationName;

    float IdleThreshold = 0.0f;
    float WalkThreshold = 1.8f;
    float RunThreshold = 5.5f;
};

class SkinnedBlendTreeRuntime
{
public:
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    // Attach済みSkinで利用可能なAnimation名をglTF animations[]順で返します。
    // Character側で固定名を推測せず、Assetが実際に持つClip名を診断・解決するための入口です。
    bool GetAnimationNames(
        std::size_t skinIndex,
        std::vector<std::string>& outNames,
        std::string* errorMessage = nullptr) const;

    bool Configure(
        std::size_t skinIndex,
        const LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

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
        bool Configured = false;
        bool OneShotActive = false;
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
