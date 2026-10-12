#include "Raven/Editor/Panels/InspectorRavenPanel.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UICollapsibleSection.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UIWindow.h"

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
    m_TransformSection = AddSection(
        *content, font, "Transform", 3u, m_TransformLabels);
    m_CameraSection = AddSection(*content, font, "Camera", 5u, m_CameraLabels);
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
    m_TransformLabels.clear();
    m_CameraLabels.clear();
    m_RigidBodyLabels.clear();
    m_ColliderLabels.clear();
    m_Snapshot = InspectorSnapshot{};
}

void InspectorRavenPanel::Update(const InspectorSnapshot& snapshot)
{
    m_Snapshot = snapshot;
    UpdateLabels();
}

void InspectorRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
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
    if (m_Snapshot.Transform.has_value() == true)
    {
        const InspectorTransformSnapshot& value = *m_Snapshot.Transform;
        SetLabelText(m_TransformLabels, 0u, "Position: " + FormatVec3(value.Position));
        SetLabelText(m_TransformLabels, 1u, "Rotation: " + FormatVec3(value.Rotation));
        SetLabelText(m_TransformLabels, 2u, "Scale: " + FormatVec3(value.Scale));
    }
    if (m_Snapshot.Camera.has_value() == true)
    {
        const InspectorCameraSnapshot& value = *m_Snapshot.Camera;
        SetLabelText(m_CameraLabels, 0u, "Vertical FOV: " + FormatFloat(value.VerticalFov));
        SetLabelText(m_CameraLabels, 1u, "Near Clip: " + FormatFloat(value.NearClip));
        SetLabelText(m_CameraLabels, 2u, "Far Clip: " + FormatFloat(value.FarClip));
        SetLabelText(m_CameraLabels, 3u, "Aspect Ratio: " + FormatFloat(value.AspectRatio));
        SetLabelText(m_CameraLabels, 4u, std::string("Primary: ") + (value.Primary ? "Yes" : "No"));
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
