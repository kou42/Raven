#include "Raven/Editor/Panels/AnimationDebugRavenPanel.h"

#include "Raven/Editor/Panels/AnimationDebugActions.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UITable.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <cstdio>
#include <utility>

namespace Raven
{
namespace
{
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

std::string FormatVec3(const math::Vec3& value)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "(%.4f, %.4f, %.4f)", value.x, value.y, value.z);
    return buffer;
}

std::string FormatTrajectoryPoint(const MotionTrajectoryPoint& point)
{
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "+%.2fs  Pos (%.3f, %.3f, %.3f)  Dir (%.3f, %.3f, %.3f)",
        point.TimeOffset,
        point.Position.x, point.Position.y, point.Position.z,
        point.Direction.x, point.Direction.y, point.Direction.z);
    return buffer;
}
} // namespace

bool AnimationDebugRavenPanel::Attach(UIContext& context, const Ref<UIFontAtlas>& font)
{
    if (m_Root != nullptr)
    {
        return false;
    }

    auto window = CreateScope<UIWindow>();
    window->SetTitle("Raven Debug / Animation");
    // Docking shell導入前はStatisticsと少しずらして重ね、狭いWindowでもTitleを画面内へ残します。
    window->SetPosition(math::Vec2(80.0f, 60.0f));
    window->SetSize(math::Vec2(960.0f, 720.0f));
    window->SetPreferredSize(math::Vec2(960.0f, 720.0f));

    auto title = CreateScope<UILabel>();
    title->SetFont(font);
    title->SetText("Raven Debug / Animation");
    title->SetPosition(math::Vec2(12.0f, 3.0f));
    title->SetSize(math::Vec2(920.0f, 24.0f));
    title->SetHitTestVisible(false);
    window->AddChild(std::move(title));

    auto modeControls = CreateScope<UIElement>();
    modeControls->SetPosition(math::Vec2(8.0f, 32.0f));
    modeControls->SetSize(math::Vec2(944.0f, 32.0f));

    auto modeLabel = CreateScope<UILabel>();
    modeLabel->SetFont(font);
    modeLabel->SetText("Runtime Mode");
    modeLabel->SetPosition(math::Vec2(4.0f, 4.0f));
    modeLabel->SetSize(math::Vec2(126.0f, 24.0f));
    modeLabel->SetHitTestVisible(false);
    modeControls->AddChild(std::move(modeLabel));

    auto modeCombo = CreateScope<UIComboBox>();
    modeCombo->SetFont(font);
    modeCombo->SetPosition(math::Vec2(132.0f, 0.0f));
    modeCombo->SetSize(math::Vec2(210.0f, 30.0f));
    modeCombo->SetOptions({ "Blend Tree", "Motion Matching" });
    modeCombo->SetSelectedIndex(0u);
    modeCombo->SetOnSelectionChanged(
        [this](std::size_t optionIndex, const std::string&)
        {
            OnModeSelected(optionIndex);
        });
    m_ModeCombo = static_cast<UIComboBox*>(modeControls->AddChild(std::move(modeCombo)));

    auto actionStatus = CreateScope<UILabel>();
    actionStatus->SetFont(font);
    actionStatus->SetPosition(math::Vec2(356.0f, 4.0f));
    actionStatus->SetSize(math::Vec2(580.0f, 24.0f));
    actionStatus->SetHitTestVisible(false);
    m_ActionStatusLabel = static_cast<UILabel*>(
        modeControls->AddChild(std::move(actionStatus)));
    m_ModeControls = window->AddChild(std::move(modeControls));

    auto table = CreateScope<UITable>();
    table->SetFont(font);
    table->SetPosition(math::Vec2(8.0f, 70.0f));
    table->SetSize(math::Vec2(944.0f, 642.0f));
    table->AddColumn("Section", 160.0f);
    table->AddColumn("Item", 230.0f);
    table->AddColumn("Value", 530.0f);
    table->SetDataSource(
        [this]() { return GetDisplayRowCount(); },
        [this](std::size_t row, std::size_t column)
        {
            return GetCellText(row, column);
        });
    table->SetOnSelectionChanged(
        [this](std::size_t row)
        {
            OnRowSelected(row);
        });
    m_Table = static_cast<UITable*>(window->AddChild(std::move(table)));

    m_Context = &context;
    m_Root = context.GetRootElement().AddChild(std::move(window));
    if (m_Root == nullptr)
    {
        m_Context = nullptr;
        m_ModeControls = nullptr;
        m_ModeCombo = nullptr;
        m_ActionStatusLabel = nullptr;
        m_Table = nullptr;
        return false;
    }

    SynchronizeModeControl();
    RebuildRows();
    return true;
}

