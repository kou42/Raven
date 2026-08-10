#pragma once

#include "Raven/Animation/AnimationTrack.h"
#include "Raven/Animation/SkeletonPose.h"

#include <cstddef>
#include <vector>

namespace Raven
{

// ============================================================================
// TransformPose
// ============================================================================
// AnimationClip::Sample()が返す「指定時刻のTransform」です。
// TransformComponentそのものを返さないことでAnimation層をScene/ECSから独立させます。
//
// この単一Transform用Track/APIは既存のScene Animation互換のため維持します。
// Skeletal Animationでは下記のBone Track群をSkeletonPoseへSampleします。
//
// RotationはQuaternionを正規表現として保持します。
// SceneのTransformComponentがEuler角を使っている間だけ、AnimationSystem境界でEulerへ戻します。
struct TransformPose
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Quat Rotation = math::Quat::Identity();
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// BoneAnimationTrack
// ============================================================================
// SkeletonのBoneIndexと、そのBoneのLocal TRS Key列を対応付けます。
// AnimationClip内でBone名ではなくIndexを使うことでRuntime Sample時の文字列検索を避けます。
// Import時にSkeleton::FindBone()などで名前からIndexへ解決しておく想定です。
struct BoneAnimationTrack
{
    BoneIndex Bone = InvalidBoneIndex;
    TransformAnimationTrack Transform;
};

// ============================================================================
// AnimationClip
// ============================================================================
// AnimationClipは「Animationデータ」だけを所有します。
// CurrentTime / Playing / Loop / Speedなどの再生状態はここへ置かず、AnimatorState / Animatorが
// 所有します。この分離により1つのClipを複数Entity/Animatorから共有できます。
class AnimationClip
{
public:
    AnimationClip() = default;
    explicit AnimationClip(float duration);

    float GetDuration() const { return m_Duration; }
    void SetDuration(float duration);

    // ------------------------------------------------------------------------
    // 単一Transform Animation
    // ------------------------------------------------------------------------
    // 既存のScene/ECS Animation経路を維持するため残しています。
    TransformAnimationTrack& GetTransformTrack() { return m_TransformTrack; }
    const TransformAnimationTrack& GetTransformTrack() const { return m_TransformTrack; }

    // 指定時刻のPoseを評価します。
    // TrackにKeyが無いChannelはTransformPoseの既定値を維持します。
    TransformPose Sample(float time) const;

    // ------------------------------------------------------------------------
    // Skeletal Animation
    // ------------------------------------------------------------------------
    // BoneごとのLocal Transform Trackを追加します。
    // 同じBoneIndexを複数回追加するとどのTrackを採用するか曖昧になるためfalseを返します。
    bool AddBoneTrack(BoneAnimationTrack track);

    const BoneAnimationTrack* FindBoneTrack(BoneIndex boneIndex) const;
    BoneAnimationTrack* FindBoneTrack(BoneIndex boneIndex);

    std::size_t GetBoneTrackCount() const { return m_BoneTracks.size(); }
    const std::vector<BoneAnimationTrack>& GetBoneTracks() const { return m_BoneTracks; }

    // 指定時刻の全Bone Local Poseを評価します。
    // Trackが存在しないBone/ChannelはSkeletonのBind Local Transformを維持します。
    // そのため「腕だけを持つClip」のような部分Animationでも未指定Boneを壊しません。
    // 全Local Transform設定後にGlobal Transformも一度だけ更新します。
    bool Sample(const Skeleton& skeleton, float time, SkeletonPose& outPose) const;

private:
    float m_Duration = 0.0f;

    // Scene/ECS向け単一Transform Track。
    TransformAnimationTrack m_TransformTrack;

    // Skeletal Animation向けBone Local Track群。
    std::vector<BoneAnimationTrack> m_BoneTracks;
};

} // namespace Raven
