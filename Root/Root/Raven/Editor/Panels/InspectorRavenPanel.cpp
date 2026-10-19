#include "Raven/Editor/Panels/InspectorRavenPanel.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UICheckbox.h"
#include "Raven/UI/Widgets/UICollapsibleSection.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UIInputNumber.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace Raven
{
namespace
{

std::string FormatFloat(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

void SetLabelText(std::vector<UILabel*>& labels, std::size_t index, std::string text)
{
    if (index < labels.size() && labels[index] != nullptr)
    {
        labels[index]->SetText(std::move(text));
    }
}

} // namespace

bool InspectorRavenPanel::Attach(UIContext& context, const Ref<UIFontAtlas>& font)
{
    if (m_Root != nullptr)
    {
        return false;
    }

    auto window = CreateScope<UIWindow>();
    window->SetTitle("Inspector");
    window->SetPosition(math::Vec2(1040.0f, 24.0f));
    window->SetSize(math::Vec2(360.0f, 720.0f));
    window->SetPreferredSize(math::Vec2(360.0f, 720.0f));

    auto title = CreateScope<UILabel>();
    title->SetFont(font);
    title->SetText("Inspector");
    title->SetPosition(math::Vec2(12.0f, 3.0f));
    title->SetSize(math::Vec2(330.0f, 24.0f));
    title->SetHitTestVisible(false);
    window->AddChild(std::move(title));

    auto scroll = CreateScope<UIScrollView>();
    scroll->SetPosition(math::Vec2(8.0f, 32.0f));
    scroll->SetSize(math::Vec2(344.0f, 680.0f));
    scroll->SetPreferredSize(math::Vec2(344.0f, 680.0f));

    auto content = CreateScope<UIElement>();
    content->SetLayoutMode(UILayoutMode::Vertical);
    content->SetSpacing(6.0f);
    content->SetPadding(4.0f);
    content->SetPreferredSize(math::Vec2(322.0f, 0.0f));

    auto status = CreateScope<UILabel>();
    status->SetFont(font);
    status->SetPreferredSize(math::Vec2(314.0f, 26.0f));
    status->SetHitTestVisible(false);
    m_StatusLabel = static_cast<UILabel*>(content->AddChild(std::move(status)));

    m_TagSection = AddSection(*content, font, "Tag", 1u, m_TagLabels);
    m_TransformSection = AddTransformSection(*content, font);
    m_CameraSection = AddCameraSection(*content, font);
    m_RigidBodySection = AddRigidBodySection(*content, font);
    m_ColliderSection = AddColliderSection(*content, font);

    scroll->SetContent(std::move(content));
    window->AddChild(std::move(scroll));
    m_Context = &context;
    m_Root = context.GetRootElement().AddChild(std::move(window));
    if (m_Root == nullptr)
    {
        Detach();
        return false;
    }
    UpdateLabels();
    return true;
}

void InspectorRavenPanel::Detach()
{
    // Tree切り離しはFocus喪失を発生させるため、終了処理をEditor操作のCommitとして
    // 誤記録しないよう編集Sessionを先に破棄します。
    ResetTransformEdit();
    ResetCameraEdit();
    ResetRigidBodyEdit();
    ResetColliderEdit();
    if (m_Root != nullptr && m_Root->GetParent() != nullptr)
    {
        m_Root->GetParent()->RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_StatusLabel = nullptr;
    m_TagSection = nullptr;
    m_TransformSection = nullptr;
    m_CameraSection = nullptr;
    m_RigidBodySection = nullptr;
    m_ColliderSection = nullptr;
    m_TagLabels.clear();
    m_TransformInputs.clear();
    m_CameraInputs.clear();
    m_CameraLabels.clear();
    m_PrimaryCameraCheckbox = nullptr;
    m_RigidBodyTypeCombo = nullptr;
    m_RigidBodyInputs.clear();
    m_RigidBodyUseGravityCheckbox = nullptr;
    m_RigidBodyAllowSleepCheckbox = nullptr;
    m_RigidBodySleepingLabel = nullptr;
    m_ColliderTypeCombo = nullptr;
    m_ColliderInputs.clear();
    m_ColliderTriggerCheckbox = nullptr;
    m_Snapshot = InspectorSnapshot{};
}

void InspectorRavenPanel::Update(const InspectorSnapshot& snapshot)
{
    const bool targetChanged =
        m_Snapshot.SelectionValid != snapshot.SelectionValid
        || m_Snapshot.Handle != snapshot.Handle;
    if (targetChanged == true)
    {
        // Scene交換、Entity破棄、Generation変更、別Entity選択では、古いBefore値を
        // 新しいEntityのCommitへ持ち越さないよう同期より先に編集Sessionを破棄します。
        ResetTransformEdit();
        ResetCameraEdit();
        ResetRigidBodyEdit();
        ResetColliderEdit();
    }
    m_Snapshot = snapshot;
    UpdateLabels();
    SynchronizeTransformInputs(targetChanged);
    SynchronizeCameraInputs(targetChanged);
    SynchronizeRigidBodyControls(targetChanged);
    SynchronizeColliderControls(targetChanged);
}

void InspectorRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
}

void InspectorRavenPanel::SetTransformPreviewRequestedHandler(
    TransformPreviewRequestedHandler handler)
{
    m_OnTransformPreviewRequested = std::move(handler);
}

void InspectorRavenPanel::SetTransformCommitRequestedHandler(
    TransformCommitRequestedHandler handler)
{
    m_OnTransformCommitRequested = std::move(handler);
}

void InspectorRavenPanel::SetCameraPreviewRequestedHandler(
    CameraPreviewRequestedHandler handler)
{
    m_OnCameraPreviewRequested = std::move(handler);
}

void InspectorRavenPanel::SetCameraCommitRequestedHandler(
    CameraCommitRequestedHandler handler)
{
    m_OnCameraCommitRequested = std::move(handler);
}

void InspectorRavenPanel::SetPrimaryCameraRequestedHandler(
    PrimaryCameraRequestedHandler handler)
{
    m_OnPrimaryCameraRequested = std::move(handler);
}

void InspectorRavenPanel::SetRigidBodyPreviewRequestedHandler(
    RigidBodyPreviewRequestedHandler handler)
{
    m_OnRigidBodyPreviewRequested = std::move(handler);
}

void InspectorRavenPanel::SetRigidBodyCommitRequestedHandler(
    RigidBodyCommitRequestedHandler handler)
{
    m_OnRigidBodyCommitRequested = std::move(handler);
}

void InspectorRavenPanel::SetRigidBodyChangeRequestedHandler(
    RigidBodyChangeRequestedHandler handler)
{
    m_OnRigidBodyChangeRequested = std::move(handler);
}

void InspectorRavenPanel::SetColliderPreviewRequestedHandler(
    ColliderPreviewRequestedHandler handler)
{
    m_OnColliderPreviewRequested = std::move(handler);
}

void InspectorRavenPanel::SetColliderCommitRequestedHandler(
    ColliderCommitRequestedHandler handler)
{
    m_OnColliderCommitRequested = std::move(handler);
}

void InspectorRavenPanel::SetColliderChangeRequestedHandler(
    ColliderChangeRequestedHandler handler)
{
    m_OnColliderChangeRequested = std::move(handler);
}

bool InspectorRavenPanel::IsAttached() const
{
    return m_Root != nullptr;
}

bool InspectorRavenPanel::IsVisible() const
{
    return m_Root != nullptr && m_Root->IsVisible() == true;
}

UICollapsibleSection* InspectorRavenPanel::AddSection(
    UIElement& parent,
    const Ref<UIFontAtlas>& font,
    const std::string& title,
    std::size_t labelCount,
    std::vector<UILabel*>& labels)
{
    auto section = CreateScope<UICollapsibleSection>();
    section->SetPreferredSize(math::Vec2(314.0f, 0.0f));

    auto headerLabel = CreateScope<UILabel>();
    headerLabel->SetFont(font);
    headerLabel->SetText(title);
    headerLabel->SetPosition(math::Vec2(10.0f, 4.0f));
    headerLabel->SetSize(math::Vec2(286.0f, 24.0f));
    headerLabel->SetHitTestVisible(false);
    section->GetHeader()->AddChild(std::move(headerLabel));

    UIElement* content = section->GetContent();
    content->SetSpacing(2.0f);
    content->SetPadding(UIThickness(8.0f, 4.0f));
    for (std::size_t index = 0u; index < labelCount; ++index)
    {
        auto label = CreateScope<UILabel>();
        label->SetFont(font);
        label->SetPreferredSize(math::Vec2(294.0f, 22.0f));
        label->SetHitTestVisible(false);
        labels.push_back(static_cast<UILabel*>(content->AddChild(std::move(label))));
    }
    return static_cast<UICollapsibleSection*>(parent.AddChild(std::move(section)));
}

UICollapsibleSection* InspectorRavenPanel::AddTransformSection(
    UIElement& parent,
    const Ref<UIFontAtlas>& font)
{
    auto section = CreateScope<UICollapsibleSection>();
    section->SetPreferredSize(math::Vec2(314.0f, 0.0f));

    auto headerLabel = CreateScope<UILabel>();
    headerLabel->SetFont(font);
    headerLabel->SetText("Transform");
    headerLabel->SetPosition(math::Vec2(10.0f, 4.0f));
    headerLabel->SetSize(math::Vec2(286.0f, 24.0f));
    headerLabel->SetHitTestVisible(false);
    section->GetHeader()->AddChild(std::move(headerLabel));

    UIElement* content = section->GetContent();
    content->SetSpacing(2.0f);
    content->SetPadding(UIThickness(8.0f, 4.0f));
    AddTransformInput(*content, font, "Position X", 0u, 0.1);
    AddTransformInput(*content, font, "Position Y", 1u, 0.1);
    AddTransformInput(*content, font, "Position Z", 2u, 0.1);
    AddTransformInput(*content, font, "Rotation X", 3u, 0.01);
    AddTransformInput(*content, font, "Rotation Y", 4u, 0.01);
    AddTransformInput(*content, font, "Rotation Z", 5u, 0.01);
    AddTransformInput(*content, font, "Scale X", 6u, 0.1);
    AddTransformInput(*content, font, "Scale Y", 7u, 0.1);
    AddTransformInput(*content, font, "Scale Z", 8u, 0.1);
    return static_cast<UICollapsibleSection*>(parent.AddChild(std::move(section)));
}

void InspectorRavenPanel::AddTransformInput(
    UIElement& parent,
    const Ref<UIFontAtlas>& font,
    const std::string& label,
    std::size_t valueIndex,
    double step)
{
    auto row = CreateScope<UIElement>();
    row->SetLayoutMode(UILayoutMode::Horizontal);
    row->SetSpacing(6.0f);
    row->SetPreferredSize(math::Vec2(294.0f, 30.0f));

    auto caption = CreateScope<UILabel>();
    caption->SetFont(font);
    caption->SetText(label);
    caption->SetPreferredSize(math::Vec2(100.0f, 30.0f));
    caption->SetHitTestVisible(false);
    row->AddChild(std::move(caption));

    auto input = CreateScope<UIInputNumber>();
    input->SetFont(font);
    input->SetStep(step);
    input->SetPreferredSize(math::Vec2(188.0f, 30.0f));
    input->SetOnValueChanged(
        [this, valueIndex](double value)
        {
            PreviewTransformValue(valueIndex, value);
        });
    input->SetOnValueCommitted(
        [this](double, double)
        {
            CommitTransformEdit();
        });
    m_TransformInputs.push_back(
        static_cast<UIInputNumber*>(row->AddChild(std::move(input))));
    parent.AddChild(std::move(row));
}

void InspectorRavenPanel::SynchronizeTransformInputs(bool targetChanged)
{
    if (m_TransformInputs.size() != 9u
        || m_Snapshot.SelectionValid == false
        || m_Snapshot.Transform.has_value() == false)
    {
        return;
    }

    const InspectorTransformSnapshot& transform = *m_Snapshot.Transform;
    const double values[9] =
    {
        transform.Position.x, transform.Position.y, transform.Position.z,
        transform.Rotation.x, transform.Rotation.y, transform.Rotation.z,
        transform.Scale.x, transform.Scale.y, transform.Scale.z
    };

    UIElement* focused = m_Context != nullptr
        ? m_Context->GetFocusedElement()
        : nullptr;
    m_SynchronizingTransformInputs = true;
    for (std::size_t index = 0u; index < m_TransformInputs.size(); ++index)
    {
        UIInputNumber* input = m_TransformInputs[index];
        if (input == nullptr)
        {
            continue;
        }

        // 同じEntityの入力中はSnapshot同期でCaretや未確定文字列を壊しません。
        // 対象が変わった場合だけ、Focus中でも新しいEntity値へ切り替えます。
        const bool editing = focused == &input->GetInputText();
        if (targetChanged == true || editing == false)
        {
            input->SetValue(values[index]);
        }
    }
    m_SynchronizingTransformInputs = false;
}

void InspectorRavenPanel::PreviewTransformValue(
    std::size_t valueIndex,
    double value)
{
    if (m_SynchronizingTransformInputs == true
        || valueIndex >= m_TransformInputs.size()
        || m_Snapshot.SelectionValid == false
        || static_cast<bool>(m_Snapshot.Handle) == false
        || m_Snapshot.Transform.has_value() == false
        || m_OnTransformPreviewRequested == nullptr)
    {
        return;
    }

    if (m_TransformEditActive == false)
    {
        m_TransformEditHandle = m_Snapshot.Handle;
        m_TransformEditBefore = *m_Snapshot.Transform;
        m_TransformEditActive = true;
    }
    else if (m_TransformEditHandle != m_Snapshot.Handle)
    {
        ResetTransformEdit();
        return;
    }

    const InspectorTransformSnapshot original = *m_Snapshot.Transform;
    InspectorTransformSnapshot requested = original;
    float* components[9] =
    {
        &requested.Position.x, &requested.Position.y, &requested.Position.z,
        &requested.Rotation.x, &requested.Rotation.y, &requested.Rotation.z,
        &requested.Scale.x, &requested.Scale.y, &requested.Scale.z
    };
    const float originalComponents[9] =
    {
        original.Position.x, original.Position.y, original.Position.z,
        original.Rotation.x, original.Rotation.y, original.Rotation.z,
        original.Scale.x, original.Scale.y, original.Scale.z
    };
    *components[valueIndex] = static_cast<float>(value);

    if (m_OnTransformPreviewRequested(m_Snapshot.Handle, requested) == true)
    {
        // 次のCharacter入力を直前Previewから継続できるよう、Panel内Snapshotも同期します。
        m_Snapshot.Transform = requested;
        return;
    }

    // Scene交換やEntity削除でActionが拒否された場合、Widgetへ古い表示値を残しません。
    const double previous = static_cast<double>(originalComponents[valueIndex]);
    m_TransformInputs[valueIndex]->SetValue(previous);
    ResetTransformEdit();
}

void InspectorRavenPanel::CommitTransformEdit()
{
    if (m_SynchronizingTransformInputs == true
        || m_TransformEditActive == false)
    {
        return;
    }

    if (m_OnTransformCommitRequested != nullptr)
    {
        // After値はEditorLayerが現在Componentから取得します。PanelがFrame遅延した
        // SnapshotをCommandへ確定して、外部更新を巻き戻すことを防ぎます。
        m_OnTransformCommitRequested(
            m_TransformEditHandle, m_TransformEditBefore);
    }
    ResetTransformEdit();
}

void InspectorRavenPanel::ResetTransformEdit()
{
    m_TransformEditHandle = InvalidEntityHandle;
    m_TransformEditBefore = InspectorTransformSnapshot{};
    m_TransformEditActive = false;
}

UICollapsibleSection* InspectorRavenPanel::AddCameraSection(
    UIElement& parent,
    const Ref<UIFontAtlas>& font)
{
    auto section = CreateScope<UICollapsibleSection>();
    section->SetPreferredSize(math::Vec2(314.0f, 0.0f));

    auto headerLabel = CreateScope<UILabel>();
    headerLabel->SetFont(font);
    headerLabel->SetText("Camera");
    headerLabel->SetPosition(math::Vec2(10.0f, 4.0f));
    headerLabel->SetSize(math::Vec2(286.0f, 24.0f));
    headerLabel->SetHitTestVisible(false);
    section->GetHeader()->AddChild(std::move(headerLabel));

    UIElement* content = section->GetContent();
    content->SetSpacing(2.0f);
    content->SetPadding(UIThickness(8.0f, 4.0f));
    AddCameraInput(*content, font, "Vertical FOV", 0u, 0.005, 0.01, 3.13);
    AddCameraInput(*content, font, "Near Clip", 1u, 0.01, 0.001, 100000.0);
    AddCameraInput(*content, font, "Far Clip", 2u, 0.1, 0.002, 100000.0);

    auto primary = CreateScope<UICheckbox>();
    primary->SetPreferredSize(math::Vec2(294.0f, 30.0f));
    primary->SetCaption("Primary Camera", font);
    primary->SetOnToggle(
        [this](bool checked)
        {
            if (m_SynchronizingCameraInputs == true
                || m_Snapshot.Camera.has_value() == false)
            {
                return;
            }

            // Primaryは全解除可能なboolではなくScene内の排他的な選択です。
            // 既にPrimaryのCameraをOFFへする操作は拒否し、表示をSnapshotへ戻します。
            if (checked == false)
            {
                m_SynchronizingCameraInputs = true;
                m_PrimaryCameraCheckbox->SetChecked(true);
                m_SynchronizingCameraInputs = false;
                return;
            }
            if (m_OnPrimaryCameraRequested != nullptr
                && m_OnPrimaryCameraRequested(m_Snapshot.Handle) == true)
            {
                m_Snapshot.Camera->Primary = true;
                return;
            }
            m_SynchronizingCameraInputs = true;
            m_PrimaryCameraCheckbox->SetChecked(false);
            m_SynchronizingCameraInputs = false;
        });
    m_PrimaryCameraCheckbox = static_cast<UICheckbox*>(
        content->AddChild(std::move(primary)));

    for (std::size_t index = 0u; index < 1u; ++index)
    {
        auto label = CreateScope<UILabel>();
        label->SetFont(font);
        label->SetPreferredSize(math::Vec2(294.0f, 22.0f));
        label->SetHitTestVisible(false);
        m_CameraLabels.push_back(
            static_cast<UILabel*>(content->AddChild(std::move(label))));
    }
    return static_cast<UICollapsibleSection*>(parent.AddChild(std::move(section)));
}

void InspectorRavenPanel::AddCameraInput(
    UIElement& parent,
    const Ref<UIFontAtlas>& font,
    const std::string& label,
    std::size_t valueIndex,
    double step,
    double minimum,
    double maximum)
{
    auto row = CreateScope<UIElement>();
    row->SetLayoutMode(UILayoutMode::Horizontal);
    row->SetSpacing(6.0f);
    row->SetPreferredSize(math::Vec2(294.0f, 30.0f));

    auto caption = CreateScope<UILabel>();
    caption->SetFont(font);
    caption->SetText(label);
    caption->SetPreferredSize(math::Vec2(100.0f, 30.0f));
    caption->SetHitTestVisible(false);
    row->AddChild(std::move(caption));

    auto input = CreateScope<UIInputNumber>();
    input->SetFont(font);
    input->SetStep(step);
    input->SetRange(minimum, maximum);
    input->SetPreferredSize(math::Vec2(188.0f, 30.0f));
    input->SetOnValueChanged(
        [this, valueIndex](double value)
        {
            PreviewCameraValue(valueIndex, value);
        });
    input->SetOnValueCommitted(
        [this](double, double)
        {
            CommitCameraEdit();
        });
    m_CameraInputs.push_back(
        static_cast<UIInputNumber*>(row->AddChild(std::move(input))));
    parent.AddChild(std::move(row));
}

void InspectorRavenPanel::SynchronizeCameraInputs(bool targetChanged)
{
    if (m_CameraInputs.size() != 3u
        || m_Snapshot.SelectionValid == false
        || m_Snapshot.Camera.has_value() == false)
    {
        return;
    }

    const InspectorCameraSnapshot& camera = *m_Snapshot.Camera;
    UIElement* focused = m_Context != nullptr
        ? m_Context->GetFocusedElement()
        : nullptr;
    const bool nearEditing = focused == &m_CameraInputs[1u]->GetInputText();
    const bool farEditing = focused == &m_CameraInputs[2u]->GetInputText();
    constexpr double ClipSeparation = 0.0001;
    m_SynchronizingCameraInputs = true;
    if (m_PrimaryCameraCheckbox != nullptr)
    {
        m_PrimaryCameraCheckbox->SetChecked(camera.Primary);
    }
    if (targetChanged == true || nearEditing == false)
    {
        m_CameraInputs[1u]->SetRange(
            0.001,
            std::max(0.001, static_cast<double>(camera.FarClip) - ClipSeparation));
    }
    if (targetChanged == true || farEditing == false)
    {
        m_CameraInputs[2u]->SetRange(
            static_cast<double>(camera.NearClip) + ClipSeparation,
            100000.0);
    }

    const double values[3] =
    {
        camera.VerticalFov, camera.NearClip, camera.FarClip
    };
    for (std::size_t index = 0u; index < m_CameraInputs.size(); ++index)
    {
        UIInputNumber* input = m_CameraInputs[index];
        if (input == nullptr)
        {
            continue;
        }
        const bool editing = focused == &input->GetInputText();
        if (targetChanged == true || editing == false)
        {
            input->SetValue(values[index]);
        }
    }
    m_SynchronizingCameraInputs = false;
}

void InspectorRavenPanel::PreviewCameraValue(
    std::size_t valueIndex,
    double value)
{
    if (m_SynchronizingCameraInputs == true
        || valueIndex >= m_CameraInputs.size()
        || m_Snapshot.SelectionValid == false
        || static_cast<bool>(m_Snapshot.Handle) == false
        || m_Snapshot.Camera.has_value() == false
        || m_OnCameraPreviewRequested == nullptr)
    {
        return;
    }

    if (m_CameraEditActive == false)
    {
        m_CameraEditHandle = m_Snapshot.Handle;
        m_CameraEditBefore = *m_Snapshot.Camera;
        m_CameraEditActive = true;
    }
    else if (m_CameraEditHandle != m_Snapshot.Handle)
    {
        ResetCameraEdit();
        return;
    }

    const InspectorCameraSnapshot original = *m_Snapshot.Camera;
    InspectorCameraSnapshot requested = original;
    float* values[3] =
    {
        &requested.VerticalFov, &requested.NearClip, &requested.FarClip
    };
    const float previous = *values[valueIndex];
    *values[valueIndex] = static_cast<float>(value);

    if (m_OnCameraPreviewRequested(m_Snapshot.Handle, requested) == true)
    {
        m_Snapshot.Camera = requested;
        return;
    }

    m_CameraInputs[valueIndex]->SetValue(previous);
    ResetCameraEdit();
}

void InspectorRavenPanel::CommitCameraEdit()
{
    if (m_SynchronizingCameraInputs == true
        || m_CameraEditActive == false)
    {
        return;
    }

    if (m_OnCameraCommitRequested != nullptr)
    {
        m_OnCameraCommitRequested(m_CameraEditHandle, m_CameraEditBefore);
    }
    ResetCameraEdit();
}

void InspectorRavenPanel::ResetCameraEdit()
{
    m_CameraEditHandle = InvalidEntityHandle;
    m_CameraEditBefore = InspectorCameraSnapshot{};
    m_CameraEditActive = false;
}

UICollapsibleSection* InspectorRavenPanel::AddRigidBodySection(
    UIElement& parent,
    const Ref<UIFontAtlas>& font)
{
    auto section = CreateScope<UICollapsibleSection>();
    section->SetPreferredSize(math::Vec2(314.0f, 0.0f));

    auto headerLabel = CreateScope<UILabel>();
    headerLabel->SetFont(font);
    headerLabel->SetText("Rigid Body");
    headerLabel->SetPosition(math::Vec2(10.0f, 4.0f));
    headerLabel->SetSize(math::Vec2(286.0f, 24.0f));
    headerLabel->SetHitTestVisible(false);
    section->GetHeader()->AddChild(std::move(headerLabel));

    UIElement* content = section->GetContent();
    content->SetSpacing(2.0f);
    content->SetPadding(UIThickness(8.0f, 4.0f));

    auto typeCombo = CreateScope<UIComboBox>();
    typeCombo->SetFont(font);
    typeCombo->SetPreferredSize(math::Vec2(294.0f, 30.0f));
    typeCombo->SetOptions({ "Static", "Dynamic", "Kinematic" });
    typeCombo->SetOnSelectionChanged(
        [this](std::size_t index, const std::string&)
        {
            if (m_SynchronizingRigidBodyControls == true
                || m_Snapshot.RigidBody.has_value() == false
                || index > static_cast<std::size_t>(BodyType::Kinematic))
            {
                return;
            }
            InspectorRigidBodySnapshot requested = *m_Snapshot.RigidBody;
            requested.Type = static_cast<BodyType>(index);
            if (m_OnRigidBodyChangeRequested != nullptr
                && m_OnRigidBodyChangeRequested(
                    m_Snapshot.Handle, InspectorRigidBodyField::Type, requested) == true)
            {
                m_Snapshot.RigidBody = requested;
                SynchronizeRigidBodyControls(true);
                return;
            }
            SynchronizeRigidBodyControls(true);
        });
    m_RigidBodyTypeCombo = static_cast<UIComboBox*>(
        content->AddChild(std::move(typeCombo)));

    AddRigidBodyInput(*content, font, "Mass", 0u, 0.05, 0.001, 100000.0);
    AddRigidBodyInput(*content, font, "Linear Vel X", 1u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Linear Vel Y", 2u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Linear Vel Z", 3u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Angular Vel X", 4u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Angular Vel Y", 5u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Angular Vel Z", 6u, 0.1, -100000.0, 100000.0);
    AddRigidBodyInput(*content, font, "Linear Damping", 7u, 0.001, 0.0, 1.0);
    AddRigidBodyInput(*content, font, "Angular Damping", 8u, 0.001, 0.0, 1.0);

    auto addCheckbox = [this, &font, content](
        const std::string& caption,
        InspectorRigidBodyField field,
        UICheckbox*& output)
    {
        auto checkbox = CreateScope<UICheckbox>();
        checkbox->SetPreferredSize(math::Vec2(294.0f, 30.0f));
        checkbox->SetCaption(caption, font);
        checkbox->SetOnToggle(
            [this, field](bool checked)
            {
                if (m_SynchronizingRigidBodyControls == true
                    || m_Snapshot.RigidBody.has_value() == false)
                {
                    return;
                }
                InspectorRigidBodySnapshot requested = *m_Snapshot.RigidBody;
                if (field == InspectorRigidBodyField::UseGravity)
                {
                    requested.UseGravity = checked;
                }
                else
                {
                    requested.AllowSleep = checked;
                }
                if (m_OnRigidBodyChangeRequested != nullptr
                    && m_OnRigidBodyChangeRequested(
                        m_Snapshot.Handle, field, requested) == true)
                {
                    m_Snapshot.RigidBody = requested;
                    return;
                }
                SynchronizeRigidBodyControls(true);
            });
        output = static_cast<UICheckbox*>(content->AddChild(std::move(checkbox)));
    };
    addCheckbox(
        "Use Gravity", InspectorRigidBodyField::UseGravity,
        m_RigidBodyUseGravityCheckbox);
    addCheckbox(
        "Allow Sleep", InspectorRigidBodyField::AllowSleep,
        m_RigidBodyAllowSleepCheckbox);

    auto sleeping = CreateScope<UILabel>();
    sleeping->SetFont(font);
    sleeping->SetPreferredSize(math::Vec2(294.0f, 22.0f));
    sleeping->SetHitTestVisible(false);
    m_RigidBodySleepingLabel = static_cast<UILabel*>(
        content->AddChild(std::move(sleeping)));
    return static_cast<UICollapsibleSection*>(parent.AddChild(std::move(section)));
}

void InspectorRavenPanel::AddRigidBodyInput(
    UIElement& parent,
    const Ref<UIFontAtlas>& font,
    const std::string& label,
    std::size_t valueIndex,
    double step,
    double minimum,
    double maximum)
{
    auto row = CreateScope<UIElement>();
    row->SetLayoutMode(UILayoutMode::Horizontal);
    row->SetSpacing(6.0f);
    row->SetPreferredSize(math::Vec2(294.0f, 30.0f));

    auto caption = CreateScope<UILabel>();
    caption->SetFont(font);
    caption->SetText(label);
    caption->SetPreferredSize(math::Vec2(100.0f, 30.0f));
    caption->SetHitTestVisible(false);
    row->AddChild(std::move(caption));

    auto input = CreateScope<UIInputNumber>();
    input->SetFont(font);
    input->SetStep(step);
    input->SetRange(minimum, maximum);
    input->SetPreferredSize(math::Vec2(188.0f, 30.0f));
    input->SetOnValueChanged(
        [this, valueIndex](double value)
        {
            PreviewRigidBodyValue(valueIndex, value);
        });
    input->SetOnValueCommitted(
        [this](double, double)
        {
            CommitRigidBodyEdit();
        });
    m_RigidBodyInputs.push_back(
        static_cast<UIInputNumber*>(row->AddChild(std::move(input))));
    parent.AddChild(std::move(row));
}

void InspectorRavenPanel::SynchronizeRigidBodyControls(bool targetChanged)
{
    if (m_RigidBodyInputs.size() != 9u
        || m_Snapshot.RigidBody.has_value() == false)
    {
        return;
    }

    const InspectorRigidBodySnapshot& value = *m_Snapshot.RigidBody;
    const double values[9] =
    {
        value.Mass,
        value.LinearVelocity.x, value.LinearVelocity.y, value.LinearVelocity.z,
        value.AngularVelocity.x, value.AngularVelocity.y, value.AngularVelocity.z,
        value.LinearDamping, value.AngularDamping
    };
    UIElement* focused = m_Context != nullptr ? m_Context->GetFocusedElement() : nullptr;
    m_SynchronizingRigidBodyControls = true;
    m_RigidBodyTypeCombo->SetSelectedIndex(static_cast<std::size_t>(value.Type));
    for (std::size_t index = 0u; index < m_RigidBodyInputs.size(); ++index)
    {
        UIInputNumber* input = m_RigidBodyInputs[index];
        const bool editing = focused == &input->GetInputText();
        if (targetChanged == true || editing == false)
        {
            input->SetValue(values[index]);
        }
    }
    m_RigidBodyUseGravityCheckbox->SetChecked(value.UseGravity);
    m_RigidBodyAllowSleepCheckbox->SetChecked(value.AllowSleep);
    // Static Body中のSetMass()は保持Massを0へ失うため、編集欄を表示しません。
    // 将来Disabled visualが共通化された場合は表示を残して無効化へ切り替えます。
    m_RigidBodyInputs[0u]->GetParent()->SetVisible(value.Type != BodyType::Static);
    m_SynchronizingRigidBodyControls = false;
}

void InspectorRavenPanel::PreviewRigidBodyValue(
    std::size_t valueIndex,
    double value)
{
    if (m_SynchronizingRigidBodyControls == true
        || valueIndex >= m_RigidBodyInputs.size()
        || m_Snapshot.SelectionValid == false
        || m_Snapshot.Handle.IsValid() == false
        || m_Snapshot.RigidBody.has_value() == false
        || m_OnRigidBodyPreviewRequested == nullptr)
    {
        return;
    }

    const InspectorRigidBodyField fields[9] =
    {
        InspectorRigidBodyField::Mass,
        InspectorRigidBodyField::LinearVelocity,
        InspectorRigidBodyField::LinearVelocity,
        InspectorRigidBodyField::LinearVelocity,
        InspectorRigidBodyField::AngularVelocity,
        InspectorRigidBodyField::AngularVelocity,
        InspectorRigidBodyField::AngularVelocity,
        InspectorRigidBodyField::LinearDamping,
        InspectorRigidBodyField::AngularDamping
    };
    const InspectorRigidBodyField field = fields[valueIndex];
    if (m_RigidBodyEditActive == true
        && m_RigidBodyEditHandle != m_Snapshot.Handle)
    {
        ResetRigidBodyEdit();
        return;
    }
    if (m_RigidBodyEditActive == true && m_RigidBodyEditField != field)
    {
        CommitRigidBodyEdit();
    }
    if (m_RigidBodyEditActive == false)
    {
        m_RigidBodyEditHandle = m_Snapshot.Handle;
        m_RigidBodyEditBefore = *m_Snapshot.RigidBody;
        m_RigidBodyEditField = field;
        m_RigidBodyEditActive = true;
    }

    const InspectorRigidBodySnapshot original = *m_Snapshot.RigidBody;
    InspectorRigidBodySnapshot requested = original;
    float* components[9] =
    {
        &requested.Mass,
        &requested.LinearVelocity.x, &requested.LinearVelocity.y, &requested.LinearVelocity.z,
        &requested.AngularVelocity.x, &requested.AngularVelocity.y, &requested.AngularVelocity.z,
        &requested.LinearDamping, &requested.AngularDamping
    };
    const float previous = *components[valueIndex];
    *components[valueIndex] = static_cast<float>(value);
    if (m_OnRigidBodyPreviewRequested(m_Snapshot.Handle, field, requested) == true)
    {
        m_Snapshot.RigidBody = requested;
        return;
    }
    m_RigidBodyInputs[valueIndex]->SetValue(previous);
    ResetRigidBodyEdit();
}

void InspectorRavenPanel::CommitRigidBodyEdit()
{
    if (m_SynchronizingRigidBodyControls == true || m_RigidBodyEditActive == false)
    {
        return;
    }
    if (m_OnRigidBodyCommitRequested != nullptr)
    {
        m_OnRigidBodyCommitRequested(
            m_RigidBodyEditHandle, m_RigidBodyEditField, m_RigidBodyEditBefore);
    }
    ResetRigidBodyEdit();
}

void InspectorRavenPanel::ResetRigidBodyEdit()
{
    m_RigidBodyEditHandle = InvalidEntityHandle;
    m_RigidBodyEditBefore = InspectorRigidBodySnapshot{};
    m_RigidBodyEditField = InspectorRigidBodyField::Mass;
    m_RigidBodyEditActive = false;
}

UICollapsibleSection* InspectorRavenPanel::AddColliderSection(
    UIElement& parent,
    const Ref<UIFontAtlas>& font)
{
    auto section = CreateScope<UICollapsibleSection>();
    section->SetPreferredSize(math::Vec2(314.0f, 0.0f));

    auto headerLabel = CreateScope<UILabel>();
    headerLabel->SetFont(font);
    headerLabel->SetText("Collider");
    headerLabel->SetPosition(math::Vec2(10.0f, 4.0f));
    headerLabel->SetSize(math::Vec2(286.0f, 24.0f));
    headerLabel->SetHitTestVisible(false);
    section->GetHeader()->AddChild(std::move(headerLabel));

    UIElement* content = section->GetContent();
    content->SetSpacing(2.0f);
    content->SetPadding(UIThickness(8.0f, 4.0f));

    auto typeCombo = CreateScope<UIComboBox>();
    typeCombo->SetFont(font);
    typeCombo->SetPreferredSize(math::Vec2(294.0f, 30.0f));
    typeCombo->SetOptions({ "Sphere", "Box", "Plane", "Capsule", "Static Mesh" });
    typeCombo->SetOnSelectionChanged(
        [this](std::size_t index, const std::string&)
        {
            if (m_SynchronizingColliderControls == true
                || m_Snapshot.Collider.has_value() == false
                || index > static_cast<std::size_t>(ColliderType::StaticMesh))
            {
                return;
            }
            InspectorColliderSnapshot requested = *m_Snapshot.Collider;
            requested.Type = static_cast<ColliderType>(index);
            if (m_OnColliderChangeRequested != nullptr
                && m_OnColliderChangeRequested(
                    m_Snapshot.Handle, InspectorColliderField::Type, requested) == true)
            {
                m_Snapshot.Collider = requested;
                SynchronizeColliderControls(true);
                return;
            }
            SynchronizeColliderControls(true);
        });
    m_ColliderTypeCombo = static_cast<UIComboBox*>(
        content->AddChild(std::move(typeCombo)));

    AddColliderInput(*content, font, "Offset X", 0u, 0.1, -100000.0, 100000.0);
    AddColliderInput(*content, font, "Offset Y", 1u, 0.1, -100000.0, 100000.0);
    AddColliderInput(*content, font, "Offset Z", 2u, 0.1, -100000.0, 100000.0);
    AddColliderInput(*content, font, "Half Extents X", 3u, 0.05, 0.001, 100000.0);
    AddColliderInput(*content, font, "Half Extents Y", 4u, 0.05, 0.001, 100000.0);
    AddColliderInput(*content, font, "Half Extents Z", 5u, 0.05, 0.001, 100000.0);
    AddColliderInput(*content, font, "Radius", 6u, 0.01, 0.001, 100000.0);
    AddColliderInput(*content, font, "Half Length", 7u, 0.01, 0.0, 100000.0);
    AddColliderInput(*content, font, "Plane Normal X", 8u, 0.1, -1.0, 1.0);
    AddColliderInput(*content, font, "Plane Normal Y", 9u, 0.1, -1.0, 1.0);
    AddColliderInput(*content, font, "Plane Normal Z", 10u, 0.1, -1.0, 1.0);
    AddColliderInput(*content, font, "Plane Offset", 11u, 0.05, -100000.0, 100000.0);
    AddColliderInput(*content, font, "Restitution", 12u, 0.01, 0.0, 1.0);
    AddColliderInput(*content, font, "Static Friction", 13u, 0.01, 0.0, 10.0);
    AddColliderInput(*content, font, "Dynamic Friction", 14u, 0.01, 0.0, 10.0);

    auto trigger = CreateScope<UICheckbox>();
    trigger->SetPreferredSize(math::Vec2(294.0f, 30.0f));
    trigger->SetCaption("Is Trigger", font);
    trigger->SetOnToggle(
        [this](bool checked)
        {
            if (m_SynchronizingColliderControls == true
                || m_Snapshot.Collider.has_value() == false)
            {
                return;
            }
            InspectorColliderSnapshot requested = *m_Snapshot.Collider;
            requested.IsTrigger = checked;
            if (m_OnColliderChangeRequested != nullptr
                && m_OnColliderChangeRequested(
                    m_Snapshot.Handle, InspectorColliderField::IsTrigger, requested) == true)
            {
                m_Snapshot.Collider = requested;
                return;
            }
            SynchronizeColliderControls(true);
        });
    m_ColliderTriggerCheckbox = static_cast<UICheckbox*>(
        content->AddChild(std::move(trigger)));
    return static_cast<UICollapsibleSection*>(parent.AddChild(std::move(section)));
}

void InspectorRavenPanel::AddColliderInput(
    UIElement& parent,
    const Ref<UIFontAtlas>& font,
    const std::string& label,
    std::size_t valueIndex,
    double step,
    double minimum,
    double maximum)
{
    auto row = CreateScope<UIElement>();
    row->SetLayoutMode(UILayoutMode::Horizontal);
    row->SetSpacing(6.0f);
    row->SetPreferredSize(math::Vec2(294.0f, 30.0f));
    auto caption = CreateScope<UILabel>();
    caption->SetFont(font);
    caption->SetText(label);
    caption->SetPreferredSize(math::Vec2(100.0f, 30.0f));
    caption->SetHitTestVisible(false);
    row->AddChild(std::move(caption));
    auto input = CreateScope<UIInputNumber>();
    input->SetFont(font);
    input->SetStep(step);
    input->SetRange(minimum, maximum);
    input->SetPreferredSize(math::Vec2(188.0f, 30.0f));
    input->SetOnValueChanged(
        [this, valueIndex](double value)
        {
            PreviewColliderValue(valueIndex, value);
        });
    input->SetOnValueCommitted(
        [this](double, double)
        {
            CommitColliderEdit();
        });
    m_ColliderInputs.push_back(
        static_cast<UIInputNumber*>(row->AddChild(std::move(input))));
    parent.AddChild(std::move(row));
}

void InspectorRavenPanel::SynchronizeColliderControls(bool targetChanged)
{
    if (m_ColliderInputs.size() != 15u || m_Snapshot.Collider.has_value() == false)
    {
        return;
    }
    const InspectorColliderSnapshot& value = *m_Snapshot.Collider;
    const double values[15] =
    {
        value.Offset.x, value.Offset.y, value.Offset.z,
        value.HalfExtents.x, value.HalfExtents.y, value.HalfExtents.z,
        value.Radius, value.HalfLength,
        value.PlaneNormal.x, value.PlaneNormal.y, value.PlaneNormal.z,
        value.PlaneOffset, value.Restitution,
        value.StaticFriction, value.DynamicFriction
    };
    UIElement* focused = m_Context != nullptr ? m_Context->GetFocusedElement() : nullptr;
    m_SynchronizingColliderControls = true;
    m_ColliderTypeCombo->SetSelectedIndex(static_cast<std::size_t>(value.Type));
    for (std::size_t index = 0u; index < m_ColliderInputs.size(); ++index)
    {
        UIInputNumber* input = m_ColliderInputs[index];
        const bool editing = focused == &input->GetInputText();
        if (targetChanged == true || editing == false)
        {
            input->SetValue(values[index]);
        }
    }
    m_ColliderTriggerCheckbox->SetChecked(value.IsTrigger);
    const bool sphere = value.Type == ColliderType::Sphere;
    const bool box = value.Type == ColliderType::Box;
    const bool plane = value.Type == ColliderType::Plane;
    const bool capsule = value.Type == ColliderType::Capsule;
    for (std::size_t index = 3u; index <= 5u; ++index)
    {
        m_ColliderInputs[index]->GetParent()->SetVisible(box);
    }
    m_ColliderInputs[6u]->GetParent()->SetVisible(sphere || capsule);
    m_ColliderInputs[7u]->GetParent()->SetVisible(capsule);
    for (std::size_t index = 8u; index <= 10u; ++index)
    {
        m_ColliderInputs[index]->GetParent()->SetVisible(plane);
    }
    m_ColliderInputs[11u]->GetParent()->SetVisible(plane);
    m_SynchronizingColliderControls = false;
}

void InspectorRavenPanel::PreviewColliderValue(
    std::size_t valueIndex,
    double value)
{
    if (m_SynchronizingColliderControls == true
        || valueIndex >= m_ColliderInputs.size()
        || m_Snapshot.SelectionValid == false
        || m_Snapshot.Handle.IsValid() == false
        || m_Snapshot.Collider.has_value() == false
        || m_OnColliderPreviewRequested == nullptr)
    {
        return;
    }
    const InspectorColliderField fields[15] =
    {
        InspectorColliderField::Offset,
        InspectorColliderField::Offset,
        InspectorColliderField::Offset,
        InspectorColliderField::HalfExtents,
        InspectorColliderField::HalfExtents,
        InspectorColliderField::HalfExtents,
        InspectorColliderField::Radius,
        InspectorColliderField::HalfLength,
        InspectorColliderField::PlaneNormal,
        InspectorColliderField::PlaneNormal,
        InspectorColliderField::PlaneNormal,
        InspectorColliderField::PlaneOffset,
        InspectorColliderField::Restitution,
        InspectorColliderField::StaticFriction,
        InspectorColliderField::DynamicFriction
    };
    const InspectorColliderField field = fields[valueIndex];
    if (m_ColliderEditActive == true
        && m_ColliderEditHandle != m_Snapshot.Handle)
    {
        ResetColliderEdit();
        return;
    }
    if (m_ColliderEditActive == true && m_ColliderEditField != field)
    {
        CommitColliderEdit();
    }
    if (m_ColliderEditActive == false)
    {
        m_ColliderEditHandle = m_Snapshot.Handle;
        m_ColliderEditBefore = *m_Snapshot.Collider;
        m_ColliderEditField = field;
        m_ColliderEditActive = true;
    }

    const InspectorColliderSnapshot original = *m_Snapshot.Collider;
    InspectorColliderSnapshot requested = original;
    float* components[15] =
    {
        &requested.Offset.x, &requested.Offset.y, &requested.Offset.z,
        &requested.HalfExtents.x, &requested.HalfExtents.y, &requested.HalfExtents.z,
        &requested.Radius, &requested.HalfLength,
        &requested.PlaneNormal.x, &requested.PlaneNormal.y, &requested.PlaneNormal.z,
        &requested.PlaneOffset, &requested.Restitution,
        &requested.StaticFriction, &requested.DynamicFriction
    };
    const float previous = *components[valueIndex];
    *components[valueIndex] = static_cast<float>(value);
    if (m_OnColliderPreviewRequested(m_Snapshot.Handle, field, requested) == true)
    {
        m_Snapshot.Collider = requested;
        return;
    }
    m_ColliderInputs[valueIndex]->SetValue(previous);
    ResetColliderEdit();
}

void InspectorRavenPanel::CommitColliderEdit()
{
    if (m_SynchronizingColliderControls == true || m_ColliderEditActive == false)
    {
        return;
    }
    if (m_OnColliderCommitRequested != nullptr)
    {
        m_OnColliderCommitRequested(
            m_ColliderEditHandle, m_ColliderEditField, m_ColliderEditBefore);
    }
    ResetColliderEdit();
}

void InspectorRavenPanel::ResetColliderEdit()
{
    m_ColliderEditHandle = InvalidEntityHandle;
    m_ColliderEditBefore = InspectorColliderSnapshot{};
    m_ColliderEditField = InspectorColliderField::Offset;
    m_ColliderEditActive = false;
}

void InspectorRavenPanel::UpdateLabels()
{
    if (m_StatusLabel == nullptr)
    {
        return;
    }

    if (m_Snapshot.HasSelection == false)
    {
        m_StatusLabel->SetText("Select an Entity in Scene Hierarchy.");
    }
    else if (m_Snapshot.SelectionValid == false)
    {
        m_StatusLabel->SetText("Selected Entity is no longer valid.");
    }
    else
    {
        m_StatusLabel->SetText(
            "Entity " + std::to_string(m_Snapshot.Handle.m_Index)
            + "  Generation " + std::to_string(m_Snapshot.Handle.m_Generation));
    }

    const bool valid = m_Snapshot.SelectionValid;
    m_TagSection->SetVisible(valid == true && m_Snapshot.Tag.has_value() == true);
    m_TransformSection->SetVisible(valid == true && m_Snapshot.Transform.has_value() == true);
    m_CameraSection->SetVisible(valid == true && m_Snapshot.Camera.has_value() == true);
    m_RigidBodySection->SetVisible(valid == true && m_Snapshot.RigidBody.has_value() == true);
    m_ColliderSection->SetVisible(valid == true && m_Snapshot.Collider.has_value() == true);

    if (m_Snapshot.Tag.has_value() == true)
    {
        SetLabelText(m_TagLabels, 0u, "Name: " + *m_Snapshot.Tag);
    }
    if (m_Snapshot.Camera.has_value() == true)
    {
        const InspectorCameraSnapshot& value = *m_Snapshot.Camera;
        SetLabelText(m_CameraLabels, 0u, "Aspect Ratio: " + FormatFloat(value.AspectRatio));
    }
    if (m_Snapshot.RigidBody.has_value() == true
        && m_RigidBodySleepingLabel != nullptr)
    {
        const InspectorRigidBodySnapshot& value = *m_Snapshot.RigidBody;
        m_RigidBodySleepingLabel->SetText(
            std::string("Sleeping: ") + (value.IsSleeping ? "Yes" : "No"));
    }
}

} // namespace Raven
