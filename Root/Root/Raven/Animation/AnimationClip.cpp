#include "Raven/Animation/AnimationClip.h"

#include <algorithm>
#include <cstddef>

namespace Raven
{
namespace
{

// ============================================================================
// SampleVec3Track
// ============================================================================
// Position / Scale Keyを線形補間します。
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

} // namespace Raven
