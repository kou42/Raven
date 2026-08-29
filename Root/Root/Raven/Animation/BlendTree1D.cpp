// Raven/Animation/BlendTree1D.cpp
#include "Raven/Animation/BlendTree1D.h"

#include "Raven/Animation/PoseBlending.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace Raven
{

bool BlendTree1D::AddChild(float threshold, std::shared_ptr<AnimationClip> clip)
{
    if (std::isfinite(threshold) == false || clip == nullptr)
    {
        return false;
    }

    // Threshold昇順になる位置を二分探索します。
    // Blend Treeは毎Frame Sampleされるため、評価時ではなく登録時に順序を確定しておきます。
    const auto it = std::lower_bound(
        m_Children.begin(),
        m_Children.end(),
        threshold,
        [](const BlendTree1DChild& child, float value)
        {
            return child.Threshold < value;
        });

    // 同一Thresholdが複数あると、どちらのClipを完全選択すべきか決められません。
    if (it != m_Children.end() && it->Threshold == threshold)
    {
        return false;
    }

    m_Children.insert(it, BlendTree1DChild{ threshold, std::move(clip) });
    return true;
}

bool BlendTree1D::SetThresholds(const std::vector<float>& thresholds)
{
    if (thresholds.size() != m_Children.size())
    {
        return false;
    }

    // Child IndexはLocomotionのIdle / Walk / Run名解決にも使われるため、並べ替えは行いません。
    // 有限かつ狭義昇順であることを先に検証し、ClipとIndexの対応を維持したまま値だけを更新します。
    for (std::size_t index = 0; index < thresholds.size(); ++index)
    {
        if (std::isfinite(thresholds[index]) == false)
        {
            return false;
        }
        if (index > 0 && thresholds[index] <= thresholds[index - 1])
        {
            return false;
        }
    }

    for (std::size_t index = 0; index < thresholds.size(); ++index)
    {
        m_Children[index].Threshold = thresholds[index];
    }
    return true;
}

void BlendTree1D::Clear()
{
    m_Children.clear();
}

bool BlendTree1D::ResolveBlend(
    float parameterValue,
    const BlendTree1DChild*& outLeft,
    const BlendTree1DChild*& outRight,
    float& outWeight) const
{
    outLeft = nullptr;
    outRight = nullptr;
    outWeight = 0.0f;

    if (std::isfinite(parameterValue) == false || m_Children.empty())
    {
        return false;
    }

    // Parameterが最小Threshold以下なら最初のMotionへClampします。
    if (parameterValue <= m_Children.front().Threshold)
    {
        outLeft = &m_Children.front();
        outRight = outLeft;
        return true;
    }

    // 最大Threshold以上なら最後のMotionへClampします。
    if (parameterValue >= m_Children.back().Threshold)
    {
        outLeft = &m_Children.back();
        outRight = outLeft;
        return true;
    }

    const auto rightIt = std::upper_bound(
        m_Children.begin(),
        m_Children.end(),
        parameterValue,
        [](float value, const BlendTree1DChild& child)
        {
            return value < child.Threshold;
        });

    if (rightIt == m_Children.begin() || rightIt == m_Children.end())
    {
        return false;
    }

    const auto leftIt = std::prev(rightIt);
    const float thresholdRange = rightIt->Threshold - leftIt->Threshold;
    if (thresholdRange <= 0.0f)
    {
        return false;
    }

    outLeft = &(*leftIt);
    outRight = &(*rightIt);
    outWeight = std::clamp(
        (parameterValue - leftIt->Threshold) / thresholdRange,
        0.0f,
        1.0f);
    return true;
}

bool BlendTree1D::GetDebugInfo(float parameterValue, BlendTree1DDebugInfo& outInfo) const
{
    outInfo = {};

    const BlendTree1DChild* left = nullptr;
    const BlendTree1DChild* right = nullptr;
    float rightWeight = 0.0f;
    if (ResolveBlend(parameterValue, left, right, rightWeight) == false ||
        left == nullptr || right == nullptr)
    {
        return false;
    }

    // ResolveBlend()と同じ結果からDebug情報を作ることが重要です。
    // Editor側でThreshold補間を再計算しないため、Runtime Poseと表示Weightが必ず一致します。
    outInfo.ParameterValue = parameterValue;
    outInfo.LeftChildIndex = static_cast<std::size_t>(left - m_Children.data());
    outInfo.RightChildIndex = static_cast<std::size_t>(right - m_Children.data());
    outInfo.LeftThreshold = left->Threshold;
    outInfo.RightThreshold = right->Threshold;

    if (left == right)
    {
        outInfo.LeftWeight = 1.0f;
        outInfo.RightWeight = 0.0f;
        outInfo.IsClamped = parameterValue <= m_Children.front().Threshold ||
            parameterValue >= m_Children.back().Threshold;
        return true;
    }

    outInfo.LeftWeight = 1.0f - rightWeight;
    outInfo.RightWeight = rightWeight;
    outInfo.IsClamped = false;
    return true;
}

bool BlendTree1D::GetBlendedDuration(float parameterValue, float& outDuration) const
{
    outDuration = 0.0f;

    const BlendTree1DChild* left = nullptr;
    const BlendTree1DChild* right = nullptr;
    float weight = 0.0f;
    if (ResolveBlend(parameterValue, left, right, weight) == false ||
        left == nullptr || right == nullptr || left->Clip == nullptr || right->Clip == nullptr)
    {
        return false;
    }

    const float leftDuration = std::max(left->Clip->GetDuration(), 0.0f);
    if (left == right)
    {
        outDuration = leftDuration;
        return true;
    }

    const float rightDuration = std::max(right->Clip->GetDuration(), 0.0f);
    outDuration = leftDuration + (rightDuration - leftDuration) * weight;
    return std::isfinite(outDuration);
}

bool BlendTree1D::SampleTransform(
    float parameterValue,
    float normalizedTime,
    TransformPose& outPose) const
{
    if (std::isfinite(normalizedTime) == false)
    {
        return false;
    }

    const BlendTree1DChild* left = nullptr;
    const BlendTree1DChild* right = nullptr;
    float weight = 0.0f;
    if (ResolveBlend(parameterValue, left, right, weight) == false ||
        left == nullptr || right == nullptr || left->Clip == nullptr || right->Clip == nullptr)
    {
        return false;
    }

    const float normalized = std::clamp(normalizedTime, 0.0f, 1.0f);
    const TransformPose leftPose = left->Clip->Sample(normalized * left->Clip->GetDuration());

    if (left == right)
    {
        outPose = leftPose;
        return true;
    }

    const TransformPose rightPose = right->Clip->Sample(normalized * right->Clip->GetDuration());

    // Position / Scaleは線形補間、RotationはQuaternion Slerpを使います。
    // CrossFadeと同じ補間規則にすることで、Blend Treeだけ別の回転挙動になることを防ぎます。
    outPose.Position = math::Vec3::Lerp(leftPose.Position, rightPose.Position, weight);
    outPose.Rotation = math::Quat::Slerp(leftPose.Rotation, rightPose.Rotation, weight);
    outPose.Scale = math::Vec3::Lerp(leftPose.Scale, rightPose.Scale, weight);
    return true;
}

bool BlendTree1D::SampleSkeleton(
    const Skeleton& skeleton,
    float parameterValue,
    float normalizedTime,
    SkeletonPose& outPose) const
{
    if (std::isfinite(normalizedTime) == false)
    {
        return false;
    }

    const BlendTree1DChild* left = nullptr;
    const BlendTree1DChild* right = nullptr;
    float weight = 0.0f;
    if (ResolveBlend(parameterValue, left, right, weight) == false ||
        left == nullptr || right == nullptr || left->Clip == nullptr || right->Clip == nullptr)
    {
        return false;
    }

    const float normalized = std::clamp(normalizedTime, 0.0f, 1.0f);

    SkeletonPose leftPose;
    if (left->Clip->Sample(
            skeleton,
            normalized * left->Clip->GetDuration(),
            leftPose) == false)
    {
        return false;
    }

    if (left == right)
    {
        outPose = std::move(leftPose);
        return true;
    }

    SkeletonPose rightPose;
    if (right->Clip->Sample(
            skeleton,
            normalized * right->Clip->GetDuration(),
            rightPose) == false)
    {
        return false;
    }

    // SkeletonはGlobal行列を直接Lerpせず、PoseBlendingでLocal TRSをBlendしてから
    // Global Transformを再構築します。これはCrossFadeと同じ重要なルールです。
    return BlendPoses(
        skeleton,
        leftPose,
        rightPose,
        weight,
        outPose);
}

} // namespace Raven
