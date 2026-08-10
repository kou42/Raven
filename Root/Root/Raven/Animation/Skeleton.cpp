// Raven/Animation/Skeleton.cpp
#include "Raven/Animation/Skeleton.h"

#include <cassert>
#include <utility>

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