void AnimationDebugRavenPanel::Detach()
{
    if (m_Context != nullptr && m_Root != nullptr)
    {
        m_Context->GetRootElement().RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_ModeControls = nullptr;
    m_ModeCombo = nullptr;
    m_ActionStatusLabel = nullptr;
    m_Table = nullptr;
    m_Snapshot = AnimationDebugSnapshot{};
    m_Rows.clear();
    m_ActionMessage.clear();
    m_SelectedBoneIndex = 0u;
    m_SynchronizingModeSelection = false;
}

void AnimationDebugRavenPanel::Update(const AnimationDebugSnapshot& snapshot)
{
    m_Snapshot = snapshot;
    if (m_Snapshot.Locomotion.BoneNames.empty() == false)
    {
        m_SelectedBoneIndex = m_Snapshot.Locomotion.SelectedBoneIndex;
    }
    else
    {
        m_SelectedBoneIndex = 0u;
    }
    SynchronizeModeControl();
    RebuildRows();
    if (m_Table != nullptr)
    {
        m_Table->NotifyDataSourceChanged();
    }
}

void AnimationDebugRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
}

bool AnimationDebugRavenPanel::IsAttached() const
{
    return m_Root != nullptr;
}

bool AnimationDebugRavenPanel::IsVisible() const
{
    return m_Root != nullptr && m_Root->IsVisible() == true;
}

std::size_t AnimationDebugRavenPanel::GetDisplayRowCount() const
{
    return m_Rows.size();
}

std::size_t AnimationDebugRavenPanel::GetSelectedBoneIndex() const
{
    return m_SelectedBoneIndex;
}

