// Raven/Physics/Ragdoll/RagdollRuntime.cpp
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Raven
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

bool IsFinite(const math::Vec3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

math::Vec3 MultiplyComponents(const math::Vec3& lhs, const math::Vec3& rhs)
{
    return math::Vec3{
        lhs.x * rhs.x,
        lhs.y * rhs.y,
        lhs.z * rhs.z
    };
}

bool DivideComponents(
    const math::Vec3& value,
    const math::Vec3& divisor,
    math::Vec3& outValue)
{
    if (std::fabs(divisor.x) <= math::Epsilon
        || std::fabs(divisor.y) <= math::Epsilon
        || std::fabs(divisor.z) <= math::Epsilon)
    {
        return false;
    }

    outValue = math::Vec3{
        value.x / divisor.x,
        value.y / divisor.y,
        value.z / divisor.z
    };
    return true;
}

bool ContainsBodyBone(
    const std::vector<RagdollBodyState>& bodies,
    BoneIndex boneIndex)
{
    const auto it = std::find_if(
        bodies.begin(),
        bodies.end(),
        [boneIndex](const RagdollBodyState& body)
        {
            return body.Bone == boneIndex;
        });

    return it != bodies.end();
}

} // namespace

bool RagdollRuntime::Build(
    const Skeleton& skeleton,
    const RagdollDefinition& definition,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (skeleton.GetBoneCount() == 0u)
    {
        return SetError(errorMessage, "Ragdollを構築するSkeletonにBoneがありません");
    }
    if (definition.Bodies.empty())
    {
        return SetError(errorMessage, "RagdollDefinitionにBodyがありません");
    }

    std::vector<RagdollBodyState> bodies;
    bodies.reserve(definition.Bodies.size());

    for (const RagdollBodyDefinition& bodyDefinition : definition.Bodies)
    {
        if (bodyDefinition.BoneName.empty())
        {
            return SetError(errorMessage, "Ragdoll BodyのBoneNameは空にできません");
        }
        if (std::isfinite(bodyDefinition.Mass) == false
            || std::isfinite(bodyDefinition.Radius) == false
            || std::isfinite(bodyDefinition.HalfLength) == false
            || bodyDefinition.Mass <= 0.0f
            || bodyDefinition.Radius <= 0.0f
            || bodyDefinition.HalfLength < 0.0f)
        {
            return SetError(errorMessage, "Ragdoll BodyのMass/形状値が不正です: " + bodyDefinition.BoneName);
        }

        const BoneIndex boneIndex = skeleton.FindBone(bodyDefinition.BoneName);
        if (boneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "Ragdoll Bodyが参照するBoneがありません: " + bodyDefinition.BoneName);
        }
        if (ContainsBodyBone(bodies, boneIndex))
        {
            return SetError(errorMessage, "同じBoneへRagdoll Bodyが重複定義されています: " + bodyDefinition.BoneName);
        }

        RagdollBodyState body{};
        body.Bone = boneIndex;
        bodies.emplace_back(body);
    }

    for (const RagdollJointDefinition& joint : definition.Joints)
    {
        if (joint.ParentBoneName.empty() || joint.ChildBoneName.empty())
        {
            return SetError(errorMessage, "Ragdoll JointのBone名は空にできません");
        }
        if (std::isfinite(joint.SwingLimitRadians) == false
            || std::isfinite(joint.TwistMinRadians) == false
            || std::isfinite(joint.TwistMaxRadians) == false
            || joint.SwingLimitRadians < 0.0f
            || joint.TwistMinRadians > joint.TwistMaxRadians)
        {
            return SetError(errorMessage, "Ragdoll Jointの角度制限が不正です");
        }

        const BoneIndex parentBone = skeleton.FindBone(joint.ParentBoneName);
        const BoneIndex childBone = skeleton.FindBone(joint.ChildBoneName);
        if (parentBone == InvalidBoneIndex || childBone == InvalidBoneIndex)
        {
            return SetError(errorMessage, "Ragdoll Jointが存在しないBoneを参照しています");
        }
        if (parentBone == childBone)
        {
            return SetError(errorMessage, "Ragdoll JointのParent/Child Boneが同一です");
        }
        if (ContainsBodyBone(bodies, parentBone) == false
            || ContainsBodyBone(bodies, childBone) == false)
        {
            return SetError(errorMessage, "Ragdoll JointはBody定義済みBone同士を参照する必要があります");
        }
    }

    // 全定義の検証に成功してからRuntime Stateを差し替えます。
    // 途中失敗で既存Ragdollが半端に再構築されることを防ぎます。
    m_Skeleton = &skeleton;
    m_Definition = definition;
    m_Bodies = std::move(bodies);
    return true;
}

