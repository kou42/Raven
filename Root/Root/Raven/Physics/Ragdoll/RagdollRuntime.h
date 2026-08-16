// Raven/Physics/Ragdoll/RagdollRuntime.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// RagdollBodyDefinition
// ============================================================================
// 1つのSkeleton BoneをRagdollのPhysics Bodyへ対応付ける定義です。
//
// Stage 6の最初の実装ではPhysicsWorld側にBall/SocketやCone/Twist Constraintがまだ無いため、
// ここではBody形状・質量とBone対応だけを保持します。後続でPhysicsWorldへRigidBodyを生成する際に
// この定義をそのまま利用できるよう、Animation側とPhysics側の中間表現として分離しています。
struct RagdollBodyDefinition
{
    std::string BoneName;

    float Mass = 1.0f;
    float Radius = 0.12f;
    float HalfLength = 0.20f;
};

// ============================================================================
// RagdollJointDefinition
// ============================================================================
// Parent / Child Body間のJoint定義です。
// Swing/Twist Constraint Solverはこの定義を参照し、Ragdoll開始時の相対姿勢を基準姿勢として
// 角度制限を適用します。TwistAxisLocalはChild Bodyローカル空間でのTwist軸です。
struct RagdollJointDefinition
{
    std::string ParentBoneName;
    std::string ChildBoneName;

    float SwingLimitRadians = 0.78539816339f;
    float TwistMinRadians = -0.52359877559f;
    float TwistMaxRadians = 0.52359877559f;

    // Humanoid BoneはY方向を長軸としているAssetが多いため+Yを既定値にします。
    // AssetごとにBone軸規約が異なる場合はRagdoll Definition側で明示的に差し替えます。
    math::Vec3 TwistAxisLocal{ 0.0f, 1.0f, 0.0f };
};

struct RagdollDefinition
{
    std::vector<RagdollBodyDefinition> Bodies;
    std::vector<RagdollJointDefinition> Joints;
};

// ============================================================================
// RagdollBodyState
// ============================================================================
// Physics SolverとSkeletonの間で受け渡すRuntime Body状態です。
// Position / RotationはSkeleton Rootの親空間を基準としたGlobal Transformです。
//
// 実際のPhysicsWorld統合後は、この値をRigidBody Transformから取得するだけで
// Skeletonへの書き戻し処理は変更しなくて済む設計にしています。
struct RagdollBodyState
{
    BoneIndex Bone = InvalidBoneIndex;

    math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
    math::Quat Rotation = math::Quat::Identity();

    math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };
    math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };
};

class RagdollRuntime
{
public:
    // SkeletonとRagdoll定義を接続します。
    // Bone名解決・Body重複・Joint参照をすべて検証してからRuntime Stateへ反映します。
    bool Build(
        const Skeleton& skeleton,
        const RagdollDefinition& definition,
        std::string* errorMessage = nullptr);

    bool IsBuilt() const
    {
        return m_Skeleton != nullptr && m_Bodies.empty() == false;
    }

    // Animation -> Ragdoll
    // 現在のSkeletonPoseから各BodyのGlobal Position / Rotationを初期化します。
    // Ragdoll開始時にAnimation PoseからPhysics Bodyが飛ばないようにするための入口です。
    bool CaptureAnimationPose(
        const SkeletonPose& pose,
        std::string* errorMessage = nullptr);

    // Physics Solver側からBody Transformを更新するための低レベルAPIです。
    // Stage 6後半でPhysicsWorldのRigidBody結果をこのAPIへ流します。
    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    // Ragdoll -> Animation
    // Ragdoll BodyのGlobal TransformからBone Local Transformへ戻し、SkeletonPoseを書き換えます。
    // weight=1で完全Ragdoll、0で元Animation Poseを維持し、中間値ではLocal TRS Blendします。
    bool ApplyToSkeletonPose(
        SkeletonPose& inOutPose,
        float weight = 1.0f,
        std::string* errorMessage = nullptr) const;

    RagdollBodyState* FindBody(BoneIndex boneIndex);
    const RagdollBodyState* FindBody(BoneIndex boneIndex) const;

    // Constraint SolverはDefinitionのBone名からRuntime Bodyへ解決するため、
    // 名前検索をRagdollRuntimeの責務として提供します。Solver側でSkeleton検索を重複実装しません。
    RagdollBodyState* FindBody(const std::string& boneName);
    const RagdollBodyState* FindBody(const std::string& boneName) const;

    const Skeleton* GetSkeleton() const
    {
        return m_Skeleton;
    }

    const std::vector<RagdollBodyState>& GetBodies() const
    {
        return m_Bodies;
    }

    const RagdollDefinition& GetDefinition() const
    {
        return m_Definition;
    }

private:
    struct GlobalBoneTransform
    {
        math::Vec3 Position{ 0.0f, 0.0f, 0.0f };
        math::Quat Rotation = math::Quat::Identity();
        math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };
    };

    bool BuildGlobalTransforms(
        const SkeletonPose& pose,
        std::vector<GlobalBoneTransform>& outGlobals,
        std::string* errorMessage) const;

private:
    const Skeleton* m_Skeleton = nullptr;
    RagdollDefinition m_Definition{};
    std::vector<RagdollBodyState> m_Bodies;
};

} // namespace Raven
