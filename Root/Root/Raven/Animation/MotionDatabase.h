#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace Raven
{

// ============================================================================
// MotionPoseFeature
// ============================================================================
// Motion MatchingのPose検索に使用する1 Bone分の特徴量です。
// PositionはRoot基準座標、VelocityはSkeleton Global空間で求めたBone速度を現在Root座標へ変換した値です。
// BoneIndexを保持することで、後続のQuery生成・Debug表示時に特徴量の意味を失わないようにします。
struct MotionPoseFeature
{
    BoneIndex Bone = InvalidBoneIndex;
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Velocity{ 0.0f, 0.0f, 0.0f };
};

// ============================================================================
// MotionPoseFeatureConfig
// ============================================================================
// Feature生成対象をSkeleton固有のBoneIndexで明示します。
// Bone名検索を毎Frame行わず、Asset準備時に一度だけ解決したIndexを渡す想定です。
struct MotionPoseFeatureConfig
{
    BoneIndex RootBone = InvalidBoneIndex;
    std::vector<BoneIndex> PoseBones;
};

// ============================================================================
// MotionTrajectoryPoint
// ============================================================================
// 現在Rootから見た将来Rootの位置・向きを表します。
// Raven Character Controllerは+ZをForwardとしてYawを決定しているため、Directionも+Z基準です。
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

// ============================================================================
// MotionFrame
// ============================================================================
// Motion Database内の1サンプルを「どのAnimationClipの何秒地点か」で表します。
struct MotionFrame
{
    std::uint32_t ClipIndex = 0;
    float Time = 0.0f;
    std::vector<MotionPoseFeature> PoseFeatures;
    std::vector<MotionTrajectoryPoint> Trajectory;
};

// ============================================================================
// MotionSearchQuery / Result
// ============================================================================
// QueryはDatabaseと同じRoot基準座標へ変換済みの値だけを保持します。
// Character ControllerやAnimatorへの依存をここへ持ち込まず、Query生成はRuntime側へ分離します。
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
// MotionDatabase
// ============================================================================
// 複数のAnimationClipをMotion Matching用の固定周期サンプル列へ変換するAsset側データです。
//
// AnimationClip自身は再生状態を持たない既存設計を維持し、MotionDatabaseもCurrent Frameなどの
// Runtime状態を持ちません。既存Animator / BlendTree / StateMachine経路には依存せず、
// Motion Matchingを使用するRuntimeだけがこのDatabaseを参照する構成にします。
class MotionDatabase
{
public:
    bool AddClip(std::shared_ptr<AnimationClip> clip);
    void Clear();

    // 登録済みClipをsampleRate Hzで [0, Duration) のMotionFrame列へ展開します。
    bool Build(float sampleRate);

    // 指定BoneのRoot-relative Position / Velocityを全Frameへ生成します。
    bool BuildPoseFeatures(
        const Skeleton& skeleton,
        const MotionPoseFeatureConfig& config);

    // 現在Root基準で将来のRoot Position / Directionを全Frameへ生成します。
    // FutureTimeOffsetsは0より大きい昇順値を要求します。
    bool BuildTrajectoryFeatures(
        const Skeleton& skeleton,
        const MotionTrajectoryFeatureConfig& config);

    // 全Frameを線形走査し、重み付き二乗距離が最小の候補を返します。
    // 最適化構造は検索仕様を固めてから追加し、まず結果の正しさを優先します。
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

private:
    void ClearPoseFeatures();
    void ClearTrajectoryFeatures();

private:
    std::vector<std::shared_ptr<AnimationClip>> m_Clips;
    std::vector<MotionFrame> m_Frames;
    float m_SampleRate = 0.0f;
};

} // namespace Raven
