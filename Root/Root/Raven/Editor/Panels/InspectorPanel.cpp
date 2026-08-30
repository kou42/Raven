#include "Raven/Editor/Panels/InspectorPanel.h"

#include "Raven/Editor/Command/RenameEntityCommand.h"
#include "Raven/Editor/Command/InspectorEditCommand.h"
#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

#include <algorithm>
#include <cstring>
#include <cmath>
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

struct TransformEditState
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    TransformComponent Before{};
    bool IsActive = false;
};

TransformEditState s_TransformEditState{};

struct CameraProjectionSettings
{
    float VerticalFov = 0.0f;
    float NearClip = 0.0f;
    float FarClip = 0.0f;
};

struct CameraProjectionEditState
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    CameraProjectionSettings Before{};
    bool IsActive = false;
};

CameraProjectionEditState s_CameraProjectionEditState{};

bool ValidateCameraProjectionTarget(Entity entity)
{
    return entity.HasComponent<CameraComponent>();
}

bool ApplyCameraProjectionSettings(
    Entity entity,
    const CameraProjectionSettings& settings)
{
    if (entity.HasComponent<CameraComponent>() == false)
    {
        return false;
    }

    // Projection Matrixの再計算とNear/Far/FOV検証を迂回しないよう、Undo / Redo時も
    // Component内部値へ直接代入せず、SceneCameraの公開Setterを必ず通します。
    entity.GetComponent<CameraComponent>().Camera.SetPerspective(
        settings.VerticalFov,
        settings.NearClip,
        settings.FarClip);
    return true;
}

bool CameraProjectionSettingsEqual(
    const CameraProjectionSettings& a,
    const CameraProjectionSettings& b)
{
    constexpr float CompareEpsilon = 0.000001f;
    return std::fabs(a.VerticalFov - b.VerticalFov) <= CompareEpsilon
        && std::fabs(a.NearClip - b.NearClip) <= CompareEpsilon
        && std::fabs(a.FarClip - b.FarClip) <= CompareEpsilon;
}

struct RigidBodyTypeSettings
{
    BodyType Type = BodyType::Dynamic;
    float Mass = 1.0f;
    math::Vec3 LinearVelocity{};
    math::Vec3 AngularVelocity{};
    math::Vec3 Force{};
    math::Vec3 Torque{};
    bool IsSleeping = false;
    float SleepTimer = 0.0f;
};

struct RigidBodyMassEditState
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    float Before = 0.0f;
    bool IsActive = false;
};

RigidBodyMassEditState s_RigidBodyMassEditState{};

bool ValidateRigidBodyTarget(Entity entity)
{
    return entity.HasComponent<RigidBodyComponent>();
}

RigidBodyTypeSettings CaptureRigidBodyTypeSettings(const RigidBodyComponent& rigidBody)
{
    RigidBodyTypeSettings settings{};
    settings.Type = rigidBody.Type;
    settings.Mass = rigidBody.Mass;
    settings.LinearVelocity = rigidBody.LinearVelocity;
    settings.AngularVelocity = rigidBody.AngularVelocity;
    settings.Force = rigidBody.Force;
    settings.Torque = rigidBody.Torque;
    settings.IsSleeping = rigidBody.IsSleeping;
    settings.SleepTimer = rigidBody.SleepTimer;
    return settings;
}

bool ApplyRigidBodyTypeSettings(Entity entity, const RigidBodyTypeSettings& settings)
{
    if (entity.HasComponent<RigidBodyComponent>() == false)
    {
        return false;
    }

    RigidBodyComponent& rigidBody = entity.GetComponent<RigidBodyComponent>();

    // SetBodyType()はInverseMassやSleep状態を同期し、Static化時には運動量を初期化します。
    // Undoで元のDynamic状態へ戻す場合は編集前SnapshotのVelocity / Forceも必要なので、
    // Setterで型固有の不変条件を整えた後、そのCommandが保存した時点の運動状態を復元します。
    rigidBody.Mass = settings.Mass;
    rigidBody.SetBodyType(settings.Type);
    rigidBody.LinearVelocity = settings.LinearVelocity;
    rigidBody.AngularVelocity = settings.AngularVelocity;
    rigidBody.Force = settings.Force;
    rigidBody.Torque = settings.Torque;
    rigidBody.IsSleeping = settings.IsSleeping;
    rigidBody.SleepTimer = settings.SleepTimer;
    return true;
}

