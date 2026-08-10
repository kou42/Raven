#pragma once

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Math/MathVector.h"

#include <vector>

namespace Raven
{

// ============================================================================
// TransformAnimationTrack
// ============================================================================
// 1つのTransformに対するPosition / Rotation / ScaleのKey列です。
//
// 現段階ではRotationも既存TransformComponentに合わせてEuler角(Vec3)で保持します。
// Skeletal Animationへ進む段階ではRotation TrackをQuaternionへ移行し、Slerpで
// 補間する予定です。まずは現在のScene/Transformと最小コストで接続できる形を優先します。
struct TransformAnimationTrack
{
    std::vector<AnimationKeyframe<math::Vec3>> PositionKeys;
    std::vector<AnimationKeyframe<math::Vec3>> RotationKeys;
    std::vector<AnimationKeyframe<math::Vec3>> ScaleKeys;

    bool Empty() const
    {
        return PositionKeys.empty() && RotationKeys.empty() && ScaleKeys.empty();
    }
};

} // namespace Raven
