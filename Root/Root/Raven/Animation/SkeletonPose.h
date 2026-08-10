// Raven/Animation/SkeletonPose.h
#pragma once

#include <vector>

#include "Raven/Animation/Skeleton.h"

namespace Raven
{

// ============================================================================
// SkeletonPose
// ============================================================================
// Skeleton定義に対する「現在姿勢」を保持するRuntime Stateです。
// Bone自体へCurrent Transformを持たせないことで、同じSkeleton Assetを複数Entityが
// 異なるAnimation状態で共有できるようにします。
class SkeletonPose
{
public:
    // 全BoneのLocal TransformをBind Poseへ戻し、その状態のGlobal Transformまで更新します。
    void ResetToBindPose(const Skeleton& skeleton);

    // Local PoseからGlobal Transformを再構築します。
    // Skeleton::AddBone()が保証する「Parent Index < Child Index」を利用するため、
    // 再帰なし・先頭から1回の走査で階層変換を解決できます。
    bool UpdateGlobalTransforms(const Skeleton& skeleton);

    bool SetLocalTransform(BoneIndex index, const BoneTransform& transform);

    const BoneTransform& GetLocalTransform(BoneIndex index) const;
    const math::Mat4& GetGlobalTransform(BoneIndex index) const;

    std::size_t GetBoneCount() const
    {
        return m_LocalTransforms.size();
    }

private:
    std::vector<BoneTransform> m_LocalTransforms;
    std::vector<math::Mat4> m_GlobalTransforms;
};

} // namespace Raven