bool RigidBodyTypeSettingsEqual(
    const RigidBodyTypeSettings& a,
    const RigidBodyTypeSettings& b)
{
    // BodyType変更Commandの成立条件は型が変化したことです。
    // 他のSnapshot値は型変更の副作用をUndoするために保持しており、編集判定には使用しません。
    return a.Type == b.Type;
}

bool ApplyRigidBodyMass(Entity entity, const float& mass)
{
    if (entity.HasComponent<RigidBodyComponent>() == false)
    {
        return false;
    }

    entity.GetComponent<RigidBodyComponent>().SetMass(mass);
    return true;
}

bool RigidBodyMassEqual(const float& a, const float& b)
{
    constexpr float CompareEpsilon = 0.000001f;
    return std::fabs(a - b) <= CompareEpsilon;
}

struct ColliderEditState
{
    EntityHandle Handle{};
    Scene* TargetScene = nullptr;
    ColliderComponent Before{};
    bool IsActive = false;
};

ColliderEditState s_ColliderEditState{};

bool ValidateColliderTarget(Entity entity)
{
    return entity.HasComponent<ColliderComponent>();
}

bool ApplyColliderSettings(Entity entity, const ColliderComponent& collider)
{
    if (entity.HasComponent<ColliderComponent>() == false)
    {
        return false;
    }

    // ColliderComponentは現段階で専用Setterや外部Resource所有を持たない値Componentです。
    // そのため、編集開始時と終了時のSnapshot全体を復元することで、形状変更を跨いだUndoでも
    // 非表示になっていた形状固有パラメータを失わず同じ状態へ戻せます。
    entity.GetComponent<ColliderComponent>() = collider;
    return true;
}

bool ColliderSettingsEqual(const ColliderComponent& a, const ColliderComponent& b)
{
    constexpr float CompareEpsilon = 0.000001f;
    const auto floatEqual = [](float left, float right)
    {
        return std::fabs(left - right) <= CompareEpsilon;
    };

    const auto vec3Equal = [&floatEqual](const math::Vec3& left, const math::Vec3& right)
    {
        return floatEqual(left.x, right.x)
            && floatEqual(left.y, right.y)
            && floatEqual(left.z, right.z);
    };

    return a.Type == b.Type
        && vec3Equal(a.Offset, b.Offset)
        && vec3Equal(a.HalfExtents, b.HalfExtents)
        && floatEqual(a.Radius, b.Radius)
        && floatEqual(a.HalfLength, b.HalfLength)
        && vec3Equal(a.PlaneNormal, b.PlaneNormal)
        && floatEqual(a.PlaneOffset, b.PlaneOffset)
        && floatEqual(a.Restitution, b.Restitution)
        && floatEqual(a.StaticFriction, b.StaticFriction)
        && floatEqual(a.DynamicFriction, b.DynamicFriction)
        && a.IsTrigger == b.IsTrigger;
}

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

