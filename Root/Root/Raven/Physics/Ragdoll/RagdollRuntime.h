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

struct RagdollBodyDefinition
{
    std::string BoneName;

    float Mass = 1.0f;
    float Radius = 0.12f;
    float HalfLength = 0.20f;
};

struct RagdollJointDefinition
{
    std::string ParentBoneName;
    std::string ChildBoneName;

    float SwingLimitRadians = 0.78539816339f;
    float TwistMinRadians = -0.52359877559f;
    float TwistMaxRadians = 0.52359877559f;

    // Child Boneのローカル空間でTwistとみなす軸です。
    // Humanoid AssetごとにBone長軸が異なるためDefinition側で明示できます。
    math::Vec3 TwistAxisLocal{ 0.0f, 1.0f, 0.0f };
};

struct RagdollDefinition
{
    std::vector<RagdollBodyDefinition> Bodies;
    std::vector<RagdollJointDefinition> Joints;
};

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
    bool Build(
        const Skeleton& skeleton,
        const RagdollDefinition& definition,
        std::string* errorMessage = nullptr);

    bool IsBuilt() const
    {
        return m_Skeleton != nullptr && m_Bodies.empty() == false;
    }

    // 現在Animation PoseをRagdoll Body Poseへ取り込みます。
    // 従来互換の「瞬間Capture」APIであり、Animation速度履歴を持たない用途を想定しているため、
    // Linear / Angular Velocityは0へ初期化します。
    bool CaptureAnimationPose(
        const SkeletonPose& pose,
        std::string* errorMessage = nullptr);

    // ========================================================================
    // Animation Velocity Sampling
    // ========================================================================
    // Animation駆動中に毎Frame、Animation評価後のPoseを渡してBody単位の速度を計測します。
    //
    // LinearVelocity:
    //   (currentBodyPosition - previousBodyPosition) / deltaTime
    //
    // AngularVelocity:
    //   currentRotation * inverse(previousRotation) の最短回転QuaternionをAxis-Angleへ変換し、
    //   world-space axis * angle / deltaTime として求めます。
    //
    // 1回目のSampleでは比較元が無いため速度は0です。2回目以降でAnimation由来速度が得られ、
    // RagdollPhysicsBridge::CreateBodies()がその値をRigidBodyへそのまま引き継げます。
    // deltaTime=0はPause等の正当なケースとして受け入れ、速度0の新しい基準Sampleとして扱います。
    bool SampleAnimationPose(
        const SkeletonPose& pose,
        float deltaTime,
        std::string* errorMessage = nullptr);

    // Animation Clip切替、Teleport、Time Seekなど「前Frameとの差分を速度として扱ってはいけない」
    // 不連続点で呼びます。次のSampleは初回扱いとなり、巨大な見かけ速度の発生を防ぎます。
    void ResetAnimationVelocityHistory();

    bool HasAnimationVelocityHistory() const
    {
        return m_HasPreviousAnimationPose;
    }

    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    bool ApplyToSkeletonPose(
        SkeletonPose& inOutPose,
        float weight = 1.0f,
        std::string* errorMessage = nullptr) const;

    RagdollBodyState* FindBody(BoneIndex boneIndex);
    const RagdollBodyState* FindBody(BoneIndex boneIndex) const;
    RagdollBodyState* FindBody(const std::string& boneName);
    const RagdollBodyState* FindBody(const std::string& boneName) const;

    const std::vector<RagdollBodyState>& GetBodies() const
    {
        return m_Bodies;
    }

    const RagdollDefinition& GetDefinition() const
    {
        return m_Definition;
    }

    // Physics BridgeがBoneIndexとBodyDefinitionを対応付けるための読み取り専用参照です。
    // Skeletonの所有権はRagdollRuntimeへ移さず、Build時に渡されたSkeletonがRuntimeより長く
    // 生存するという既存Lifetime契約を維持します。
    const Skeleton* GetSkeleton() const
    {
        return m_Skeleton;
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

    // Animation速度はPhysics Stateとは別の履歴から求めます。
    // Ragdollへ入った後にm_BodiesがPhysicsで更新されても、次回Animationへ戻った際の
    // Sampling履歴と混同しないため、前Animation Sampleを独立して保持します。
    std::vector<RagdollBodyState> m_PreviousAnimationBodies;
    bool m_HasPreviousAnimationPose = false;
};

} // namespace Raven
