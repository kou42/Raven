#pragma once

#include "Raven/Animation/MotionMatcher.h"
#include "Raven/Animation/MotionQueryBuilder.h"
#include "Raven/Gltf/GltfDocument.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Raven
{
namespace Gltf
{

class SkinnedMeshRuntimeAsset;

struct SkinnedMotionMatchingConfig
{
    float SampleRate = 30.0f;
    MotionPoseFeatureConfig PoseFeatures{};
    MotionTrajectoryFeatureConfig TrajectoryFeatures{};
    MotionMatcherConfig Matcher{};

    // Database Featureの単位差を標準偏差で吸収してからSemantic Weightを適用します。
    // falseの場合はMatcher.SearchWeightsをそのまま使用します。
    bool NormalizeFeatures = true;
};

// glTF SkinごとにMotionDatabase / MotionMatcherを所有し、検索結果Poseを既存の
// SkinnedMeshRuntimeAssetへ配布するRuntime Bridgeです。
// CharacterControllerや入力Deviceには依存せず、呼び出し側がMotionSearchQueryだけを渡すことで
// Player / AI / Networkのどの経路からでも同じMotion Matching本体を利用できます。
class SkinnedMotionMatchingRuntime
{
public:
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    // 指定Skinの全Animation ClipからMotionDatabaseを構築します。
    // AttachとConfigureを分離することで、Pose BoneやTrajectory HorizonをCharacterごとに選択できます。
    bool Configure(
        std::size_t skinIndex,
        const SkinnedMotionMatchingConfig& config,
        std::string* errorMessage = nullptr);

    // Queryを評価し、選択Poseを同じSkinを参照する全Primitiveへ同期します。
    bool Update(
        std::size_t skinIndex,
        const MotionSearchQuery& query,
        float deltaTime,
        std::string* errorMessage = nullptr);

    const Skeleton* GetSkeleton(std::size_t skinIndex) const;
    const SkeletonPose* GetCurrentPose(std::size_t skinIndex) const;
    const SkeletonPose* GetPreviousPose(std::size_t skinIndex) const;

    bool GetInertializationBoneDebugInfo(
        std::size_t skinIndex,
        BoneIndex boneIndex,
        PoseInertializerBoneDebugInfo& outInfo) const;

    const MotionMatcher* GetMotionMatcher(std::size_t skinIndex) const;

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
        const Skeleton* SkeletonData = nullptr;
        std::vector<RuntimeClip> Clips;
        std::shared_ptr<MotionDatabase> Database;
        MotionMatcher Matcher;
        SkeletonPose PreviousPose{};
        SkeletonPose CurrentPose{};
        bool Configured = false;
        bool HasPoseHistory = false;
    };

    SkinState* FindSkinState(std::size_t skinIndex);
    const SkinState* FindSkinState(std::size_t skinIndex) const;

    bool ApplyPoseToSkin(
        const SkinState& state,
        const SkeletonPose& pose,
        std::string* errorMessage);

private:
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::vector<SkinState> m_SkinStates;
};

} // namespace Gltf
} // namespace Raven
