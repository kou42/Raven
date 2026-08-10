// Raven/Animation/AnimatorStateMachineLocomotion.cpp
#include "Raven/Animation/AnimatorStateMachine.h"

#include <cmath>
#include <utility>

namespace Raven
{

bool AnimatorStateMachine::AddLocomotionTransitions(
    const std::string& speedParameterName,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& stateNames,
    float crossFadeDuration)
{
    if (!std::isfinite(thresholds.IdleMaxSpeed) ||
        !std::isfinite(thresholds.RunMinSpeed) ||
        thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed ||
        !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f)
    {
        return false;
    }

    // Speed ParameterはFloatとして登録済みである必要があります。
    // 別型Parameterを暗黙変換せず、Controller定義の誤りを構築時に検出します。
    float unusedSpeed = 0.0f;
    if (!GetFloat(speedParameterName, unusedSpeed) ||
        !HasState(stateNames.Idle) || !HasState(stateNames.Walk) || !HasState(stateNames.Run))
    {
        return false;
    }

    auto makeSpeedTransition = [&speedParameterName, crossFadeDuration](
        const std::string& from, const std::string& to,
        AnimatorConditionOperator op, float threshold)
    {
        AnimatorTransition transition{};
        transition.FromState = from;
        transition.ToState = to;
        transition.CrossFadeDuration = crossFadeDuration;
        transition.Conditions.push_back(AnimatorCondition{ speedParameterName, op, threshold });
        return transition;
    };

    // 境界値を隙間なく分割します。
    // Idle: Speed <= IdleMaxSpeed
    // Walk: IdleMaxSpeed < Speed < RunMinSpeed
    // Run : Speed >= RunMinSpeed
    const AnimatorTransition transitions[] =
    {
        makeSpeedTransition(stateNames.Idle, stateNames.Walk, AnimatorConditionOperator::Greater, thresholds.IdleMaxSpeed),
        makeSpeedTransition(stateNames.Walk, stateNames.Idle, AnimatorConditionOperator::LessEqual, thresholds.IdleMaxSpeed),
        makeSpeedTransition(stateNames.Walk, stateNames.Run, AnimatorConditionOperator::GreaterEqual, thresholds.RunMinSpeed),
        makeSpeedTransition(stateNames.Run, stateNames.Walk, AnimatorConditionOperator::Less, thresholds.RunMinSpeed)
    };

    for (const AnimatorTransition& transition : transitions)
    {
        if (!AddTransition(transition))
        {
            return false;
        }
    }
    return true;
}

bool AnimatorStateMachine::BuildLocomotionController(
    std::shared_ptr<AnimationClip> idleClip,
    std::shared_ptr<AnimationClip> walkClip,
    std::shared_ptr<AnimationClip> runClip,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& stateNames,
    const std::string& speedParameterName,
    float crossFadeDuration,
    bool startImmediately)
{
    // Builderは入力と名前衝突を先に検証します。
    // 途中までStateを登録してから失敗するケースを可能な限り避けるためです。
    if (!idleClip || !walkClip || !runClip ||
        stateNames.Idle.empty() || stateNames.Walk.empty() || stateNames.Run.empty() ||
        stateNames.Idle == stateNames.Walk || stateNames.Idle == stateNames.Run || stateNames.Walk == stateNames.Run ||
        speedParameterName.empty() ||
        HasState(stateNames.Idle) || HasState(stateNames.Walk) || HasState(stateNames.Run) ||
        HasParameter(speedParameterName) ||
        !std::isfinite(thresholds.IdleMaxSpeed) || !std::isfinite(thresholds.RunMinSpeed) ||
        thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed ||
        !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f)
    {
        return false;
    }

    // 低レベルAPIを組み合わせ、State 3個・Speed Parameter・4 Transition・初期Idleを構築します。
    if (!AddState(stateNames.Idle, std::move(idleClip), crossFadeDuration) ||
        !AddState(stateNames.Walk, std::move(walkClip), crossFadeDuration) ||
        !AddState(stateNames.Run, std::move(runClip), crossFadeDuration) ||
        !AddFloatParameter(speedParameterName, 0.0f) ||
        !AddLocomotionTransitions(speedParameterName, thresholds, stateNames, crossFadeDuration) ||
        !SetInitialState(stateNames.Idle, startImmediately))
    {
        return false;
    }
    return true;
}

bool AnimatorStateMachine::AddCharacterParameters(
    const CharacterAnimationParameters& parameterNames,
    bool initialGrounded)
{
    // Parameter名の重複は型の異なる値が同じ名前を奪い合うため禁止します。
    // SpeedだけはBuildLocomotionController()が先に作る一般的な構築順を許可します。
    if (parameterNames.Speed.empty() || parameterNames.Grounded.empty() || parameterNames.Jump.empty() ||
        parameterNames.Speed == parameterNames.Grounded ||
        parameterNames.Speed == parameterNames.Jump ||
        parameterNames.Grounded == parameterNames.Jump)
    {
        return false;
    }

    float speed = 0.0f;
    if (HasParameter(parameterNames.Speed) && !GetFloat(parameterNames.Speed, speed))
    {
        return false;
    }

    if (HasParameter(parameterNames.Grounded) || HasParameter(parameterNames.Jump))
    {
        return false;
    }

    // Groundedは継続状態なのでBool、Jumpは押下イベントなのでTriggerとして保持します。
    // この区別によりJump入力を毎Framefalseへ戻す処理をGameplay側へ要求しません。
    if (!HasParameter(parameterNames.Speed) && !AddFloatParameter(parameterNames.Speed, 0.0f))
    {
        return false;
    }

    if (!AddBoolParameter(parameterNames.Grounded, initialGrounded) ||
        !AddTriggerParameter(parameterNames.Jump))
    {
        return false;
    }

    return true;
}

bool AnimatorStateMachine::UpdateCharacterParameters(
    float speed,
    bool grounded,
    bool jumpRequested,
    const CharacterAnimationParameters& parameterNames)
{
    // 物理・入力側はAnimation State名を知らず、観測したCharacter状態だけを渡します。
    // State遷移の条件はAnimatorTransition側へ集約することでGameplayとの依存を薄く保ちます。
    if (!SetFloat(parameterNames.Speed, speed) ||
        !SetBool(parameterNames.Grounded, grounded))
    {
        return false;
    }

    // Triggerは成立したTransitionが開始した時点でState Machine自身が消費します。
    // jumpRequested=falseではResetしません。Fade中などでまだ消費できていないJump要求を
    // 次Frameまで保持することがTrigger Parameterの重要な役割だからです。
    if (jumpRequested && !SetTrigger(parameterNames.Jump))
    {
        return false;
    }

    return true;
}

} // namespace Raven
