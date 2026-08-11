// Raven/Animation/AnimatorTransition.h
#pragma once

#include "Raven/Animation/AnimatorParameter.h"

#include <string>
#include <vector>

namespace Raven
{

// ============================================================================
// AnimatorConditionOperator
// ============================================================================
// Transition Conditionで使用する比較演算です。
// Floatは数値比較、Bool/TriggerはEqual / NotEqualを使用します。
enum class AnimatorConditionOperator
{
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual
};

// ============================================================================
// AnimatorCondition
// ============================================================================
// 1つのParameterに対する遷移条件です。
// 例:
//   Speed > 0.1
//   Grounded == true
//   JumpTrigger == true
struct AnimatorCondition
{
    std::string ParameterName;
    AnimatorConditionOperator Operator = AnimatorConditionOperator::Equal;
    AnimatorParameterValue ExpectedValue = false;
};

// ============================================================================
// AnimatorTransition
// ============================================================================
// From StateからTo Stateへ移るための定義です。
// ConditionsはAND条件として扱い、すべて成立した場合のみ遷移します。
//
// Priorityは同じFrom Stateから複数Transitionが同時成立した場合の優先順位です。
// 数値が大きいTransitionを先に評価し、同じPriorityなら登録順を維持します。
// Any Stateのような横断的Transitionを通常Locomotionより優先したい場合に利用します。
//
// HasExitTime=trueの場合はConditionに加えて、遷移元AnimationのNormalized Timeが
// ExitTime以上へ到達していることも要求します。ExitTimeは0.0～1.0の範囲で扱い、
// 0.8なら「Clipを80%再生してから遷移可能」という意味になります。
struct AnimatorTransition
{
    std::string FromState;
    std::string ToState;

    float CrossFadeDuration = 0.2f;

    // Transition評価順です。大きい値ほど高優先度です。
    // 既存Transitionは0のままなので、Priorityを明示しないコードの挙動は登録順のまま維持されます。
    int Priority = 0;

    // Animation時間を遷移条件として使う場合だけtrueにします。
    // falseの場合は従来どおりParameter Conditionだけで遷移を判定します。
    bool HasExitTime = false;

    // 遷移元Clipの正規化再生位置です。0.0=先頭、1.0=末尾です。
    // AddTransition()登録時に0.0～1.0へ収まっているか検証します。
    float ExitTime = 1.0f;

    std::vector<AnimatorCondition> Conditions;
};

} // namespace Raven
