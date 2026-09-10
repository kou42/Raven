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

// ============================================================================
// MotionFeatureNormalization
// ============================================================================
// Feature種別ごとにDatabase全Frameから求めた平均・標準偏差です。
// 現段階では各Vec3のXYZを同一Feature種別のScalar集合として集計します。
// Bone/Trajectory slotごとの過学習的なScale差を避けつつ、m と m/s とDirectionという
// 単位差を検索Costから取り除くことを優先した最初のNormalizationです。
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

    // 現在構築済みのPose / Trajectory Feature全体から正規化統計を計算します。
    // Featureを再構築した場合は統計を無効化するため、最後にこの関数を呼び直します。
    bool BuildFeatureNormalization();

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
