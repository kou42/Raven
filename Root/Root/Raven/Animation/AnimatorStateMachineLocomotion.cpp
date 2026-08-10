// Raven/Animation/AnimatorStateMachineLocomotion.cpp
#include "Raven/Animation/AnimatorStateMachine.h"

#include <cmath>
#include <utility>

namespace Raven
{

bool AnimatorStateMachine::AddLocomotionTransitions(const std::string& speedParameterName, const LocomotionThresholds& thresholds, const LocomotionStateNames& stateNames, float crossFadeDuration)
{
    if (!std::isfinite(thresholds.IdleMaxSpeed) || !std::isfinite(thresholds.RunMinSpeed) || thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed || !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f) return false;
    float unusedSpeed = 0.0f;
    if (!GetFloat(speedParameterName, unusedSpeed) || !HasState(stateNames.Idle) || !HasState(stateNames.Walk) || !HasState(stateNames.Run)) return false;
    auto makeSpeedTransition = [&speedParameterName, crossFadeDuration](const std::string& from, const std::string& to, AnimatorConditionOperator op, float threshold)
    {
        AnimatorTransition transition{};
        transition.FromState = from; transition.ToState = to; transition.CrossFadeDuration = crossFadeDuration;
        transition.Conditions.push_back(AnimatorCondition{ speedParameterName, op, threshold });
        return transition;
    };
    const AnimatorTransition transitions[] = {
        makeSpeedTransition(stateNames.Idle, stateNames.Walk, AnimatorConditionOperator::Greater, thresholds.IdleMaxSpeed),
        makeSpeedTransition(stateNames.Walk, stateNames.Idle, AnimatorConditionOperator::LessEqual, thresholds.IdleMaxSpeed),
        makeSpeedTransition(stateNames.Walk, stateNames.Run, AnimatorConditionOperator::GreaterEqual, thresholds.RunMinSpeed),
        makeSpeedTransition(stateNames.Run, stateNames.Walk, AnimatorConditionOperator::Less, thresholds.RunMinSpeed) };
    for (const AnimatorTransition& transition : transitions) if (!AddTransition(transition)) return false;
    return true;
}

bool AnimatorStateMachine::BuildLocomotionController(std::shared_ptr<AnimationClip> idleClip, std::shared_ptr<AnimationClip> walkClip, std::shared_ptr<AnimationClip> runClip, const LocomotionThresholds& thresholds, const LocomotionStateNames& stateNames, const std::string& speedParameterName, float crossFadeDuration, bool startImmediately)
{
    if (!idleClip || !walkClip || !runClip || stateNames.Idle.empty() || stateNames.Walk.empty() || stateNames.Run.empty() || stateNames.Idle == stateNames.Walk || stateNames.Idle == stateNames.Run || stateNames.Walk == stateNames.Run || speedParameterName.empty() || HasState(stateNames.Idle) || HasState(stateNames.Walk) || HasState(stateNames.Run) || HasParameter(speedParameterName) || !std::isfinite(thresholds.IdleMaxSpeed) || !std::isfinite(thresholds.RunMinSpeed) || thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed || !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f) return false;
    if (!AddState(stateNames.Idle, std::move(idleClip), crossFadeDuration) || !AddState(stateNames.Walk, std::move(walkClip), crossFadeDuration) || !AddState(stateNames.Run, std::move(runClip), crossFadeDuration) || !AddFloatParameter(speedParameterName, 0.0f) || !AddLocomotionTransitions(speedParameterName, thresholds, stateNames, crossFadeDuration) || !SetInitialState(stateNames.Idle, startImmediately)) return false;
    return true;
}

bool AnimatorStateMachine::AddCharacterParameters(const CharacterAnimationParameters& parameterNames, bool initialGrounded)
{
    if (parameterNames.Speed.empty() || parameterNames.Grounded.empty() || parameterNames.Jump.empty() || parameterNames.Speed == parameterNames.Grounded || parameterNames.Speed == parameterNames.Jump || parameterNames.Grounded == parameterNames.Jump) return false;
    float speed = 0.0f;
    if (HasParameter(parameterNames.Speed) && !GetFloat(parameterNames.Speed, speed)) return false;
    if (HasParameter(parameterNames.Grounded) || HasParameter(parameterNames.Jump)) return false;
    if (!HasParameter(parameterNames.Speed) && !AddFloatParameter(parameterNames.Speed, 0.0f)) return false;
    if (!AddBoolParameter(parameterNames.Grounded, initialGrounded) || !AddTriggerParameter(parameterNames.Jump)) return false;
    return true;
}

bool AnimatorStateMachine::UpdateCharacterParameters(float speed, bool grounded, bool jumpRequested, const CharacterAnimationParameters& parameterNames)
{
    if (!SetFloat(parameterNames.Speed, speed) || !SetBool(parameterNames.Grounded, grounded)) return false;
    // TriggerはTransitionが実際に開始した時だけ消費されます。Fade中のJump要求を失わないため、
    // jumpRequested=falseのFrameで明示的にResetすることはしません。
    if (jumpRequested && !SetTrigger(parameterNames.Jump)) return false;
    return true;
}

bool AnimatorStateMachine::AddJumpStateAndTransitions(std::shared_ptr<AnimationClip> jumpClip, const LocomotionThresholds& thresholds, const LocomotionStateNames& locomotionStateNames, const JumpStateNames& jumpStateNames, const CharacterAnimationParameters& parameterNames, float crossFadeDuration)
{
    if (!jumpClip || jumpStateNames.Jump.empty() || HasState(jumpStateNames.Jump) || jumpStateNames.Jump == locomotionStateNames.Idle || jumpStateNames.Jump == locomotionStateNames.Walk || jumpStateNames.Jump == locomotionStateNames.Run || !HasState(locomotionStateNames.Idle) || !HasState(locomotionStateNames.Walk) || !HasState(locomotionStateNames.Run) || !std::isfinite(thresholds.IdleMaxSpeed) || !std::isfinite(thresholds.RunMinSpeed) || thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed || !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f) return false;

    float speed = 0.0f;
    bool grounded = false;
    if (!GetFloat(parameterNames.Speed, speed) || !GetBool(parameterNames.Grounded, grounded) || !SetTrigger(parameterNames.Jump) || !ResetTrigger(parameterNames.Jump)) return false;
    if (!AddState(jumpStateNames.Jump, std::move(jumpClip), crossFadeDuration)) return false;

    auto makeJumpTransition = [&](const std::string& from)
    {
        AnimatorTransition transition{};
        transition.FromState = from;
        transition.ToState = jumpStateNames.Jump;
        transition.CrossFadeDuration = crossFadeDuration;
        // Jump Triggerだけでは空中で再Jumpできてしまうため、GroundedもAND条件にします。
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Jump, AnimatorConditionOperator::Equal, true });
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Grounded, AnimatorConditionOperator::Equal, true });
        return transition;
    };

    const AnimatorTransition jumpTransitions[] = { makeJumpTransition(locomotionStateNames.Idle), makeJumpTransition(locomotionStateNames.Walk), makeJumpTransition(locomotionStateNames.Run) };
    for (const AnimatorTransition& transition : jumpTransitions) if (!AddTransition(transition)) return false;

    auto makeLandingTransition = [&](const std::string& to, AnimatorConditionOperator speedOperator, float speedThreshold)
    {
        AnimatorTransition transition{};
        transition.FromState = jumpStateNames.Jump;
        transition.ToState = to;
        transition.CrossFadeDuration = crossFadeDuration;
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Grounded, AnimatorConditionOperator::Equal, true });
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, speedOperator, speedThreshold });
        return transition;
    };

    AnimatorTransition landIdle = makeLandingTransition(locomotionStateNames.Idle, AnimatorConditionOperator::LessEqual, thresholds.IdleMaxSpeed);
    AnimatorTransition landRun = makeLandingTransition(locomotionStateNames.Run, AnimatorConditionOperator::GreaterEqual, thresholds.RunMinSpeed);
    AnimatorTransition landWalk{};
    landWalk.FromState = jumpStateNames.Jump;
    landWalk.ToState = locomotionStateNames.Walk;
    landWalk.CrossFadeDuration = crossFadeDuration;
    landWalk.Conditions.push_back(AnimatorCondition{ parameterNames.Grounded, AnimatorConditionOperator::Equal, true });
    landWalk.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, AnimatorConditionOperator::Greater, thresholds.IdleMaxSpeed });
    landWalk.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, AnimatorConditionOperator::Less, thresholds.RunMinSpeed });

    if (!AddTransition(std::move(landIdle)) || !AddTransition(std::move(landWalk)) || !AddTransition(std::move(landRun))) return false;
    return true;
}

