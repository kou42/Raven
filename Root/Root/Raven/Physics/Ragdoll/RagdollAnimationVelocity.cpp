// Raven/Physics/Ragdoll/RagdollAnimationVelocity.cpp
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Raven
{
namespace
{

bool SetAnimationVelocityError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

const RagdollBodyState* FindPreviousAnimationBody(
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

    if (it == bodies.end())
    {
        return nullptr;
    }

    return &(*it);
}

math::Vec3 ComputeAnimationAngularVelocity(
    const math::Quat& previousRotation,
    const math::Quat& currentRotation,
    float deltaTime)
{
    if (deltaTime <= math::Epsilon)
    {
        return math::Vec3{};
    }

    // ========================================================================
    // Quaternion差分 -> World Angular Velocity
    // ========================================================================
    // Body RotationはGlobal/World姿勢なので、
    //
    //     qDelta = qCurrent * inverse(qPrevious)
    //
    // の順序で差分を取ると、回転軸もworld-spaceになります。
    // PhysicsWorld::RigidBodyComponent::AngularVelocityもworld-space契約なので、追加変換なしで
    // そのままRagdoll開始速度として渡せます。
    math::Quat delta = (
        currentRotation.Normalized()
        * previousRotation.Normalized().Conjugate()).Normalized();

    // Quaternion q と -q は同じ姿勢を表します。
    // Animation補間の都合で符号だけ反転したFrameをそのままAxis-Angle化すると、ほぼ0度の動きが
    // 約2PI回転として解釈され巨大なAngularVelocityになります。w>=0側へ揃え、常に最短回転を使います。
    if (delta.w < 0.0f)
    {
        delta.x = -delta.x;
        delta.y = -delta.y;
        delta.z = -delta.z;
        delta.w = -delta.w;
    }

    const float clampedW = std::clamp(delta.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(clampedW);
    const float sinHalfSquared = std::max(1.0f - clampedW * clampedW, 0.0f);

    if (sinHalfSquared <= 1.0e-12f)
    {
        // 微小角では axis = xyz / sin(angle/2) が0/0へ近づき数値的に不安定になります。
        // q.xyz ~= axis * angle/2 の一次近似を使えば、omega ~= 2*q.xyz/dt と安全に求められます。
        return math::Vec3{ delta.x, delta.y, delta.z } * (2.0f / deltaTime);
    }

    const float inverseSinHalf = 1.0f / std::sqrt(sinHalfSquared);
    const math::Vec3 axis{
        delta.x * inverseSinHalf,
        delta.y * inverseSinHalf,
        delta.z * inverseSinHalf
    };
    return axis * (angle / deltaTime);
}

} // namespace

void RagdollRuntime::ResetAnimationVelocityHistory()
{
    m_PreviousAnimationBodies.clear();
    m_HasPreviousAnimationPose = false;
    m_AnimationHistorySkeleton = nullptr;
}

bool RagdollRuntime::SampleAnimationPose(
    const SkeletonPose& pose,
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetAnimationVelocityError(
            errorMessage,
            "Animation Velocity SamplingのdeltaTimeは0以上の有限値である必要があります");
    }

    std::vector<GlobalBoneTransform> globals;
    if (BuildGlobalTransforms(pose, globals, errorMessage) == false)
    {
        return false;
    }

    // Build()を同じRuntimeへ再実行した場合は旧Skeletonの履歴を絶対に流用しません。
    // Bone数やBoneIndexが偶然一致していても、別Asset間の位置差を速度として解釈してしまうためです。
    if (m_AnimationHistorySkeleton != nullptr
        && m_AnimationHistorySkeleton != m_Skeleton)
    {
        ResetAnimationVelocityHistory();
    }

    std::vector<RagdollBodyState> sampledBodies = m_Bodies;
    const bool canCalculateVelocity =
        m_HasPreviousAnimationPose
        && deltaTime > math::Epsilon;

    // ========================================================================
    // Animation Pose -> Ragdoll Body Pose + Velocity
    // ========================================================================
    // Bone Local Transform差分ではなく、階層合成後のGlobal Body Position / Rotation差分を使います。
    // これにより、例えばUpperArm自身のTranslationが変わらなくてもShoulder/Rootが動けば、そのBodyには
    // 正しいworld LinearVelocityが入り、走行中や回転中のCharacterをRagdoll化しても勢いが失われません。
    for (RagdollBodyState& body : sampledBodies)
    {
        if (body.Bone >= globals.size())
        {
            return SetAnimationVelocityError(
                errorMessage,
                "Animation Velocity Sampling中にRagdoll BodyのBoneIndexが範囲外になりました");
        }

        const GlobalBoneTransform& current = globals[body.Bone];
        body.Position = current.Position;
        body.Rotation = current.Rotation.Normalized();
        body.LinearVelocity = math::Vec3{};
        body.AngularVelocity = math::Vec3{};

        if (canCalculateVelocity)
        {
            const RagdollBodyState* previous = FindPreviousAnimationBody(
                m_PreviousAnimationBodies,
                body.Bone);
            if (previous == nullptr)
            {
                return SetAnimationVelocityError(
                    errorMessage,
                    "Animation Velocity履歴に対応するRagdoll Bodyがありません");
            }

            body.LinearVelocity = (body.Position - previous->Position) / deltaTime;
            body.AngularVelocity = ComputeAnimationAngularVelocity(
                previous->Rotation,
                body.Rotation,
                deltaTime);
        }
    }

    // 全Bodyの計算に成功してから現在Stateと履歴をまとめてcommitします。
    // 途中のBoneだけ更新された半端なRagdoll Stateを残さないためです。
    m_Bodies = sampledBodies;
    m_PreviousAnimationBodies = sampledBodies;
    m_HasPreviousAnimationPose = true;
    m_AnimationHistorySkeleton = m_Skeleton;
    return true;
}

} // namespace Raven
