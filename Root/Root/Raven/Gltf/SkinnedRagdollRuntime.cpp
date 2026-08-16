// Raven/Gltf/SkinnedRagdollRuntime.cpp
#include "Raven/Gltf/SkinnedRagdollRuntime.h"

#include <cmath>
#include <string>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"

namespace Raven
{
namespace Gltf
{
namespace
{

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

SkeletalMeshDeformer* GetSkeletalDeformer(
    const RuntimeSkinnedPrimitive& primitive,
    std::string* errorMessage)
{
    if (primitive.DeformationInstance == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象のMeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象のMeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象DeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }

    return skeletalDeformer;
}

bool SkeletonsMatch(const Skeleton& lhs, const Skeleton& rhs)
{
    if (lhs.GetBoneCount() != rhs.GetBoneCount())
    {
        return false;
    }

    for (std::size_t boneSlot = 0u; boneSlot < lhs.GetBoneCount(); ++boneSlot)
    {
        const BoneIndex boneIndex = static_cast<BoneIndex>(boneSlot);
        const Bone& leftBone = lhs.GetBone(boneIndex);
        const Bone& rightBone = rhs.GetBone(boneIndex);

        if (leftBone.Name != rightBone.Name || leftBone.Parent != rightBone.Parent)
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool SkinnedRagdollRuntime::Attach(
    std::size_t skinIndex,
    SkinnedMeshRuntimeAsset& targetAsset,
    const RagdollDefinition& definition,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkeletalMeshDeformer* referenceDeformer = nullptr;

    for (const RuntimeSkinnedPrimitive& primitive : targetAsset.GetPrimitives())
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        if (referenceDeformer == nullptr)
        {
            referenceDeformer = deformer;
            continue;
        }

        // 同一Skinを共有するBody / Clothesは同じBone Index契約でPoseをコピーするため、
        // Attach時点でSkeleton構造が一致していることを確認します。
        if (SkeletonsMatch(referenceDeformer->GetSkeleton(), deformer->GetSkeleton()) == false)
        {
            return SetError(errorMessage, "同一SkinIndexのRuntime Primitive間でSkeletonが一致しません");
        }
    }

    if (referenceDeformer == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    RagdollRuntime ragdoll;
    if (ragdoll.Build(referenceDeformer->GetSkeleton(), definition, errorMessage) == false)
    {
        return false;
    }

    m_SkinIndex = skinIndex;
    m_TargetAsset = &targetAsset;
    m_Ragdoll = std::move(ragdoll);
    return true;
}

bool SkinnedRagdollRuntime::EnterRagdoll(std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr || m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "SkinnedRagdollRuntimeがAttachされていません");
    }

    // 同じSkinを使うPrimitiveはAnimation Runtime側で同じPoseへ同期されるため、
    // 最初の1つをRagdoll開始Poseの基準として使用します。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        return m_Ragdoll.CaptureAnimationPose(deformer->GetPose(), errorMessage);
    }

    return SetError(errorMessage, "Ragdoll開始Poseを取得できるPrimitiveがありません");
}

bool SkinnedRagdollRuntime::SetBodyState(
    BoneIndex boneIndex,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    return m_Ragdoll.SetBodyState(boneIndex, state, errorMessage);
}

bool SkinnedRagdollRuntime::SetBodyState(
    const std::string& boneName,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    return m_Ragdoll.SetBodyState(boneName, state, errorMessage);
}

bool SkinnedRagdollRuntime::ApplyPose(
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr || m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "SkinnedRagdollRuntimeがAttachされていません");
    }
    if (std::isfinite(weight) == false)
    {
        return SetError(errorMessage, "Ragdoll Blend Weightは有限値である必要があります");
    }

    SkeletonPose outputPose;
    bool poseCreated = false;
    const Skeleton* referenceSkeleton = nullptr;

    // ========================================================================
    // Animation PoseをBlend元としてRagdoll Poseを構築
    // ========================================================================
    // weight < 1では「現在Animation Pose」とPhysics Poseを混ぜます。
    // そのため最初のPrimitiveの現在PoseをコピーしてからRagdollRuntimeへ渡します。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        outputPose = deformer->GetPose();
        referenceSkeleton = &deformer->GetSkeleton();
        poseCreated = true;
        break;
    }

    if (poseCreated == false || referenceSkeleton == nullptr)
    {
        return SetError(errorMessage, "Ragdoll PoseのBlend元を取得できません");
    }

    if (m_Ragdoll.ApplyToSkeletonPose(outputPose, weight, errorMessage) == false)
    {
        return false;
    }

    // 完成Poseを同じSkinを使う全Primitiveへ配布します。
    // Body / Clothesを別々にRagdoll変換しないことでPose差を防ぎます。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        if (SkeletonsMatch(*referenceSkeleton, deformer->GetSkeleton()) == false)
        {
            return SetError(errorMessage, "Ragdoll Pose配布先Skeletonが基準Skeletonと一致しません");
        }

        deformer->GetPose() = outputPose;
    }

    return true;
}

bool SkinnedRagdollRuntime::UpdateMesh(
    float deltaTime,
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    if (ApplyPose(weight, errorMessage) == false)
    {
        return false;
    }

    return m_TargetAsset->Update(deltaTime, errorMessage);
}

} // namespace Gltf
} // namespace Raven