bool AnimatorStateMachine::BuildCharacterController(
    std::shared_ptr<AnimationClip> idleClip,
    std::shared_ptr<AnimationClip> walkClip,
    std::shared_ptr<AnimationClip> runClip,
    std::shared_ptr<AnimationClip> jumpClip,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& locomotionStateNames,
    const JumpStateNames& jumpStateNames,
    const CharacterAnimationParameters& parameterNames,
    float locomotionCrossFadeDuration,
    float jumpCrossFadeDuration,
    bool initialGrounded,
    bool startImmediately)
{
    // 一括Builderでは名前衝突を先に検証します。
    // 特にSpeed名はBuildLocomotionControllerへ渡す値とCharacter Parameter側で一致している必要があります。
    if (parameterNames.Speed.empty() || parameterNames.Grounded.empty() || parameterNames.Jump.empty() ||
        parameterNames.Speed == parameterNames.Grounded || parameterNames.Speed == parameterNames.Jump || parameterNames.Grounded == parameterNames.Jump ||
        jumpStateNames.Jump.empty() || jumpStateNames.Jump == locomotionStateNames.Idle ||
        jumpStateNames.Jump == locomotionStateNames.Walk || jumpStateNames.Jump == locomotionStateNames.Run ||
        HasParameter(parameterNames.Grounded) || HasParameter(parameterNames.Jump) || HasState(jumpStateNames.Jump))
    {
        return false;
    }

    // Locomotionを先に構築し、そのSpeed Parameterを再利用してGrounded / Jumpを追加します。
    // startImmediatelyは最後まで構築できた後に適用したいため、Locomotion構築時はいったんfalseにします。
    if (!BuildLocomotionController(
            std::move(idleClip), std::move(walkClip), std::move(runClip),
            thresholds, locomotionStateNames, parameterNames.Speed,
            locomotionCrossFadeDuration, false) ||
        !AddCharacterParameters(parameterNames, initialGrounded) ||
        !AddJumpStateAndTransitions(
            std::move(jumpClip), thresholds, locomotionStateNames,
            jumpStateNames, parameterNames, jumpCrossFadeDuration))
    {
        return false;
    }

    // Controller定義が完成してから初期Stateを開始します。
    // これにより構築途中の状態でAnimation再生だけが始まることを避けます。
    return SetInitialState(locomotionStateNames.Idle, startImmediately);
}

} // namespace Raven
