// Raven/Animation/Tests/BlendTreeRuntimeSelfTests.cpp
#include "Raven/Animation/Tests/BlendTreeRuntimeSelfTests.h"

#include "Raven/Animation/AnimationRuntimeDebug.h"

#include <cassert>
#include <cmath>
#include <memory>

namespace Raven::tests
{
namespace
{
bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::abs(a - b) <= tolerance;
}

std::shared_ptr<AnimationClip> MakeClip(float duration)
{
    return std::make_shared<AnimationClip>(duration);
}

void ValidateWeightSum(const BlendTree1DDebugInfo& info)
{
    assert(info.LeftWeight >= 0.0f);
    assert(info.RightWeight >= 0.0f);
    assert(info.LeftWeight <= 1.0f);
    assert(info.RightWeight <= 1.0f);
    assert(NearlyEqual(info.LeftWeight + info.RightWeight, 1.0f));
}

void RunDirectWeightDebugTest()
{
    BlendTree1D tree;
    assert(tree.AddChild(0.0f, MakeClip(1.0f)));
    assert(tree.AddChild(2.0f, MakeClip(1.0f)));
    assert(tree.AddChild(6.0f, MakeClip(1.0f)));

    BlendTree1DDebugInfo info{};

    assert(tree.GetDebugInfo(0.0f, info));
    assert(info.LeftChildIndex == 0);
    assert(info.RightChildIndex == 0);
    assert(NearlyEqual(info.LeftWeight, 1.0f));
    assert(NearlyEqual(info.RightWeight, 0.0f));
    ValidateWeightSum(info);

    assert(tree.GetDebugInfo(1.0f, info));
    assert(info.LeftChildIndex == 0);
    assert(info.RightChildIndex == 1);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
    ValidateWeightSum(info);

    assert(tree.GetDebugInfo(4.0f, info));
    assert(info.LeftChildIndex == 1);
    assert(info.RightChildIndex == 2);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
    ValidateWeightSum(info);

    assert(tree.GetDebugInfo(6.0f, info));
    assert(info.LeftChildIndex == 2);
    assert(info.RightChildIndex == 2);
    assert(NearlyEqual(info.LeftWeight, 1.0f));
    assert(NearlyEqual(info.RightWeight, 0.0f));
    ValidateWeightSum(info);
}

void ValidateRuntimeSpeed(
    AnimatorStateMachine& stateMachine,
    float speed,
    float deltaTime)
{
    assert(stateMachine.SetFloat("Speed", speed));
    stateMachine.Update(deltaTime);

    AnimatorStateMachineRuntimeDebugInfo runtime{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, runtime));

    assert(runtime.Current.HasState);
    assert(runtime.Current.StateName == "Locomotion");
    assert(runtime.Current.IsBlendTree);
    assert(runtime.Current.BlendParameterName == "Speed");
    assert(NearlyEqual(runtime.Current.BlendParameterValue, speed));
    assert(runtime.Pending.HasState == false);
    assert(runtime.IsCrossFading == false);
    ValidateWeightSum(runtime.Current.BlendTree);
}

void RunSmoothSpeedRuntimeTest()
{
    Animator animator;
    AnimatorStateMachine stateMachine(animator);

    assert(stateMachine.AddFloatParameter("Speed", 0.0f));

    auto tree = std::make_shared<BlendTree1D>();
    assert(tree->AddChild(0.0f, MakeClip(1.0f)));
    assert(tree->AddChild(2.0f, MakeClip(0.8f)));
    assert(tree->AddChild(6.0f, MakeClip(0.6f)));

    assert(stateMachine.AddBlendTreeState("Locomotion", tree, "Speed", 0.15f));
    assert(stateMachine.SetInitialState("Locomotion", true));

    constexpr float deltaTime = 1.0f / 60.0f;
    constexpr int segmentFrames = 60;

    for (int frame = 0; frame <= segmentFrames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(segmentFrames);
        ValidateRuntimeSpeed(stateMachine, 2.0f * t, deltaTime);
    }

    for (int frame = 1; frame <= segmentFrames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(segmentFrames);
        ValidateRuntimeSpeed(stateMachine, 2.0f + 4.0f * t, deltaTime);

        const float normalizedTime = animator.GetNormalizedTime();
        assert(std::isfinite(normalizedTime));
        assert(normalizedTime >= 0.0f);
        assert(normalizedTime <= 1.0f);
    }

    AnimatorStateMachineRuntimeDebugInfo finalRuntime{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, finalRuntime));
    assert(NearlyEqual(finalRuntime.Current.BlendParameterValue, 6.0f));
    assert(finalRuntime.Current.BlendTree.LeftChildIndex == 2);
    assert(finalRuntime.Current.BlendTree.RightChildIndex == 2);
    assert(NearlyEqual(finalRuntime.Current.BlendTree.LeftWeight, 1.0f));
}

