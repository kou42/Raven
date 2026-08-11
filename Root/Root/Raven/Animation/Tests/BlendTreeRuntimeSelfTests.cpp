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

// 1D Blend Treeは通常、隣接する2 MotionのWeight合計が1.0になります。
// Clamp領域では同一Childを左右として返すため、その場合もDebug情報上の合計が1.0になることを保証します。
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

    // Speed=0は最小ThresholdそのものなのでIdleのみ100%になります。
    assert(tree.GetDebugInfo(0.0f, info));
    assert(info.LeftChildIndex == 0);
    assert(info.RightChildIndex == 0);
    assert(NearlyEqual(info.LeftWeight, 1.0f));
    assert(NearlyEqual(info.RightWeight, 0.0f));
    ValidateWeightSum(info);

    // Speed=1はThreshold 0と2の中点なのでIdle/Walkが50%ずつになります。
    assert(tree.GetDebugInfo(1.0f, info));
    assert(info.LeftChildIndex == 0);
    assert(info.RightChildIndex == 1);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
    ValidateWeightSum(info);

    // Speed=4はThreshold 2と6の中点なのでWalk/Runが50%ずつになります。
    assert(tree.GetDebugInfo(4.0f, info));
    assert(info.LeftChildIndex == 1);
    assert(info.RightChildIndex == 2);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
    ValidateWeightSum(info);

    // Speed=6は最大ThresholdそのものなのでRunのみ100%になります。
    assert(tree.GetDebugInfo(6.0f, info));
    assert(info.LeftChildIndex == 2);
    assert(info.RightChildIndex == 2);
    assert(NearlyEqual(info.LeftWeight, 1.0f));
    assert(NearlyEqual(info.RightWeight, 0.0f));
    ValidateWeightSum(info);
}

void ValidateRuntimeSpeed(AnimatorStateMachine& stateMachine, float speed, float deltaTime)
{
    assert(stateMachine.SetFloat("Speed", speed));
    stateMachine.Update(deltaTime);

    AnimatorStateMachineRuntimeDebugInfo runtime{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, runtime));

    // Speed変更ではLocomotion Stateを維持し、State遷移ではなくBlend Weightだけが変わることを保証します。
    // ここでRuntime Snapshotまで確認することで、実際のAnimatorとEditor表示用情報が同じ値を参照していることも検証します。
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

    // 60fps相当で1秒かけてSpeed 0 -> 2へ連続変化させます。
    // 代表点だけではなく各Frameを検証し、補間途中でもWeightやState情報が破綻しないことを確認します。
    for (int frame = 0; frame <= segmentFrames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(segmentFrames);
        ValidateRuntimeSpeed(stateMachine, 2.0f * t, deltaTime);
    }

    // 続けて1秒かけてSpeed 2 -> 6へ変化させます。
    // ChildのClip Durationが異なるため、Blend中でもAnimatorのNormalizedTimeが有限かつ0..1を維持することも確認します。
    for (int frame = 1; frame <= segmentFrames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(segmentFrames);
        ValidateRuntimeSpeed(stateMachine, 2.0f + 4.0f * t, deltaTime);
        const float normalizedTime = animator.GetNormalizedTime();
        assert(std::isfinite(normalizedTime));
        assert(normalizedTime >= 0.0f);
        assert(normalizedTime <= 1.0f);
    }

    // 最終的にSpeed=6へ到達したとき、Run Childが100%選択されることをRuntime Snapshotでも固定します。
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

    // CrossFade開始前はGraph定義自体は取得できますが、Active Transitionは存在しません。
    AnimatorStateMachineRuntimeDebugInfo before{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, before));
    assert(before.Nodes.size() == 2);
    assert(before.Transitions.size() == 1);
    assert(before.Transitions[0].IsActive == false);

    // TransitionTo直後はAnimatorのCurrent/Nextが同時に存在します。
    // Graph SnapshotでもLocomotion -> JumpStartの1本だけがActiveになり、Editorが実際のCrossFadeを強調表示できることを確認します。
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
        if (node.StateName == "Locomotion") foundCurrent = node.IsCurrent && node.IsBlendTree;
        else if (node.StateName == "JumpStart") foundPending = node.IsPending;
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

    // Bool/Trigger/FloatとExit Timeを同時に持つTransitionを用意し、
    // 「どの条件が未成立なのか」と「Parameterは成立したが時間条件だけ未成立」を個別に診断できることを確認します。
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

    assert(stateMachine.SetTrigger("Jump"));
    assert(stateMachine.SetBool("Grounded", true));
    AnimatorStateMachineRuntimeDebugInfo conditionsMet{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, conditionsMet));
    assert(conditionsMet.Transitions[0].AreConditionsMet);
    assert(conditionsMet.Transitions[0].IsExitTimeMet == false);
    assert(conditionsMet.Transitions[0].IsEligible == false);

    // StateMachine::Update()を呼ぶと成立したTransitionが即座に開始されるため、
    // ここではAnimatorだけを0.55秒進めて「発火直前」のSnapshotを意図的に作ります。
    animator.Update(0.55f);
    AnimatorStateMachineRuntimeDebugInfo eligible{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, eligible));
    assert(eligible.Transitions[0].SourceNormalizedTime >= 0.5f);
    assert(eligible.Transitions[0].IsExitTimeMet);
    assert(eligible.Transitions[0].AreConditionsMet);
    assert(eligible.Transitions[0].IsEligible);
}

