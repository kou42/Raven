#include "Raven/Editor/Panels/AnimationDebugPanel.h"

#include "Raven/Animation/AnimationRuntimeDebug.h"
#include "Raven/Character/Debug/CharacterControllerDemoLocomotionRuntime.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

namespace Raven
{
namespace
{
const char* ConditionOperatorText(AnimatorConditionOperator conditionOperator)
{
    switch (conditionOperator)
    {
    case AnimatorConditionOperator::Equal:        return "==";
    case AnimatorConditionOperator::NotEqual:     return "!=";
    case AnimatorConditionOperator::Greater:      return ">";
    case AnimatorConditionOperator::GreaterEqual: return ">=";
    case AnimatorConditionOperator::Less:         return "<";
    case AnimatorConditionOperator::LessEqual:    return "<=";
    }

    return "?";
}

const char* StateNameOrNone(const AnimatorStateRuntimeDebugInfo& state)
{
    return state.HasState ? state.StateName.c_str() : "-";
}

const char* LocomotionModeText(CharacterLocomotionRuntimeMode mode)
{
    switch (mode)
    {
    case CharacterLocomotionRuntimeMode::BlendTree:
        return "Blend Tree";
    case CharacterLocomotionRuntimeMode::MotionMatching:
        return "Motion Matching";
    }

    return "Unknown";
}

void DrawVec3(const char* label, const math::Vec3& value)
{
    ImGui::Text("%s: (%.4f, %.4f, %.4f)", label, value.x, value.y, value.z);
}
} // namespace

void AnimationDebugPanel::OnImGuiRender(Scene* scene)
{
    ImGui::Begin("Animation Debug");

    if (scene == nullptr)
    {
        ImGui::TextDisabled("No active scene.");
        ImGui::End();
        return;
    }

    // ========================================================================
    // Character Locomotion Runtime
    // ========================================================================
    // Character DemoのLocomotionはECS AnimatorComponentを経由しないため、StateMachine探索より先に
    // 専用Runtime Snapshotを表示します。Panel側では検索やInertialization計算を再実装しません。
    CharacterControllerDemoLocomotionRuntime* locomotionRuntime =
        CharacterControllerDemoLocomotionRuntime::GetActiveDebugRuntime();

    if (ImGui::CollapsingHeader("Character Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (locomotionRuntime == nullptr)
        {
            ImGui::TextDisabled("No active Character Locomotion Runtime.");
        }
        else
        {
            CharacterLocomotionRuntimeDebugInfo locomotionDebug{};
            const bool hasRuntimeDebug = locomotionRuntime->GetRuntimeDebugInfo(locomotionDebug);
            const CharacterLocomotionRuntimeMode currentMode = locomotionRuntime->GetMode();

            int selectedMode = currentMode == CharacterLocomotionRuntimeMode::MotionMatching ? 1 : 0;
            const char* modeItems[] = { "Blend Tree", "Motion Matching" };
            if (ImGui::Combo("Runtime Mode", &selectedMode, modeItems, 2))
            {
                const CharacterLocomotionRuntimeMode requestedMode = selectedMode == 1
                    ? CharacterLocomotionRuntimeMode::MotionMatching
                    : CharacterLocomotionRuntimeMode::BlendTree;

                if (locomotionRuntime->SetMode(requestedMode, &m_LocomotionRuntimeError) == true)
                {
                    m_LocomotionRuntimeError.clear();
                }
            }

            ImGui::Text("Mode: %s", LocomotionModeText(currentMode));
            ImGui::Text("Runtime: %s",
                hasRuntimeDebug == true && locomotionDebug.Active == true ? "Active" : "Inactive");

            if (m_LocomotionRuntimeError.empty() == false)
            {
                ImGui::TextWrapped("Runtime Error: %s", m_LocomotionRuntimeError.c_str());
            }

            if (hasRuntimeDebug == true
                && currentMode == CharacterLocomotionRuntimeMode::MotionMatching)
            {
                const CharacterMotionMatchingRuntimeDebugInfo& motionMatching =
                    locomotionDebug.MotionMatching;

                ImGui::SeparatorText("Motion Matching");
                ImGui::Text("Selection: %s", motionMatching.HasSelection == true ? "Valid" : "None");

                if (motionMatching.HasSelection == true)
                {
                    ImGui::Text("Selected Frame: %zu", motionMatching.SelectedFrameIndex);
                    ImGui::Text("Clip Index: %u", motionMatching.ClipIndex);
                    ImGui::Text("Clip Time: %.4f", motionMatching.ClipTime);
                    ImGui::Text("Search Cost: %.6f", motionMatching.SearchCost);
                }

                ImGui::Text("Inertialization: %s",
                    motionMatching.Inertializing == true ? "Active" : "Inactive");
                ImGui::Text("Inertialization Time: %.4f", motionMatching.InertializationElapsedTime);

                ImGui::SeparatorText("Bone Inertialization");
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputInt("Bone Index", &m_SelectedInertializationBoneIndex))
                {
                    if (m_SelectedInertializationBoneIndex < 0)
                    {
                        m_SelectedInertializationBoneIndex = 0;
                    }
                }

                PoseInertializerBoneDebugInfo boneDebug{};
                const BoneIndex boneIndex = static_cast<BoneIndex>(m_SelectedInertializationBoneIndex);
                if (locomotionRuntime->GetInertializationBoneDebugInfo(boneIndex, boneDebug) == true)
                {
                    ImGui::Text("Bone: %u", static_cast<unsigned int>(boneDebug.Bone));
                    DrawVec3("Translation Offset", boneDebug.InitialTranslationOffset);
                    DrawVec3("Rotation Offset", boneDebug.InitialRotationOffset);
                    DrawVec3("Linear Velocity Error", boneDebug.InitialLinearVelocityError);
                    DrawVec3("Angular Velocity Error", boneDebug.InitialAngularVelocityError);
                }
                else
                {
                    ImGui::TextDisabled(
                        "Bone diagnostics are available while Pose Inertialization is active.");
                }
            }
            else if (currentMode == CharacterLocomotionRuntimeMode::BlendTree)
            {
                ImGui::TextDisabled(
                    "Motion Matching diagnostics are hidden while Blend Tree mode is active.");
            }
        }
    }

    // ========================================================================
    // Target Animator
    // ========================================================================
    // 現段階ではScene内で最初に見つかった有効なStateMachine付きAnimatorを表示対象にします。
    // bootstrap Overlayと同じ選択規則に揃えることで、移行途中でも両者が同じRuntimeを示します。
    // Scene Hierarchy / Inspector実装後は「選択EntityのAnimator」を優先する形へ拡張します。
    const AnimatorStateMachine* stateMachine = nullptr;
    for (auto [entity, animatorComponent] : scene->View<AnimatorComponent>())
    {
        static_cast<void>(entity);

        if (animatorComponent.Enabled == true && animatorComponent.StateMachine != nullptr)
        {
            stateMachine = animatorComponent.StateMachine.get();
            break;
        }
    }

    if (stateMachine == nullptr)
    {
        ImGui::Separator();
        ImGui::TextDisabled("No enabled Animator StateMachine found.");
        ImGui::End();
        return;
    }

    // Runtime側が公開しているSnapshotを一度構築し、以降のUIはこの値だけを参照します。
    // Transition条件やBlend Tree WeightをPanel側で再計算しないことが重要です。
    AnimatorStateMachineRuntimeDebugInfo runtime{};
    if (BuildAnimatorStateMachineRuntimeDebugInfo(*stateMachine, runtime) == false)
    {
        ImGui::TextDisabled("Failed to build animation runtime debug snapshot.");
        ImGui::End();
        return;
    }

    // ========================================================================
    // Runtime State
    // ========================================================================
    if (ImGui::CollapsingHeader("Runtime State", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Current: %s", StateNameOrNone(runtime.Current));
        ImGui::Text("Pending: %s", StateNameOrNone(runtime.Pending));
        ImGui::Text("Queued: %s", runtime.QueuedStateName.empty() == true ? "-" : runtime.QueuedStateName.c_str());
        ImGui::Text("Normalized Time: %.3f", runtime.Current.NormalizedTime);
        ImGui::Text("Cross Fade: %s", runtime.IsCrossFading == true ? "Active" : "Inactive");
        ImGui::Text("Cross Fade Weight: %.3f", runtime.CrossFadeWeight);
    }

    // ========================================================================
    // Blend Tree
    // ========================================================================
    // Current Stateが1D Blend Treeの場合だけ、Parameter値と各ChildのThreshold/Weightを表示します。
    // WeightはAnimationRuntimeDebugで既に解決済みなのでEditorは表示だけを担当します。
    if (runtime.Current.IsBlendTree == true
        && ImGui::CollapsingHeader("Blend Tree", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Parameter: %s", runtime.Current.BlendParameterName.c_str());
        ImGui::Text("Value: %.3f", runtime.Current.BlendParameterValue);
        ImGui::Separator();

        for (const BlendTree1DChildRuntimeDebugInfo& child : runtime.Current.BlendChildren)
        {
            ImGui::PushID(static_cast<int>(child.ChildIndex));
            ImGui::Text("Child %zu  Threshold %.3f", child.ChildIndex, child.Threshold);
            ImGui::ProgressBar(child.Weight, ImVec2(-1.0f, 0.0f));
            ImGui::PopID();
        }
    }

    // ========================================================================
    // State Machine Nodes
    // ========================================================================
    // Graph描画自体は後で専用Canvasへ発展させますが、まずNode SnapshotをEditor Window内で
    // 確認できるようにし、bootstrap Overlayから情報表示責務を移します。
    if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const AnimatorStateMachineNodeRuntimeDebugInfo& node : runtime.Nodes)
        {
            const char* status = "";
            if (node.IsCurrent == true)
            {
                status = " [CURRENT]";
            }
            else if (node.IsPending == true)
            {
                status = " [PENDING]";
            }
            else if (node.IsQueued == true)
            {
                status = " [QUEUED]";
            }

            ImGui::BulletText("%s%s%s",
                node.StateName.c_str(),
                node.IsBlendTree == true ? " [BLEND TREE]" : "",
                status);
        }
    }

    // ========================================================================
    // Transition Diagnostics
    // ========================================================================
    // Current Stateから出るTransitionを中心に、Runtimeが実際に評価した条件値を表示します。
    // ELIGIBLEとSELECTEDを分けることで、複数条件成立時のPriority競合もEditor上で追跡できます。
    if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t transitionIndex = 0; transitionIndex < runtime.Transitions.size(); ++transitionIndex)
        {
            const AnimatorTransitionRuntimeDebugInfo& transition = runtime.Transitions[transitionIndex];
            if (transition.IsSourceCurrent == false && transition.IsActive == false)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(transitionIndex));

