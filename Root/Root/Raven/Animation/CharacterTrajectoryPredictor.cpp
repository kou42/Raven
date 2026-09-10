#include "Raven/Animation/CharacterTrajectoryPredictor.h"

#include <algorithm>
#include <cmath>

namespace Raven
{
namespace
{

float NormalizeAngle(float angle)
{
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = Pi * 2.0f;

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

float MoveTowards(float current, float target, float maxDelta)
{
    if (current < target)
    {
        return std::min(current + maxDelta, target);
    }
    if (current > target)
    {
        return std::max(current - maxDelta, target);
    }
    return target;
}

math::Vec3 WorldToRootYawSpace(const math::Vec3& worldVector, float rootYaw)
{
    const float cosine = std::cos(rootYaw);
    const float sine = std::sin(rootYaw);
    return {
        cosine * worldVector.x - sine * worldVector.z,
        worldVector.y,
        sine * worldVector.x + cosine * worldVector.z
    };
}

bool ValidateOffsets(const std::vector<float>& offsets)
{
    if (offsets.empty() == true)
    {
        return false;
    }

    float previous = 0.0f;
    for (float offset : offsets)
    {
        if (offset <= previous || std::isfinite(offset) == false)
        {
            return false;
        }
        previous = offset;
    }
    return true;
}

} // namespace

bool CharacterTrajectoryPredictor::Predict(
    const CharacterController& controller,
    const CharacterControllerInput& input,
    const TransformComponent& characterTransform,
    const MotionTrajectoryFeatureConfig& trajectoryConfig,
    const CharacterTrajectoryPredictorConfig& predictorConfig,
    std::vector<MotionTrajectoryPoint>& outTrajectory)
{
    outTrajectory.clear();

    if (ValidateOffsets(trajectoryConfig.FutureTimeOffsets) == false ||
        predictorConfig.SimulationStep <= 0.0f ||
        std::isfinite(predictorConfig.SimulationStep) == false ||
        std::isfinite(characterTransform.Rotation.y) == false)
    {
        return false;
    }

    const CharacterControllerConfig& controllerConfig = controller.GetConfig();
    if (controllerConfig.Acceleration < 0.0f ||
        controllerConfig.Deceleration < 0.0f ||
        std::isfinite(controllerConfig.Acceleration) == false ||
        std::isfinite(controllerConfig.Deceleration) == false ||
        std::isfinite(controllerConfig.TurnSpeed) == false)
    {
        return false;
    }

    math::Vec2 moveInput = input.Move;
    const float moveLengthSquared = moveInput.x * moveInput.x + moveInput.y * moveInput.y;
    if (moveLengthSquared > 1.0f)
    {
        const float inverseLength = 1.0f / std::sqrt(moveLengthSquared);
        moveInput.x *= inverseLength;
        moveInput.y *= inverseLength;
    }
    const bool hasMoveInput = (moveInput.x * moveInput.x + moveInput.y * moveInput.y) > 1.0e-6f;

    float targetSpeed = controllerConfig.WalkSpeed;
    if (input.Sprint == true)
    {
        targetSpeed = controllerConfig.SprintSpeed;
    }
    else if (input.Run == true)
    {
        targetSpeed = controllerConfig.RunSpeed;
    }

    math::Vec3 targetVelocity{ 0.0f, 0.0f, 0.0f };
    bool useImmediateVelocity = false;

    if (input.HasHorizontalVelocityOverride == true)
    {
        targetVelocity.x = input.HorizontalVelocityOverride.x;
        targetVelocity.z = input.HorizontalVelocityOverride.y;
        useImmediateVelocity = true;
    }
    else if (controller.IsDashing() == true)
    {
        // Dash方向はCharacterDashAction側で開始時に固定されているため、Inputから再構築せず
        // Controllerが現在保持している実速度を予測区間でも維持します。
        const math::Vec3 currentVelocity = controller.GetVelocity();
        targetVelocity.x = currentVelocity.x;
        targetVelocity.z = currentVelocity.z;
        useImmediateVelocity = true;
    }
    else if (hasMoveInput == true)
    {
        targetVelocity.x = moveInput.x * targetSpeed;
        targetVelocity.z = moveInput.y * targetSpeed;
    }

    const math::Vec3 controllerVelocity = controller.GetVelocity();
    if (std::isfinite(controllerVelocity.x) == false ||
        std::isfinite(controllerVelocity.z) == false)
    {
        return false;
    }

    math::Vec3 predictedVelocity{ controllerVelocity.x, 0.0f, controllerVelocity.z };
    math::Vec3 predictedWorldPosition{ 0.0f, 0.0f, 0.0f };
    float predictedYaw = characterTransform.Rotation.y;
    float elapsed = 0.0f;

    outTrajectory.reserve(trajectoryConfig.FutureTimeOffsets.size());

    for (float timeOffset : trajectoryConfig.FutureTimeOffsets)
    {
        while (elapsed < timeOffset)
        {
            const float step = std::min(predictorConfig.SimulationStep, timeOffset - elapsed);

            if (useImmediateVelocity == true)
            {
                predictedVelocity = targetVelocity;
            }
            else
            {
                const float rate = hasMoveInput ? controllerConfig.Acceleration : controllerConfig.Deceleration;
                const float maxDelta = rate * step;
                predictedVelocity.x = MoveTowards(predictedVelocity.x, targetVelocity.x, maxDelta);
                predictedVelocity.z = MoveTowards(predictedVelocity.z, targetVelocity.z, maxDelta);
            }

            predictedWorldPosition += predictedVelocity * step;

            const float speedSquared = predictedVelocity.x * predictedVelocity.x +
                predictedVelocity.z * predictedVelocity.z;
            if (speedSquared > 1.0e-6f)
            {
                const float targetYaw = std::atan2(predictedVelocity.x, predictedVelocity.z);
                const float deltaYaw = NormalizeAngle(targetYaw - predictedYaw);

                if (controllerConfig.TurnSpeed <= 0.0f)
                {
                    predictedYaw = targetYaw;
                }
                else
                {
                    const float maxYawDelta = controllerConfig.TurnSpeed * step;
                    predictedYaw = NormalizeAngle(
                        predictedYaw + std::clamp(deltaYaw, -maxYawDelta, maxYawDelta));
                }
            }

            elapsed += step;
        }

        MotionTrajectoryPoint point{};
        point.TimeOffset = timeOffset;
        point.Position = WorldToRootYawSpace(predictedWorldPosition, characterTransform.Rotation.y);

        const math::Vec3 predictedWorldForward{
            std::sin(predictedYaw),
            0.0f,
            std::cos(predictedYaw)
        };
        point.Direction = WorldToRootYawSpace(
            predictedWorldForward,
            characterTransform.Rotation.y).Normalized();

        outTrajectory.emplace_back(point);
    }

    return true;
}

} // namespace Raven
