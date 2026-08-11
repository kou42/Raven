// Raven/Animation/AnimatorStateMachineBlendTree.cpp
#include "Raven/Animation/AnimatorStateMachine.h"

#include <cmath>
#include <utility>

namespace Raven
{

bool AnimatorStateMachine::BuildCharacterBlendTreeController(
    std::shared_ptr<AnimationClip> idleClip,
    std::shared_ptr<AnimationClip> walkClip,
    std::shared_ptr<AnimationClip> runClip,
    std::shared_ptr<AnimationClip> jumpStartClip,
    std::shared_ptr<AnimationClip> fallClip,
    std::shared_ptr<AnimationClip> landClip,
    const LocomotionBlendTreeSettings& blendSettings,
    const JumpStateNames& jumpStateNames,
    const CharacterAnimationParameters& parameterNames,
    float transitionCrossFadeDuration,
    bool initialGrounded,
    bool startImmediately)
{
    if (!idleClip || !walkClip || !runClip ||
        !jumpStartClip || !fallClip || !landClip ||
        blendSettings.StateName.empty() ||
        jumpStateNames.JumpStart.empty() ||
        jumpStateNames.Fall.empty() ||
        jumpStateNames.Land.empty() ||
        !std::isfinite(blendSettings.IdleThreshold) ||
        !std::isfinite(blendSettings.WalkThreshold) ||
        !std::isfinite(blendSettings.RunThreshold) ||
        !(blendSettings.IdleThreshold < blendSettings.WalkThreshold) ||
        !(blendSettings.WalkThreshold < blendSettings.RunThreshold) ||
        !std::isfinite(transitionCrossFadeDuration) ||
        transitionCrossFadeDuration < 0.0f)
    {
        return false;
    }

    // Character Parameter名はすべて一意にします。
    // Blend TreeがSpeedを直接参照するため、特にSpeedと他Parameterの衝突は
    // State登録前に検出しておく必要があります。
    const std::string* parameterNameArray[] =
    {
        &parameterNames.Speed,
        &parameterNames.Grounded,
        &parameterNames.Jump,
        &parameterNames.VerticalVelocity
    };

    for (const std::string* name : parameterNameArray)
    {
        if (name->empty()) return false;
    }

    for (std::size_t i = 0; i < 4; ++i)
    {
        for (std::size_t j = i + 1; j < 4; ++j)
        {
            if (*parameterNameArray[i] == *parameterNameArray[j]) return false;
        }
    }

    // Locomotion / Jump State名もすべて一意にします。
    if (blendSettings.StateName == jumpStateNames.JumpStart ||
        blendSettings.StateName == jumpStateNames.Fall ||
        blendSettings.StateName == jumpStateNames.Land ||
        jumpStateNames.JumpStart == jumpStateNames.Fall ||
        jumpStateNames.JumpStart == jumpStateNames.Land ||
        jumpStateNames.Fall == jumpStateNames.Land ||
        HasState(blendSettings.StateName) ||
        HasState(jumpStateNames.JumpStart) ||
        HasState(jumpStateNames.Fall) ||
        HasState(jumpStateNames.Land))
    {
        return false;
    }

    if (HasParameter(parameterNames.Speed) ||
        HasParameter(parameterNames.Grounded) ||
        HasParameter(parameterNames.Jump) ||
        HasParameter(parameterNames.VerticalVelocity))
    {
        return false;
    }

    // ========================================================================
    // Locomotion 1D Blend Tree
    // ========================================================================
    // 従来はIdle / Walk / Runを3 StateとしてSpeed境界でCrossFadeしていました。
    // Blend Tree版では3 Clipを1つのLocomotion Stateへまとめ、Speedの連続値でPoseを補間します。
    // State遷移が発生しないため、0.1や4.0のような境界を跨いだ瞬間にもMotionが連続します。
    if (!AddFloatParameter(parameterNames.Speed, 0.0f))
    {
        return false;
    }

    auto locomotionTree = std::make_shared<BlendTree1D>();
    if (!locomotionTree->AddChild(blendSettings.IdleThreshold, std::move(idleClip)) ||
        !locomotionTree->AddChild(blendSettings.WalkThreshold, std::move(walkClip)) ||
        !locomotionTree->AddChild(blendSettings.RunThreshold, std::move(runClip)))
    {
        return false;
    }

    // Blend Tree StateはSpeed Parameter名だけを保持します。
    // RuntimeのSpeed値はAnimatorStateMachine::Update()でAnimatorへ毎Frame同期されます。
    if (!AddBlendTreeState(
            blendSettings.StateName,
            std::move(locomotionTree),
            parameterNames.Speed,
            transitionCrossFadeDuration))
    {
        return false;
    }

    // Speedは上で登録済みなので、AddCharacterParameters()は型を確認して再利用し、
    // Grounded / Jump / VerticalVelocityだけを追加します。
    if (!AddCharacterParameters(parameterNames, initialGrounded))
    {
        return false;
    }

    if (!AddState(jumpStateNames.JumpStart, std::move(jumpStartClip), transitionCrossFadeDuration) ||
        !AddState(jumpStateNames.Fall, std::move(fallClip), transitionCrossFadeDuration) ||
        !AddState(jumpStateNames.Land, std::move(landClip), transitionCrossFadeDuration))
    {
        return false;
    }

    // ========================================================================
    // Any State -> JumpStart
    // ========================================================================
    // 全State登録後にAny Stateを追加することが重要です。
    // 現実装は登録時点のStateへ通常Transitionを展開するため、この順序なら
    // Locomotion / Fall / Landなどから共通Jump要求を受けられます。
    std::vector<AnimatorCondition> jumpConditions;
    jumpConditions.push_back(AnimatorCondition{
        parameterNames.Jump,
        AnimatorConditionOperator::Equal,
        true });
    jumpConditions.push_back(AnimatorCondition{
        parameterNames.Grounded,
        AnimatorConditionOperator::Equal,
        true });

    if (!AddAnyStateTransition(
            jumpStateNames.JumpStart,
            std::move(jumpConditions),
            transitionCrossFadeDuration))
    {
        return false;
    }

    // JumpStart -> FallはAnimation時間ではなくPhysicsの上下速度で判定します。
    AnimatorTransition beginFall{};
    beginFall.FromState = jumpStateNames.JumpStart;
    beginFall.ToState = jumpStateNames.Fall;
    beginFall.CrossFadeDuration = transitionCrossFadeDuration;
    beginFall.Conditions.push_back(AnimatorCondition{
        parameterNames.VerticalVelocity,
        AnimatorConditionOperator::LessEqual,
        0.0f });
    if (!AddTransition(std::move(beginFall)))
    {
        return false;
    }

    // Fall -> Landは接地というGameplay上の事実で切り替えます。
    AnimatorTransition beginLand{};
    beginLand.FromState = jumpStateNames.Fall;
    beginLand.ToState = jumpStateNames.Land;
    beginLand.CrossFadeDuration = transitionCrossFadeDuration;
    beginLand.Conditions.push_back(AnimatorCondition{
        parameterNames.Grounded,
        AnimatorConditionOperator::Equal,
        true });
    if (!AddTransition(std::move(beginLand)))
    {
        return false;
    }

    // ========================================================================
    // Land -> Locomotion Blend Tree
    // ========================================================================
    // Blend Tree側がSpeedからIdle/Walk/Runを直接選ぶため、着地後に3本のSpeed Transitionを
    // 用意する必要はありません。Landを80%再生したらLocomotionへ1本だけ戻し、
    // CrossFade先のBlend TreeがそのFrameのSpeedで適切なPoseを生成します。
    AnimatorTransition finishLanding{};
    finishLanding.FromState = jumpStateNames.Land;
    finishLanding.ToState = blendSettings.StateName;
    finishLanding.CrossFadeDuration = transitionCrossFadeDuration;
    finishLanding.HasExitTime = true;
    finishLanding.ExitTime = 0.8f;
    if (!AddTransition(std::move(finishLanding)))
    {
        return false;
    }

    // Controller定義が完成してから初期Locomotionを開始します。
    return SetInitialState(blendSettings.StateName, startImmediately);
}

} // namespace Raven
