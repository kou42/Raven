// Raven/Animation/Skeleton.cpp
#include "Raven/Animation/Skeleton.h"

#include <cassert>
#include <cmath>
#include <utility>

#include "Raven/Math/Math.h"

namespace Raven
{

BoneIndex Skeleton::AddBone(Bone bone)
{
    const BoneIndex newIndex = static_cast<BoneIndex>(m_Bones.size());

    // Parentは既にSkeletonへ登録済みでなければなりません。
    // この制約をここで固定することで、Pose更新時に再帰やトポロジカルソートが不要になります。
    if (bone.Parent != InvalidBoneIndex && bone.Parent >= newIndex)
    {
        assert(false && "Skeleton::AddBone(): parent bone must be added before child bone.");
        return InvalidBoneIndex;
    }

    // Bind TransformのScaleが0だとInverse Bind Matrixを構築できません。
    // Skinningではこの逆変換が必須なので、不正Skeletonを登録時点で拒否します。
    const math::Vec3& scale = bone.BindLocalTransform.Scale;
    if (std::fabs(scale.x) <= math::Epsilon
        || std::fabs(scale.y) <= math::Epsilon
        || std::fabs(scale.z) <= math::Epsilon)
    {
        assert(false && "Skeleton::AddBone(): bind scale must be non-zero.");
        return InvalidBoneIndex;
    }

    // ========================================================================
    // Inverse Bind Matrix construction
    // ========================================================================
    // Bind Global:
    //   childGlobal = parentGlobal * childLocal
    //
    // したがって逆行列は:
    //   inverse(childGlobal) = inverse(childLocal) * inverse(parentGlobal)
    //
    // Parentが先に登録される契約を利用し、一般4x4 inverse無しで逐次計算できます。
    const math::Mat4 inverseLocal = bone.BindLocalTransform.ToInverseMatrix();

    if (bone.Parent == InvalidBoneIndex)
    {
        bone.InverseBindMatrix = inverseLocal;
    }
    else
    {
        bone.InverseBindMatrix = inverseLocal * GetBone(bone.Parent).InverseBindMatrix;
    }

    m_Bones.emplace_back(std::move(bone));
    return newIndex;
}

const Bone& Skeleton::GetBone(BoneIndex index) const
{
    assert(IsValidBoneIndex(index));
    return m_Bones[static_cast<std::size_t>(index)];
}

Bone& Skeleton::GetBone(BoneIndex index)
{
    assert(IsValidBoneIndex(index));
    return m_Bones[static_cast<std::size_t>(index)];
}

BoneIndex Skeleton::FindBone(std::string_view name) const
{
    for (BoneIndex i = 0; i < static_cast<BoneIndex>(m_Bones.size()); ++i)
    {
        if (m_Bones[static_cast<std::size_t>(i)].Name == name)
            return i;
    }

    return InvalidBoneIndex;
}

} // namespace Raven
