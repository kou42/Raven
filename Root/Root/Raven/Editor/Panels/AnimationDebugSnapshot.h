#pragma once

#include "Raven/Animation/AnimationRuntimeDebug.h"
#include "Raven/Animation/CharacterLocomotionRuntime.h"
#include "Raven/Animation/PoseInertializer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Raven
{

class Scene;

struct AnimationLocomotionDebugSnapshot
{
    bool RuntimeAvailable = false;
    CharacterLocomotionRuntimeMode Mode = CharacterLocomotionRuntimeMode::BlendTree;
    bool RuntimeDebugAvailable = false;
    CharacterLocomotionRuntimeDebugInfo RuntimeDebug;

    bool TrajectoryAvailable = false;
    std::vector<MotionTrajectoryPoint> Trajectory;

    // Skeleton自体への参照をPanelへ渡さず、選択UIに必要な安定した名前だけをコピーします。
    std::vector<std::string> BoneNames;
    std::size_t SelectedBoneIndex = 0u;
    bool SelectedBoneDebugAvailable = false;
    PoseInertializerBoneDebugInfo SelectedBoneDebug;
};

struct AnimationAnimatorDebugSnapshot
{
    // AnimatorAvailableとRuntimeDebugAvailableを分けることで、対象なしと構築失敗を区別します。
    bool AnimatorAvailable = false;
    bool RuntimeDebugAvailable = false;
    AnimatorStateMachineRuntimeDebugInfo RuntimeDebug;
};

struct AnimationDebugSnapshot
{
    bool SceneAvailable = false;
    AnimationLocomotionDebugSnapshot Locomotion;
    AnimationAnimatorDebugSnapshot Animator;
};

// Runtime依存のCapture処理と値コピーを分離するための入力です。
// Pointerが指す値はBuild中だけ参照し、返却Snapshotはすべて自己完結したコピーになります。
struct AnimationDebugSnapshotInput
{
    bool SceneAvailable = false;

    bool LocomotionRuntimeAvailable = false;
    CharacterLocomotionRuntimeMode LocomotionMode = CharacterLocomotionRuntimeMode::BlendTree;
    const CharacterLocomotionRuntimeDebugInfo* LocomotionRuntimeDebug = nullptr;
    const std::vector<MotionTrajectoryPoint>* Trajectory = nullptr;
    const std::vector<std::string>* BoneNames = nullptr;
    std::size_t SelectedBoneIndex = 0u;
    const PoseInertializerBoneDebugInfo* SelectedBoneDebug = nullptr;

    bool AnimatorAvailable = false;
    const AnimatorStateMachineRuntimeDebugInfo* AnimatorRuntimeDebug = nullptr;
};

AnimationDebugSnapshot BuildAnimationDebugSnapshot(const AnimationDebugSnapshotInput& input);
AnimationDebugSnapshot CaptureAnimationDebugSnapshot(
    const Scene* scene, std::size_t selectedBoneIndex);

} // namespace Raven
