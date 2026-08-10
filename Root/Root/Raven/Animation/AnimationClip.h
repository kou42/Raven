#pragma once

#include "Raven/Animation/AnimationTrack.h"

namespace Raven
{

// ============================================================================
// TransformPose
// ============================================================================
// AnimationClip::Sample()が返す「指定時刻のTransform」です。
// TransformComponentそのものを返さないことでAnimation層をScene/ECSから独立させます。
// これにより将来的にPose同士のBlendやCross FadeをRenderer/Sceneに依存せず実装できます。
struct TransformPose
{
    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Rotation{ 0.0f, 0.0f, 0.0f };
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };
};

// ============================================================================
// AnimationClip
// ============================================================================
// AnimationClipは「Animationデータ」だけを所有します。
// CurrentTime / Playing / Loop / Speedなどの再生状態はここへ置かず、後続のAnimatorが
// 所有します。この分離により1つのClipを複数Entity/Animatorから共有できます。
class AnimationClip
{
public:
    AnimationClip() = default;
    explicit AnimationClip(float duration);

    float GetDuration() const { return m_Duration; }
    void SetDuration(float duration);

    TransformAnimationTrack& GetTransformTrack() { return m_TransformTrack; }
    const TransformAnimationTrack& GetTransformTrack() const { return m_TransformTrack; }

    // 指定時刻のPoseを評価します。
    // TrackにKeyが無いChannelはTransformPoseの既定値を維持します。
    TransformPose Sample(float time) const;

private:
    float m_Duration = 0.0f;
    TransformAnimationTrack m_TransformTrack;
};

} // namespace Raven
