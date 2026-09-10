#pragma once

#include "Raven/Animation/MotionQueryBuilder.h"
#include "Raven/Character/CharacterController.h"

namespace Raven
{

// ============================================================================
// CharacterMotionMatchingAdapter
// ============================================================================
// CharacterControllerが保持するWorld速度とCharacter TransformのYawを、
// MotionQueryBuilderが要求するRoot基準のTrajectory入力へ変換する薄いAdapterです。
//
// Motion Matching本体をCharacterControllerへ直接依存させないことで、既存Animator / BlendTree経路を
// 維持したまま、Player CharacterだけMotionMatcherを選択できる構成にします。
class CharacterMotionMatchingAdapter
{
public:
    // Controllerの実速度をTrajectoryの希望速度として使用します。
    // CharacterControllerは衝突・加減速後のm_Velocityを保持するため、壁SlideやDashを含む
    // 実際の移動結果に近いQueryを作れます。
    static bool BuildTrajectoryInput(
        const CharacterController& controller,
        const TransformComponent& characterTransform,
        MotionTrajectoryQueryInput& outInput);

    // Pose Query生成まで含めたCharacter向けConvenience関数です。
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
};

} // namespace Raven
