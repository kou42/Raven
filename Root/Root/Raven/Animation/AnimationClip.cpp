#include "Raven/Animation/AnimationClip.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Raven
{
namespace
{

// 1 Bone分のTrackを、Bind Local Transformを基準値として評価します。
// Channel単位でKeyが欠けていてもBind値を維持することが重要です。
BoneTransform SampleBoneTransform(
    const TransformAnimationTrack& track,
    float time,
    const BoneTransform& bindTransform)
{
    BoneTransform result = bindTransform;

    result.Translation = track.PositionKeys.Sample(
        time,
        bindTransform.Translation);

    result.Rotation = track.RotationKeys.Sample(
        time,
        bindTransform.Rotation);

    result.Scale = track.ScaleKeys.Sample(
        time,
        bindTransform.Scale);

    return result;
}

} // namespace

AnimationClip::AnimationClip(float duration)
{
    SetDuration(duration);
}

void AnimationClip::SetDuration(float duration)
{
    // 負のDurationはAnimationとして意味を持たないため0へClampします。
    m_Duration = std::max(duration, 0.0f);
}

TransformPose AnimationClip::Sample(float time) const
{
    TransformPose pose{};

    // Clip単体でSampleした場合にも負の時刻を安全に扱います。
    // Duration超過については各Curveの最終KeyでClampされます。
    const float sampleTime = std::max(time, 0.0f);

    pose.Position = m_TransformTrack.PositionKeys.Sample(
        sampleTime,
        pose.Position);

    pose.Rotation = m_TransformTrack.RotationKeys.Sample(
        sampleTime,
        pose.Rotation);

    pose.Scale = m_TransformTrack.ScaleKeys.Sample(
        sampleTime,
        pose.Scale);

    return pose;
}

bool AnimationClip::AddPropertyTrack(AnimationPropertyTrack track)
{
    const AnimationPropertyBinding& binding = GetAnimationPropertyBinding(track);
    if (binding.IsValid() == false || IsAnimationPropertyTrackEmpty(track))
    {
        return false;
    }

    // 同じTarget/Propertyへ複数Trackを登録すると適用順で結果が変わります。
    // AnimationClip内部に暗黙の優先順位を持ち込まず、Layer/Blend実装へ責務を残します。
    const auto duplicateIt = std::find_if(
        m_PropertyTracks.begin(),
        m_PropertyTracks.end(),
        [&binding](const AnimationPropertyTrack& existing)
        {
            return GetAnimationPropertyBinding(existing) == binding;
        });

    if (duplicateIt != m_PropertyTracks.end())
    {
        return false;
    }

    m_PropertyTracks.emplace_back(std::move(track));
    return true;
}

void AnimationClip::SampleProperties(
    float time,
    std::vector<AnimationPropertySample>& outSamples) const
{
    // Animator側のLoop/Speed規則をClipへ持ち込まず、既存Transform/Skeleton Sampleと同様に
    // 負の時刻だけを0へClampします。Duration超過は各Curveの最終KeyへClampされます。
    const float sampleTime = std::max(time, 0.0f);

    outSamples.clear();
    outSamples.reserve(m_PropertyTracks.size());

    for (const AnimationPropertyTrack& track : m_PropertyTracks)
    {
        outSamples.emplace_back(SampleAnimationPropertyTrack(track, sampleTime));
    }
}

bool AnimationClip::AddBoneTrack(BoneAnimationTrack track)
{
    if (track.Bone == InvalidBoneIndex)
    {
        return false;
    }

    // 1 Boneにつき1 Trackという契約にします。
    // Import時の重複をここで拒否しておけばRuntime Sample時の優先順位が不要になります。
    if (FindBoneTrack(track.Bone) != nullptr)
    {
        return false;
    }

    m_BoneTracks.push_back(std::move(track));
    return true;
}

const BoneAnimationTrack* AnimationClip::FindBoneTrack(BoneIndex boneIndex) const
{
    const auto it = std::find_if(
        m_BoneTracks.begin(),
        m_BoneTracks.end(),
        [boneIndex](const BoneAnimationTrack& track)
        {
            return track.Bone == boneIndex;
        });

    return (it != m_BoneTracks.end()) ? &(*it) : nullptr;
}

BoneAnimationTrack* AnimationClip::FindBoneTrack(BoneIndex boneIndex)
{
    const auto it = std::find_if(
        m_BoneTracks.begin(),
        m_BoneTracks.end(),
        [boneIndex](const BoneAnimationTrack& track)
        {
            return track.Bone == boneIndex;
        });

    return (it != m_BoneTracks.end()) ? &(*it) : nullptr;
}

bool AnimationClip::Sample(
    const Skeleton& skeleton,
    float time,
    SkeletonPose& outPose) const
{
    // SkeletonPoseは最初にBind Poseへ戻します。
    // これによりTrackを持たないBoneはBind Local Transformをそのまま維持できます。
    outPose.ResetToBindPose(skeleton);

    // Clip単体のSampleなのでAnimatorのLoop規則はここへ持ち込みません。
    // AnimatorStateが時間を[0, Duration]へ正規化し、Clipは渡された時刻を評価するだけです。
    const float sampleTime = std::max(time, 0.0f);

    for (const BoneAnimationTrack& track : m_BoneTracks)
    {
        // ClipとSkeletonの対応が壊れている場合は、別Skeleton向けClipを誤適用している可能性が
        // 高いため、黙って無視せずSample失敗として返します。
        if (skeleton.IsValidBoneIndex(track.Bone) == false)
        {
            return false;
        }

        const BoneTransform& bindTransform =
            skeleton.GetBone(track.Bone).BindLocalTransform;

        const BoneTransform sampled = SampleBoneTransform(
            track.Transform,
            sampleTime,
            bindTransform);

        if (outPose.SetLocalTransform(track.Bone, sampled) == false)
        {
            return false;
        }
    }

    // ResetToBindPose()時にもGlobalは構築されますが、その後Track対象BoneのLocal値を
    // 上書きしているため、最後に一度だけ階層Global Transformを再計算します。
    return outPose.UpdateGlobalTransforms(skeleton);
}

} // namespace Raven
