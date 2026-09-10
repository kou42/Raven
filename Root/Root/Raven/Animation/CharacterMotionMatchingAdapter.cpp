#include "Raven/Animation/CharacterMotionMatchingAdapter.h"

#include <cmath>

namespace Raven
{
namespace
{

bool IsFiniteVector(const math::Vec3& value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

math::Vec3 WorldToRootYawSpace(const math::Vec3& worldVector, float yaw)
{
    // CharacterControllerのYawは+Z Forwardを基準にしているため、Worldベクトルへ-Yawを適用すると
    // MotionDatabase / MotionQueryBuilderが使用するRoot基準(+Z Forward)へ変換できます。
    // Local +ZがWorldで(sin(yaw), cos(yaw))を向くRavenのYaw規約に対する逆変換です。
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);

    return {
        cosine * worldVector.x - sine * worldVector.z,
        worldVector.y,
        sine * worldVector.x + cosine * worldVector.z
    };
}

} // namespace

bool CharacterMotionMatchingAdapter::BuildTrajectoryInput(
    const CharacterController& controller,
    const TransformComponent& characterTransform,
    MotionTrajectoryQueryInput& outInput)
{
    outInput = MotionTrajectoryQueryInput{};

    const math::Vec3 worldVelocity = controller.GetVelocity();
    if (IsFiniteVector(worldVelocity) == false ||
        std::isfinite(characterTransform.Rotation.y) == false)
    {
        return false;
    }

    // Locomotion検索ではJump/Gravityの鉛直速度をTrajectoryへ混ぜません。
    // 現在のMotionDatabaseは地上移動のRoot XZ trajectoryを対象としているため、
    // Character Controllerの水平実速度だけをRoot基準へ変換します。
    const math::Vec3 horizontalWorldVelocity{
        worldVelocity.x,
        0.0f,
        worldVelocity.z
    };

    outInput.DesiredVelocity = WorldToRootYawSpace(
        horizontalWorldVelocity,
        characterTransform.Rotation.y);

    const float horizontalSpeedSquared = outInput.DesiredVelocity.x * outInput.DesiredVelocity.x +
        outInput.DesiredVelocity.z * outInput.DesiredVelocity.z;

    if (horizontalSpeedSquared > 1.0e-10f)
    {
        const float inverseLength = 1.0f / std::sqrt(horizontalSpeedSquared);
        outInput.DesiredDirection = {
            outInput.DesiredVelocity.x * inverseLength,
            0.0f,
            outInput.DesiredVelocity.z * inverseLength
        };
    }
    else
    {
        // 停止時は進行方向が定義できないため、Root自身のForward(+Z)を維持します。
        // 0ベクトルを渡してQueryBuilder側のFallbackへ依存するより契約を明示します。
        outInput.DesiredDirection = { 0.0f, 0.0f, 1.0f };
    }

    return true;
}

bool CharacterMotionMatchingAdapter::BuildQuery(
    const CharacterController& controller,
    const TransformComponent& characterTransform,
    const Skeleton& skeleton,
    const SkeletonPose& currentPose,
    const SkeletonPose& previousPose,
    float deltaTime,
    const MotionPoseFeatureConfig& poseConfig,
    const MotionTrajectoryFeatureConfig& trajectoryConfig,
    MotionSearchQuery& outQuery)
{
    MotionTrajectoryQueryInput trajectoryInput{};
    if (BuildTrajectoryInput(controller, characterTransform, trajectoryInput) == false)
    {
        return false;
    }

    return MotionQueryBuilder::Build(
        skeleton,
        currentPose,
        previousPose,
        deltaTime,
        poseConfig,
        trajectoryConfig,
        trajectoryInput,
        outQuery);
}

} // namespace Raven