void DrawTransformVec3Control(
    const char* label,
    Entity entity,
    TransformComponent& transform,
    math::Vec3& value)
{
    // Drag開始frameの値をCommandのBeforeとして保存する必要があるため、Widgetが値を更新する前に
    // Transform全体をSnapshotします。Positionだけを編集した場合でもTransform全体を保存することで、
    // GizmoとInspectorが同じTransformCommandを共有でき、Undo時の適用経路も一つに保てます。
    const TransformComponent transformBeforeThisFrame = transform;
    DrawVec3Control(label, value);

    if (ImGui::IsItemActivated())
    {
        s_TransformEditState.Handle = entity.GetHandle();
        s_TransformEditState.TargetScene = entity.GetScene();
        s_TransformEditState.Before = transformBeforeThisFrame;
        s_TransformEditState.IsActive = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()
        && s_TransformEditState.IsActive == true)
    {
        const bool isSameEntity =
            s_TransformEditState.Handle == entity.GetHandle()
            && s_TransformEditState.TargetScene == entity.GetScene();

        if (isSameEntity == true)
        {
            // Drag中の値はInspectorからComponentへ既に反映されています。
            // Mouse Release時にはExecuteを重ねず、開始値と終了値を実行済みCommandとして登録します。
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<TransformCommand>(
                    entity,
                    s_TransformEditState.Before,
                    transform));
        }

        s_TransformEditState = TransformEditState{};
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

    DrawTransformVec3Control("Position", entity, transform, transform.Position);
    DrawTransformVec3Control("Rotation", entity, transform, transform.Rotation);
    DrawTransformVec3Control("Scale", entity, transform, transform.Scale);
}

void DrawCameraProjectionFloat(
    const char* label,
    Entity entity,
    CameraProjectionSettings& settings,
    float& value,
    float speed,
    float minimum,
    float maximum)
{
    const CameraProjectionSettings settingsBeforeThisFrame = settings;
    if (ImGui::DragFloat(label, &value, speed, minimum, maximum))
    {
        ApplyCameraProjectionSettings(entity, settings);

        // SetPerspective()は入力を安全範囲へ補正できます。Commandへ要求値ではなく実際に確定した値を
        // 保存することで、Redo後も最初の編集結果と完全に同じProjection状態を再現します。
        const SceneCamera& camera = entity.GetComponent<CameraComponent>().Camera;
        settings.VerticalFov = camera.GetPerspectiveVerticalFov();
        settings.NearClip = camera.GetPerspectiveNearClip();
        settings.FarClip = camera.GetPerspectiveFarClip();
    }

    if (ImGui::IsItemActivated())
    {
        s_CameraProjectionEditState.Handle = entity.GetHandle();
        s_CameraProjectionEditState.TargetScene = entity.GetScene();
        s_CameraProjectionEditState.Before = settingsBeforeThisFrame;
        s_CameraProjectionEditState.IsActive = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()
        && s_CameraProjectionEditState.IsActive == true)
    {
        const bool isSameEntity =
            s_CameraProjectionEditState.Handle == entity.GetHandle()
            && s_CameraProjectionEditState.TargetScene == entity.GetScene();

        if (isSameEntity == true)
        {
            // Drag中にSetPerspective()で適用済みなので、ここでは実行済みCommandとして登録します。
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<InspectorEditCommand<CameraProjectionSettings>>(
                    entity,
                    s_CameraProjectionEditState.Before,
                    settings,
                    &ValidateCameraProjectionTarget,
                    &ApplyCameraProjectionSettings,
                    &CameraProjectionSettingsEqual));
        }

        s_CameraProjectionEditState = CameraProjectionEditState{};
    }
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
    CameraProjectionSettings projectionSettings{
        camera.GetPerspectiveVerticalFov(),
        camera.GetPerspectiveNearClip(),
        camera.GetPerspectiveFarClip()
    };

    DrawCameraProjectionFloat(
        "Vertical FOV (rad)",
        entity,
        projectionSettings,
        projectionSettings.VerticalFov,
        0.005f,
        0.01f,
        3.13f);

    DrawCameraProjectionFloat(
        "Near Clip",
        entity,
        projectionSettings,
        projectionSettings.NearClip,
        0.01f,
        0.001f,
        projectionSettings.FarClip);

    DrawCameraProjectionFloat(
        "Far Clip",
        entity,
        projectionSettings,
        projectionSettings.FarClip,
        0.1f,
        projectionSettings.NearClip,
        100000.0f);

    ImGui::TextDisabled("Aspect Ratio: %.3f", camera.GetAspectRatio());
}

