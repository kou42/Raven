// Raven/Animation/SkeletonPose.cpp
#include "Raven/Animation/SkeletonPose.h"

#include <cassert>

namespace Raven
{

void SkeletonPose::ResetToBindPose(const Skeleton& skeleton)
{
    const std::size_t boneCount = skeleton.GetBoneCount();

    m_LocalTransforms.resize(boneCount);
    m_GlobalTransforms.resize(boneCount, math::Mat4::Identity());

    for (BoneIndex i = 0; i < static_cast<BoneIndex>(boneCount); ++i)
        m_LocalTransforms[static_cast<std::size_t>(i)] = skeleton.GetBone(i).BindLocalTransform;

    // Bind Local Poseをコピーした直後にGlobalも同期しておくことで、Reset直後から
    // GetGlobalTransform()を安全に利用できる状態にします。
    const bool updated = UpdateGlobalTransforms(skeleton);
    assert(updated);
    (void)updated;
}

bool SkeletonPose::UpdateGlobalTransforms(const Skeleton& skeleton)
{
    const std::size_t boneCount = skeleton.GetBoneCount();

    // PoseはSkeletonと1対1で対応します。
    // AnimationClipなどが将来Local Poseを書き換える際にも、Skeleton差し替えによる
    // サイズ不一致を黙って補正すると意図せずPoseが失われるため、明示的に失敗させます。
    if (m_LocalTransforms.size() != boneCount)
        return false;

    m_GlobalTransforms.resize(boneCount, math::Mat4::Identity());

    for (BoneIndex i = 0; i < static_cast<BoneIndex>(boneCount); ++i)
    {
        const Bone& bone = skeleton.GetBone(i);
        const math::Mat4 local = m_LocalTransforms[static_cast<std::size_t>(i)].ToMatrix();

        if (bone.Parent == InvalidBoneIndex)
        {
            m_GlobalTransforms[static_cast<std::size_t>(i)] = local;
            continue;
        }

        // Skeleton::AddBone()で parent < child を保証していますが、外部からBoneを編集した
        // 場合にも破損した階層をそのまま参照しないようRuntime側でも防御します。
        if (bone.Parent >= i || !skeleton.IsValidBoneIndex(bone.Parent))
            return false;

        // Raven::math::Mat4はcolumn-vector方式なので、Child Globalは
        // Parent Global * Child Local の順で合成します。
        m_GlobalTransforms[static_cast<std::size_t>(i)] =
            m_GlobalTransforms[static_cast<std::size_t>(bone.Parent)] * local;
    }

    return true;
}

bool SkeletonPose::SetLocalTransform(BoneIndex index, const BoneTransform& transform)
{
    if (static_cast<std::size_t>(index) >= m_LocalTransforms.size())
        return false;

    m_LocalTransforms[static_cast<std::size_t>(index)] = transform;
    return true;
}

const BoneTransform& SkeletonPose::GetLocalTransform(BoneIndex index) const
{
    assert(static_cast<std::size_t>(index) < m_LocalTransforms.size());
    return m_LocalTransforms[static_cast<std::size_t>(index)];
}

const math::Mat4& SkeletonPose::GetGlobalTransform(BoneIndex index) const
{
    assert(static_cast<std::size_t>(index) < m_GlobalTransforms.size());
    return m_GlobalTransforms[static_cast<std::size_t>(index)];
}

} // namespace Raven
