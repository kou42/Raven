// Raven/Animation/AnimationRuntimeDebug.cpp
#include "Raven/Animation/AnimationRuntimeDebug.h"

#include <algorithm>
#include <variant>

namespace Raven
{
namespace
{
bool EvaluateConditionForDebug(
    const AnimatorStateMachine& stateMachine,
    const AnimatorCondition& condition,
    AnimatorConditionRuntimeDebugInfo& outInfo)
{
    outInfo = {};
    outInfo.ParameterName = condition.ParameterName;
    outInfo.Operator = condition.Operator;

    // AddTransition()時点でExpectedValue型とParameter型の整合性は検証済みです。
    // Debug側では同じGetFloat/GetBool APIと同じ比較演算を使い、Runtime評価との食い違いを防ぎます。
    if (const float* expected = std::get_if<float>(&condition.ExpectedValue))
    {
        outInfo.IsFloat = true;
        outInfo.ExpectedFloat = *expected;

        float actual = 0.0f;
        if (stateMachine.GetFloat(condition.ParameterName, actual) == false)
        {
            return false;
        }

        outInfo.ActualFloat = actual;
        switch (condition.Operator)
        {
        case AnimatorConditionOperator::Equal:        outInfo.IsMet = actual == *expected; break;
        case AnimatorConditionOperator::NotEqual:     outInfo.IsMet = actual != *expected; break;
        case AnimatorConditionOperator::Greater:      outInfo.IsMet = actual > *expected; break;
        case AnimatorConditionOperator::GreaterEqual: outInfo.IsMet = actual >= *expected; break;
        case AnimatorConditionOperator::Less:         outInfo.IsMet = actual < *expected; break;
        case AnimatorConditionOperator::LessEqual:    outInfo.IsMet = actual <= *expected; break;
        }
        return true;
    }

    const bool* expected = std::get_if<bool>(&condition.ExpectedValue);
    if (expected == nullptr)
    {
        return false;
    }

    bool actual = false;
    if (stateMachine.GetBool(condition.ParameterName, actual) == false)
    {
        return false;
    }

    outInfo.ActualBool = actual;
    outInfo.ExpectedBool = *expected;

    switch (condition.Operator)
    {
    case AnimatorConditionOperator::Equal:    outInfo.IsMet = actual == *expected; break;
    case AnimatorConditionOperator::NotEqual: outInfo.IsMet = actual != *expected; break;
    default:                                  outInfo.IsMet = false; break;
    }
    return true;
}

bool BuildStateRuntimeInfo(
    const AnimatorStateMachine& stateMachine,
    const std::string& stateName,
    const AnimatorState& animatorState,
    AnimatorStateRuntimeDebugInfo& outInfo)
{
    outInfo = {};

    if (stateName.empty() || animatorState.IsValid() == false)
    {
        return false;
    }

    outInfo.HasState = true;
    outInfo.StateName = stateName;
    outInfo.MotionType = animatorState.MotionType;
    outInfo.Time = animatorState.Time;
    outInfo.NormalizedTime = animatorState.NormalizedTime;

    const AnimatorStateDefinition* stateDefinition = stateMachine.FindState(stateName);
    if (stateDefinition == nullptr)
    {
        return true;
    }

    if (animatorState.MotionType != AnimatorMotionType::BlendTree1D ||
        animatorState.BlendTree == nullptr)
    {
        return true;
    }

    outInfo.IsBlendTree = true;
    outInfo.BlendParameterName = stateDefinition->BlendParameterName;
    outInfo.BlendParameterValue = animatorState.BlendParameter;

    if (animatorState.BlendTree->GetDebugInfo(
            animatorState.BlendParameter,
            outInfo.BlendTree) == false)
    {
        return true;
    }

    const auto& children = animatorState.BlendTree->GetChildren();
    outInfo.BlendChildren.reserve(children.size());

    for (std::size_t i = 0; i < children.size(); ++i)
    {
        float weight = 0.0f;

        if (outInfo.BlendTree.LeftChildIndex == outInfo.BlendTree.RightChildIndex)
        {
            if (i == outInfo.BlendTree.LeftChildIndex)
            {
                weight = 1.0f;
            }
        }
        else if (i == outInfo.BlendTree.LeftChildIndex)
        {
            weight = outInfo.BlendTree.LeftWeight;
        }
        else if (i == outInfo.BlendTree.RightChildIndex)
        {
            weight = outInfo.BlendTree.RightWeight;
        }

        outInfo.BlendChildren.push_back({ i, children[i].Threshold, weight });
    }

    return true;
}

void AddNodeIfMissing(
    const AnimatorStateMachine& stateMachine,
    const std::string& stateName,
    AnimatorStateMachineRuntimeDebugInfo& outInfo)
{
    if (stateName.empty())
    {
        return;
    }

    const auto found = std::find_if(
        outInfo.Nodes.begin(),
        outInfo.Nodes.end(),
        [&stateName](const AnimatorStateMachineNodeRuntimeDebugInfo& node)
        {
            return node.StateName == stateName;
        });

    if (found != outInfo.Nodes.end())
    {
        return;
    }

    AnimatorStateMachineNodeRuntimeDebugInfo node{};
    node.StateName = stateName;

    const AnimatorStateDefinition* definition = stateMachine.FindState(stateName);
    if (definition != nullptr)
    {
        node.IsBlendTree = definition->IsBlendTreeState();
    }

    outInfo.Nodes.push_back(std::move(node));
}

void BuildGraphRuntimeInfo(
    const AnimatorStateMachine& stateMachine,
    AnimatorStateMachineRuntimeDebugInfo& outInfo)
{
    const auto& transitions = stateMachine.GetTransitions();
    outInfo.Transitions.reserve(transitions.size());

    for (const AnimatorTransition& transition : transitions)
    {
        AddNodeIfMissing(stateMachine, transition.FromState, outInfo);
        AddNodeIfMissing(stateMachine, transition.ToState, outInfo);

        AnimatorTransitionRuntimeDebugInfo debugTransition{};
        debugTransition.FromState = transition.FromState;
        debugTransition.ToState = transition.ToState;
        debugTransition.HasExitTime = transition.HasExitTime;
        debugTransition.ExitTime = transition.ExitTime;
        debugTransition.Priority = transition.Priority;
        debugTransition.IsActive =
            outInfo.IsCrossFading &&
            outInfo.Current.HasState &&
            outInfo.Pending.HasState &&
            transition.FromState == outInfo.Current.StateName &&
            transition.ToState == outInfo.Pending.StateName;

        debugTransition.IsSourceCurrent =
            outInfo.Current.HasState &&
            transition.FromState == outInfo.Current.StateName;

        debugTransition.SourceNormalizedTime = debugTransition.IsSourceCurrent
            ? outInfo.Current.NormalizedTime
            : 0.0f;

        debugTransition.IsExitTimeMet = transition.HasExitTime == false ||
            (debugTransition.IsSourceCurrent &&
             debugTransition.SourceNormalizedTime >= transition.ExitTime);

        debugTransition.Conditions.reserve(transition.Conditions.size());
        debugTransition.AreConditionsMet = true;

        for (const AnimatorCondition& condition : transition.Conditions)
        {
            AnimatorConditionRuntimeDebugInfo conditionInfo{};
            const bool valid = EvaluateConditionForDebug(stateMachine, condition, conditionInfo);
            if (valid == false || conditionInfo.IsMet == false)
            {
                debugTransition.AreConditionsMet = false;
            }
            debugTransition.Conditions.push_back(std::move(conditionInfo));
        }

        // EvaluateTransitions()はCurrent Stateから出るTransitionだけを評価し、
        // CrossFade中は新規Transitionを開始しません。DebugのEligibleも同じ前提に揃えます。
        debugTransition.IsEligible =
            debugTransition.IsSourceCurrent &&
            outInfo.IsCrossFading == false &&
            debugTransition.IsExitTimeMet &&
            debugTransition.AreConditionsMet;

        outInfo.Transitions.push_back(std::move(debugTransition));
    }

    AddNodeIfMissing(stateMachine, outInfo.Current.StateName, outInfo);
    AddNodeIfMissing(stateMachine, outInfo.Pending.StateName, outInfo);
    AddNodeIfMissing(stateMachine, outInfo.QueuedStateName, outInfo);

    std::sort(
        outInfo.Nodes.begin(),
        outInfo.Nodes.end(),
        [](const AnimatorStateMachineNodeRuntimeDebugInfo& lhs,
           const AnimatorStateMachineNodeRuntimeDebugInfo& rhs)
        {
            return lhs.StateName < rhs.StateName;
        });

    for (auto& node : outInfo.Nodes)
    {
        node.IsCurrent = outInfo.Current.HasState && node.StateName == outInfo.Current.StateName;
        node.IsPending = outInfo.Pending.HasState && node.StateName == outInfo.Pending.StateName;
        node.IsQueued = outInfo.QueuedStateName.empty() == false && node.StateName == outInfo.QueuedStateName;
    }
}
} // namespace

bool BuildAnimatorStateMachineRuntimeDebugInfo(
    const AnimatorStateMachine& stateMachine,
    AnimatorStateMachineRuntimeDebugInfo& outInfo)
{
    outInfo = {};

    const Animator& animator = stateMachine.GetAnimator();

    if (stateMachine.HasCurrentState())
    {
        BuildStateRuntimeInfo(
            stateMachine,
            stateMachine.GetCurrentStateName(),
            animator.GetCurrentState(),
            outInfo.Current);
    }

    if (stateMachine.GetPendingStateName().empty() == false)
    {
        BuildStateRuntimeInfo(
            stateMachine,
            stateMachine.GetPendingStateName(),
            animator.GetNextState(),
            outInfo.Pending);
    }

    outInfo.QueuedStateName = stateMachine.GetQueuedStateName();
    outInfo.IsCrossFading = animator.IsCrossFading();
    outInfo.CrossFadeWeight = animator.GetCrossFadeWeight();

    BuildGraphRuntimeInfo(stateMachine, outInfo);
    return true;
}

} // namespace Raven