bool RagdollRuntime::BuildGlobalTransforms(
    const SkeletonPose& pose,
    std::vector<GlobalBoneTransform>& outGlobals,
    std::string* errorMessage) const
{
    if (m_Skeleton == nullptr)
    {
        return SetError(errorMessage, "RagdollRuntimeがBuildされていません");
    }
    if (pose.GetBoneCount() != m_Skeleton->GetBoneCount())
    {
        return SetError(errorMessage, "SkeletonPoseのBone数がRagdoll Skeletonと一致しません");
    }

    outGlobals.clear();
    outGlobals.resize(m_Skeleton->GetBoneCount());

    // ========================================================================
    // Local TRS -> Global TRS
    // ========================================================================
    // Ragdoll BodyはPosition / Quaternionを直接扱うため、Matrixを一度作って再分解するのではなく
    // Bone Local TRSを親から順に合成します。Skeleton::AddBone()はParent Index < Child Indexを
    // 保証しているため、再帰なしの1回走査でGlobal Transformを構築できます。
    for (std::size_t boneSlot = 0u; boneSlot < m_Skeleton->GetBoneCount(); ++boneSlot)
    {
        const BoneIndex boneIndex = static_cast<BoneIndex>(boneSlot);
        const Bone& bone = m_Skeleton->GetBone(boneIndex);
        const BoneTransform& local = pose.GetLocalTransform(boneIndex);

        if (IsFinite(local.Translation) == false
            || IsFinite(local.Scale) == false
            || std::isfinite(local.Rotation.LengthSq()) == false
            || local.Rotation.LengthSq() <= math::Epsilon)
        {
            return SetError(errorMessage, "SkeletonPoseに不正なBone Transformがあります");
        }

        GlobalBoneTransform global{};
        if (bone.Parent == InvalidBoneIndex)
        {
            global.Position = local.Translation;
            global.Rotation = local.Rotation.Normalized();
            global.Scale = local.Scale;
        }
        else
        {
            const GlobalBoneTransform& parent = outGlobals[bone.Parent];
            const math::Vec3 scaledLocalTranslation = MultiplyComponents(parent.Scale, local.Translation);

            global.Position = parent.Position + parent.Rotation.Rotate(scaledLocalTranslation);
            global.Rotation = (parent.Rotation * local.Rotation).Normalized();
            global.Scale = MultiplyComponents(parent.Scale, local.Scale);
        }

        outGlobals[boneIndex] = global;
    }

    return true;
}

bool RagdollRuntime::CaptureAnimationPose(
    const SkeletonPose& pose,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::vector<GlobalBoneTransform> globals;
    if (BuildGlobalTransforms(pose, globals, errorMessage) == false)
    {
        return false;
    }

    // ========================================================================
    // Animation -> Ragdoll Body
    // ========================================================================
    // Ragdoll開始FrameではPhysics Bodyを現在Animation Poseへ完全一致させます。
    // Bind Poseや原点からBodyを生成すると有効化した瞬間にCharacterが跳ぶため、この同期を
    // Physics Simulation開始前の必須ステップとして独立APIにしています。
    for (RagdollBodyState& body : m_Bodies)
    {
        if (body.Bone >= globals.size())
        {
            return SetError(errorMessage, "Ragdoll BodyのBoneIndexが範囲外です");
        }

        const GlobalBoneTransform& global = globals[body.Bone];
        body.Position = global.Position;
        body.Rotation = global.Rotation;
        body.LinearVelocity = math::Vec3{};
        body.AngularVelocity = math::Vec3{};
    }

    return true;
}

RagdollBodyState* RagdollRuntime::FindBody(BoneIndex boneIndex)
{
    const auto it = std::find_if(
        m_Bodies.begin(),
        m_Bodies.end(),
        [boneIndex](const RagdollBodyState& body)
        {
            return body.Bone == boneIndex;
        });

    if (it == m_Bodies.end())
    {
        return nullptr;
    }

    return &(*it);
}

const RagdollBodyState* RagdollRuntime::FindBody(BoneIndex boneIndex) const
{
    const auto it = std::find_if(
        m_Bodies.begin(),
        m_Bodies.end(),
        [boneIndex](const RagdollBodyState& body)
        {
            return body.Bone == boneIndex;
        });

    if (it == m_Bodies.end())
    {
        return nullptr;
    }

    return &(*it);
}

bool RagdollRuntime::SetBodyState(
    BoneIndex boneIndex,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    RagdollBodyState* body = FindBody(boneIndex);
    if (body == nullptr)
    {
        return SetError(errorMessage, "指定BoneIndexのRagdoll Bodyがありません");
    }

    const float rotationLengthSquared = state.Rotation.LengthSq();
    if (IsFinite(state.Position) == false
        || IsFinite(state.LinearVelocity) == false
        || IsFinite(state.AngularVelocity) == false
        || std::isfinite(rotationLengthSquared) == false
        || rotationLengthSquared <= math::Epsilon)
    {
        return SetError(errorMessage, "Ragdoll Body Stateに非有限値または不正Quaternionがあります");
    }

    body->Bone = boneIndex;
    body->Position = state.Position;
    body->Rotation = state.Rotation.Normalized();
    body->LinearVelocity = state.LinearVelocity;
    body->AngularVelocity = state.AngularVelocity;
    return true;
}

