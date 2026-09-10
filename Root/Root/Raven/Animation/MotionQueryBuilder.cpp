#include "Raven/Animation/MotionQueryBuilder.h"

#include <cmath>

namespace Raven
{
namespace
{

math::Vec3 ExtractTranslation(const math::Mat4& matrix)
{
    return {
        matrix.m[0][3],
        matrix.m[1][3],
        matrix.m[2][3]
    };
}

math::Vec3 TransformPoint(const math::Mat4& matrix, const math::Vec3& point)
{
    const math::Vec4 transformed = matrix * math::Vec4(point, 1.0f);
    return { transformed.x, transformed.y, transformed.z };
}

math::Vec3 TransformVector(const math::Mat4& matrix, const math::Vec3& vector)
{
    const math::Vec4 transformed = matrix * math::Vec4(vector, 0.0f);
    return { transformed.x, transformed.y, transformed.z };
}

bool ValidateRootBone(const Skeleton& skeleton, BoneIndex rootBone)
{
    if (skeleton.IsValidBoneIndex(rootBone) == false)
    {
        return false;
    }

    return skeleton.GetBone(rootBone).Parent == InvalidBoneIndex;
}

bool ValidateTrajectoryOffsets(const std::vector<float>& offsets)
{
    if (offsets.empty() == true)
    {
        return false;
    }

    float previousOffset = 0.0f;
    for (float offset : offsets)
    {
        if (offset <= 0.0f || std::isfinite(offset) == false)
        {
            return false;
        }

        if (offset <= previousOffset)
        {
            return false;
        }

        previousOffset = offset;
    }

    return true;
}

} // namespace

bool MotionQueryBuilder::Build(
    const Skeleton& skeleton,
    const SkeletonPose& currentPose,
    const SkeletonPose& previousPose,
    float deltaTime,
    const MotionPoseFeatureConfig& poseConfig,
    const MotionTrajectoryFeatureConfig& trajectoryConfig,
    const MotionTrajectoryQueryInput& trajectoryInput,
    MotionSearchQuery& outQuery)
{
    outQuery = MotionSearchQuery{};

    if (deltaTime <= 0.0f || std::isfinite(deltaTime) == false)
    {
        return false;
    }

    if (currentPose.GetBoneCount() != skeleton.GetBoneCount() ||
        previousPose.GetBoneCount() != skeleton.GetBoneCount())
    {
        return false;
    }

    if (poseConfig.RootBone != trajectoryConfig.RootBone ||
        ValidateRootBone(skeleton, poseConfig.RootBone) == false ||
        poseConfig.PoseBones.empty() == true ||
        ValidateTrajectoryOffsets(trajectoryConfig.FutureTimeOffsets) == false)
    {
        return false;
    }

    for (BoneIndex boneIndex : poseConfig.PoseBones)
    {
        if (skeleton.IsValidBoneIndex(boneIndex) == false)
        {
            return false;
        }
    }

    const BoneTransform& currentRoot = currentPose.GetLocalTransform(poseConfig.RootBone);
    const math::Mat4 currentRootInverse = currentRoot.ToInverseMatrix();

    outQuery.PoseFeatures.reserve(poseConfig.PoseBones.size());

    for (BoneIndex boneIndex : poseConfig.PoseBones)
    {
        const math::Vec3 currentPosition =
            ExtractTranslation(currentPose.GetGlobalTransform(boneIndex));
        const math::Vec3 previousPosition =
            ExtractTranslation(previousPose.GetGlobalTransform(boneIndex));

        const math::Vec3 skeletonVelocity =
            (currentPosition - previousPosition) / deltaTime;

        MotionPoseFeature feature{};
        feature.Bone = boneIndex;
        feature.Position = TransformPoint(currentRootInverse, currentPosition);
        feature.Velocity = TransformVector(currentRootInverse, skeletonVelocity);
        outQuery.PoseFeatures.emplace_back(feature);
    }

    math::Vec3 desiredDirection = trajectoryInput.DesiredDirection;
    if (desiredDirection.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        // 入力が静止状態などで向きを与えられない場合は、Databaseと同じ+Z Forwardを使います。
        // Zero VectorのままCost計算すると全方向に一定のPenaltyが生じるため、明示的な既定値を採用します。
        desiredDirection = math::Vec3{ 0.0f, 0.0f, 1.0f };
    }
    else
    {
        desiredDirection.Normalize();
    }

    outQuery.Trajectory.reserve(trajectoryConfig.FutureTimeOffsets.size());

    for (float timeOffset : trajectoryConfig.FutureTimeOffsets)
    {
        MotionTrajectoryPoint point{};
        point.TimeOffset = timeOffset;

        // 現段階では一定速度モデルで未来位置を予測します。
        // Character Controllerとの接続後は加減速や旋回を考慮したPredictorへ差し替えられるよう、
        // Query BuilderはRoot基準の予測値を受け取る単純な責務に留めています。
        point.Position = trajectoryInput.DesiredVelocity * timeOffset;
        point.Direction = desiredDirection;

        outQuery.Trajectory.emplace_back(point);
    }

    return true;
}

} // namespace Raven
