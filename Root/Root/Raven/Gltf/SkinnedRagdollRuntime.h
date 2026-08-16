// Raven/Gltf/SkinnedRagdollRuntime.h
#pragma once

#include <cstddef>
#include <string>

#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven
{
namespace Gltf
{

class SkinnedMeshRuntimeAsset;

// ============================================================================
// SkinnedRagdollRuntime
// ============================================================================
// glTF SkinnedMesh RuntimeとRagdollRuntimeを接続する薄いBridgeです。
//
// 重要:
// - RagdollRuntimeはSkeleton / Pose変換だけを担当する
// - SkinnedRagdollRuntimeはSkinIndex単位でBody/Clothes等のPose同期を担当する
// - PhysicsWorld統合後もRagdollRuntime::SetBodyState()へRigidBody結果を流すだけでよい
//
// これによりAnimation / glTF / Physicsの責務を1クラスへ集中させません。
class SkinnedRagdollRuntime
{
public:
    bool Attach(
        std::size_t skinIndex,
        SkinnedMeshRuntimeAsset& targetAsset,
        const RagdollDefinition& definition,
        std::string* errorMessage = nullptr);

    // 現在のDeformer PoseをPhysics Body初期Poseとして取り込みます。
    // Animation Runtime更新後、Physics Simulationを有効化する直前に呼びます。
    bool EnterRagdoll(std::string* errorMessage = nullptr);

    // Physics側からBody状態を更新します。
    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    // 現在Body PoseをSkeletonへ反映し、同じSkinを使う全Primitiveへ配布します。
    // weight=1で完全Ragdoll、0～1でAnimation Poseとの復帰Blendとして利用できます。
    bool ApplyPose(
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // ApplyPose()後に既存CPU Skinning / GPU同期まで進める利便APIです。
    bool UpdateMesh(
        float deltaTime,
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    RagdollRuntime& GetRagdoll()
    {
        return m_Ragdoll;
    }

    const RagdollRuntime& GetRagdoll() const
    {
        return m_Ragdoll;
    }

private:
    std::size_t m_SkinIndex = 0u;
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    RagdollRuntime m_Ragdoll{};
};

} // namespace Gltf
} // namespace Raven
