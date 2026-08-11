// Raven/Animation/AnimationRuntimeDebug.h
#pragma once

#include "Raven/Animation/AnimatorStateMachine.h"
#include "Raven/Animation/BlendTree1D.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Raven
{

// Editorで1D Blend Tree全体を描くためのChild単位Runtime情報です。
// Weightは現在Poseへ寄与していないChildでは0になります。
struct BlendTree1DChildRuntimeDebugInfo
{
    std::size_t ChildIndex = 0;
    float Threshold = 0.0f;
    float Weight = 0.0f;
};

// State Machine Graphの1 Nodeを表すSnapshotです。
// EditorはCurrent/Pending/Queued判定を再実装せず、このフラグだけでNode色を決定できます。
struct AnimatorStateMachineNodeRuntimeDebugInfo
{
    std::string StateName;
    bool IsBlendTree = false;
    bool IsCurrent = false;
    bool IsPending = false;
    bool IsQueued = false;
};

// Transition Condition 1件のRuntime評価結果です。
// Editor側でParameterを再取得・再比較せず、Runtimeと同じ演算規則の結果をそのまま表示します。
struct AnimatorConditionRuntimeDebugInfo
{
    std::string ParameterName;
    AnimatorConditionOperator Operator = AnimatorConditionOperator::Equal;

    // Float Conditionの場合に使用します。
    bool IsFloat = false;
    float ActualFloat = 0.0f;
    float ExpectedFloat = 0.0f;

    // Bool / Trigger Conditionの場合に使用します。
    bool ActualBool = false;
    bool ExpectedBool = false;

    // このCondition単体が現在値で成立しているかを表します。
    bool IsMet = false;
};

// State Machine Graphの1 Transitionを表すSnapshotです。
// IsActiveは現在CrossFadeしているCurrent -> Pendingだけtrueになります。
struct AnimatorTransitionRuntimeDebugInfo
{
    std::string FromState;
    std::string ToState;
    bool IsActive = false;
    bool HasExitTime = false;
    float ExitTime = 0.0f;
    int Priority = 0;
    float CrossFadeDuration = 0.0f;

    // Transition発火理由をEditorから確認するためのRuntime診断値です。
    // Parameter比較とExit Timeを別々に保持することで「何が足りないか」を特定できます。
    std::vector<AnimatorConditionRuntimeDebugInfo> Conditions;
    bool AreConditionsMet = false;
    float SourceNormalizedTime = 0.0f;
    bool IsExitTimeMet = true;
    bool IsSourceCurrent = false;

    // EvaluateTransitions()と同じ前提で、このTransitionが現在Frameに候補になれるかを示します。
    // CrossFade中は新しい自動Transitionを開始しないためfalseになります。
    bool IsEligible = false;

    // 同Frameに複数TransitionがEligibleになった場合、実際のEvaluateTransitions()と同じ
    // Priority降順・同Priorityは登録順という規則で最終選択される1本だけtrueになります。
    // Editorはこの値を見ることで「条件は成立しているのに、なぜ別Transitionが選ばれたか」を説明できます。
    bool IsSelectedCandidate = false;
};

// ============================================================================
// AnimatorStateRuntimeDebugInfo
// ============================================================================
// EditorがState / Blend Treeを表示するために必要なRuntime情報を1つにまとめたSnapshotです。
// EditorはAnimator / StateMachine内部へ個別アクセスせず、この構造体だけを描画入力にできます。
struct AnimatorStateRuntimeDebugInfo
{
    bool HasState = false;
    std::string StateName;
    AnimatorMotionType MotionType = AnimatorMotionType::None;

    float Time = 0.0f;
    float NormalizedTime = 0.0f;

    bool IsBlendTree = false;
    std::string BlendParameterName;
    float BlendParameterValue = 0.0f;
    BlendTree1DDebugInfo BlendTree;

    // Threshold軸・Weight BarをEditor側だけで描けるよう、全Child情報もSnapshot化します。
    // Blend解決ロジック自体はRuntime側のGetDebugInfo()結果を利用し、UI側で再計算しません。
    std::vector<BlendTree1DChildRuntimeDebugInfo> BlendChildren;
};

struct AnimatorStateMachineRuntimeDebugInfo
{
    AnimatorStateRuntimeDebugInfo Current;
    AnimatorStateRuntimeDebugInfo Pending;

    std::string QueuedStateName;

    bool IsCrossFading = false;
    float CrossFadeWeight = 0.0f;

    // State Machine Editor/Overlay用Graph情報です。
    // NodeはState名順で安定ソートし、TransitionはRuntime登録順を維持します。
    std::vector<AnimatorStateMachineNodeRuntimeDebugInfo> Nodes;
    std::vector<AnimatorTransitionRuntimeDebugInfo> Transitions;
};

// StateMachine / Animatorが既に公開しているRuntime情報からEditor向けSnapshotを構築します。
// Animation本体にEditor依存を入れないため、描画コードはこの関数の戻り値だけを参照してください。
bool BuildAnimatorStateMachineRuntimeDebugInfo(
    const AnimatorStateMachine& stateMachine,
    AnimatorStateMachineRuntimeDebugInfo& outInfo);

} // namespace Raven
