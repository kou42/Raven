// Raven/Animation/AnimationRuntimeDebug.cpp
#include "Raven/Animation/AnimationRuntimeDebug.h"

#include <algorithm>
#include <variant>

namespace Raven
{
namespace
{
// Transition ConditionをEditor側で再評価させないためのRuntime診断関数です。
// StateMachine本体と同じParameter取得API・比較規則を使い、実際の遷移判定とDebug表示の意味がずれないようにします。
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

    // TriggerもStateMachine上ではBool値として読み出せるため、Bool Conditionと同じ経路で現在値を記録します。
    // Trigger消費前後の値もSnapshotに反映されるので、遷移が発火しない原因の追跡に利用できます。
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

// Animatorが現在保持しているStateから、Editor表示に必要な再生情報をSnapshotへ変換します。
// Blend Treeの場合も定義を再計算するのではなく、Animatorが実際に使用しているParameterとBlendTree Debug結果を採用します。
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

    // GetDebugInfo()は実際に補間対象となる左右2 Childを返します。
    // Editorでは全Thresholdを描画したいため、全Childへ展開し、選択されていないChildのWeightを0としてSnapshot化します。
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        float weight = 0.0f;

        // Clamp領域ではLeft/Rightが同じChildを指します。
        // この場合にLeftWeightだけをそのまま使うのではなく、そのChildの表示Weightを明示的に1.0とします。
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

// Transition一覧だけでは孤立Stateを列挙できないため、Graph構築中に参照されたStateを重複なく追加します。
// Current/Pending/Queuedは後段でも追加し、少なくとも実行中StateがGraphから欠落しないようにしています。
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

// State Machine GraphとTransition診断情報を1つのSnapshotへまとめます。
// Editor側はこの結果だけを描画し、Transition ConditionやExit Timeを独自に再評価しないことを前提とします。
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

        // IsActiveは「条件が成立している」ではなく、現在AnimatorがこのFrom -> ToをCrossFadeしていることを表します。
        // IsEligibleとは意味を分けることで、発火直前と発火後の状態をEditorから区別できます。
        debugTransition.IsActive =
            outInfo.IsCrossFading &&
            outInfo.Current.HasState &&
            outInfo.Pending.HasState &&
            transition.FromState == outInfo.Current.StateName &&
            transition.ToState == outInfo.Pending.StateName;

        debugTransition.IsSourceCurrent =
            outInfo.Current.HasState &&
            transition.FromState == outInfo.Current.StateName;

        // Exit Timeは遷移元StateのNormalizedTimeに対して評価されるため、
        // Current以外のTransitionには誤解を招く再生時間を設定せず0.0のままにします。
        debugTransition.SourceNormalizedTime = debugTransition.IsSourceCurrent
            ? outInfo.Current.NormalizedTime
            : 0.0f;

        debugTransition.IsExitTimeMet = transition.HasExitTime == false ||
            (debugTransition.IsSourceCurrent &&
             debugTransition.SourceNormalizedTime >= transition.ExitTime);

        // ConditionsはAND条件です。各Conditionの個別結果を残したうえで、Transition全体の成立状態も保持します。
        // Parameter取得に失敗した場合も成立扱いにはせず、Debug表示から異常を見つけられるようfalseへ倒します。
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
        // したがってIsEligibleは「このFrameで遷移候補になれるか」を表し、IsActiveとは別概念です。
        debugTransition.IsEligible =
            debugTransition.IsSourceCurrent &&
            outInfo.IsCrossFading == false &&
            debugTransition.IsExitTimeMet &&
            debugTransition.AreConditionsMet;

        outInfo.Transitions.push_back(std::move(debugTransition));
    }

    // Transitionを持たない実行中StateもGraph上には必要なので、Runtime参照中のStateを最後に補完します。
    AddNodeIfMissing(stateMachine, outInfo.Current.StateName, outInfo);
    AddNodeIfMissing(stateMachine, outInfo.Pending.StateName, outInfo);
    AddNodeIfMissing(stateMachine, outInfo.QueuedStateName, outInfo);

    // unorderedな内部格納順にEditorレイアウトが引きずられないよう、State名で順序を安定化します。
    // 同じState MachineならFrame間でNode位置が不用意に入れ替わらないことが目的です。
    std::sort(
        outInfo.Nodes.begin(),
        outInfo.Nodes.end(),
        [](const AnimatorStateMachineNodeRuntimeDebugInfo& lhs,
           const AnimatorStateMachineNodeRuntimeDebugInfo& rhs)
        {
            return lhs.StateName < rhs.StateName;
        });

    // Graph Node自身にもRuntime役割を付与し、描画側がState名を再比較しなくても色分けできるようにします。
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

    // Current/PendingはAnimatorが実際に再生しているStateから構築します。
    // StateMachine定義だけから再構築するとCrossFade途中のTime/BlendParameterと食い違うためです。
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

    // Queued/CrossFade情報を先に確定してからGraphを構築します。
    // BuildGraphRuntimeInfo()はこれらを使ってActive TransitionやIsEligibleを判定します。
    outInfo.QueuedStateName = stateMachine.GetQueuedStateName();
    outInfo.IsCrossFading = animator.IsCrossFading();
    outInfo.CrossFadeWeight = animator.GetCrossFadeWeight();

    BuildGraphRuntimeInfo(stateMachine, outInfo);
    return true;
}

} // namespace Raven
