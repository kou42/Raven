// Raven/Animation/Skeleton.cpp
#include "Raven/Animation/Skeleton.h"

#include <cassert>
#include <cmath>
#include <utility>

#include "Raven/Math/Math.h"

namespace Raven
{
namespace
{

bool ValidateBoneForAppend(const std::vector<Bone>& bones, const Bone& bone)
{
    const BoneIndex newIndex = static_cast<BoneIndex>(bones.size());

    // Parentは必ず先に登録済みでなければなりません。
    if (bone.Parent != InvalidBoneIndex && bone.Parent >= newIndex)
    {
        assert(false && "Skeleton: parent bone must be added before child bone.");
        return false;
    }

    // BoneTransformはTRSとして逆変換可能である必要があります。
    const math::Vec3& scale = bone.BindLocalTransform.Scale;
    if (std::fabs(scale.x) <= math::Epsilon
        || std::fabs(scale.y) <= math::Epsilon
        || std::fabs(scale.z) <= math::Epsilon)
    {
        assert(false && "Skeleton: bind scale must be non-zero.");
        return false;
    }

    return true;
}

} // namespace

BoneIndex Skeleton::AddBone(Bone bone)
{
    if (ValidateBoneForAppend(m_Bones, bone) == false)
    {
        return InvalidBoneIndex;
    }

    const BoneIndex newIndex = static_cast<BoneIndex>(m_Bones.size());

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

BoneIndex Skeleton::AddBoneWithInverseBindMatrix(
    Bone bone,
    const math::Mat4& inverseBindMatrix)
{
    if (ValidateBoneForAppend(m_Bones, bone) == false)
    {
        return InvalidBoneIndex;
    }

    const BoneIndex newIndex = static_cast<BoneIndex>(m_Bones.size());

    // glTF skin.inverseBindMatricesはMesh Bind Space -> Joint Spaceの正規データです。
    // ここではRaven側で再計算せず、その値をそのまま採用します。
    bone.InverseBindMatrix = inverseBindMatrix;
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
        {
            return i;
        }
    }

    return InvalidBoneIndex;
}

} // namespace Raven