void RunStateGraphRuntimeSnapshotTest()
{
    Animator animator;
    AnimatorStateMachine stateMachine(animator);
    assert(stateMachine.AddFloatParameter("Speed", 0.0f));

    auto tree = std::make_shared<BlendTree1D>();
    assert(tree->AddChild(0.0f, MakeClip(1.0f)));
    assert(tree->AddChild(2.0f, MakeClip(1.0f)));
    assert(stateMachine.AddBlendTreeState("Locomotion", tree, "Speed", 0.2f));
    assert(stateMachine.AddState("JumpStart", MakeClip(0.5f), 0.2f));

    AnimatorTransition transition{};
    transition.FromState = "Locomotion";
    transition.ToState = "JumpStart";
    transition.CrossFadeDuration = 0.2f;
    assert(stateMachine.AddTransition(transition));
    assert(stateMachine.SetInitialState("Locomotion", true));

    AnimatorStateMachineRuntimeDebugInfo before{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, before));
    assert(before.Nodes.size() == 2);
    assert(before.Transitions.size() == 1);
    assert(before.Transitions[0].IsActive == false);

    assert(stateMachine.TransitionTo("JumpStart", 0.2f));

    AnimatorStateMachineRuntimeDebugInfo duringFade{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, duringFade));
    assert(duringFade.Current.StateName == "Locomotion");
    assert(duringFade.Pending.StateName == "JumpStart");
    assert(duringFade.IsCrossFading);
    assert(duringFade.Transitions.size() == 1);
    assert(duringFade.Transitions[0].IsActive);

    bool foundCurrent = false;
    bool foundPending = false;
    for (const auto& node : duringFade.Nodes)
    {
        if (node.StateName == "Locomotion")
        {
            foundCurrent = node.IsCurrent && node.IsBlendTree;
        }
        else if (node.StateName == "JumpStart")
        {
            foundPending = node.IsPending;
        }
    }

    assert(foundCurrent);
    assert(foundPending);
}

void RunTransitionConditionRuntimeSnapshotTest()
{
    Animator animator;
    AnimatorStateMachine stateMachine(animator);

    assert(stateMachine.AddFloatParameter("VerticalVelocity", 3.0f));
    assert(stateMachine.AddBoolParameter("Grounded", false));
    assert(stateMachine.AddTriggerParameter("Jump"));
    assert(stateMachine.AddState("Locomotion", MakeClip(1.0f), 0.2f));
    assert(stateMachine.AddState("JumpStart", MakeClip(1.0f), 0.2f));

    AnimatorTransition transition{};
    transition.FromState = "Locomotion";
    transition.ToState = "JumpStart";
    transition.CrossFadeDuration = 0.2f;
    transition.HasExitTime = true;
    transition.ExitTime = 0.5f;
    transition.Conditions = {
        { "Jump", AnimatorConditionOperator::Equal, true },
        { "Grounded", AnimatorConditionOperator::Equal, true },
        { "VerticalVelocity", AnimatorConditionOperator::Greater, 0.0f }
    };
    assert(stateMachine.AddTransition(transition));
    assert(stateMachine.SetInitialState("Locomotion", true));

    // 初期状態ではJump=false / Grounded=false、速度だけ成立です。
    AnimatorStateMachineRuntimeDebugInfo initial{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, initial));
    assert(initial.Transitions.size() == 1);
    const auto& initialTransition = initial.Transitions[0];
    assert(initialTransition.IsSourceCurrent);
    assert(initialTransition.Conditions.size() == 3);
    assert(initialTransition.Conditions[0].ActualBool == false);
    assert(initialTransition.Conditions[0].ExpectedBool == true);
    assert(initialTransition.Conditions[0].IsMet == false);
    assert(initialTransition.Conditions[1].IsMet == false);
    assert(initialTransition.Conditions[2].IsFloat);
    assert(NearlyEqual(initialTransition.Conditions[2].ActualFloat, 3.0f));
    assert(initialTransition.Conditions[2].IsMet);
    assert(initialTransition.AreConditionsMet == false);
    assert(initialTransition.IsExitTimeMet == false);
    assert(initialTransition.IsEligible == false);

    // Parameter条件を成立させてもExit Time未到達なら候補にはなりません。
    assert(stateMachine.SetTrigger("Jump"));
    assert(stateMachine.SetBool("Grounded", true));

    AnimatorStateMachineRuntimeDebugInfo conditionsMet{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, conditionsMet));
    assert(conditionsMet.Transitions[0].AreConditionsMet);
    assert(conditionsMet.Transitions[0].IsExitTimeMet == false);
    assert(conditionsMet.Transitions[0].IsEligible == false);

    // 0.5秒以上進めるとExit Timeも成立し、Update()前Snapshotでは発火可能状態になります。
    animator.Update(0.55f);

    AnimatorStateMachineRuntimeDebugInfo eligible{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, eligible));
    assert(eligible.Transitions[0].SourceNormalizedTime >= 0.5f);
    assert(eligible.Transitions[0].IsExitTimeMet);
    assert(eligible.Transitions[0].AreConditionsMet);
    assert(eligible.Transitions[0].IsEligible);
}
} // namespace

void RunBlendTreeRuntimeSelfTests()
{
    RunDirectWeightDebugTest();
    RunSmoothSpeedRuntimeTest();
    RunStateGraphRuntimeSnapshotTest();
    RunTransitionConditionRuntimeSnapshotTest();
}

} // namespace Raven::tests