void DrawRigidBodyMassControl(Entity entity, RigidBodyComponent& rigidBody)
{
    const float massBeforeThisFrame = rigidBody.Mass;
    float mass = rigidBody.Mass;

    if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0f, 100000.0f))
    {
        // Drag中もPhysics状態へ即時反映します。InverseMassとの対応はSetMass()へ一元化します。
        rigidBody.SetMass(mass);
        mass = rigidBody.Mass;
    }

    if (ImGui::IsItemActivated())
    {
        s_RigidBodyMassEditState.Handle = entity.GetHandle();
        s_RigidBodyMassEditState.TargetScene = entity.GetScene();
        s_RigidBodyMassEditState.Before = massBeforeThisFrame;
        s_RigidBodyMassEditState.IsActive = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()
        && s_RigidBodyMassEditState.IsActive == true)
    {
        const bool isSameEntity =
            s_RigidBodyMassEditState.Handle == entity.GetHandle()
            && s_RigidBodyMassEditState.TargetScene == entity.GetScene();

        if (isSameEntity == true)
        {
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<InspectorEditCommand<float>>(
                    entity,
                    s_RigidBodyMassEditState.Before,
                    rigidBody.Mass,
                    &ValidateRigidBodyTarget,
                    &ApplyRigidBodyMass,
                    &RigidBodyMassEqual));
        }

        s_RigidBodyMassEditState = RigidBodyMassEditState{};
    }
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
        const RigidBodyTypeSettings before = CaptureRigidBodyTypeSettings(rigidBody);

        // UI入力を一度Componentのcopyへ適用し、Setterが生む副作用を含むAfter Snapshotを作ります。
        // 実Entityの変更はExecuteAndRecordEditorCommand()に任せるため、通常Command経路も検証できます。
        RigidBodyComponent afterRigidBody = rigidBody;
        afterRigidBody.SetBodyType(static_cast<BodyType>(bodyTypeIndex));
        const RigidBodyTypeSettings after = CaptureRigidBodyTypeSettings(afterRigidBody);

        ExecuteAndRecordEditorCommand(
            std::make_unique<InspectorEditCommand<RigidBodyTypeSettings>>(
                entity,
                before,
                after,
                &ValidateRigidBodyTarget,
                &ApplyRigidBodyTypeSettings,
                &RigidBodyTypeSettingsEqual));
    }

    ImGui::TextDisabled("Current: %s", BodyTypeName(rigidBody.Type));

    // MassとInverseMassは常に対応している必要があるためSetMass()経由で更新します。
    // Static BodyのMassはSetBodyType()がDynamicへ戻る時の基準値として保持されます。
    // Static中にSetMass()を呼ぶと保持値まで0へ失われるため、編集を無効化してInverseMass=0を維持します。
    if (rigidBody.Type == BodyType::Static)
    {
        ImGui::BeginDisabled();
        DrawRigidBodyMassControl(entity, rigidBody);
        ImGui::EndDisabled();
    }
    else
    {
        DrawRigidBodyMassControl(entity, rigidBody);
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

void RecordColliderDragAfterItem(
    Entity entity,
    const ColliderComponent& colliderBeforeThisFrame,
    const ColliderComponent& colliderAfterThisFrame)
{
    if (ImGui::IsItemActivated())
    {
        s_ColliderEditState.Handle = entity.GetHandle();
        s_ColliderEditState.TargetScene = entity.GetScene();
        s_ColliderEditState.Before = colliderBeforeThisFrame;
        s_ColliderEditState.IsActive = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()
        && s_ColliderEditState.IsActive == true)
    {
        const bool isSameEntity =
            s_ColliderEditState.Handle == entity.GetHandle()
            && s_ColliderEditState.TargetScene == entity.GetScene();

        if (isSameEntity == true)
        {
            RecordAlreadyExecutedEditorCommand(
                std::make_unique<InspectorEditCommand<ColliderComponent>>(
                    entity,
                    s_ColliderEditState.Before,
                    colliderAfterThisFrame,
                    &ValidateColliderTarget,
                    &ApplyColliderSettings,
                    &ColliderSettingsEqual));
        }

        s_ColliderEditState = ColliderEditState{};
    }
}

void DrawColliderVec3Control(
    const char* label,
    Entity entity,
    ColliderComponent& collider,
    math::Vec3& value,
    float speed = 0.1f)
{
    const ColliderComponent before = collider;
    DrawVec3Control(label, value, speed);
    RecordColliderDragAfterItem(entity, before, collider);
}

void DrawColliderFloatControl(
    const char* label,
    Entity entity,
    ColliderComponent& collider,
    float& value,
    float speed,
    float minimum,
    float maximum)
{
    const ColliderComponent before = collider;
    ImGui::DragFloat(label, &value, speed, minimum, maximum);
    RecordColliderDragAfterItem(entity, before, collider);
}

void ExecuteColliderChange(
    Entity entity,
    const ColliderComponent& before,
    const ColliderComponent& after)
{
    ExecuteAndRecordEditorCommand(
        std::make_unique<InspectorEditCommand<ColliderComponent>>(
            entity,
            before,
            after,
            &ValidateColliderTarget,
            &ApplyColliderSettings,
            &ColliderSettingsEqual));
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
        const ColliderComponent before = collider;
        ColliderComponent after = collider;
        after.Type = InspectorIndexToColliderType(colliderTypeIndex);
        ExecuteColliderChange(entity, before, after);
    }

    ImGui::TextDisabled("Current: %s", ColliderTypeName(collider.Type));
    DrawColliderVec3Control("Offset", entity, collider, collider.Offset);

    // Collider Typeごとに意味のあるパラメータだけを表示します。
    // 無関係な値を同時に見せないことで、どの値が実際のCollision Shapeへ使われるかを明確にします。
    if (collider.Type == ColliderType::Sphere)
    {
        DrawColliderFloatControl(
            "Radius", entity, collider, collider.Radius, 0.01f, 0.001f, 100000.0f);
    }
    else if (collider.Type == ColliderType::Box)
    {
        DrawColliderVec3Control("Half Extents", entity, collider, collider.HalfExtents, 0.05f);
    }
    else if (collider.Type == ColliderType::Capsule)
    {
        // Capsuleの全高は 2 * (HalfLength + Radius) です。
        // HalfLengthを「円柱部を含む中心線分の半長」として編集することで、Ragdoll定義と
        // Inspectorのパラメータ意味を一致させています。
        DrawColliderFloatControl(
            "Radius", entity, collider, collider.Radius, 0.01f, 0.001f, 100000.0f);
        DrawColliderFloatControl(
            "Half Length", entity, collider, collider.HalfLength, 0.01f, 0.0f, 100000.0f);
        ImGui::TextDisabled(
            "Full Height: %.3f",
            2.0f * (std::max(collider.HalfLength, 0.0f) + std::max(collider.Radius, 0.0f)));
    }
    else if (collider.Type == ColliderType::Plane)
    {
        DrawColliderVec3Control("Plane Normal", entity, collider, collider.PlaneNormal);
        DrawColliderFloatControl(
            "Plane Offset", entity, collider, collider.PlaneOffset, 0.05f, 0.0f, 0.0f);
    }

    ImGui::SeparatorText("Material");
    DrawColliderFloatControl(
        "Restitution", entity, collider, collider.Restitution, 0.01f, 0.0f, 1.0f);
    DrawColliderFloatControl(
        "Static Friction", entity, collider, collider.StaticFriction, 0.01f, 0.0f, 10.0f);
    DrawColliderFloatControl(
        "Dynamic Friction", entity, collider, collider.DynamicFriction, 0.01f, 0.0f, 10.0f);

    bool isTrigger = collider.IsTrigger;
    if (ImGui::Checkbox("Is Trigger", &isTrigger))
    {
        const ColliderComponent before = collider;
        ColliderComponent after = collider;
        after.IsTrigger = isTrigger;
        ExecuteColliderChange(entity, before, after);
    }
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