bool RagdollRuntime::SetBodyState(
    const std::string& boneName,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    if (m_Skeleton == nullptr)
    {
        return SetError(errorMessage, "RagdollRuntimeがBuildされていません");
    }

    const BoneIndex boneIndex = m_Skeleton->FindBone(boneName);
    if (boneIndex == InvalidBoneIndex)
    {
        return SetError(errorMessage, "指定Bone名がSkeletonにありません: " + boneName);
    }

    return SetBodyState(boneIndex, state, errorMessage);
}

bool RagdollRuntime::ApplyToSkeletonPose(
    SkeletonPose& inOutPose,
    float weight,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_Skeleton == nullptr)
    {
        return SetError(errorMessage, "RagdollRuntimeがBuildされていません");
    }
    if (inOutPose.GetBoneCount() != m_Skeleton->GetBoneCount())
    {
        return SetError(errorMessage, "SkeletonPoseのBone数がRagdoll Skeletonと一致しません");
    }
    if (std::isfinite(weight) == false)
    {
        return SetError(errorMessage, "Ragdoll Blend Weightは有限値である必要があります");
    }

    const float blendWeight = std::clamp(weight, 0.0f, 1.0f);
    if (blendWeight <= 0.0f)
    {
        return true;
    }

    std::vector<GlobalBoneTransform> outputGlobals;
    outputGlobals.resize(m_Skeleton->GetBoneCount());

    // ========================================================================
    // Ragdoll Global -> Bone Local
    // ========================================================================
    // Bone階層を親から順に処理し、Ragdoll BodyがあるBoneだけPhysics Global Transformを
    // Local Transformへ逆変換します。Bodyを持たないFinger等は元Animation Localを維持するため、
    // Physicsで動いたArmの子として自然に追従します。
    for (std::size_t boneSlot = 0u; boneSlot < m_Skeleton->GetBoneCount(); ++boneSlot)
    {
        const BoneIndex boneIndex = static_cast<BoneIndex>(boneSlot);
        const Bone& bone = m_Skeleton->GetBone(boneIndex);
        const BoneTransform animationLocal = inOutPose.GetLocalTransform(boneIndex);
        BoneTransform targetLocal = animationLocal;

        const RagdollBodyState* body = FindBody(boneIndex);
        if (body != nullptr)
        {
            if (bone.Parent == InvalidBoneIndex)
            {
                targetLocal.Translation = body->Position;
                targetLocal.Rotation = body->Rotation.Normalized();
            }
            else
            {
                const GlobalBoneTransform& parentGlobal = outputGlobals[bone.Parent];
                const math::Quat inverseParentRotation = parentGlobal.Rotation.Inversed();

                const math::Vec3 parentToBody{
                    body->Position.x - parentGlobal.Position.x,
                    body->Position.y - parentGlobal.Position.y,
                    body->Position.z - parentGlobal.Position.z
                };
                const math::Vec3 unrotatedTranslation = inverseParentRotation.Rotate(parentToBody);

                math::Vec3 localTranslation{};
                if (DivideComponents(unrotatedTranslation, parentGlobal.Scale, localTranslation) == false)
                {
                    return SetError(errorMessage, "RagdollからLocal Poseへ戻す際にParent Scaleが特異です");
                }

                targetLocal.Translation = localTranslation;
                targetLocal.Rotation = (inverseParentRotation * body->Rotation).Normalized();
            }
        }

        BoneTransform blendedLocal{};
        blendedLocal.Translation = math::Vec3::Lerp(
            animationLocal.Translation,
            targetLocal.Translation,
            blendWeight);
        blendedLocal.Rotation = math::Quat::Slerp(
            animationLocal.Rotation,
            targetLocal.Rotation,
            blendWeight);
        blendedLocal.Scale = math::Vec3::Lerp(
            animationLocal.Scale,
            targetLocal.Scale,
            blendWeight);

        if (inOutPose.SetLocalTransform(boneIndex, blendedLocal) == false)
        {
            return SetError(errorMessage, "Ragdoll Local TransformのSkeletonPose反映に失敗しました");
        }

        GlobalBoneTransform global{};
        if (bone.Parent == InvalidBoneIndex)
        {
            global.Position = blendedLocal.Translation;
            global.Rotation = blendedLocal.Rotation.Normalized();
            global.Scale = blendedLocal.Scale;
        }
        else
        {
            const GlobalBoneTransform& parent = outputGlobals[bone.Parent];
            const math::Vec3 scaledTranslation = MultiplyComponents(parent.Scale, blendedLocal.Translation);
            global.Position = parent.Position + parent.Rotation.Rotate(scaledTranslation);
            global.Rotation = (parent.Rotation * blendedLocal.Rotation).Normalized();
            global.Scale = MultiplyComponents(parent.Scale, blendedLocal.Scale);
        }

        outputGlobals[boneIndex] = global;
    }

    if (inOutPose.UpdateGlobalTransforms(*m_Skeleton) == false)
    {
        return SetError(errorMessage, "Ragdoll Pose反映後のGlobal Transform更新に失敗しました");
    }

    return true;
}

} // namespace Raven
