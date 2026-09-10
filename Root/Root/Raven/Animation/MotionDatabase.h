#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Raven
{

struct MotionPoseFeature
{
    BoneIndex Bone = InvalidBoneIndex;
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Velocity{ 0.0f, 0.0f, 0.0f };
};

struct MotionPoseFeatureConfig
{
    BoneIndex RootBone = InvalidBoneIndex;
    std::vector<BoneIndex> PoseBones;
};

struct MotionTrajectoryPoint
{
    float TimeOffset = 0.0f;
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Direction{ 0.0f, 0.0f, 1.0f };
};

struct MotionTrajectoryFeatureConfig
{
    BoneIndex RootBone = InvalidBoneIndex;
    std::vector<float> FutureTimeOffsets;
};

struct MotionFrame
{
    std::uint32_t ClipIndex = 0;
    float Time = 0.0f;
    std::vector<MotionPoseFeature> PoseFeatures;
    std::vector<MotionTrajectoryPoint> Trajectory;
};

struct MotionSearchQuery
{
    std::vector<MotionPoseFeature> PoseFeatures;
    std::vector<MotionTrajectoryPoint> Trajectory;
};

struct MotionSearchWeights
{
    float PosePosition = 1.0f;
    float PoseVelocity = 1.0f;
    float TrajectoryPosition = 1.0f;
    float TrajectoryDirection = 1.0f;
};

struct MotionSearchResult
{
    std::size_t FrameIndex = std::numeric_limits<std::size_t>::max();
    float Cost = std::numeric_limits<float>::max();

    bool IsValid() const
    {
        return FrameIndex != std::numeric_limits<std::size_t>::max();
    }
};

// Feature種別ごとのDatabase統計です。平均はDebug/分析用、標準偏差は検索Scale補正に使います。
// XYZを同じFeature種別のScalar集合として集計し、まず単位差(m / m/s / direction)を除去します。
struct MotionFeatureNormalization
{
    float PosePositionMean = 0.0f;
    float PosePositionStdDev = 1.0f;
    float PoseVelocityMean = 0.0f;
    float PoseVelocityStdDev = 1.0f;
    float TrajectoryPositionMean = 0.0f;
    float TrajectoryPositionStdDev = 1.0f;
    float TrajectoryDirectionMean = 0.0f;
    float TrajectoryDirectionStdDev = 1.0f;
    bool Valid = false;
};

class MotionDatabase
{
public:
    bool AddClip(std::shared_ptr<AnimationClip> clip);
    void Clear();
    bool Build(float sampleRate);

    bool BuildPoseFeatures(
        const Skeleton& skeleton,
        const MotionPoseFeatureConfig& config);

    bool BuildTrajectoryFeatures(
        const Skeleton& skeleton,
        const MotionTrajectoryFeatureConfig& config);

    // BuildPoseFeatures / BuildTrajectoryFeaturesの後に呼びます。
    // Featureを再構築した場合は検索前に必ず再実行し、統計とFeature内容を同期させます。
    bool BuildFeatureNormalization();

    // ||delta / sigma||^2 == ||delta||^2 / sigma^2 を利用してWeightへ正規化を畳み込みます。
    // 既存FindBestMatchとMotionMatcherのContinuation Costを同じ式のまま利用できます。
    bool MakeNormalizedSearchWeights(
        const MotionSearchWeights& semanticWeights,
        MotionSearchWeights& outWeights) const;

    bool FindBestMatch(
        const MotionSearchQuery& query,
        const MotionSearchWeights& weights,
        MotionSearchResult& outResult) const;

    std::size_t GetClipCount() const { return m_Clips.size(); }
    std::size_t GetFrameCount() const { return m_Frames.size(); }
    float GetSampleRate() const { return m_SampleRate; }

    const std::shared_ptr<AnimationClip>& GetClip(std::size_t clipIndex) const;
    const MotionFrame* GetFrame(std::size_t frameIndex) const;
    const std::vector<MotionFrame>& GetFrames() const { return m_Frames; }

    bool HasFeatureNormalization() const { return m_Normalization.Valid; }
    const MotionFeatureNormalization& GetFeatureNormalization() const { return m_Normalization; }

private:
    void ClearPoseFeatures();
    void ClearTrajectoryFeatures();
    void ClearFeatureNormalization();

private:
    std::vector<std::shared_ptr<AnimationClip>> m_Clips;
    std::vector<MotionFrame> m_Frames;
    float m_SampleRate = 0.0f;
    MotionFeatureNormalization m_Normalization{};
};

} // namespace Raven
