#include "Raven/Editor/Panels/AnimationDebugPanel.h"

#include "Raven/Animation/AnimationRuntimeDebug.h"
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
    // Target Animator
    // ========================================================================
    // 現段階ではScene内で最初に見つかった有効なStateMachine付きAnimatorを表示対象にします。
    // bootstrap Overlayと同じ選択規則に揃えることで、移行途中でも両者が同じRuntimeを示します。
    // Scene Hierarchy / Inspector実装後は「選択EntityのAnimator」を優先する形へ拡張します。
    const AnimatorStateMachine* stateMachine = nullptr;
    for (auto [entity, animatorComponent] : scene->View<AnimatorComponent>())
    {
        static_cast<void>(entity);

        if (animatorComponent.Enabled && animatorComponent.StateMachine != nullptr)
        {
            stateMachine = animatorComponent.StateMachine.get();
            break;
        }
    }

    if (stateMachine == nullptr)
    {
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
        ImGui::Text("Queued: %s", runtime.QueuedStateName.empty() ? "-" : runtime.QueuedStateName.c_str());
        ImGui::Text("Normalized Time: %.3f", runtime.Current.NormalizedTime);
        ImGui::Text("Cross Fade: %s", runtime.IsCrossFading ? "Active" : "Inactive");
        ImGui::Text("Cross Fade Weight: %.3f", runtime.CrossFadeWeight);
    }

    // ========================================================================
    // Blend Tree
    // ========================================================================
    // Current Stateが1D Blend Treeの場合だけ、Parameter値と各ChildのThreshold/Weightを表示します。
    // WeightはAnimationRuntimeDebugで既に解決済みなのでEditorは表示だけを担当します。
    if (runtime.Current.IsBlendTree && ImGui::CollapsingHeader("Blend Tree", ImGuiTreeNodeFlags_DefaultOpen))
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
            if (node.IsCurrent)
            {
                status = " [CURRENT]";
            }
            else if (node.IsPending)
            {
                status = " [PENDING]";
            }
            else if (node.IsQueued)
            {
                status = " [QUEUED]";
            }

            ImGui::BulletText("%s%s%s",
                node.StateName.c_str(),
                node.IsBlendTree ? " [BLEND TREE]" : "",
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
            if (transition.IsActive)
            {
                status = " [ACTIVE]";
            }
            else if (transition.IsSelectedCandidate)
            {
                status = " [SELECTED]";
            }
            else if (transition.IsEligible)
            {
                status = " [ELIGIBLE]";
            }

            if (ImGui::TreeNode("Transition", "%s -> %s%s",
                transition.FromState.c_str(), transition.ToState.c_str(), status))
            {
                ImGui::Text("Priority: %d", transition.Priority);
                ImGui::Text("Cross Fade Duration: %.3f", transition.CrossFadeDuration);

                if (transition.HasExitTime)
                {
                    ImGui::Text("Exit Time: %.3f / Source %.3f  %s",
                        transition.ExitTime,
                        transition.SourceNormalizedTime,
                        transition.IsExitTimeMet ? "[OK]" : "[NG]");
                }
                else
                {
                    ImGui::TextDisabled("Exit Time: disabled");
                }

                if (transition.Conditions.empty())
                {
                    ImGui::TextDisabled("No conditions.");
                }
                else
                {
                    ImGui::SeparatorText("Conditions");
                    for (const AnimatorConditionRuntimeDebugInfo& condition : transition.Conditions)
                    {
                        if (condition.IsFloat)
                        {
                            ImGui::BulletText("%s %s %.3f | Actual %.3f %s",
                                condition.ParameterName.c_str(),
                                ConditionOperatorText(condition.Operator),
                                condition.ExpectedFloat,
                                condition.ActualFloat,
                                condition.IsMet ? "[OK]" : "[NG]");
                        }
                        else
                        {
                            ImGui::BulletText("%s %s %s | Actual %s %s",
                                condition.ParameterName.c_str(),
                                ConditionOperatorText(condition.Operator),
                                condition.ExpectedBool ? "true" : "false",
                                condition.ActualBool ? "true" : "false",
                                condition.IsMet ? "[OK]" : "[NG]");
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
