// Raven/Animation/BlendTree1D.h
#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace Raven
{

// ============================================================================
// BlendTree1DChild
// ============================================================================
// 1D Blend Treeを構成する1つのMotionです。
// ThresholdはSpeedなどの1つのFloat Parameter上で、このClipが完全に選択される位置を表します。
// 例: Idle=0.0, Walk=2.0, Run=6.0
struct BlendTree1DChild
{
    float Threshold = 0.0f;
    std::shared_ptr<AnimationClip> Clip;
};

// ============================================================================
// BlendTree1D
// ============================================================================
// 1つのFloat値から隣接する2つのAnimationClipを選び、Poseを線形補間する最小Blend Treeです。
//
// State遷移のIdle -> Walk -> Runでは境界を跨ぐたびCrossFadeが発生しますが、Blend Treeでは
// Speedが0.0 -> 6.0へ連続変化するのに合わせ、Idle / Walk / Run Poseも連続的に混ざります。
//
// このクラス自身は再生時刻を持ちません。Animatorと同様に「Animationデータ」と「Runtime時間」を
// 分離するため、呼び出し側からNormalized Time(0.0～1.0)を受け取って各ClipをSampleします。
// Clip長が異なっても同じNormalized Timeを使うため、歩行周期の同じ位相同士をBlendできます。
class BlendTree1D
{
public:
    // Threshold順に自動挿入します。同じThresholdを複数登録すると補間区間が0になり曖昧なので拒否します。
    bool AddChild(float threshold, std::shared_ptr<AnimationClip> clip);

    void Clear();

    std::size_t GetChildCount() const { return m_Children.size(); }
    const std::vector<BlendTree1DChild>& GetChildren() const { return m_Children; }

    // 現在Parameter値で選ばれる2 ClipのDurationをPoseと同じWeightで補間します。
    // Animatorはこの値を「1周期の実時間」として使い、Normalized Timeをdtから進めます。
    // Parameterが変化してもNormalized Time自体を保持するため、Walk -> Runで歩行位相が跳びません。
    bool GetBlendedDuration(float parameterValue, float& outDuration) const;

    // 単一Transform Animation用のPose Sampleです。
    // parameterValueが最小/最大Thresholdの外側なら端のChildへClampします。
    bool SampleTransform(
        float parameterValue,
        float normalizedTime,
        TransformPose& outPose) const;

    // Skeletal Animation用のPose Sampleです。
    // 2 ClipをそれぞれSkeletonPoseへSampleしてからLocal TRSをBlendします。
    bool SampleSkeleton(
        const Skeleton& skeleton,
        float parameterValue,
        float normalizedTime,
        SkeletonPose& outPose) const;

private:
    // parameterValueを挟む2 Childと0.0～1.0の補間率を求めます。
    // Childが1つだけ、または範囲外の場合はleft == right、weight=0になります。
    bool ResolveBlend(
        float parameterValue,
        const BlendTree1DChild*& outLeft,
        const BlendTree1DChild*& outRight,
        float& outWeight) const;

private:
    // Threshold昇順を常に維持します。Sample時に毎Frame Sortしないことが重要です。
    std::vector<BlendTree1DChild> m_Children;
};

} // namespace Raven