void AnimationDebugRavenPanel::RebuildRows()
{
    m_Rows.clear();
    if (m_Snapshot.SceneAvailable == false)
    {
        AddRow("Status", "Scene", "No active scene");
        return;
    }
    AddRow("Status", "Scene", "Active");

    const AnimationLocomotionDebugSnapshot& locomotion = m_Snapshot.Locomotion;
    if (locomotion.RuntimeAvailable == false)
    {
        AddRow("Locomotion", "Runtime", "Unavailable");
    }
    else
    {
        AddRow("Locomotion", "Mode", LocomotionModeText(locomotion.Mode));
        AddRow("Locomotion", "Diagnostics",
            locomotion.RuntimeDebugAvailable == true ? "Available" : "Unavailable");
        if (locomotion.RuntimeDebugAvailable == true)
        {
            const CharacterMotionMatchingRuntimeDebugInfo& motion =
                locomotion.RuntimeDebug.MotionMatching;
            AddRow("Locomotion", "Runtime",
                locomotion.RuntimeDebug.Active == true ? "Active" : "Inactive");
            if (locomotion.Mode == CharacterLocomotionRuntimeMode::MotionMatching)
            {
                AddRow("Motion Matching", "Selection", motion.HasSelection == true ? "Valid" : "None");
                if (motion.HasSelection == true)
                {
                    char buffer[256];
                    std::snprintf(buffer, sizeof(buffer),
                        "Frame %zu / Clip %u @ %.4fs / Cost %.6f",
                        motion.SelectedFrameIndex, motion.ClipIndex, motion.ClipTime, motion.SearchCost);
                    AddRow("Motion Matching", "Selected Motion", buffer);
                }
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%s / %.4fs",
                    motion.Inertializing == true ? "Active" : "Inactive",
                    motion.InertializationElapsedTime);
                AddRow("Motion Matching", "Inertialization", buffer);

                if (locomotion.TrajectoryAvailable == false)
                {
                    AddRow("Trajectory", "Status", "Unavailable");
                }
                else
                {
                    for (std::size_t index = 0u; index < locomotion.Trajectory.size(); ++index)
                    {
                        AddRow("Trajectory", "Point " + std::to_string(index),
                            FormatTrajectoryPoint(locomotion.Trajectory[index]));
                    }
                }

                for (std::size_t index = 0u; index < motion.SearchCandidates.size(); ++index)
                {
                    const MotionSearchCandidateDebugInfo& candidate = motion.SearchCandidates[index];
                    const bool playing = motion.HasSelection == true &&
                        candidate.FrameIndex == motion.SelectedFrameIndex;
                    char candidateText[256];
                    std::snprintf(candidateText, sizeof(candidateText),
                        "Frame %zu / Clip %u @ %.3fs / Cost %.6f%s%s",
                        candidate.FrameIndex, candidate.ClipIndex, candidate.ClipTime, candidate.Cost,
                        index == 0u ? " [BEST]" : "", playing == true ? " [PLAYING]" : "");
                    AddRow("Candidates", "Candidate " + std::to_string(index + 1u), candidateText);

                    if (locomotion.TrajectoryAvailable == true &&
                        candidate.Trajectory.size() == locomotion.Trajectory.size())
                    {
                        for (std::size_t point = 0u; point < candidate.Trajectory.size(); ++point)
                        {
                            const math::Vec3 positionDelta = candidate.Trajectory[point].Position -
                                locomotion.Trajectory[point].Position;
                            const math::Vec3 directionDelta = candidate.Trajectory[point].Direction -
                                locomotion.Trajectory[point].Direction;
                            AddRow("Candidate Delta",
                                std::to_string(index + 1u) + "." + std::to_string(point),
                                "Pos " + FormatVec3(positionDelta) + " / Dir " + FormatVec3(directionDelta));
                        }
                    }
                }
            }
        }
    }

    for (std::size_t index = 0u; index < locomotion.BoneNames.size(); ++index)
    {
        AddBoneRow(index, locomotion.BoneNames[index], index == locomotion.SelectedBoneIndex);
    }
    if (locomotion.SelectedBoneDebugAvailable == true)
    {
        const PoseInertializerBoneDebugInfo& bone = locomotion.SelectedBoneDebug;
        AddRow("Bone Diagnostics", "Translation Offset", FormatVec3(bone.InitialTranslationOffset));
        AddRow("Bone Diagnostics", "Rotation Offset", FormatVec3(bone.InitialRotationOffset));
        AddRow("Bone Diagnostics", "Linear Velocity Error", FormatVec3(bone.InitialLinearVelocityError));
        AddRow("Bone Diagnostics", "Angular Velocity Error", FormatVec3(bone.InitialAngularVelocityError));
    }

    const AnimationAnimatorDebugSnapshot& animator = m_Snapshot.Animator;
    if (animator.AnimatorAvailable == false)
    {
        AddRow("Animator", "State Machine", "No enabled Animator StateMachine");
        return;
    }
    if (animator.RuntimeDebugAvailable == false)
    {
        AddRow("Animator", "Diagnostics", "Snapshot capture failed");
        return;
    }

    const AnimatorStateMachineRuntimeDebugInfo& runtime = animator.RuntimeDebug;
    AddRow("Animator", "Current", runtime.Current.HasState == true ? runtime.Current.StateName : "-");
    AddRow("Animator", "Pending", runtime.Pending.HasState == true ? runtime.Pending.StateName : "-");
    AddRow("Animator", "Queued", runtime.QueuedStateName.empty() == false ? runtime.QueuedStateName : "-");
    char runtimeText[192];
    std::snprintf(runtimeText, sizeof(runtimeText), "Time %.3f / Cross Fade %s / Weight %.3f",
        runtime.Current.NormalizedTime,
        runtime.IsCrossFading == true ? "Active" : "Inactive",
        runtime.CrossFadeWeight);
    AddRow("Animator", "Runtime State", runtimeText);

    if (runtime.Current.IsBlendTree == true)
    {
        char blendText[192];
        std::snprintf(blendText, sizeof(blendText), "%s = %.3f",
            runtime.Current.BlendParameterName.c_str(), runtime.Current.BlendParameterValue);
        AddRow("Blend Tree", "Parameter", blendText);
        for (const BlendTree1DChildRuntimeDebugInfo& child : runtime.Current.BlendChildren)
        {
            char childText[160];
            std::snprintf(childText, sizeof(childText), "Threshold %.3f / Weight %.3f",
                child.Threshold, child.Weight);
            AddRow("Blend Tree", "Child " + std::to_string(child.ChildIndex), childText);
        }
    }

    for (const AnimatorStateMachineNodeRuntimeDebugInfo& node : runtime.Nodes)
    {
        std::string state = node.IsBlendTree == true ? "Blend Tree" : "State";
        if (node.IsCurrent == true)
        {
            state += " [CURRENT]";
        }
        else if (node.IsPending == true)
        {
            state += " [PENDING]";
        }
        else if (node.IsQueued == true)
        {
            state += " [QUEUED]";
        }
        AddRow("States", node.StateName, std::move(state));
    }

    for (const AnimatorTransitionRuntimeDebugInfo& transition : runtime.Transitions)
    {
        if (transition.IsSourceCurrent == false && transition.IsActive == false)
        {
            continue;
        }
        std::string status = "Inactive";
        if (transition.IsActive == true)
        {
            status = "Active";
        }
        else if (transition.IsSelectedCandidate == true)
        {
            status = "Selected";
        }
        else if (transition.IsEligible == true)
        {
            status = "Eligible";
        }
        char transitionText[256];
        std::snprintf(transitionText, sizeof(transitionText),
            "%s / Priority %d / Fade %.3fs / Exit %s",
            status.c_str(), transition.Priority, transition.CrossFadeDuration,
            transition.HasExitTime == true
                ? (transition.IsExitTimeMet == true ? "OK" : "NG")
                : "Disabled");
        const std::string transitionName = transition.FromState + " -> " + transition.ToState;
        AddRow("Transitions", transitionName, transitionText);

        for (const AnimatorConditionRuntimeDebugInfo& condition : transition.Conditions)
        {
            char conditionText[256];
            if (condition.IsFloat == true)
            {
                std::snprintf(conditionText, sizeof(conditionText), "%s %.3f / Actual %.3f [%s]",
                    ConditionOperatorText(condition.Operator), condition.ExpectedFloat,
                    condition.ActualFloat, condition.IsMet == true ? "OK" : "NG");
            }
            else
            {
                std::snprintf(conditionText, sizeof(conditionText), "%s %s / Actual %s [%s]",
                    ConditionOperatorText(condition.Operator),
                    condition.ExpectedBool == true ? "true" : "false",
                    condition.ActualBool == true ? "true" : "false",
                    condition.IsMet == true ? "OK" : "NG");
            }
            AddRow("Conditions", condition.ParameterName, conditionText);
        }
    }
}

