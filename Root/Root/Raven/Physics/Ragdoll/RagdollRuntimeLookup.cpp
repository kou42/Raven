// Raven/Physics/Ragdoll/RagdollRuntimeLookup.cpp
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven
{

RagdollBodyState* RagdollRuntime::FindBody(const std::string& boneName)
{
    if (m_Skeleton == nullptr)
    {
        return nullptr;
    }

    const BoneIndex boneIndex = m_Skeleton->FindBone(boneName);
    if (boneIndex == InvalidBoneIndex)
    {
        return nullptr;
    }

    return FindBody(boneIndex);
}

const RagdollBodyState* RagdollRuntime::FindBody(const std::string& boneName) const
{
    if (m_Skeleton == nullptr)
    {
        return nullptr;
    }

    const BoneIndex boneIndex = m_Skeleton->FindBone(boneName);
    if (boneIndex == InvalidBoneIndex)
    {
        return nullptr;
    }

    return FindBody(boneIndex);
}

} // namespace Raven
