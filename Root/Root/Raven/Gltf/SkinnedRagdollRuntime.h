// Raven/Gltf/SkinnedRagdollRuntime.h
#pragma once

#include <cstddef>
#include <string>

#include "Raven/Physics/Ragdoll/RagdollConstraintSolver.h"
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
// - RagdollConstraintSolverはJoint接続とSwing/Twist制限を担当する
// - SkinnedRagdollRuntimeはSkinIndex単位でBody/Clothes等のPose同期を担当する
// - PhysicsWorld統合後もRigidBody結果をSetBodyState()へ流してConstraintを解けばよい
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

    // EnterRagdoll()で取り込んだ現在PoseをJointの基準姿勢としてConstraintを初期化します。
    // AnimationからRagdollへ切り替えるたびに呼ぶことで、その瞬間の姿勢から自然にPhysicsへ移行します。
    bool InitializeConstraints(std::string* errorMessage = nullptr);

    // Physics側からBody状態を更新します。
    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    // Physics積分後のBody StateへBall/Socket + Swing/Twist制約を反復適用します。
    bool SolveConstraints(
        const RagdollConstraintSolverSettings& settings = {},
        std::string* errorMessage = nullptr);

    // 現在Body PoseをSkeletonへ反映し、同じSkinを使う全Primitiveへ配布します。
    // weight=1で完全Ragdoll、0～1でAnimation Poseとの復帰Blendとして利用できます。
    bool ApplyPose(
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // Constraint解決 -> ApplyPose -> CPU Skinning / GPU同期まで進める利便APIです。
    bool UpdateConstrainedMesh(
        float deltaTime,
        const RagdollConstraintSolverSettings& settings = {},
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // Constraintを使わず、現在Body PoseをそのままMeshへ反映する従来APIです。
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

    RagdollConstraintSolver& GetConstraintSolver()
    {
        return m_ConstraintSolver;
    }

    const RagdollConstraintSolver& GetConstraintSolver() const
    {
        return m_ConstraintSolver;
    }

private:
    std::size_t m_SkinIndex = 0u;
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    RagdollRuntime m_Ragdoll{};
    RagdollConstraintSolver m_ConstraintSolver{};
};

} // namespace Gltf
} // namespace Raven
