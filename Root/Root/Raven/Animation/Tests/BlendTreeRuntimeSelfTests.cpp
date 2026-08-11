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

    // Speed変更ではLocomotion Stateを維持し、Blend Weightだけが変わることを保証します。
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

    // 1秒かけてSpeed 0 -> 2へ滑らかに変化させます。
    for (int frame = 0; frame <= segmentFrames; ++frame)
    {
        const float t = static_cast<float>(frame) / static_cast<float>(segmentFrames);
        ValidateRuntimeSpeed(stateMachine, 2.0f * t, deltaTime);
    }

    // 続けて1秒かけてSpeed 2 -> 6へ変化させ、位相が正常範囲を維持することも確認します。
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

    // TransitionTo直後はAnimatorのCurrent/Nextが同時に存在するため、
    // Graph SnapshotでもCurrent -> Pendingの1本だけがActiveになることを確認します。
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
} // namespace

void RunBlendTreeRuntimeSelfTests()
{
    RunDirectWeightDebugTest();
    RunSmoothSpeedRuntimeTest();
    RunStateGraphRuntimeSnapshotTest();
}

} // namespace Raven::tests