            const char* status = "";
            if (transition.IsActive == true)
            {
                status = " [ACTIVE]";
            }
            else if (transition.IsSelectedCandidate == true)
            {
                status = " [SELECTED]";
            }
            else if (transition.IsEligible == true)
            {
                status = " [ELIGIBLE]";
            }

            if (ImGui::TreeNode("Transition", "%s -> %s%s",
                transition.FromState.c_str(), transition.ToState.c_str(), status))
            {
                ImGui::Text("Priority: %d", transition.Priority);
                ImGui::Text("Cross Fade Duration: %.3f", transition.CrossFadeDuration);

                if (transition.HasExitTime == true)
                {
                    ImGui::Text("Exit Time: %.3f / Source %.3f  %s",
                        transition.ExitTime,
                        transition.SourceNormalizedTime,
                        transition.IsExitTimeMet == true ? "[OK]" : "[NG]");
                }
                else
                {
                    ImGui::TextDisabled("Exit Time: disabled");
                }

                if (transition.Conditions.empty() == true)
                {
                    ImGui::TextDisabled("No conditions.");
                }
                else
                {
                    ImGui::SeparatorText("Conditions");
                    for (const AnimatorConditionRuntimeDebugInfo& condition : transition.Conditions)
                    {
                        if (condition.IsFloat == true)
                        {
                            ImGui::BulletText("%s %s %.3f | Actual %.3f %s",
                                condition.ParameterName.c_str(),
                                ConditionOperatorText(condition.Operator),
                                condition.ExpectedFloat,
                                condition.ActualFloat,
                                condition.IsMet == true ? "[OK]" : "[NG]");
                        }
                        else
                        {
                            ImGui::BulletText("%s %s %s | Actual %s %s",
                                condition.ParameterName.c_str(),
                                ConditionOperatorText(condition.Operator),
                                condition.ExpectedBool == true ? "true" : "false",
                                condition.ActualBool == true ? "true" : "false",
                                condition.IsMet == true ? "[OK]" : "[NG]");
                        }
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    }

    ImGui::End();
}

} // namespace Raven
