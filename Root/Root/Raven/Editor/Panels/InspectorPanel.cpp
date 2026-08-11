#include "Raven/Editor/Panels/InspectorPanel.h"

#include "Raven/Scene/Components.h"

#include <algorithm>
#include <cstring>

#include <imgui.h>

namespace Raven
{
namespace
{
void DrawVec3Control(const char* label, math::Vec3& value, float speed = 0.1f)
{
    // RavenのVec3内部表現へEditorが依存しすぎないよう、ImGuiへ渡す値は一度ローカル配列へ
    // 展開します。編集確定後にVec3へ戻すことで、InspectorのUI責務を明確に保ちます。
    float values[3] = { value.x, value.y, value.z };
    if (ImGui::DragFloat3(label, values, speed))
    {
        value.x = values[0];
        value.y = values[1];
        value.z = values[2];
    }
}

const char* BodyTypeName(BodyType type)
{
    switch (type)
    {
    case BodyType::Static:    return "Static";
    case BodyType::Dynamic:   return "Dynamic";
    case BodyType::Kinematic: return "Kinematic";
    }

    return "Unknown";
}

const char* ColliderTypeName(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere: return "Sphere";
    case ColliderType::Box:    return "Box";
    case ColliderType::Plane:  return "Plane";
    }

    return "Unknown";
}

void DrawTagComponent(Entity entity)
{
    if (entity.HasComponent<TagComponent>() == false)
    {
        return;
    }

    TagComponent& tag = entity.GetComponent<TagComponent>();

    // std::stringの内部bufferをImGuiへ直接渡すとcapacity管理が必要になるため、固定長の編集用
    // bufferを介します。Entity名として十分な長さを確保し、確定時だけstd::stringへ反映します。
    char buffer[256]{};
    const std::size_t copyLength = std::min(tag.Tag.size(), sizeof(buffer) - 1);
    std::memcpy(buffer, tag.Tag.data(), copyLength);
    buffer[copyLength] = '\0';

    if (ImGui::InputText("Name", buffer, sizeof(buffer)))
    {
        tag.Tag = buffer;
    }
}

void DrawTransformComponent(Entity entity)
{
    if (entity.HasComponent<TransformComponent>() == false)
    {
        return;
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) == false)
    {
        return;
    }

    TransformComponent& transform = entity.GetComponent<TransformComponent>();

    DrawVec3Control("Position", transform.Position);
    DrawVec3Control("Rotation", transform.Rotation);
    DrawVec3Control("Scale", transform.Scale);
}

void DrawRigidBodyComponent(Entity entity)
{
    if (entity.HasComponent<RigidBodyComponent>() == false)
    {
        return;
    }

    if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen) == false)
    {
        return;
    }

    RigidBodyComponent& rigidBody = entity.GetComponent<RigidBodyComponent>();

    // BodyType変更はSetBodyType()を必ず経由します。
    // Staticへ変更した際のVelocity/Force初期化やInverseMass更新をInspector側で複製すると、
    // Runtime実装変更時にEditorだけ不整合になるためです。
    int bodyTypeIndex = static_cast<int>(rigidBody.Type);
    const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
    if (ImGui::Combo("Body Type", &bodyTypeIndex, bodyTypes, 3))
    {
        rigidBody.SetBodyType(static_cast<BodyType>(bodyTypeIndex));
    }

    ImGui::TextDisabled("Current: %s", BodyTypeName(rigidBody.Type));

    // MassとInverseMassは常に対応している必要があるためSetMass()経由で更新します。
    // Static BodyではSetMass()がMass=0 / InverseMass=0を維持します。
    float mass = rigidBody.Mass;
    if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0f, 100000.0f))
    {
        rigidBody.SetMass(mass);
    }

    DrawVec3Control("Linear Velocity", rigidBody.LinearVelocity);
    DrawVec3Control("Angular Velocity", rigidBody.AngularVelocity);

    ImGui::DragFloat("Linear Damping", &rigidBody.LinearDamping, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Angular Damping", &rigidBody.AngularDamping, 0.001f, 0.0f, 1.0f);
    ImGui::Checkbox("Use Gravity", &rigidBody.UseGravity);
    ImGui::Checkbox("Allow Sleep", &rigidBody.AllowSleep);

    // IsSleepingはPhysicsWorldが管理するRuntime状態でもあるため、初期Inspectorではread-only表示にします。
    // Editorから直接変更する場合はWakeUp/Sleepの正式APIをPhysicsWorld側へ用意してから扱います。
    ImGui::Text("Sleeping: %s", rigidBody.IsSleeping ? "Yes" : "No");
}

void DrawColliderComponent(Entity entity)
{
    if (entity.HasComponent<ColliderComponent>() == false)
    {
        return;
    }

    if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen) == false)
    {
        return;
    }

    ColliderComponent& collider = entity.GetComponent<ColliderComponent>();

    int colliderTypeIndex = static_cast<int>(collider.Type);
    const char* colliderTypes[] = { "Sphere", "Box", "Plane" };
    if (ImGui::Combo("Collider Type", &colliderTypeIndex, colliderTypes, 3))
    {
        collider.Type = static_cast<ColliderType>(colliderTypeIndex);
    }

    ImGui::TextDisabled("Current: %s", ColliderTypeName(collider.Type));
    DrawVec3Control("Offset", collider.Offset);

    // Collider Typeごとに意味のあるパラメータだけを表示します。
    // 無関係な値を同時に見せないことで、どの値が実際のCollision Shapeへ使われるかを明確にします。
    if (collider.Type == ColliderType::Sphere)
    {
        ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 100000.0f);
    }
    else if (collider.Type == ColliderType::Box)
    {
        DrawVec3Control("Half Extents", collider.HalfExtents, 0.05f);
    }
    else if (collider.Type == ColliderType::Plane)
    {
        DrawVec3Control("Plane Normal", collider.PlaneNormal);
        ImGui::DragFloat("Plane Offset", &collider.PlaneOffset, 0.05f);
    }

    ImGui::SeparatorText("Material");
    ImGui::DragFloat("Restitution", &collider.Restitution, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Static Friction", &collider.StaticFriction, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Dynamic Friction", &collider.DynamicFriction, 0.01f, 0.0f, 10.0f);
    ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
}
} // namespace

void InspectorPanel::OnImGuiRender(Entity selectedEntity)
{
    ImGui::Begin("Inspector");

    if (static_cast<bool>(selectedEntity) == false)
    {
        ImGui::TextDisabled("Select an Entity in Scene Hierarchy.");
        ImGui::End();
        return;
    }

    Scene* scene = selectedEntity.GetScene();
    if (scene == nullptr || scene->IsEntityAlive(selectedEntity) == false)
    {
        ImGui::TextDisabled("Selected Entity is no longer valid.");
        ImGui::End();
        return;
    }

    // HeaderではEntity Handleも表示します。
    // Index再利用時のGeneration変化をEditorから確認できるため、ECS Debugにも利用できます。
    ImGui::Text("Entity %u", selectedEntity.GetIndex());
    ImGui::SameLine();
    ImGui::TextDisabled("Generation %u", selectedEntity.GetGeneration());
    ImGui::Separator();

    DrawTagComponent(selectedEntity);
    ImGui::Separator();
    DrawTransformComponent(selectedEntity);
    DrawRigidBodyComponent(selectedEntity);
    DrawColliderComponent(selectedEntity);

    ImGui::End();
}

} // namespace Raven
