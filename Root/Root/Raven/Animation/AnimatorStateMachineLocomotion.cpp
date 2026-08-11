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
    // Character向けParameter名はすべて一意である必要があります。
    // VerticalVelocityとSpeedが同名だと水平速度でJumpStart -> Fallが誤発火するため、
    // Builderの段階で名前衝突を検出します。
    const std::string* names[] =
    {
        &parameterNames.Speed,
        &parameterNames.Grounded,
        &parameterNames.Jump,
        &parameterNames.VerticalVelocity
    };

    for (const std::string* name : names)
    {
        if (name->empty()) return false;
    }

    for (std::size_t i = 0; i < 4; ++i)
    {
        for (std::size_t j = i + 1; j < 4; ++j)
        {
            if (*names[i] == *names[j]) return false;
        }
    }

    float speed = 0.0f;
    if (HasParameter(parameterNames.Speed) && !GetFloat(parameterNames.Speed, speed)) return false;
    if (HasParameter(parameterNames.Grounded) || HasParameter(parameterNames.Jump) || HasParameter(parameterNames.VerticalVelocity)) return false;
    if (!HasParameter(parameterNames.Speed) && !AddFloatParameter(parameterNames.Speed, 0.0f)) return false;
    if (!AddBoolParameter(parameterNames.Grounded, initialGrounded) ||
        !AddTriggerParameter(parameterNames.Jump) ||
        !AddFloatParameter(parameterNames.VerticalVelocity, 0.0f)) return false;
    return true;
}

bool AnimatorStateMachine::UpdateCharacterParameters(float speed, bool grounded, bool jumpRequested, float verticalVelocity, const CharacterAnimationParameters& parameterNames)
{
    if (!std::isfinite(speed) || !std::isfinite(verticalVelocity)) return false;
    if (!SetFloat(parameterNames.Speed, speed) ||
        !SetBool(parameterNames.Grounded, grounded) ||
        !SetFloat(parameterNames.VerticalVelocity, verticalVelocity)) return false;

    // TriggerはTransitionが実際に開始した時だけ消費されます。Fade中のJump要求を失わないため、
    // jumpRequested=falseのFrameで明示的にResetすることはしません。
    if (jumpRequested && !SetTrigger(parameterNames.Jump)) return false;
    return true;
}

bool AnimatorStateMachine::AddJumpStatesAndTransitions(
    std::shared_ptr<AnimationClip> jumpStartClip,
    std::shared_ptr<AnimationClip> fallClip,
    std::shared_ptr<AnimationClip> landClip,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& locomotionStateNames,
    const JumpStateNames& jumpStateNames,
    const CharacterAnimationParameters& parameterNames,
    float crossFadeDuration)
{
    if (!jumpStartClip || !fallClip || !landClip ||
        jumpStateNames.JumpStart.empty() || jumpStateNames.Fall.empty() || jumpStateNames.Land.empty() ||
        jumpStateNames.JumpStart == jumpStateNames.Fall || jumpStateNames.JumpStart == jumpStateNames.Land || jumpStateNames.Fall == jumpStateNames.Land ||
        jumpStateNames.JumpStart == locomotionStateNames.Idle || jumpStateNames.JumpStart == locomotionStateNames.Walk || jumpStateNames.JumpStart == locomotionStateNames.Run ||
        jumpStateNames.Fall == locomotionStateNames.Idle || jumpStateNames.Fall == locomotionStateNames.Walk || jumpStateNames.Fall == locomotionStateNames.Run ||
        jumpStateNames.Land == locomotionStateNames.Idle || jumpStateNames.Land == locomotionStateNames.Walk || jumpStateNames.Land == locomotionStateNames.Run ||
        HasState(jumpStateNames.JumpStart) || HasState(jumpStateNames.Fall) || HasState(jumpStateNames.Land) ||
        !HasState(locomotionStateNames.Idle) || !HasState(locomotionStateNames.Walk) || !HasState(locomotionStateNames.Run) ||
        !std::isfinite(thresholds.IdleMaxSpeed) || !std::isfinite(thresholds.RunMinSpeed) || thresholds.IdleMaxSpeed >= thresholds.RunMinSpeed ||
        !std::isfinite(crossFadeDuration) || crossFadeDuration < 0.0f) return false;

    float speed = 0.0f;
    float verticalVelocity = 0.0f;
    bool grounded = false;
    if (!GetFloat(parameterNames.Speed, speed) ||
        !GetFloat(parameterNames.VerticalVelocity, verticalVelocity) ||
        !GetBool(parameterNames.Grounded, grounded) ||
        !SetTrigger(parameterNames.Jump) || !ResetTrigger(parameterNames.Jump)) return false;

    if (!AddState(jumpStateNames.JumpStart, std::move(jumpStartClip), crossFadeDuration) ||
        !AddState(jumpStateNames.Fall, std::move(fallClip), crossFadeDuration) ||
        !AddState(jumpStateNames.Land, std::move(landClip), crossFadeDuration)) return false;

    auto makeJumpTransition = [&](const std::string& from)
    {
        AnimatorTransition transition{};
        transition.FromState = from;
        transition.ToState = jumpStateNames.JumpStart;
        transition.CrossFadeDuration = crossFadeDuration;
        // Jump Triggerだけでは空中で再Jumpできてしまうため、GroundedもAND条件にします。
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Jump, AnimatorConditionOperator::Equal, true });
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Grounded, AnimatorConditionOperator::Equal, true });
        return transition;
    };

    // Any Stateは次段階で導入するため、現段階ではLocomotionの各Stateから明示的に接続します。
    // Any State対応後はこの3本を1本へ集約できるよう、JumpStart以降の遷移は独立して定義します。
    const AnimatorTransition jumpTransitions[] =
    {
        makeJumpTransition(locomotionStateNames.Idle),
        makeJumpTransition(locomotionStateNames.Walk),
        makeJumpTransition(locomotionStateNames.Run)
    };
    for (const AnimatorTransition& transition : jumpTransitions) if (!AddTransition(transition)) return false;

    AnimatorTransition beginFall{};
    beginFall.FromState = jumpStateNames.JumpStart;
    beginFall.ToState = jumpStateNames.Fall;
    beginFall.CrossFadeDuration = crossFadeDuration;
    // JumpStart -> FallはAnimation再生時間ではなく、Physicsが持つ上下方向速度で判定します。
    // VerticalVelocity <= 0 は上昇から下降へ転じた事実を表し、Animation側で頂点時刻を推測しません。
    beginFall.Conditions.push_back(AnimatorCondition{ parameterNames.VerticalVelocity, AnimatorConditionOperator::LessEqual, 0.0f });
    if (!AddTransition(std::move(beginFall))) return false;

    AnimatorTransition beginLand{};
    beginLand.FromState = jumpStateNames.Fall;
    beginLand.ToState = jumpStateNames.Land;
    beginLand.CrossFadeDuration = crossFadeDuration;
    // Fall -> Landは接地というGameplay上の事実だけで判定します。
    // Landを独立Stateにしておくことで、次にExit Timeを導入した際に着地Animationを
    // 一定位置まで再生してからLocomotionへ戻す構造へ自然に拡張できます。
    beginLand.Conditions.push_back(AnimatorCondition{ parameterNames.Grounded, AnimatorConditionOperator::Equal, true });
    if (!AddTransition(std::move(beginLand))) return false;

    auto makeLandingTransition = [&](const std::string& to, AnimatorConditionOperator speedOperator, float speedThreshold)
    {
        AnimatorTransition transition{};
        transition.FromState = jumpStateNames.Land;
        transition.ToState = to;
        transition.CrossFadeDuration = crossFadeDuration;
        transition.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, speedOperator, speedThreshold });
        return transition;
    };

    AnimatorTransition landIdle = makeLandingTransition(locomotionStateNames.Idle, AnimatorConditionOperator::LessEqual, thresholds.IdleMaxSpeed);
    AnimatorTransition landRun = makeLandingTransition(locomotionStateNames.Run, AnimatorConditionOperator::GreaterEqual, thresholds.RunMinSpeed);
    AnimatorTransition landWalk{};
    landWalk.FromState = jumpStateNames.Land;
    landWalk.ToState = locomotionStateNames.Walk;
    landWalk.CrossFadeDuration = crossFadeDuration;
    landWalk.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, AnimatorConditionOperator::Greater, thresholds.IdleMaxSpeed });
    landWalk.Conditions.push_back(AnimatorCondition{ parameterNames.Speed, AnimatorConditionOperator::Less, thresholds.RunMinSpeed });

    if (!AddTransition(std::move(landIdle)) || !AddTransition(std::move(landWalk)) || !AddTransition(std::move(landRun))) return false;
    return true;
}

