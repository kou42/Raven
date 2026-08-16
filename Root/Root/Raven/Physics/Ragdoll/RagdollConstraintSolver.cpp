// Raven/Physics/Ragdoll/RagdollConstraintSolver.cpp
#include "Raven/Physics/Ragdoll/RagdollConstraintSolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Raven
{
namespace
{

constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = Pi * 2.0f;

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

math::Quat Negated(const math::Quat& value)
{
    return math::Quat{ -value.x, -value.y, -value.z, -value.w };
}

math::Quat ShortestArcQuaternion(const math::Quat& value)
{
    const math::Quat normalized = value.Normalized();
    if (normalized.w < 0.0f)
    {
        return Negated(normalized);
    }

    return normalized;
}

float NormalizeAngle(float angle)
{
    while (angle > Pi)
    {
        angle -= TwoPi;
    }
    while (angle < -Pi)
    {
        angle += TwoPi;
    }

    return angle;
}

float FindBodyInverseMass(
    const RagdollDefinition& definition,
    const std::string& boneName)
{
    for (const RagdollBodyDefinition& body : definition.Bodies)
    {
        if (body.BoneName == boneName)
        {
            if (body.Mass > 0.0f && std::isfinite(body.Mass))
            {
                return 1.0f / body.Mass;
            }

            return 0.0f;
        }
    }

    return 0.0f;
}

bool DecomposeSwingTwist(
    const math::Quat& rotation,
    const math::Vec3& twistAxis,
    math::Quat& outSwing,
    math::Quat& outTwist,
    float& outTwistAngle)
{
    const float axisLengthSquared = twistAxis.LengthSq();
    if (axisLengthSquared <= 1.0e-12f || std::isfinite(axisLengthSquared) == false)
    {
        return false;
    }

    const math::Vec3 axis = twistAxis / std::sqrt(axisLengthSquared);
    const math::Quat normalizedRotation = ShortestArcQuaternion(rotation);

    const math::Vec3 vectorPart{
        normalizedRotation.x,
        normalizedRotation.y,
        normalizedRotation.z
    };
    const float projectionLength = math::Vec3::Dot(vectorPart, axis);
    const math::Vec3 projected = axis * projectionLength;

    math::Quat twist{
        projected.x,
        projected.y,
        projected.z,
        normalizedRotation.w
    };

    if (twist.LengthSq() <= 1.0e-12f)
    {
        twist = math::Quat::Identity();
    }
    else
    {
        twist = ShortestArcQuaternion(twist);
    }

    outTwist = twist;
    outSwing = ShortestArcQuaternion(normalizedRotation * twist.Inversed());

    const math::Vec3 twistVector{ outTwist.x, outTwist.y, outTwist.z };
    const float signedSinHalf = math::Vec3::Dot(twistVector, axis);
    outTwistAngle = NormalizeAngle(2.0f * std::atan2(signedSinHalf, outTwist.w));
    return true;
}

math::Quat ClampSwing(
    const math::Quat& swing,
    float swingLimit)
{
    const math::Quat normalizedSwing = ShortestArcQuaternion(swing);
    const float clampedW = std::clamp(normalizedSwing.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(clampedW);

    if (angle <= swingLimit || angle <= 1.0e-6f)
    {
        return normalizedSwing;
    }

    const float t = std::clamp(swingLimit / angle, 0.0f, 1.0f);
    return math::Quat::Slerp(math::Quat::Identity(), normalizedSwing, t).Normalized();
}

} // namespace

bool RagdollConstraintSolver::Initialize(
    RagdollRuntime& ragdoll,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Ragdoll Constraint初期化にはBuild済みRagdollRuntimeが必要です");
    }

    const RagdollDefinition& definition = ragdoll.GetDefinition();
    if (definition.Joints.empty())
    {
        return SetError(errorMessage, "RagdollDefinitionにJointがありません");
    }

    std::vector<RagdollJointConstraint> constraints;
    constraints.reserve(definition.Joints.size());

    for (const RagdollJointDefinition& joint : definition.Joints)
    {
        RagdollBodyState* parent = ragdoll.FindBody(joint.ParentBoneName);
        RagdollBodyState* child = ragdoll.FindBody(joint.ChildBoneName);
        if (parent == nullptr || child == nullptr)
        {
            return SetError(
                errorMessage,
                "Ragdoll JointがRuntime Bodyへ解決できません: "
                    + joint.ParentBoneName + " -> " + joint.ChildBoneName);
        }

        const float axisLengthSquared = joint.TwistAxisLocal.LengthSq();
        if (IsFinite(joint.TwistAxisLocal) == false
            || std::isfinite(axisLengthSquared) == false
            || axisLengthSquared <= 1.0e-12f)
        {
            return SetError(errorMessage, "Ragdoll JointのTwistAxisLocalが不正です");
        }

        RagdollJointConstraint constraint{};
        constraint.ParentBone = parent->Bone;
        constraint.ChildBone = child->Bone;
        constraint.ParentInverseMass = FindBodyInverseMass(definition, joint.ParentBoneName);
        constraint.ChildInverseMass = FindBodyInverseMass(definition, joint.ChildBoneName);

        if (constraint.ParentInverseMass + constraint.ChildInverseMass <= 1.0e-12f)
        {
            return SetError(errorMessage, "Ragdoll Jointの両Bodyが移動不能です");
        }

        // ====================================================================
        // Ball / Socket Anchor
        // ====================================================================
        // RagdollBodyState::PositionはBone原点です。
        // Child Bone原点をJoint位置とみなし、開始PoseでのParentローカル位置を保存します。
        const math::Quat inverseParentRotation = parent->Rotation.Normalized().Inversed();
        constraint.ParentLocalAnchor = inverseParentRotation.Rotate(
            child->Position - parent->Position);
        constraint.ChildLocalAnchor = math::Vec3{};

        // 開始Poseの相対回転を角度制限の中心とします。
        constraint.ReferenceRelativeRotation =
            (inverseParentRotation * child->Rotation.Normalized()).Normalized();
        constraint.TwistAxisLocal = joint.TwistAxisLocal.Normalized();
        constraint.SwingLimitRadians = joint.SwingLimitRadians;
        constraint.TwistMinRadians = joint.TwistMinRadians;
        constraint.TwistMaxRadians = joint.TwistMaxRadians;

        constraints.emplace_back(constraint);
    }

    m_Constraints = std::move(constraints);
    return true;
}

bool RagdollConstraintSolver::Solve(
    RagdollRuntime& ragdoll,
    const RagdollConstraintSolverSettings& settings,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Ragdoll Constraint SolveにはBuild済みRagdollRuntimeが必要です");
    }
    if (m_Constraints.empty())
    {
        return SetError(errorMessage, "Ragdoll Constraint SolverがInitializeされていません");
    }
    if (std::isfinite(settings.PositionStiffness) == false
        || std::isfinite(settings.AngularStiffness) == false)
    {
        return SetError(errorMessage, "Ragdoll Constraint Solver設定に非有限値があります");
    }

    const float positionStiffness = std::clamp(settings.PositionStiffness, 0.0f, 1.0f);
    const float angularStiffness = std::clamp(settings.AngularStiffness, 0.0f, 1.0f);

    // Contact Solverと同じく複数回反復して各Joint誤差を段階的に減らします。
    // 人体はJointが鎖状につながるため、1回だけの補正では隣接Jointの修正が再び誤差を作ります。
    for (std::size_t iteration = 0u; iteration < settings.Iterations; ++iteration)
    {
        for (const RagdollJointConstraint& constraint : m_Constraints)
        {
            if (SolvePositionConstraint(
                    ragdoll,
                    constraint,
                    positionStiffness,
                    errorMessage) == false)
            {
                return false;
            }

            if (SolveAngularConstraint(
                    ragdoll,
                    constraint,
                    angularStiffness,
                    errorMessage) == false)
            {
                return false;
            }
        }
    }

    return true;
}

