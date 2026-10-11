#include "Raven/Editor/Panels/AnimationDebugActions.h"

#include "Raven/Character/Debug/CharacterControllerDemoLocomotionRuntime.h"

#include <utility>

namespace Raven
{

AnimationDebugActionResult SetCharacterLocomotionModeAction(
    CharacterLocomotionRuntimeMode mode)
{
    CharacterControllerDemoLocomotionRuntime* runtime =
        CharacterControllerDemoLocomotionRuntime::GetActiveDebugRuntime();
    if (runtime == nullptr)
    {
        return {
            AnimationDebugActionStatus::RuntimeUnavailable,
            "No active Character Locomotion Runtime."
        };
    }

    std::string error;
    if (runtime->SetMode(mode, &error) == false)
    {
        return {
            AnimationDebugActionStatus::Failed,
            error.empty() == false ? std::move(error) : "Failed to change Runtime Mode."
        };
    }

    return {
        AnimationDebugActionStatus::Succeeded,
        mode == CharacterLocomotionRuntimeMode::MotionMatching
            ? "Runtime Mode changed to Motion Matching."
            : "Runtime Mode changed to Blend Tree."
    };
}

} // namespace Raven
