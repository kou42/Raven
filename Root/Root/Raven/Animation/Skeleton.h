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

    // glTFなど外部Asset側が正規のInverse Bind Matrixを保持している場合の登録経路です。
    // AddBone()と同じ親->子順契約とScale検証を維持しつつ、InverseBindMatrixだけは
    // Asset側の値を採用します。これによりImporterが一度読み取ったbind情報を
    // Raven側で再計算して丸め差やMesh Bind Space差を生むことを避けます。
    BoneIndex AddBoneWithInverseBindMatrix(
        Bone bone,
        const math::Mat4& inverseBindMatrix);

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
