#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{

// ============================================================================
// MotionPoseFeature
// ============================================================================
// Motion MatchingのPose検索に使用する1 Bone分の特徴量です。
// PositionはRoot基準座標、VelocityはWorldで求めたBone速度を現在Root座標へ変換した値です。
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
// MotionFrame
// ============================================================================
// Motion Database内の1サンプルを「どのAnimationClipの何秒地点か」で表します。
// PoseFeaturesはBuildPoseFeatures()を実行した後に設定されます。
// Trajectory Featureは後続実装で別責務として追加します。
struct MotionFrame
{
    std::uint32_t ClipIndex = 0;
    float Time = 0.0f;
    std::vector<MotionPoseFeature> PoseFeatures;
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
    // nullptrや同一Clipの重複登録は、検索候補の無効化・重複化を防ぐため拒否します。
    bool AddClip(std::shared_ptr<AnimationClip> clip);

    void Clear();

    // 登録済みClipをsampleRate HzでMotionFrame列へ展開します。
    // sampleRate <= 0、Clip未登録、Duration <= 0のClipを含む場合はfalseを返し、
    // 部分的なDatabaseを残さないようFrame列を空にします。
    //
    // 各Clipは [0, Duration) を一定間隔でサンプルします。
    // Duration地点はLoop Clipでは0秒地点と重複しやすく、Motion Matching候補として
    // 二重登録されるのを避けるため、この基盤段階では終端を含めません。
    bool Build(float sampleRate);

    // Build()済みの全MotionFrameについて、指定BoneのPose Featureを生成します。
    // RootBoneはSkeleton階層のRootである必要があります。Root以外を許可するとGlobal Transformの
    // 逆変換が別途必要になり座標系契約が曖昧になるため、まずは明示的に制限します。
    // PoseBonesの重複、不正Index、空リストは拒否し、失敗時は全FrameのFeatureを空へ戻します。
    bool BuildPoseFeatures(
        const Skeleton& skeleton,
        const MotionPoseFeatureConfig& config);

    std::size_t GetClipCount() const { return m_Clips.size(); }
    std::size_t GetFrameCount() const { return m_Frames.size(); }
    float GetSampleRate() const { return m_SampleRate; }

    const std::shared_ptr<AnimationClip>& GetClip(std::size_t clipIndex) const;
    const MotionFrame* GetFrame(std::size_t frameIndex) const;

    const std::vector<MotionFrame>& GetFrames() const { return m_Frames; }

private:
    void ClearPoseFeatures();

private:
    std::vector<std::shared_ptr<AnimationClip>> m_Clips;
    std::vector<MotionFrame> m_Frames;
    float m_SampleRate = 0.0f;
};

} // namespace Raven
