#pragma once

#include "Raven/Animation/CharacterMotionMatchingAdapter.h"
#include "Raven/Gltf/SkinnedMotionMatchingRuntime.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Raven
{

// ============================================================================
// CharacterMotionMatchingRuntimeDebugInfo
// ============================================================================
// Character側Debug UIがMotionMatcher本体へ直接依存せず、選択状態とInertialization状態を
// 1つのSnapshotとして参照するためのRuntime診断値です。
struct CharacterMotionMatchingRuntimeDebugInfo
{
    bool Active = false;
    bool HasSelection = false;
    std::size_t SelectedFrameIndex = 0u;
    std::uint32_t ClipIndex = 0u;
    float ClipTime = 0.0f;
    float SearchCost = 0.0f;
    std::vector<MotionSearchCandidateDebugInfo> SearchCandidates;
    bool Inertializing = false;
    float InertializationElapsedTime = 0.0f;
};

// ============================================================================
// CharacterMotionMatchingRuntimeDriver
// ============================================================================
// CharacterControllerの状態・入力をMotionSearchQueryへ変換し、SkinnedMotionMatchingRuntimeへ
// 渡す接続Driverです。MotionMatcher本体はCharacterControllerへ依存させず、Gameplay側だけが
// Character固有の予測Trajectoryを組み立てる既存責務分離を維持します。
class CharacterMotionMatchingRuntimeDriver
{
public:
    // Demo/検証用の汎用初期設定をSkeletonから構築します。
    // Asset固有のBone名を推測せず、最上位RootをTrajectory Root、残りのBoneをPose Featureに使います。
    static bool BuildDefaultConfig(
        const Skeleton& skeleton,
        Gltf::SkinnedMotionMatchingConfig& outConfig,
        std::string* errorMessage = nullptr);

    bool Configure(
        Gltf::SkinnedMotionMatchingRuntime& runtime,
        std::size_t skinIndex,
        const Gltf::SkinnedMotionMatchingConfig& runtimeConfig,
        const CharacterTrajectoryPredictorConfig& predictorConfig = {},
        std::string* errorMessage = nullptr);

    void Reset();

    bool Update(
        const CharacterController& controller,
        const CharacterControllerInput& input,
        const TransformComponent& characterTransform,
        float deltaTime,
        std::string* errorMessage = nullptr);

    bool IsConfigured() const
    {
        return m_Runtime != nullptr && m_Configured == true;
    }

    std::size_t GetSkinIndex() const
    {
        return m_SkinIndex;
    }

    bool GetDebugInfo(CharacterMotionMatchingRuntimeDebugInfo& outInfo) const;

    // 直近UpdateでMotionMatcherへ渡したQueryの将来TrajectoryだけをSnapshotとして返します。
    // Editor側でCharacterTrajectoryPredictorを再実行せず、実際の検索入力をそのまま可視化できます。
    bool GetTrajectoryDebugInfo(std::vector<MotionTrajectoryPoint>& outTrajectory) const;

    bool GetInertializationBoneDebugInfo(
        BoneIndex boneIndex,
        PoseInertializerBoneDebugInfo& outInfo) const;

private:
    Gltf::SkinnedMotionMatchingRuntime* m_Runtime = nullptr;
    std::size_t m_SkinIndex = Gltf::InvalidGltfIndex;
    MotionPoseFeatureConfig m_PoseFeatures{};
    MotionTrajectoryFeatureConfig m_TrajectoryFeatures{};
    CharacterTrajectoryPredictorConfig m_PredictorConfig{};
    MotionSearchQuery m_LastQuery{};
    bool m_HasLastQuery = false;
    bool m_Configured = false;
};

} // namespace Raven
