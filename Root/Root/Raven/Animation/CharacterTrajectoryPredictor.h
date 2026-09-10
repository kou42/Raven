#pragma once

#include "Raven/Animation/MotionDatabase.h"
#include "Raven/Character/CharacterController.h"

namespace Raven
{

// Character Controllerの入力・現在速度から、Motion Matching検索用の将来Trajectoryを予測します。
// Controller本体の状態は変更せず、同じWalk/Run/Sprint速度・加減速・TurnSpeed規約を読み取り専用で再現します。
struct CharacterTrajectoryPredictorConfig
{
    // Controller Updateより粗い1ステップで長時間を積分すると停止・旋回位置の誤差が増えるため、
    // 将来区間を小さく分割して予測します。既定は60Hz相当です。
    float SimulationStep = 1.0f / 60.0f;
};

class CharacterTrajectoryPredictor
{
public:
    static bool Predict(
        const CharacterController& controller,
        const CharacterControllerInput& input,
        const TransformComponent& characterTransform,
        const MotionTrajectoryFeatureConfig& trajectoryConfig,
        const CharacterTrajectoryPredictorConfig& predictorConfig,
        std::vector<MotionTrajectoryPoint>& outTrajectory);
};

} // namespace Raven
