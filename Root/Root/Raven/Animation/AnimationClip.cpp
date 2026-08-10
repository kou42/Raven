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
// Keyframe列から指定時刻の値を線形補間します。
//
// 前提:
//   KeysはTime昇順に登録されていること。
//
// 境界の扱い:
//   time <= first.Time : first.Value
//   time >= last.Time  : last.Value
//
// このClampをSample側で行うことでAnimatorは「再生時刻をどう進めるか」だけに集中でき、
// Clipは任意時刻に対して安全に評価できる純粋なデータオブジェクトになります。
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

    // upper_boundを使うと「timeより後にある最初のKey」を取得できます。
    // その1つ前が補間開始Keyになるため、全Keyを毎回線形探索するよりも
    // Key数が増えたときのSampleコストを抑えられます。
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

    // 同一時刻のKeyが誤って隣接していても0除算しないようにします。
    // 正式なAsset Importerを追加するときにはKey列のValidationも別途行います。
    if (interval <= 0.0f)
    {
        return rightIt->Value;
    }

    const float alpha = (time - leftIt->Time) / interval;
    return math::Vec3::Lerp(leftIt->Value, rightIt->Value, alpha);
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

    pose.Rotation = SampleVec3Track(
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
