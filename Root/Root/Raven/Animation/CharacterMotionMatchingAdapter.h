#pragma once

#include "Raven/Animation/CharacterTrajectoryPredictor.h"
#include "Raven/Animation/MotionQueryBuilder.h"
#include "Raven/Character/CharacterController.h"

namespace Raven
{

// ============================================================================
// CharacterMotionMatchingAdapter
// ============================================================================
// Character固有の入力・World速度・YawをMotion MatchingのRoot基準Queryへ接続する薄いAdapterです。
// Motion Matching本体をCharacterControllerへ直接依存させないため、既存Animator / BlendTree経路を
// 維持したままPlayer / AIなど必要なCharacterだけMotionMatcherを選択できます。
class CharacterMotionMatchingAdapter
{
public:
    // 従来互換経路。Controllerの現在実速度を一定速度Trajectoryとして使用します。
    static bool BuildTrajectoryInput(
        const CharacterController& controller,
        const TransformComponent& characterTransform,
        MotionTrajectoryQueryInput& outInput);

    // 従来互換の一定速度Queryです。検索比較やPredictor無効時のFallbackに使用できます。
    static bool BuildQuery(
        const CharacterController& controller,
        const TransformComponent& characterTransform,
        const Skeleton& skeleton,
        const SkeletonPose& currentPose,
        const SkeletonPose& previousPose,
        float deltaTime,
        const MotionPoseFeatureConfig& poseConfig,
        const MotionTrajectoryFeatureConfig& trajectoryConfig,
        MotionSearchQuery& outQuery);

    // Character入力を先読みし、Controllerと同じ加減速・TurnSpeed規約で将来Trajectoryを予測します。
    // Pose Feature生成は既存MotionQueryBuilderを再利用し、Trajectory部分だけ予測結果へ差し替えます。
    static bool BuildPredictedQuery(
        const CharacterController& controller,
        const CharacterControllerInput& input,
        const TransformComponent& characterTransform,
        const Skeleton& skeleton,
        const SkeletonPose& currentPose,
        const SkeletonPose& previousPose,
        float deltaTime,
        const MotionPoseFeatureConfig& poseConfig,
        const MotionTrajectoryFeatureConfig& trajectoryConfig,
        const CharacterTrajectoryPredictorConfig& predictorConfig,
        MotionSearchQuery& outQuery);
};

} // namespace Raven
