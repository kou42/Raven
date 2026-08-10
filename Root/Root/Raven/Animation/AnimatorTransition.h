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
// Any State相当はまだ導入せず、まず明示的なFrom/Toの最小構成に限定します。
struct AnimatorTransition
{
    std::string FromState;
    std::string ToState;

    float CrossFadeDuration = 0.2f;

    std::vector<AnimatorCondition> Conditions;
};

} // namespace Raven
