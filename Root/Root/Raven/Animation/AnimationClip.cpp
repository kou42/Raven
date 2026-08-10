#include "Raven/Animation/AnimationClip.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Raven
{
namespace
{

// ============================================================================
// SampleVec3Track
// ============================================================================
// Position / Translation / Scale Keyを線形補間します。
math::Vec3 SampleVec3Track(
    const std::vector<AnimationKeyframe<math::Vec3>>& keys,
    float time,
    const math::Vec3& defaultValue)
{
    if (keys.empty())
    {
        return defaultValue;
    }

    if (keys.size() == 1 || time <= keys.front().Time)
    {
        return keys.front().Value;
    }

    if (time >= keys.back().Time)
    {
        return keys.back().Value;
    }

    const auto rightIt = std::upper_bound(
        keys.begin(),
        keys.end(),
        time,
        [](float sampleTime, const AnimationKeyframe<math::Vec3>& key)
        {
            return sampleTime < key.Time;
        });

    const auto leftIt = rightIt - 1;
    const float interval = rightIt->Time - leftIt->Time;

    if (interval <= 0.0f)
    {
        return rightIt->Value;
    }

    const float alpha = (time - leftIt->Time) / interval;
    return math::Vec3::Lerp(leftIt->Value, rightIt->Value, alpha);
}

// ============================================================================
// SampleQuatTrack
// ============================================================================
// Rotation KeyはQuaternionの球面線形補間(Slerp)を使います。
// Euler角を各軸別にLerpすると、±pi境界で遠回りしたり、複数軸回転で姿勢変化が
// 不自然になりやすいため、Animation runtimeではQuaternionを正規表現にします。
math::Quat SampleQuatTrack(
    const std::vector<AnimationKeyframe<math::Quat>>& keys,
    float time,
    const math::Quat& defaultValue)
{
    if (keys.empty())
    {
        return defaultValue;
    }

    if (keys.size() == 1 || time <= keys.front().Time)
    {
        return keys.front().Value.Normalized();
    }

    if (time >= keys.back().Time)
    {
        return keys.back().Value.Normalized();
    }

    const auto rightIt = std::upper_bound(
        keys.begin(),
        keys.end(),
        time,
        [](float sampleTime, const AnimationKeyframe<math::Quat>& key)
        {
            return sampleTime < key.Time;
        });

    const auto leftIt = rightIt - 1;
    const float interval = rightIt->Time - leftIt->Time;

    if (interval <= 0.0f)
    {
        return rightIt->Value.Normalized();
    }

    const float alpha = (time - leftIt->Time) / interval;
    return math::Quat::Slerp(leftIt->Value, rightIt->Value, alpha);
}

// 1 Bone分のTrackを、Bind Local Transformを基準値として評価します。
// Channel単位でKeyが欠けていてもBind値を維持することが重要です。
BoneTransform SampleBoneTransform(
    const TransformAnimationTrack& track,
    float time,
    const BoneTransform& bindTransform)
{
    BoneTransform result = bindTransform;

    result.Translation = SampleVec3Track(
        track.PositionKeys,
        time,
        bindTransform.Translation);

    result.Rotation = SampleQuatTrack(
        track.RotationKeys,
        time,
        bindTransform.Rotation);

    result.Scale = SampleVec3Track(
        track.ScaleKeys,
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
    // Duration超過については各Trackの最終KeyでClampされます。
    const float sampleTime = std::max(time, 0.0f);

    pose.Position = SampleVec3Track(
        m_TransformTrack.PositionKeys,
        sampleTime,
        pose.Position);

    pose.Rotation = SampleQuatTrack(
        m_TransformTrack.RotationKeys,
        sampleTime,
        pose.Rotation);

    pose.Scale = SampleVec3Track(
        m_TransformTrack.ScaleKeys,
        sampleTime,
        pose.Scale);

    return pose;
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
        if (!skeleton.IsValidBoneIndex(track.Bone))
        {
            return false;
        }

        const BoneTransform& bindTransform =
            skeleton.GetBone(track.Bone).BindLocalTransform;

        const BoneTransform sampled = SampleBoneTransform(
            track.Transform,
            sampleTime,
            bindTransform);

        if (!outPose.SetLocalTransform(track.Bone, sampled))
        {
            return false;
        }
    }

    // ResetToBindPose()時にもGlobalは構築されますが、その後Track対象BoneのLocal値を
    // 上書きしているため、最後に一度だけ階層Global Transformを再計算します。
    return outPose.UpdateGlobalTransforms(skeleton);
}

} // namespace Raven
