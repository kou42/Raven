#include "Raven/Editor/Panels/AnimationDebugSnapshot.h"

#include "Raven/Character/Debug/CharacterControllerDemoLocomotionRuntime.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <algorithm>

namespace Raven
{

AnimationDebugSnapshot BuildAnimationDebugSnapshot(const AnimationDebugSnapshotInput& input)
{
    AnimationDebugSnapshot snapshot{};
    snapshot.SceneAvailable = input.SceneAvailable;
    if (snapshot.SceneAvailable == false)
    {
        return snapshot;
    }

    AnimationLocomotionDebugSnapshot& locomotion = snapshot.Locomotion;
    locomotion.RuntimeAvailable = input.LocomotionRuntimeAvailable;
    locomotion.Mode = input.LocomotionMode;
    if (locomotion.RuntimeAvailable == true && input.LocomotionRuntimeDebug != nullptr)
    {
        locomotion.RuntimeDebugAvailable = true;
        locomotion.RuntimeDebug = *input.LocomotionRuntimeDebug;
    }
    if (locomotion.RuntimeAvailable == true && input.Trajectory != nullptr)
    {
        locomotion.TrajectoryAvailable = true;
        locomotion.Trajectory = *input.Trajectory;
    }
    if (locomotion.RuntimeAvailable == true && input.BoneNames != nullptr)
    {
        locomotion.BoneNames = *input.BoneNames;
        if (locomotion.BoneNames.empty() == false)
        {
            locomotion.SelectedBoneIndex = std::min(
                input.SelectedBoneIndex, locomotion.BoneNames.size() - 1u);
        }
    }
    if (locomotion.BoneNames.empty() == false && input.SelectedBoneDebug != nullptr)
    {
        locomotion.SelectedBoneDebugAvailable = true;
        locomotion.SelectedBoneDebug = *input.SelectedBoneDebug;
    }

    AnimationAnimatorDebugSnapshot& animator = snapshot.Animator;
    animator.AnimatorAvailable = input.AnimatorAvailable;
    if (animator.AnimatorAvailable == true && input.AnimatorRuntimeDebug != nullptr)
    {
        animator.RuntimeDebugAvailable = true;
        animator.RuntimeDebug = *input.AnimatorRuntimeDebug;
    }
    return snapshot;
}

AnimationDebugSnapshot CaptureAnimationDebugSnapshot(
    const Scene* scene, std::size_t selectedBoneIndex)
{
    AnimationDebugSnapshotInput input{};
    input.SceneAvailable = scene != nullptr;
    if (scene == nullptr)
    {
        return BuildAnimationDebugSnapshot(input);
    }

    CharacterLocomotionRuntimeDebugInfo locomotionRuntimeDebug{};
    std::vector<MotionTrajectoryPoint> trajectory;
    std::vector<std::string> boneNames;
    PoseInertializerBoneDebugInfo selectedBoneDebug{};
    CharacterControllerDemoLocomotionRuntime* locomotionRuntime =
        CharacterControllerDemoLocomotionRuntime::GetActiveDebugRuntime();
    if (locomotionRuntime != nullptr)
    {
        input.LocomotionRuntimeAvailable = true;
        input.LocomotionMode = locomotionRuntime->GetMode();
        if (locomotionRuntime->GetRuntimeDebugInfo(locomotionRuntimeDebug) == true)
        {
            input.LocomotionRuntimeDebug = &locomotionRuntimeDebug;
        }
        if (locomotionRuntime->GetMotionMatchingTrajectoryDebugInfo(trajectory) == true)
        {
            input.Trajectory = &trajectory;
        }

        const Skeleton* skeleton = locomotionRuntime->GetMotionMatchingSkeleton();
        if (skeleton != nullptr && skeleton->GetBoneCount() > 0u)
        {
            boneNames.reserve(skeleton->GetBoneCount());
            for (const Bone& bone : skeleton->GetBones())
            {
                boneNames.push_back(bone.Name);
            }
            input.BoneNames = &boneNames;
            input.SelectedBoneIndex = std::min(selectedBoneIndex, boneNames.size() - 1u);
            if (locomotionRuntime->GetInertializationBoneDebugInfo(
                    static_cast<BoneIndex>(input.SelectedBoneIndex), selectedBoneDebug) == true)
            {
                input.SelectedBoneDebug = &selectedBoneDebug;
            }
        }
    }

    // Scene::View()は現時点で非const APIですが、ここではComponentを変更せず対象を読むだけです。
    const AnimatorStateMachine* stateMachine = nullptr;
    Scene& mutableScene = const_cast<Scene&>(*scene);
    for (auto [entity, animatorComponent] : mutableScene.View<AnimatorComponent>())
    {
        static_cast<void>(entity);
        if (animatorComponent.Enabled == true && animatorComponent.StateMachine != nullptr)
        {
            stateMachine = animatorComponent.StateMachine.get();
            break;
        }
    }

    AnimatorStateMachineRuntimeDebugInfo animatorRuntimeDebug{};
    if (stateMachine != nullptr)
    {
        input.AnimatorAvailable = true;
        if (BuildAnimatorStateMachineRuntimeDebugInfo(
                *stateMachine, animatorRuntimeDebug) == true)
        {
            input.AnimatorRuntimeDebug = &animatorRuntimeDebug;
        }
    }
    return BuildAnimationDebugSnapshot(input);
}

} // namespace Raven