bool RagdollConstraintSolver::SolvePositionConstraint(
    RagdollRuntime& ragdoll,
    const RagdollJointConstraint& constraint,
    float stiffness,
    std::string* errorMessage) const
{
    RagdollBodyState* parent = ragdoll.FindBody(constraint.ParentBone);
    RagdollBodyState* child = ragdoll.FindBody(constraint.ChildBone);
    if (parent == nullptr || child == nullptr)
    {
        return SetError(errorMessage, "Ragdoll Position ConstraintのBodyが見つかりません");
    }

    const math::Vec3 parentAnchor = parent->Position
        + parent->Rotation.Normalized().Rotate(constraint.ParentLocalAnchor);
    const math::Vec3 childAnchor = child->Position
        + child->Rotation.Normalized().Rotate(constraint.ChildLocalAnchor);

    const math::Vec3 error = childAnchor - parentAnchor;
    if (error.LengthSq() <= 1.0e-12f || stiffness <= 0.0f)
    {
        return true;
    }

    const float inverseMassSum = constraint.ParentInverseMass + constraint.ChildInverseMass;
    if (inverseMassSum <= 1.0e-12f)
    {
        return SetError(errorMessage, "Ragdoll Position ConstraintのInverseMass合計が0です");
    }

    // Parent / Childの質量比で同じAnchor誤差を分担します。
    // 重い胴体より軽い腕・脚の方が大きく移動するため、Ragdoll全体の重心が暴れにくくなります。
    const math::Vec3 correction = error * stiffness;
    parent->Position += correction * (constraint.ParentInverseMass / inverseMassSum);
    child->Position -= correction * (constraint.ChildInverseMass / inverseMassSum);
    return true;
}

