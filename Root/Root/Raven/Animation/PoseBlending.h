// Raven/Animation/PoseBlending.h
#pragma once

#include "Raven/Animation/SkeletonPose.h"

namespace Raven
{

// 2つのLocal PoseをBone単位で補間し、resultへGlobal Transformまで構築します。
// CrossFadeはAnimationClipそのものではなく、各StateからSampleされたPoseを
// この関数で混ぜることでClip形式とBlend処理を疎結合に保ちます。
bool BlendPoses(
    const Skeleton& skeleton,
    const SkeletonPose& from,
    const SkeletonPose& to,
    float weight,
    SkeletonPose& result);

} // namespace Raven
