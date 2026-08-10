// Raven/Animation/Skeleton.h
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "Raven/Animation/Bone.h"

namespace Raven
{

// ============================================================================
// Skeleton
// ============================================================================
// Bone階層そのものを保持する共有可能な定義データです。
//
// 重要な契約:
// Parent Boneは必ずChild Boneより小さいIndexを持ちます。
// この順序をAddBone()で保証することで、SkeletonPoseは先頭から1回走査するだけで
// Global Transformを計算できます。
class Skeleton
{
public:
    BoneIndex AddBone(Bone bone);

    const Bone& GetBone(BoneIndex index) const;
    Bone& GetBone(BoneIndex index);

    std::size_t GetBoneCount() const
    {
        return m_Bones.size();
    }

    bool IsValidBoneIndex(BoneIndex index) const
    {
        return static_cast<std::size_t>(index) < m_Bones.size();
    }

    BoneIndex FindBone(std::string_view name) const;

    const std::vector<Bone>& GetBones() const
    {
        return m_Bones;
    }

private:
    std::vector<Bone> m_Bones;
};

} // namespace Raven
