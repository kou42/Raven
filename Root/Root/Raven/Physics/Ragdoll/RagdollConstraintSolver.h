// Raven/Physics/Ragdoll/RagdollConstraintSolver.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven
{

// ============================================================================
// RagdollConstraintSolverSettings
// ============================================================================
// Position Based Dynamicsに近い反復ProjectionでRagdoll Jointを安定化する設定です。
// 現段階ではRagdollBodyState上で動作し、PhysicsWorldのRigidBody統合後もConstraint定義と
// 基準Pose計算を再利用できるようSolverを独立させています。
struct RagdollConstraintSolverSettings
{
    std::size_t Iterations = 8u;

    // Anchor誤差を1反復で何割補正するか。1.0で全量補正します。
    float PositionStiffness = 0.8f;

    // Swing/Twist超過分を1反復で何割補正するか。
    float AngularStiffness = 0.7f;
};

struct RagdollJointConstraint
{
    BoneIndex ParentBone = InvalidBoneIndex;
    BoneIndex ChildBone = InvalidBoneIndex;

    float ParentInverseMass = 0.0f;
    float ChildInverseMass = 0.0f;

    // Ragdoll開始PoseでChild Bone原点がParent Bodyのどこにあるかを保存します。
    math::Vec3 ParentLocalAnchor{ 0.0f, 0.0f, 0.0f };
    math::Vec3 ChildLocalAnchor{ 0.0f, 0.0f, 0.0f };

    // Ragdoll開始時のParent -> Child相対回転です。
    // Swing/Twist制限はこの姿勢からの差分へ適用します。
    math::Quat ReferenceRelativeRotation = math::Quat::Identity();

    math::Vec3 TwistAxisLocal{ 0.0f, 1.0f, 0.0f };
    float SwingLimitRadians = 0.78539816339f;
    float TwistMinRadians = -0.52359877559f;
    float TwistMaxRadians = 0.52359877559f;
};

class RagdollConstraintSolver
{
public:
    // CaptureAnimationPose()後に呼び、現在Body PoseをConstraint基準姿勢として保存します。
    // Animation -> Ragdoll切り替え直後の姿勢がそのままRest Poseになるため、有効化時に
    // Joint Solverが急激な補正を発生させません。
    bool Initialize(
        RagdollRuntime& ragdoll,
        std::string* errorMessage = nullptr);

    // RagdollBodyStateへPosition / Angular Constraintを反復適用します。
    // Physics積分後、SkeletonPoseへ戻す前に呼ぶことを想定しています。
    bool Solve(
        RagdollRuntime& ragdoll,
        const RagdollConstraintSolverSettings& settings = {},
        std::string* errorMessage = nullptr) const;

    bool IsInitialized() const
    {
        return m_Constraints.empty() == false;
    }

    const std::vector<RagdollJointConstraint>& GetConstraints() const
    {
        return m_Constraints;
    }

private:
    bool SolvePositionConstraint(
        RagdollRuntime& ragdoll,
        const RagdollJointConstraint& constraint,
        float stiffness,
        std::string* errorMessage) const;

    bool SolveAngularConstraint(
        RagdollRuntime& ragdoll,
        const RagdollJointConstraint& constraint,
        float stiffness,
        std::string* errorMessage) const;

private:
    std::vector<RagdollJointConstraint> m_Constraints;
};

} // namespace Raven