void RunTransitionPrioritySelectionRuntimeSnapshotTest()
{
    Animator animator;
    AnimatorStateMachine stateMachine(animator);
    assert(stateMachine.AddBoolParameter("CanTransition", true));
    assert(stateMachine.AddState("Source", MakeClip(1.0f), 0.1f));
    assert(stateMachine.AddState("LowPriority", MakeClip(1.0f), 0.1f));
    assert(stateMachine.AddState("HighPriority", MakeClip(1.0f), 0.1f));

    // 両方のConditionを同時成立させ、Priorityだけが異なるケースです。
    // 登録順ではLowPriorityが先ですが、Runtime規則ではPriorityの高いHighPriorityが最終候補になります。
    AnimatorTransition low{};
    low.FromState = "Source";
    low.ToState = "LowPriority";
    low.Priority = 10;
    low.CrossFadeDuration = 0.15f;
    low.Conditions = { { "CanTransition", AnimatorConditionOperator::Equal, true } };
    assert(stateMachine.AddTransition(low));

    AnimatorTransition high{};
    high.FromState = "Source";
    high.ToState = "HighPriority";
    high.Priority = 100;
    high.CrossFadeDuration = 0.35f;
    high.Conditions = { { "CanTransition", AnimatorConditionOperator::Equal, true } };
    assert(stateMachine.AddTransition(high));
    assert(stateMachine.SetInitialState("Source", true));

    AnimatorStateMachineRuntimeDebugInfo runtime{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, runtime));
    assert(runtime.Transitions.size() == 2);
    assert(runtime.Transitions[0].IsEligible);
    assert(runtime.Transitions[1].IsEligible);
    assert(runtime.Transitions[0].IsSelectedCandidate == false);
    assert(runtime.Transitions[1].IsSelectedCandidate);
    assert(runtime.Transitions[0].Priority == 10);
    assert(runtime.Transitions[1].Priority == 100);
    assert(NearlyEqual(runtime.Transitions[0].CrossFadeDuration, 0.15f));
    assert(NearlyEqual(runtime.Transitions[1].CrossFadeDuration, 0.35f));
}

void RunTransitionSamePriorityTieBreakRuntimeSnapshotTest()
{
    Animator animator;
    AnimatorStateMachine stateMachine(animator);
    assert(stateMachine.AddBoolParameter("CanTransition", true));
    assert(stateMachine.AddState("Source", MakeClip(1.0f), 0.1f));
    assert(stateMachine.AddState("FirstRegistered", MakeClip(1.0f), 0.1f));
    assert(stateMachine.AddState("SecondRegistered", MakeClip(1.0f), 0.1f));

    // 同Priorityの場合、EvaluateTransitions()はPriorityが「より大きい」場合だけ候補を置き換えます。
    // そのため同値なら最初に登録されたTransitionが維持されることをDebug Snapshotでも固定します。
    AnimatorTransition first{};
    first.FromState = "Source";
    first.ToState = "FirstRegistered";
    first.Priority = 50;
    first.Conditions = { { "CanTransition", AnimatorConditionOperator::Equal, true } };
    assert(stateMachine.AddTransition(first));

    AnimatorTransition second{};
    second.FromState = "Source";
    second.ToState = "SecondRegistered";
    second.Priority = 50;
    second.Conditions = { { "CanTransition", AnimatorConditionOperator::Equal, true } };
    assert(stateMachine.AddTransition(second));
    assert(stateMachine.SetInitialState("Source", true));

    AnimatorStateMachineRuntimeDebugInfo runtime{};
    assert(BuildAnimatorStateMachineRuntimeDebugInfo(stateMachine, runtime));
    assert(runtime.Transitions.size() == 2);
    assert(runtime.Transitions[0].IsEligible);
    assert(runtime.Transitions[1].IsEligible);
    assert(runtime.Transitions[0].IsSelectedCandidate);
    assert(runtime.Transitions[1].IsSelectedCandidate == false);
    assert(runtime.Transitions[0].Priority == runtime.Transitions[1].Priority);
}
} // namespace

void RunBlendTreeRuntimeSelfTests()
{
    // Debug API単体 -> 実際の連続Blend -> State Graph -> Condition診断 -> Priority選択の順で、
    // Editor表示に必要なRuntime情報とTransition選択規則を下層から段階的に検証します。
    RunDirectWeightDebugTest();
    RunSmoothSpeedRuntimeTest();
    RunStateGraphRuntimeSnapshotTest();
    RunTransitionConditionRuntimeSnapshotTest();
    RunTransitionPrioritySelectionRuntimeSnapshotTest();
    RunTransitionSamePriorityTieBreakRuntimeSnapshotTest();
}

} // namespace Raven::tests
