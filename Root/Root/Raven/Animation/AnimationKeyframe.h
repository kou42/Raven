#pragma once

namespace Raven
{

// ============================================================================
// AnimationKeyframe
// ============================================================================
// Animation上の「ある時刻における値」を表します。
//
// AnimationClip自身には再生中の時刻を持たせません。
// Clipは共有可能な不変データとして扱い、現在時刻・Loop・再生速度などの
// runtime stateは後続のAnimatorへ分離します。
//
// Tにはまずmath::Vec3を利用しますが、template化しておくことで、将来的に
// Quaternion / float / Morph WeightなどのTrackにも同じKeyframe表現を再利用できます。
template <typename T>
struct AnimationKeyframe
{
    float Time = 0.0f;
    T Value{};
};

} // namespace Raven
