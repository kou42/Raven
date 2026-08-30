#include "Raven/Editor/Panels/InspectorPanel.h"

#include "Raven/Editor/Command/RenameEntityCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include <imgui.h>

namespace Raven
{
namespace
{
struct RenameEditState
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    std::string Before;
    bool IsActive = false;
};

RenameEditState s_RenameEditState{};

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
    case ColliderType::Sphere:  return "Sphere";
    case ColliderType::Box:     return "Box";
    case ColliderType::Capsule: return "Capsule";
    case ColliderType::Plane:   return "Plane";
    }

    return "Unknown";
}

// ============================================================================
// ColliderType <-> Inspector表示順
// ============================================================================
// ColliderTypeは既存保存データとの互換性のため Sphere=0 / Box=1 / Plane=2 を維持し、
// Capsuleは末尾へ追加しています。一方Inspectorでは関連する有限形状を並べて
// Sphere / Box / Capsule / Plane の順に表示したいため、enum値をそのままCombo indexとして
// 使用しません。
//
// この明示変換を挟むことで、将来ColliderTypeへ新形状を追加しても「enumへ挿入した位置」と
// 「Editor上の表示順」が暗黙に結び付くことを防げます。
int ColliderTypeToInspectorIndex(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere:  return 0;
    case ColliderType::Box:     return 1;
    case ColliderType::Capsule: return 2;
    case ColliderType::Plane:   return 3;
    }

    return 0;
}

ColliderType InspectorIndexToColliderType(int index)
{
    switch (index)
    {
    case 0: return ColliderType::Sphere;
    case 1: return ColliderType::Box;
    case 2: return ColliderType::Capsule;
    case 3: return ColliderType::Plane;
    default: return ColliderType::Box;
    }
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

    const std::string nameBeforeThisFrame = tag.Tag;
    if (ImGui::InputText("Name", buffer, sizeof(buffer)))
    {
        // 編集中はHierarchy表示へ即時反映します。ただし各文字を履歴へ積まず、
        // InputTextが非Activeになった時点で編集開始名と終了名を1 Commandへまとめます。
        tag.Tag = buffer;
    }

    if (ImGui::IsItemActivated())
    {
        s_RenameEditState.Handle = entity.GetHandle();
        s_RenameEditState.TargetScene = entity.GetScene();
        s_RenameEditState.Before = nameBeforeThisFrame;
        s_RenameEditState.IsActive = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()
        && s_RenameEditState.IsActive == true)
    {
        const bool isSameEntity =
            s_RenameEditState.Handle == entity.GetHandle()
            && s_RenameEditState.TargetScene == entity.GetScene();

        if (isSameEntity == true)
        {
            // TagはInputTextにより既に変更済みなので、Gizmoと同じ実行済み経路へ登録します。
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<RenameEntityCommand>(
                    entity,
                    s_RenameEditState.Before,
                    tag.Tag));
        }

        s_RenameEditState = RenameEditState{};
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

void DrawCameraComponent(Entity entity)
{
    if (entity.HasComponent<CameraComponent>() == false)
    {
        return;
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen) == false)
    {
        return;
    }

    CameraComponent& cameraComponent = entity.GetComponent<CameraComponent>();
    SceneCamera& camera = cameraComponent.Camera;

    // ========================================================================
    // Primary Camera
    // ========================================================================
    // PrimaryはScene内で排他的な状態として扱います。
    // boolをCheckboxで直接編集すると複数Cameraが同時にPrimaryになったり、意図せず全解除できるため、
    // Inspectorからは「Make Primary」という操作としてSceneCameraSystemへ依頼します。
    if (cameraComponent.Primary)
    {
        ImGui::Text("Primary Camera");
    }
    else
    {
        if (ImGui::Button("Make Primary"))
        {
            Scene* scene = entity.GetScene();
            if (scene != nullptr)
            {
                SceneCameraSystem::SetPrimaryCamera(*scene, entity);
            }
        }
    }

    // SceneCameraのProjection値は必ずSetPerspective()経由で変更します。
    // Inspector側からメンバ値を直接書き換えるとProjection再計算や入力値検証を迂回してしまうためです。
    float verticalFov = camera.GetPerspectiveVerticalFov();
    float nearClip = camera.GetPerspectiveNearClip();
    float farClip = camera.GetPerspectiveFarClip();

    bool projectionChanged = false;
    if (ImGui::DragFloat("Vertical FOV (rad)", &verticalFov, 0.005f, 0.01f, 3.13f))
    {
        projectionChanged = true;
    }

    if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, farClip))
    {
        projectionChanged = true;
    }

    if (ImGui::DragFloat("Far Clip", &farClip, 0.1f, nearClip, 100000.0f))
    {
        projectionChanged = true;
    }

    if (projectionChanged)
    {
        camera.SetPerspective(verticalFov, nearClip, farClip);
    }

    ImGui::TextDisabled("Aspect Ratio: %.3f", camera.GetAspectRatio());
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

    int colliderTypeIndex = ColliderTypeToInspectorIndex(collider.Type);
    const char* colliderTypes[] = { "Sphere", "Box", "Capsule", "Plane" };
    if (ImGui::Combo("Collider Type", &colliderTypeIndex, colliderTypes, 4))
    {
        collider.Type = InspectorIndexToColliderType(colliderTypeIndex);
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
    else if (collider.Type == ColliderType::Capsule)
    {
        // Capsuleの全高は 2 * (HalfLength + Radius) です。
        // HalfLengthを「円柱部を含む中心線分の半長」として編集することで、Ragdoll定義と
        // Inspectorのパラメータ意味を一致させています。
        ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 100000.0f);
        ImGui::DragFloat("Half Length", &collider.HalfLength, 0.01f, 0.0f, 100000.0f);
        ImGui::TextDisabled(
            "Full Height: %.3f",
            2.0f * (std::max(collider.HalfLength, 0.0f) + std::max(collider.Radius, 0.0f)));
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
    DrawCameraComponent(selectedEntity);
    DrawRigidBodyComponent(selectedEntity);
    DrawColliderComponent(selectedEntity);

    ImGui::End();
}

} // namespace Raven
