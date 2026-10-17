#include "Raven/Editor/Panels/InspectorRavenPanel.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UICollapsibleSection.h"
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

std::string FormatVec3(const math::Vec3& value)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "(%.3f, %.3f, %.3f)",
        value.x, value.y, value.z);
    return buffer;
}

const char* BodyTypeText(BodyType type)
{
    switch (type)
    {
    case BodyType::Static: return "Static";
    case BodyType::Dynamic: return "Dynamic";
    case BodyType::Kinematic: return "Kinematic";
    }
    return "Unknown";
}

const char* ColliderTypeText(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere: return "Sphere";
    case ColliderType::Box: return "Box";
    case ColliderType::Plane: return "Plane";
    case ColliderType::Capsule: return "Capsule";
    case ColliderType::StaticMesh: return "Static Mesh";
    }
    return "Unknown";
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
    m_RigidBodySection = AddSection(
        *content, font, "Rigid Body", 9u, m_RigidBodyLabels);
    m_ColliderSection = AddSection(
        *content, font, "Collider", 11u, m_ColliderLabels);

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
    if (m_Context != nullptr && m_Root != nullptr)
    {
        m_Context->GetRootElement().RemoveChild(m_Root);
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
    m_RigidBodyLabels.clear();
    m_ColliderLabels.clear();
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
    }
    m_Snapshot = snapshot;
    UpdateLabels();
    SynchronizeTransformInputs(targetChanged);
    SynchronizeCameraInputs(targetChanged);
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

    for (std::size_t index = 0u; index < 2u; ++index)
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
        SetLabelText(m_CameraLabels, 1u, std::string("Primary: ") + (value.Primary ? "Yes" : "No"));
    }
    if (m_Snapshot.RigidBody.has_value() == true)
    {
        const InspectorRigidBodySnapshot& value = *m_Snapshot.RigidBody;
        SetLabelText(m_RigidBodyLabels, 0u, std::string("Type: ") + BodyTypeText(value.Type));
        SetLabelText(m_RigidBodyLabels, 1u, "Mass: " + FormatFloat(value.Mass));
        SetLabelText(m_RigidBodyLabels, 2u, "Linear Velocity: " + FormatVec3(value.LinearVelocity));
        SetLabelText(m_RigidBodyLabels, 3u, "Angular Velocity: " + FormatVec3(value.AngularVelocity));
        SetLabelText(m_RigidBodyLabels, 4u, "Linear Damping: " + FormatFloat(value.LinearDamping));
        SetLabelText(m_RigidBodyLabels, 5u, "Angular Damping: " + FormatFloat(value.AngularDamping));
        SetLabelText(m_RigidBodyLabels, 6u, std::string("Use Gravity: ") + (value.UseGravity ? "Yes" : "No"));
        SetLabelText(m_RigidBodyLabels, 7u, std::string("Allow Sleep: ") + (value.AllowSleep ? "Yes" : "No"));
        SetLabelText(m_RigidBodyLabels, 8u, std::string("Sleeping: ") + (value.IsSleeping ? "Yes" : "No"));
    }
    if (m_Snapshot.Collider.has_value() == true)
    {
        const InspectorColliderSnapshot& value = *m_Snapshot.Collider;
        SetLabelText(m_ColliderLabels, 0u, std::string("Type: ") + ColliderTypeText(value.Type));
        SetLabelText(m_ColliderLabels, 1u, "Offset: " + FormatVec3(value.Offset));
        SetLabelText(m_ColliderLabels, 2u, "Half Extents: " + FormatVec3(value.HalfExtents));
        SetLabelText(m_ColliderLabels, 3u, "Radius: " + FormatFloat(value.Radius));
        SetLabelText(m_ColliderLabels, 4u, "Half Length: " + FormatFloat(value.HalfLength));
        SetLabelText(m_ColliderLabels, 5u, "Plane Normal: " + FormatVec3(value.PlaneNormal));
        SetLabelText(m_ColliderLabels, 6u, "Plane Offset: " + FormatFloat(value.PlaneOffset));
        SetLabelText(m_ColliderLabels, 7u, "Restitution: " + FormatFloat(value.Restitution));
        SetLabelText(m_ColliderLabels, 8u, "Static Friction: " + FormatFloat(value.StaticFriction));
        SetLabelText(m_ColliderLabels, 9u, "Dynamic Friction: " + FormatFloat(value.DynamicFriction));
        SetLabelText(m_ColliderLabels, 10u, std::string("Trigger: ") + (value.IsTrigger ? "Yes" : "No"));
    }
}

} // namespace Raven
