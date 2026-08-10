// Raven/Animation/Skinning.h
#pragma once

#include <vector>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

// Current Global PoseとInverse Bind Matrixから、各BoneのSkinning Matrixを構築します。
// Ravenのcolumn-vector規約では、Bind頂点pへ対して
//   p' = CurrentGlobal * InverseBind * p
// の順で作用します。
bool BuildSkinningMatrices(
    const Skeleton& skeleton,
    const SkeletonPose& pose,
    std::vector<math::Mat4>& outSkinningMatrices);

// Bind Pose時のSkinning Matrixが全BoneでIdentityになることを確認するSelf Test用Helperです。
bool AreSkinningMatricesIdentity(
    const std::vector<math::Mat4>& skinningMatrices,
    float tolerance = 1.0e-4f);

} // namespace Raven
