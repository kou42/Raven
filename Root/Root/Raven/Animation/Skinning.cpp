// Raven/Animation/Skinning.cpp
#include "Raven/Animation/Skinning.h"

#include <cmath>
#include <cstddef>

namespace Raven
{

bool BuildSkinningMatrices(
    const Skeleton& skeleton,
    const SkeletonPose& pose,
    std::vector<math::Mat4>& outSkinningMatrices)
{
    const std::size_t boneCount = skeleton.GetBoneCount();

    // PoseとSkeletonはBone数が一致していなければ対応関係を定義できません。
    if (pose.GetBoneCount() != boneCount)
    {
        outSkinningMatrices.clear();
        return false;
    }

    outSkinningMatrices.resize(boneCount, math::Mat4::Identity());

    for (BoneIndex i = 0; i < static_cast<BoneIndex>(boneCount); ++i)
    {
        const Bone& bone = skeleton.GetBone(i);

        // ====================================================================
        // Linear Blend Skinning matrix
        // ====================================================================
        // Bind Poseの頂点はMesh Bind Spaceに存在します。
        // まずInverseBindで「そのBoneのBind Space」へ戻し、その後CurrentGlobalで
        // 現在PoseのMesh Spaceへ運びます。
        //
        // column-vector方式なので式は以下の順序になります。
        //   M_skin = M_currentGlobal * M_inverseBind
        outSkinningMatrices[static_cast<std::size_t>(i)] =
            pose.GetGlobalTransform(i) * bone.InverseBindMatrix;
    }

    return true;
}

bool AreSkinningMatricesIdentity(
    const std::vector<math::Mat4>& skinningMatrices,
    float tolerance)
{
    const math::Mat4 identity = math::Mat4::Identity();

    for (const math::Mat4& matrix : skinningMatrices)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (std::fabs(matrix[row][column] - identity[row][column]) > tolerance)
                    return false;
            }
        }
    }

    return true;
}

} // namespace Raven
