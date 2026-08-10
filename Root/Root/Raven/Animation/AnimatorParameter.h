// Raven/Animation/AnimatorParameter.h
#pragma once

#include <string>
#include <variant>

namespace Raven
{

// ============================================================================
// AnimatorParameter
// ============================================================================
// State MachineのTransition Conditionから参照するRuntime Parameterです。
//
// 最初の段階ではLocomotionに必要なfloatに加え、今後のGrounded/Crouching/Jumpを見据えて
// bool / Triggerも扱える形にします。Triggerは「一度消費されたらfalseへ戻るイベント値」で、
// 通常のboolとは意味が異なるためTypeを分けています。
enum class AnimatorParameterType
{
    Float,
    Bool,
    Trigger
};

using AnimatorParameterValue = std::variant<float, bool>;

struct AnimatorParameter
{
    std::string Name;
    AnimatorParameterType Type = AnimatorParameterType::Float;
    AnimatorParameterValue Value = 0.0f;
};

} // namespace Raven
