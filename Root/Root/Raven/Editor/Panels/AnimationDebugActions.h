#pragma once

#include "Raven/Animation/CharacterLocomotionRuntime.h"

#include <string>

namespace Raven
{

enum class AnimationDebugActionStatus
{
    Succeeded = 0,
    RuntimeUnavailable,
    Failed
};

struct AnimationDebugActionResult
{
    AnimationDebugActionStatus Status = AnimationDebugActionStatus::RuntimeUnavailable;
    std::string Message;
};

// Editor UIからCharacter Demo Runtimeへ送る書き込み操作の唯一の入口です。
// PanelはRuntime pointerを保持せず、実行時に有効な対象を解決して結果だけを受け取ります。
AnimationDebugActionResult SetCharacterLocomotionModeAction(
    CharacterLocomotionRuntimeMode mode);

} // namespace Raven