bool AnimatorStateMachine::BuildCharacterController(
    std::shared_ptr<AnimationClip> idleClip,
    std::shared_ptr<AnimationClip> walkClip,
    std::shared_ptr<AnimationClip> runClip,
    std::shared_ptr<AnimationClip> jumpStartClip,
    std::shared_ptr<AnimationClip> fallClip,
    std::shared_ptr<AnimationClip> landClip,
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
    const std::string* parameterNamesArray[] =
    {
        &parameterNames.Speed,
        &parameterNames.Grounded,
        &parameterNames.Jump,
        &parameterNames.VerticalVelocity
    };
    for (const std::string* name : parameterNamesArray) if (name->empty()) return false;
    for (std::size_t i = 0; i < 4; ++i)
    {
        for (std::size_t j = i + 1; j < 4; ++j)
        {
            if (*parameterNamesArray[i] == *parameterNamesArray[j]) return false;
        }
    }

    const std::string* jumpNames[] =
    {
        &jumpStateNames.JumpStart,
        &jumpStateNames.Fall,
        &jumpStateNames.Land
    };
    for (const std::string* name : jumpNames)
    {
        if (name->empty() || *name == locomotionStateNames.Idle || *name == locomotionStateNames.Walk || *name == locomotionStateNames.Run || HasState(*name)) return false;
    }
    if (jumpStateNames.JumpStart == jumpStateNames.Fall || jumpStateNames.JumpStart == jumpStateNames.Land || jumpStateNames.Fall == jumpStateNames.Land ||
        HasParameter(parameterNames.Grounded) || HasParameter(parameterNames.Jump) || HasParameter(parameterNames.VerticalVelocity))
    {
        return false;
    }

    // Locomotionを先に構築し、そのSpeed Parameterを再利用してGrounded / Jump / VerticalVelocityを追加します。
    // startImmediatelyは最後まで構築できた後に適用したいため、Locomotion構築時はいったんfalseにします。
    if (!BuildLocomotionController(
            std::move(idleClip), std::move(walkClip), std::move(runClip),
            thresholds, locomotionStateNames, parameterNames.Speed,
            locomotionCrossFadeDuration, false) ||
        !AddCharacterParameters(parameterNames, initialGrounded) ||
        !AddJumpStatesAndTransitions(
            std::move(jumpStartClip), std::move(fallClip), std::move(landClip),
            thresholds, locomotionStateNames,
            jumpStateNames, parameterNames, jumpCrossFadeDuration))
    {
        return false;
    }

    // Controller定義が完成してから初期Stateを開始します。
    // これにより構築途中の状態でAnimation再生だけが始まることを避けます。
    return SetInitialState(locomotionStateNames.Idle, startImmediately);
}

} // namespace Raven
