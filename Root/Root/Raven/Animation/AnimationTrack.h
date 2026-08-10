#pragma once

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Math/MathVector.h"

#include <vector>

namespace Raven
{

// ============================================================================
// TransformAnimationTrack
// ============================================================================
// 1つのTransformに対するPosition / Rotation / ScaleのKey列です。
//
// Position / ScaleはVec3を線形補間します。
// RotationはEuler角を直接補間せずQuaternionとして保持し、AnimationClip::Sample()で
// Slerpします。これにより180度境界を跨ぐ回転や複数軸回転で不自然な補間を避けます。
struct TransformAnimationTrack
{
    std::vector<AnimationKeyframe<math::Vec3>> PositionKeys;
    std::vector<AnimationKeyframe<math::Quat>> RotationKeys;
    std::vector<AnimationKeyframe<math::Vec3>> ScaleKeys;

    bool Empty() const
    {
        return PositionKeys.empty() && RotationKeys.empty() && ScaleKeys.empty();
    }
};

} // namespace Raven