void AnimationDebugRavenPanel::AddRow(
    std::string section, std::string item, std::string value)
{
    m_Rows.push_back({ std::move(section), std::move(item), std::move(value) });
}

void AnimationDebugRavenPanel::AddBoneRow(
    std::size_t boneIndex, std::string name, bool selected)
{
    DisplayRow row{};
    row.Section = "Bones";
    row.Item = std::to_string(boneIndex);
    row.Value = std::move(name);
    if (selected == true)
    {
        row.Value += " [SELECTED]";
    }
    row.BoneIndex = boneIndex;
    m_Rows.push_back(std::move(row));
}

std::string AnimationDebugRavenPanel::GetCellText(
    std::size_t row, std::size_t column) const
{
    if (row >= m_Rows.size())
    {
        return {};
    }
    const DisplayRow& displayRow = m_Rows[row];
    switch (column)
    {
    case 0u: return displayRow.Section;
    case 1u: return displayRow.Item;
    case 2u: return displayRow.Value;
    default: return {};
    }
}

void AnimationDebugRavenPanel::OnRowSelected(std::size_t row)
{
    if (row >= m_Rows.size())
    {
        return;
    }
    const std::size_t boneIndex = m_Rows[row].BoneIndex;
    if (boneIndex != std::numeric_limits<std::size_t>::max())
    {
        m_SelectedBoneIndex = boneIndex;
    }
}

void AnimationDebugRavenPanel::OnModeSelected(std::size_t optionIndex)
{
    if (m_SynchronizingModeSelection == true || optionIndex > 1u)
    {
        return;
    }

    const CharacterLocomotionRuntimeMode requestedMode = optionIndex == 1u
        ? CharacterLocomotionRuntimeMode::MotionMatching
        : CharacterLocomotionRuntimeMode::BlendTree;
    const AnimationDebugActionResult result =
        SetCharacterLocomotionModeAction(requestedMode);
    m_ActionMessage = result.Message;
    if (m_ActionStatusLabel != nullptr)
    {
        m_ActionStatusLabel->SetText(m_ActionMessage);
    }

    if (result.Status != AnimationDebugActionStatus::Succeeded)
    {
        // 失敗時は表示選択だけがRuntimeより先行しないよう、同Frame Snapshotの値へ戻します。
        SynchronizeModeControl();
    }
}

void AnimationDebugRavenPanel::SynchronizeModeControl()
{
    if (m_ModeControls == nullptr || m_ModeCombo == nullptr)
    {
        return;
    }

    const bool available = m_Snapshot.SceneAvailable == true &&
        m_Snapshot.Locomotion.RuntimeAvailable == true;
    m_ModeControls->SetVisible(available);
    if (available == false)
    {
        return;
    }

    const std::size_t optionIndex =
        m_Snapshot.Locomotion.Mode == CharacterLocomotionRuntimeMode::MotionMatching ? 1u : 0u;
    m_SynchronizingModeSelection = true;
    m_ModeCombo->SetSelectedIndex(optionIndex);
    m_SynchronizingModeSelection = false;
}

} // namespace Raven
