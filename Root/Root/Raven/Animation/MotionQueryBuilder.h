#pragma once

#include "Raven/Animation/MotionDatabase.h"

namespace Raven
{

// ============================================================================
// MotionTrajectoryQueryInput
// ============================================================================
// Runtime側が予測した移動要求をRoot基準座標で受け取ります。
// CharacterControllerへ直接依存しないため、Player / AI / Networkなど入力源が変わっても
// Motion Matching本体を変更せず利用できます。
struct MotionTrajectoryQueryInput
{
    // Root基準の希望移動速度です。
    math::Vec3 DesiredVelocity{ 0.0f, 0.0f, 0.0f };

    // Root基準の希望進行方向です。0ベクトルの場合は+Z Forwardを使用します。
    math::Vec3 DesiredDirection{ 0.0f, 0.0f, 1.0f };
};

// ============================================================================
// MotionQueryBuilder
// ============================================================================
// 現在Runtime Poseと移動要求をMotionDatabaseと同じFeature Layoutへ変換します。
// Database構築とQuery生成で座標系・Bone順・Trajectory時刻を揃えることが重要なため、
// Feature Configを共通入力として使用します。
class MotionQueryBuilder
{
public:
    static bool Build(
        const Skeleton& skeleton,
        const SkeletonPose& currentPose,
        const SkeletonPose& previousPose,
        float deltaTime,
        const MotionPoseFeatureConfig& poseConfig,
        const MotionTrajectoryFeatureConfig& trajectoryConfig,
        const MotionTrajectoryQueryInput& trajectoryInput,
        MotionSearchQuery& outQuery);
};

} // namespace Raven