bool RagdollConstraintSolver::SolveAngularConstraint(
    RagdollRuntime& ragdoll,
    const RagdollJointConstraint& constraint,
    float stiffness,
    std::string* errorMessage) const
{
    if (stiffness <= 0.0f)
    {
        return true;
    }

    RagdollBodyState* parent = ragdoll.FindBody(constraint.ParentBone);
    RagdollBodyState* child = ragdoll.FindBody(constraint.ChildBone);
    if (parent == nullptr || child == nullptr)
    {
        return SetError(errorMessage, "Ragdoll Angular ConstraintのBodyが見つかりません");
    }

    const math::Quat parentRotation = parent->Rotation.Normalized();
    const math::Quat childRotation = child->Rotation.Normalized();
    const math::Quat currentRelative =
        (parentRotation.Inversed() * childRotation).Normalized();

    // Reference Relativeからの差分だけを制限します。
    // これによりT-Poseなど絶対姿勢を0度と仮定せず、実際のAnimation開始姿勢をJoint中心にできます。
    const math::Quat deltaRelative =
        (constraint.ReferenceRelativeRotation.Inversed() * currentRelative).Normalized();

    math::Quat swing{};
    math::Quat twist{};
    float twistAngle = 0.0f;
    if (DecomposeSwingTwist(
            deltaRelative,
            constraint.TwistAxisLocal,
            swing,
            twist,
            twistAngle) == false)
    {
        return SetError(errorMessage, "Ragdoll Swing/Twist分解に失敗しました");
    }

    const math::Quat clampedSwing = ClampSwing(
        swing,
        std::max(constraint.SwingLimitRadians, 0.0f));
    const float clampedTwistAngle = std::clamp(
        twistAngle,
        constraint.TwistMinRadians,
        constraint.TwistMaxRadians);
    const math::Quat clampedTwist = math::Quat::FromAxisAngle(
        constraint.TwistAxisLocal,
        clampedTwistAngle);

    const math::Quat targetDelta = (clampedSwing * clampedTwist).Normalized();
    const math::Quat targetRelative =
        (constraint.ReferenceRelativeRotation * targetDelta).Normalized();
    const math::Quat targetChildRotation =
        (parentRotation * targetRelative).Normalized();

    // 現段階ではRagdollBodyStateに慣性Tensorが無いため、角度補正はChild側へ適用します。
    // PhysicsWorld統合時にはContact Solverと同様にWorld Inertiaを使い、Parent/Childへ角Impulseを
    // 分配します。ここではConstraint意味論とSwing/Twist制限を先に確立します。
    child->Rotation = math::Quat::Slerp(
        childRotation,
        targetChildRotation,
        stiffness).Normalized();

    return true;
}

} // namespace Raven
