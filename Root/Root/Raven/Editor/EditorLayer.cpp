#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Core/MouseCodes.h"
#include "Raven/Editor/Command/CreateEntityCommand.h"
#include "Raven/Editor/Command/DeleteEntityCommand.h"
#include "Raven/Editor/Command/InspectorEditCommand.h"
#include "Raven/Editor/Command/RenameEntityCommand.h"
#include "Raven/Editor/Command/ReparentEntityCommand.h"
#include "Raven/Editor/Command/SetPrimaryCameraCommand.h"
#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/EditorTranslateGizmo.h"
#include "Raven/Editor/EditorViewportGeometry.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneViewportRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Raven
{
namespace
{

constexpr float CameraProjectionCompareEpsilon = 0.000001f;

bool IsEditorCameraNavigationKey(int keyCode)
{
    return keyCode == Key::W || keyCode == Key::A ||
        keyCode == Key::S || keyCode == Key::D ||
        keyCode == Key::Q || keyCode == Key::E;
}

bool IsEditorViewportOwnedCharacter(unsigned int codepoint)
{
    return codepoint == static_cast<unsigned int>('w') ||
        codepoint == static_cast<unsigned int>('W') ||
        codepoint == static_cast<unsigned int>('a') ||
        codepoint == static_cast<unsigned int>('A') ||
        codepoint == static_cast<unsigned int>('s') ||
        codepoint == static_cast<unsigned int>('S') ||
        codepoint == static_cast<unsigned int>('d') ||
        codepoint == static_cast<unsigned int>('D') ||
        codepoint == static_cast<unsigned int>('q') ||
        codepoint == static_cast<unsigned int>('Q') ||
        codepoint == static_cast<unsigned int>('e') ||
        codepoint == static_cast<unsigned int>('E') ||
        codepoint == static_cast<unsigned int>('r') ||
        codepoint == static_cast<unsigned int>('R');
}

bool IsControlKey(int keyCode)
{
    return keyCode == Key::LeftControl || keyCode == Key::RightControl;
}

bool IsEditorGizmoShortcutKey(int keyCode)
{
    return keyCode == Key::W || keyCode == Key::E ||
        keyCode == Key::R || keyCode == Key::Q ||
        keyCode == Key::Z || keyCode == Key::Y;
}

bool ValidateCameraProjectionTarget(Entity entity)
{
    return entity.HasComponent<CameraComponent>();
}

bool ApplyCameraProjectionSnapshot(
    Entity entity,
    const InspectorCameraSnapshot& snapshot)
{
    if (ValidateCameraProjectionTarget(entity) == false)
    {
        return false;
    }

    SceneCamera& camera = entity.GetComponent<CameraComponent>().Camera;
    camera.SetPerspective(
        snapshot.VerticalFov,
        snapshot.NearClip,
        snapshot.FarClip);

    // SetPerspective()は不正なNear/Far/FOVを拒否して現在値を維持します。
    // Previewを成功扱いにする前に、要求値が実際のCamera状態へ反映されたことを確認します。
    return std::fabs(camera.GetPerspectiveVerticalFov() - snapshot.VerticalFov)
            <= CameraProjectionCompareEpsilon
        && std::fabs(camera.GetPerspectiveNearClip() - snapshot.NearClip)
            <= CameraProjectionCompareEpsilon
        && std::fabs(camera.GetPerspectiveFarClip() - snapshot.FarClip)
            <= CameraProjectionCompareEpsilon;
}

bool CameraProjectionSnapshotsEqual(
    const InspectorCameraSnapshot& left,
    const InspectorCameraSnapshot& right)
{
    // Aspect RatioはViewport、PrimaryはSceneCameraSystemが所有するため、このCommandでは比較しません。
    return std::fabs(left.VerticalFov - right.VerticalFov)
            <= CameraProjectionCompareEpsilon
        && std::fabs(left.NearClip - right.NearClip)
            <= CameraProjectionCompareEpsilon
        && std::fabs(left.FarClip - right.FarClip)
            <= CameraProjectionCompareEpsilon;
}

InspectorCameraSnapshot CaptureCameraSnapshot(const CameraComponent& component)
{
    return InspectorCameraSnapshot{
        component.Camera.GetPerspectiveVerticalFov(),
        component.Camera.GetPerspectiveNearClip(),
        component.Camera.GetPerspectiveFarClip(),
        component.Camera.GetAspectRatio(),
        component.Primary };
}

constexpr float InspectorValueCompareEpsilon = 0.000001f;

bool InspectorFloatEqual(float left, float right)
{
    return std::fabs(left - right) <= InspectorValueCompareEpsilon;
}

bool InspectorVec3Equal(const math::Vec3& left, const math::Vec3& right)
{
    return InspectorFloatEqual(left.x, right.x)
        && InspectorFloatEqual(left.y, right.y)
        && InspectorFloatEqual(left.z, right.z);
}

bool ValidateRigidBodyEditTarget(Entity entity)
{
    return entity.HasComponent<RigidBodyComponent>();
}

bool ApplyRigidBodyEditValue(
    Entity entity,
    const InspectorRigidBodyEditValue& value)
{
    if (ValidateRigidBodyEditTarget(entity) == false)
    {
        return false;
    }

    RigidBodyComponent& component = entity.GetComponent<RigidBodyComponent>();
    const InspectorRigidBodySnapshot& snapshot = value.Snapshot;
    switch (value.Field)
    {
    case InspectorRigidBodyField::Mass:
        // Static中のSetMass()は保持Massまで0へ変更するため編集を拒否します。
        if (component.Type == BodyType::Static)
        {
            return false;
        }
        component.SetMass(snapshot.Mass);
        return true;
    case InspectorRigidBodyField::LinearVelocity:
        component.LinearVelocity = snapshot.LinearVelocity;
        return true;
    case InspectorRigidBodyField::AngularVelocity:
        component.AngularVelocity = snapshot.AngularVelocity;
        return true;
    case InspectorRigidBodyField::LinearDamping:
        component.LinearDamping = snapshot.LinearDamping;
        return true;
    case InspectorRigidBodyField::AngularDamping:
        component.AngularDamping = snapshot.AngularDamping;
        return true;
    case InspectorRigidBodyField::UseGravity:
        component.UseGravity = snapshot.UseGravity;
        return true;
    case InspectorRigidBodyField::AllowSleep:
        component.AllowSleep = snapshot.AllowSleep;
        return true;
    case InspectorRigidBodyField::Type:
        return false;
    }
    return false;
}

bool RigidBodyEditValuesEqual(
    const InspectorRigidBodyEditValue& left,
    const InspectorRigidBodyEditValue& right)
{
    if (left.Field != right.Field)
    {
        return false;
    }
    switch (left.Field)
    {
    case InspectorRigidBodyField::Mass:
        return InspectorFloatEqual(left.Snapshot.Mass, right.Snapshot.Mass);
    case InspectorRigidBodyField::LinearVelocity:
        return InspectorVec3Equal(
            left.Snapshot.LinearVelocity, right.Snapshot.LinearVelocity);
    case InspectorRigidBodyField::AngularVelocity:
        return InspectorVec3Equal(
            left.Snapshot.AngularVelocity, right.Snapshot.AngularVelocity);
    case InspectorRigidBodyField::LinearDamping:
        return InspectorFloatEqual(
            left.Snapshot.LinearDamping, right.Snapshot.LinearDamping);
    case InspectorRigidBodyField::AngularDamping:
        return InspectorFloatEqual(
            left.Snapshot.AngularDamping, right.Snapshot.AngularDamping);
    case InspectorRigidBodyField::UseGravity:
        return left.Snapshot.UseGravity == right.Snapshot.UseGravity;
    case InspectorRigidBodyField::AllowSleep:
        return left.Snapshot.AllowSleep == right.Snapshot.AllowSleep;
    case InspectorRigidBodyField::Type:
        return left.Snapshot.Type == right.Snapshot.Type;
    }
    return true;
}

bool ApplyRigidBodyComponent(Entity entity, const RigidBodyComponent& value)
{
    if (ValidateRigidBodyEditTarget(entity) == false)
    {
        return false;
    }
    entity.GetComponent<RigidBodyComponent>() = value;
    return true;
}

bool RigidBodyComponentsEqual(
    const RigidBodyComponent& left,
    const RigidBodyComponent& right)
{
    return left.Type == right.Type;
}

bool ValidateColliderEditTarget(Entity entity)
{
    return entity.HasComponent<ColliderComponent>();
}

bool ApplyColliderEditValue(
    Entity entity,
    const InspectorColliderEditValue& value)
{
    if (ValidateColliderEditTarget(entity) == false)
    {
        return false;
    }
    ColliderComponent& component = entity.GetComponent<ColliderComponent>();
    const InspectorColliderSnapshot& snapshot = value.Snapshot;
    switch (value.Field)
    {
    case InspectorColliderField::Type:
        // StaticMeshへ切り替えるにはCPU Geometryが必要です。空Shapeを作る操作は拒否します。
        if (snapshot.Type == ColliderType::StaticMesh
            && component.StaticMeshGeometry == nullptr)
        {
            return false;
        }
        component.Type = snapshot.Type;
        return true;
    case InspectorColliderField::Offset:
        component.Offset = snapshot.Offset;
        return true;
    case InspectorColliderField::HalfExtents:
        component.HalfExtents = snapshot.HalfExtents;
        return true;
    case InspectorColliderField::Radius:
        component.Radius = snapshot.Radius;
        return true;
    case InspectorColliderField::HalfLength:
        component.HalfLength = snapshot.HalfLength;
        return true;
    case InspectorColliderField::PlaneNormal:
        component.PlaneNormal = snapshot.PlaneNormal;
        return true;
    case InspectorColliderField::PlaneOffset:
        component.PlaneOffset = snapshot.PlaneOffset;
        return true;
    case InspectorColliderField::Restitution:
        component.Restitution = snapshot.Restitution;
        return true;
    case InspectorColliderField::StaticFriction:
        component.StaticFriction = snapshot.StaticFriction;
        return true;
    case InspectorColliderField::DynamicFriction:
        component.DynamicFriction = snapshot.DynamicFriction;
        return true;
    case InspectorColliderField::IsTrigger:
        component.IsTrigger = snapshot.IsTrigger;
        return true;
    }
    return false;
}

bool ColliderEditValuesEqual(
    const InspectorColliderEditValue& left,
    const InspectorColliderEditValue& right)
{
    if (left.Field != right.Field)
    {
        return false;
    }
    switch (left.Field)
    {
    case InspectorColliderField::Type:
        return left.Snapshot.Type == right.Snapshot.Type;
    case InspectorColliderField::Offset:
        return InspectorVec3Equal(left.Snapshot.Offset, right.Snapshot.Offset);
    case InspectorColliderField::HalfExtents:
        return InspectorVec3Equal(
            left.Snapshot.HalfExtents, right.Snapshot.HalfExtents);
    case InspectorColliderField::Radius:
        return InspectorFloatEqual(left.Snapshot.Radius, right.Snapshot.Radius);
    case InspectorColliderField::HalfLength:
        return InspectorFloatEqual(
            left.Snapshot.HalfLength, right.Snapshot.HalfLength);
    case InspectorColliderField::PlaneNormal:
        return InspectorVec3Equal(
            left.Snapshot.PlaneNormal, right.Snapshot.PlaneNormal);
    case InspectorColliderField::PlaneOffset:
        return InspectorFloatEqual(
            left.Snapshot.PlaneOffset, right.Snapshot.PlaneOffset);
    case InspectorColliderField::Restitution:
        return InspectorFloatEqual(
            left.Snapshot.Restitution, right.Snapshot.Restitution);
    case InspectorColliderField::StaticFriction:
        return InspectorFloatEqual(
            left.Snapshot.StaticFriction, right.Snapshot.StaticFriction);
    case InspectorColliderField::DynamicFriction:
        return InspectorFloatEqual(
            left.Snapshot.DynamicFriction, right.Snapshot.DynamicFriction);
    case InspectorColliderField::IsTrigger:
        return left.Snapshot.IsTrigger == right.Snapshot.IsTrigger;
    }
    return true;
}

} // namespace

EditorLayer::EditorLayer(Application& application, EditorUIBackend uiBackend)
    : m_Application(&application)
    , m_UIBackend(uiBackend)
{
}

bool EditorLayer::IsPanelOwnedByDearImGui(PanelUIOwner owner) const
{
    if (m_UIBackend == EditorUIBackend::DearImGui)
    {
        return true;
    }
    if (m_UIBackend == EditorUIBackend::RavenUI)
    {
        return false;
    }
    return owner == PanelUIOwner::DearImGui;
}

void EditorLayer::OnAttach()
{
    m_ViewportRenderingEnabled = m_Application != nullptr &&
        m_Application->GetWindow().GetBackend() == RHIBackend::OpenGL;

    if (m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.Statistics) == false)
    {
        m_StatisticsRavenPanel.Attach(
            m_Application->GetUIContext(), m_Application->GetRuntimeUIFont());
    }
    if (m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.AnimationDebug) == false)
    {
        m_AnimationDebugRavenPanel.Attach(
            m_Application->GetUIContext(), m_Application->GetRuntimeUIFont());
    }
    if (m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.SceneHierarchy) == false)
    {
        // CallbackはSceneを保持せず、その時点のActive SceneへHandleを解決します。
        // Scene交換後に古いScene pointerへ書き込まないための重要なLifetime境界です。
        m_SceneHierarchyRavenPanel.SetSelectionRequestedHandler(
            [this](EntityHandle handle)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    SetSelectedEntity(Entity{});
                    return;
                }

                Entity candidate(handle, activeScene);
                SetSelectedEntity(activeScene->IsEntityAlive(candidate) == true
                    ? candidate
                    : Entity{});
            });
        m_SceneHierarchyRavenPanel.SetCreateRequestedHandler(
            [this]()
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr)
                {
                    return;
                }

                auto command = std::make_unique<CreateEntityCommand>(activeScene, "Entity");
                CreateEntityCommand* executedCommand = command.get();
                if (ExecuteAndRecordEditorCommand(std::move(command)) == true)
                {
                    // CommandはHistoryへ移譲済みですが、同期Execute直後なので生成Handleを
                    // 読み出せます。Panel側にはScene/Entity参照を保持させません。
                    SetSelectedEntity(executedCommand->GetCreatedEntity());
                }
            });
        m_SceneHierarchyRavenPanel.SetRenameRequestedHandler(
            [this](EntityHandle handle, const std::string& requestedName)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    return false;
                }

                Entity entity(handle, activeScene);
                if (activeScene->IsEntityAlive(entity) == false ||
                    entity.HasComponent<TagComponent>() == false)
                {
                    return false;
                }

                const std::string before = entity.GetComponent<TagComponent>().Tag;
                return ExecuteAndRecordEditorCommand(
                    std::make_unique<RenameEntityCommand>(
                        entity, before, requestedName));
            });
        m_SceneHierarchyRavenPanel.SetDeleteRequestedHandler(
            [this](EntityHandle handle)
            {
                Scene* scene = m_Application != nullptr ? m_Application->GetScene() : nullptr;
                if (scene == nullptr || scene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                const bool result = ExecuteAndRecordEditorCommand(
                    std::make_unique<DeleteEntityCommand>(scene, Entity(handle, scene)));
                if (result == true && m_SelectedEntity.GetHandle() == handle)
                {
                    // 削除済みHandleをInspector / Gizmoへ1frameも公開しません。
                    SetSelectedEntity(Entity{});
                }
                return result;
            });
        m_SceneHierarchyRavenPanel.SetReparentRequestedHandler(
            [this](EntityHandle child, EntityHandle parent)
            {
                Scene* scene = m_Application != nullptr ? m_Application->GetScene() : nullptr;
                if (scene == nullptr || scene->CanSetEntityParent(child, parent) == false)
                {
                    return false;
                }
                return ExecuteAndRecordEditorCommand(
                    std::make_unique<ReparentEntityCommand>(scene, child, parent));
            });
        m_SceneHierarchyRavenPanel.Attach(
            m_Application->GetUIContext(), m_Application->GetRuntimeUIFont());
    }
    if (m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.Inspector) == false)
    {
        m_InspectorRavenPanel.SetTransformPreviewRequestedHandler(
            [this](EntityHandle handle, const InspectorTransformSnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    return false;
                }

                Entity entity(handle, activeScene);
                if (activeScene->IsEntityAlive(entity) == false
                    || entity.HasComponent<TransformComponent>() == false)
                {
                    return false;
                }

                // PreviewはUndo履歴へ積まず、Runtime Transformへ即時反映します。
                // WidgetはScene参照を保持せず、ActionごとにHandleのGenerationを再検証します。
                TransformComponent& transform = entity.GetComponent<TransformComponent>();
                transform.Position = requested.Position;
                transform.Rotation = requested.Rotation;
                transform.Scale = requested.Scale;
                return true;
            });
        m_InspectorRavenPanel.SetTransformCommitRequestedHandler(
            [this](EntityHandle handle, const InspectorTransformSnapshot& beforeSnapshot)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    return false;
                }

                Entity entity(handle, activeScene);
                if (activeScene->IsEntityAlive(entity) == false
                    || entity.HasComponent<TransformComponent>() == false)
                {
                    return false;
                }

                TransformComponent before = entity.GetComponent<TransformComponent>();
                before.Position = beforeSnapshot.Position;
                before.Rotation = beforeSnapshot.Rotation;
                before.Scale = beforeSnapshot.Scale;

                // Preview適用済みの現在値をAfterとして取得し、1編集につき1 Commandだけ記録します。
                // Panelの前Frame SnapshotをAfterへ使わないため、Runtime側の外部更新も巻き戻しません。
                const TransformComponent after = entity.GetComponent<TransformComponent>();
                return RecordAlreadyExecutedEditorCommand(
                    std::make_unique<TransformCommand>(entity, before, after));
            });
        m_InspectorRavenPanel.SetCameraPreviewRequestedHandler(
            [this](EntityHandle handle, const InspectorCameraSnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    return false;
                }

                Entity entity(handle, activeScene);
                if (activeScene->IsEntityAlive(entity) == false)
                {
                    return false;
                }
                return ApplyCameraProjectionSnapshot(entity, requested);
            });
        m_InspectorRavenPanel.SetCameraCommitRequestedHandler(
            [this](EntityHandle handle, const InspectorCameraSnapshot& before)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || static_cast<bool>(handle) == false)
                {
                    return false;
                }

                Entity entity(handle, activeScene);
                if (activeScene->IsEntityAlive(entity) == false
                    || ValidateCameraProjectionTarget(entity) == false)
                {
                    return false;
                }

                const InspectorCameraSnapshot after = CaptureCameraSnapshot(
                    entity.GetComponent<CameraComponent>());
                return RecordAlreadyExecutedEditorCommand(
                    std::make_unique<InspectorEditCommand<InspectorCameraSnapshot>>(
                        entity,
                        before,
                        after,
                        &ValidateCameraProjectionTarget,
                        &ApplyCameraProjectionSnapshot,
                        &CameraProjectionSnapshotsEqual));
            });
        m_InspectorRavenPanel.SetPrimaryCameraRequestedHandler(
            [this](EntityHandle handle)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || handle.IsValid() == false)
                {
                    return false;
                }
                return ExecuteAndRecordEditorCommand(
                    std::make_unique<SetPrimaryCameraCommand>(activeScene, handle));
            });
        m_InspectorRavenPanel.SetRigidBodyPreviewRequestedHandler(
            [this](EntityHandle handle, InspectorRigidBodyField field,
                const InspectorRigidBodySnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                return ApplyRigidBodyEditValue(
                    Entity(handle, activeScene),
                    InspectorRigidBodyEditValue{ field, requested });
            });
        m_InspectorRavenPanel.SetRigidBodyCommitRequestedHandler(
            [this](EntityHandle handle, InspectorRigidBodyField field,
                const InspectorRigidBodySnapshot& before)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                Entity entity(handle, activeScene);
                const InspectorSnapshot current = CaptureInspectorSnapshot(entity);
                if (current.RigidBody.has_value() == false)
                {
                    return false;
                }
                return RecordAlreadyExecutedEditorCommand(
                    std::make_unique<InspectorEditCommand<InspectorRigidBodyEditValue>>(
                        entity,
                        InspectorRigidBodyEditValue{ field, before },
                        InspectorRigidBodyEditValue{ field, *current.RigidBody },
                        &ValidateRigidBodyEditTarget,
                        &ApplyRigidBodyEditValue,
                        &RigidBodyEditValuesEqual));
            });
        m_InspectorRavenPanel.SetRigidBodyChangeRequestedHandler(
            [this](EntityHandle handle, InspectorRigidBodyField field,
                const InspectorRigidBodySnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                Entity entity(handle, activeScene);
                if (ValidateRigidBodyEditTarget(entity) == false)
                {
                    return false;
                }
                if (field == InspectorRigidBodyField::Type)
                {
                    const RigidBodyComponent before =
                        entity.GetComponent<RigidBodyComponent>();
                    RigidBodyComponent after = before;
                    after.SetBodyType(requested.Type);
                    return ExecuteAndRecordEditorCommand(
                        std::make_unique<InspectorEditCommand<RigidBodyComponent>>(
                            entity, before, after,
                            &ValidateRigidBodyEditTarget,
                            &ApplyRigidBodyComponent,
                            &RigidBodyComponentsEqual));
                }
                const InspectorSnapshot current = CaptureInspectorSnapshot(entity);
                if (current.RigidBody.has_value() == false)
                {
                    return false;
                }
                return ExecuteAndRecordEditorCommand(
                    std::make_unique<InspectorEditCommand<InspectorRigidBodyEditValue>>(
                        entity,
                        InspectorRigidBodyEditValue{ field, *current.RigidBody },
                        InspectorRigidBodyEditValue{ field, requested },
                        &ValidateRigidBodyEditTarget,
                        &ApplyRigidBodyEditValue,
                        &RigidBodyEditValuesEqual));
            });
        m_InspectorRavenPanel.SetColliderPreviewRequestedHandler(
            [this](EntityHandle handle, InspectorColliderField field,
                const InspectorColliderSnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                return ApplyColliderEditValue(
                    Entity(handle, activeScene),
                    InspectorColliderEditValue{ field, requested });
            });
        m_InspectorRavenPanel.SetColliderCommitRequestedHandler(
            [this](EntityHandle handle, InspectorColliderField field,
                const InspectorColliderSnapshot& before)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                Entity entity(handle, activeScene);
                const InspectorSnapshot current = CaptureInspectorSnapshot(entity);
                if (current.Collider.has_value() == false)
                {
                    return false;
                }
                return RecordAlreadyExecutedEditorCommand(
                    std::make_unique<InspectorEditCommand<InspectorColliderEditValue>>(
                        entity,
                        InspectorColliderEditValue{ field, before },
                        InspectorColliderEditValue{ field, *current.Collider },
                        &ValidateColliderEditTarget,
                        &ApplyColliderEditValue,
                        &ColliderEditValuesEqual));
            });
        m_InspectorRavenPanel.SetColliderChangeRequestedHandler(
            [this](EntityHandle handle, InspectorColliderField field,
                const InspectorColliderSnapshot& requested)
            {
                Scene* activeScene = m_Application != nullptr
                    ? m_Application->GetScene()
                    : nullptr;
                if (activeScene == nullptr || activeScene->IsEntityAlive(handle) == false)
                {
                    return false;
                }
                Entity entity(handle, activeScene);
                const InspectorSnapshot current = CaptureInspectorSnapshot(entity);
                if (current.Collider.has_value() == false)
                {
                    return false;
                }
                return ExecuteAndRecordEditorCommand(
                    std::make_unique<InspectorEditCommand<InspectorColliderEditValue>>(
                        entity,
                        InspectorColliderEditValue{ field, *current.Collider },
                        InspectorColliderEditValue{ field, requested },
                        &ValidateColliderEditTarget,
                        &ApplyColliderEditValue,
                        &ColliderEditValuesEqual));
            });
        m_InspectorRavenPanel.Attach(
            m_Application->GetUIContext(), m_Application->GetRuntimeUIFont());
    }

    if (m_ViewportRenderingEnabled == true && m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.SceneView) == false)
    {
        // PanelはSceneやFramebufferを所有せず、入力時点のPixel座標だけをEditorLayerへ返します。
        // CallbackのLifetimeはEditorLayer所有Panelの範囲内であり、Detach時に外部へ残りません。
        m_SceneViewportRavenPanel.SetPickingRequestedHandler(
            [this](std::uint32_t pixelX, std::uint32_t pixelY)
            {
                PickSceneEntityAt(pixelX, pixelY);
            });
        m_SceneViewportRavenPanel.SetGizmoPointerHandler(
            [this](const EditorGizmoPointerEvent& event)
            {
                return m_RavenViewportGizmo.HandlePointerEvent(
                    m_SelectedEntity, m_EditorCamera, event);
            });
        m_SceneViewportRavenPanel.Attach(
            m_Application->GetUIContext(),
            m_Application->GetRuntimeUIFont(),
            "Scene View",
            math::Vec2(360.0f, 24.0f),
            math::Vec2(800.0f, 480.0f));
    }
    if (m_ViewportRenderingEnabled == true && m_Application != nullptr &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.GameView) == false)
    {
        m_GameViewportRavenPanel.Attach(
            m_Application->GetUIContext(),
            m_Application->GetRuntimeUIFont(),
            "Game View",
            math::Vec2(400.0f, 530.0f),
            math::Vec2(720.0f, 420.0f));
    }

    if (m_ViewportRenderingEnabled == false)
    {
        // Explicit BackendではMain SceneとRaven UIをRuntimeが直接描画します。
        // Editor用off-screen Framebufferが未実装のため、空Panelや未対応Resourceを生成しません。
        return;
    }

    // ========================================================================
    // Editor Viewport GPU resources
    // ========================================================================
    // Scene View / Game ViewはMain framebufferを直接見せるのではなく、
    // それぞれ専用FramebufferへSceneを描画し、そのColor Attachment Textureを
    // 所有Backendに応じてDear ImGuiまたはRaven UIのWindow内へ表示します。
    //
    // 重要:
    // EditorLayerはOpenGLFramebuffer等の具体的なPlatform実装を直接生成しません。
    // Framebuffer::Create()を通すことで、現在のRendererAPIに対応した実装をRenderer層で選択し、
    // Editor側からOpenGL / DirectX / Vulkan等の違いを隠蔽します。
    //
    // 初期サイズはDock Layout確定前なので仮値です。
    // 実際の表示サイズはDear ImGuiのContentRegion、またはRaven UI Panelの論理領域から取得し、
    // 次FrameのOnRender()でFramebuffer::Resize()へ反映します。
    //
    // SceneとGameを最初から別Framebufferにしている理由は、Scene Viewだけへ
    // Editor Camera / Grid / Selection Outline / Gizmo等を追加しても、Game Viewの
    // Runtime表示へEditor専用描画が混入しない構造を保つためです。
    //
    // Scene ViewだけはEntity Picking用R32I Attachmentを追加します。
    // Game ViewはEditor選択情報を必要としないため、従来のRGBA8 + Depth構成を維持します。
    FramebufferSpecification sceneFramebufferSpecification;
    sceneFramebufferSpecification.Width =
        static_cast<std::uint32_t>(m_SceneViewportWidth);
    sceneFramebufferSpecification.Height =
        static_cast<std::uint32_t>(m_SceneViewportHeight);
    sceneFramebufferSpecification.Attachments =
    {
        TextureFormat::RGBA8,
        TextureFormat::R32I,
        TextureFormat::Depth24Stencil8
    };

    m_SceneFramebuffer = Framebuffer::Create(sceneFramebufferSpecification);

    m_GameFramebuffer = Framebuffer::Create(
        static_cast<std::uint32_t>(m_GameViewportWidth),
        static_cast<std::uint32_t>(m_GameViewportHeight));

    // EditorCameraのProjectionも初期Viewportサイズへ合わせます。
    // 以降はScene ViewのContentRegionサイズが変わるたびRenderSceneView()から更新します。
    m_EditorCamera.SetViewportSize(m_SceneViewportWidth, m_SceneViewportHeight);
}

void EditorLayer::OnDetach()
{
    m_InspectorPanel.ResetEditingState();
    // UIContextはApplicationが所有しEditorLayerより長生きするため、終了時にTreeから明示的に外します。
    m_GameViewportRavenPanel.Detach();
    m_SceneViewportRavenPanel.Detach();
    m_RavenViewportGizmo.Reset();
    m_SceneHierarchyRavenPanel.Detach();
    m_InspectorRavenPanel.Detach();
    m_AnimationDebugRavenPanel.Detach();
    m_StatisticsRavenPanel.Detach();

    // Editor終了後にSceneへの非所有参照を履歴側へ残しません。
    SetEditorCommandHistoryScene(nullptr);

    // ========================================================================
    // Editor Viewport GPU resource shutdown
    // ========================================================================
    // Applicationの破棄順序により、この時点ではWindow / Graphics Contextがまだ生存しています。
    // Framebufferの具体実装はGPU Resourceを所有するため、Graphics Contextが破棄される前の
    // EditorLayer::OnDetach()で明示的に解放します。
    m_EntityPickingMaterial.reset();
    m_SelectionOutlineMaterial.reset();
    m_SceneFramebuffer.reset();
    m_GameFramebuffer.reset();
    m_ViewportRenderingEnabled = false;
}

void EditorLayer::OnUpdate(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    // Scene差し替えとEntity破棄に関するEditor状態はGraphics Backendに依存しません。
    // Dear ImGui frameが存在しないExplicit Backendでも毎frame同じLifetime規約を適用します。
    SetEditorCommandHistoryScene(m_Application->GetScene());
    ValidateSelectedEntity();

    if (m_ViewportRenderingEnabled == false)
    {
        return;
    }

    // ========================================================================
    // Editor-only update
    // ========================================================================
    // Runtime Sceneの更新はApplication -> Scene::OnUpdate()で行われます。
    // Editor Camera / Gizmo等はゲームロジックとは独立したEditor状態なので、
    // Runtime更新へ混ぜずEditorLayerから更新します。
    //
    if (m_SceneViewportRavenPanel.IsAttached() == true)
    {
        const std::pair<float, float> mousePosition = Input::GetMousePosition();
        m_SceneViewportHovered = m_SceneViewportRavenPanel.IsPointerOverViewport(
            math::Vec2(mousePosition.first, mousePosition.second));
        m_SceneViewportFocused = m_SceneViewportRavenPanel.HasKeyboardFocus();
    }

    // Dear ImGui経路は前Frameの状態、Raven UI経路は現在のPointerとFocus状態を使います。
    // Hoverだけでは別Panel編集中のWASDを拾い、FocusだけではPointerが外へ出た後もCameraが
    // 動き続けるため、両方が成立した場合だけInput pollingを許可します。
    const bool editorCameraInputEnabled =
        m_ShowSceneView
        && m_SceneViewportHovered
        && m_SceneViewportFocused
        && m_RavenViewportGizmo.IsActive() == false;

    m_EditorCamera.Update(dt, editorCameraInputEnabled);

    // Framebuffer ResizeはGPU Resourceの再生成を伴うため、UI構築中ではなく
    // 描画処理側のOnRender()でまとめて行います。
}

void EditorLayer::OnRender()
{
    if (m_ViewportRenderingEnabled == false ||
        m_Application == nullptr || m_Application->GetScene() == nullptr)
    {
        return;
    }

    // ========================================================================
    // Off-screen Scene / Game rendering
    // ========================================================================
    // Applicationは現在、互換性維持のためRuntime SceneをMain framebufferへ一度描画します。
    // その後EditorLayerからScene View / Game View専用Framebufferへ再描画しています。
    //
    // ここからScene ViewとGame ViewのCameraを明確に分離します。
    //   Scene View -> EditorCamera
    //   Game View  -> Runtime Scene自身のCamera
    //
    // 同じScene状態を別Cameraから描くだけなので、Entity/Physics/AnimationのUpdateを
    // Scene View用に二重実行しないことが重要です。
    if (m_ShowSceneView && m_SceneFramebuffer != nullptr)
    {
        // UI側は0サイズ・最小化中に新しいResize要求を作らず、最後の有効な寸法を保持します。
        // GPUへ渡す値も防御的に最低1 pixelへ丸めます。
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_SceneViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_SceneViewportHeight, 1.0f));

        m_SceneFramebuffer->Resize(width, height);

        // EditorCameraはCamera基底を実装しているため、View/Projectionを個別に取り出さず
        // Cameraオブジェクトそのものを描画経路へ渡します。
        // これにより将来SceneCameraを追加しても同じ描画入口を再利用できます。
        RenderSceneToFramebuffer(*m_SceneFramebuffer, m_EditorCamera);
    }

    if (m_ShowGameView && m_GameFramebuffer != nullptr)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_GameViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_GameViewportHeight, 1.0f));

        m_GameFramebuffer->Resize(width, height);

        // Game ViewはRuntime Cameraを変更せず、Scene本来のOnRender()をそのまま使います。
        RenderSceneToFramebuffer(*m_GameFramebuffer);
    }
}

void EditorLayer::RenderSceneToFramebuffer(Framebuffer& framebuffer)
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // ========================================================================
    // Runtime camera render target switching
    // ========================================================================
    // EditorLayerから見えるのはFramebuffer共通インターフェースだけです。
    // Bind()内部でOpenGLならFBO、将来DirectXなら対応するRender Targetを設定します。
    // 具体APIをEditorへ漏らさないことでRenderer backendの差し替え範囲を限定します。
    //
    // Scene::OnRender()内のClearを含む全描画命令は、Bindされたoff-screen描画先へ出力されます。
    // 描画終了後はUnbind()で通常の描画先へ戻し、後続のImGui描画へ影響を残しません。
    framebuffer.Bind();
    activeScene->OnRender();
    framebuffer.Unbind();
}

void EditorLayer::RenderSceneToFramebuffer(
    Framebuffer& framebuffer,
    const Camera& camera)
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    framebuffer.Bind();

    // ========================================================================
    // External camera rendering
    // ========================================================================
    // Scene ViewではRuntime Cameraを上書きせず、SceneViewportRendererへCamera基底参照を
    // そのまま渡して一時的な別視点として描画します。
    // EditorLayer / SceneViewportRendererの境界でView/Projectionを分解しないため、
    // EditorCameraだけでなく今後追加するSceneCameraも同じ経路を利用できます。
    //
    // SceneViewportRendererを実装していないSceneでもEditorを完全に描画不能にしないため、
    // 通常のOnRender()へfallbackします。この場合Scene ViewはRuntime Camera表示になります。
    SceneViewportRenderer* viewportRenderer = dynamic_cast<SceneViewportRenderer*>(activeScene);
    if (viewportRenderer != nullptr)
    {
        viewportRenderer->RenderWithCamera(camera);
    }
    else
    {
        activeScene->OnRender();
    }

    // 通常Scene描画で確定したDepth Bufferを利用してPicking IDだけを別Passで書き込みます。
    // ゲーム用ShaderへEditor専用出力を追加しないため、描画責務を分離したままEntity選択を実現できます。
    RenderEntityPickingPass(framebuffer, camera);

    framebuffer.Unbind();
}

void EditorLayer::OnImGuiRender(float dt)
{
    if (m_ViewportRenderingEnabled == false || m_Application == nullptr)
    {
        return;
    }

    // ========================================================================
    // Editor root window / DockSpace
    // ========================================================================
    // 各Panelより先にDockSpaceを作ります。
    // Dear ImGui DockingではDock可能Windowの受け皿となるDockSpace Nodeが先に必要です。
    BeginDockSpace();

    // ========================================================================
    // Scene View / Game View
    // ========================================================================
    // OnRender()で作ったColor Attachment Textureを、独立したImGui Windowとして表示します。
    // WindowのContentRegionサイズは次frameのFramebuffer Resizeにも利用します。
    if (m_ShowSceneView &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.SceneView) == true)
    {
        RenderSceneView();
    }
    else
    {
        // Windowを非表示にしたframe以降、以前のhover/focus状態をCamera入力へ残しません。
        m_SceneViewportHovered = false;
        m_SceneViewportFocused = false;
    }

    if (m_ShowGameView &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.GameView) == true)
    {
        RenderGameView();
    }

    // ========================================================================
    // Statistics
    // ========================================================================
    // EditorLayerはPanelとApplicationの橋渡しだけを行います。
    // SceneをPanel内へ永続保持しないため、SetScene()後もそのframeのActive Sceneへ追従します。
    if (m_ShowStatisticsPanel &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.Statistics) == true)
    {
        m_StatisticsPanel.OnImGuiRender(
            dt,
            m_Application->GetWindow(),
            m_Application->GetScene());
    }

    // ========================================================================
    // Animation Debug
    // ========================================================================
    // AnimationDebugPanelもSceneを所有せず、毎frame現在のActive Sceneを受け取ります。
    // これによりScene差し替え時に古いSceneへの参照を保持しません。
    if (m_ShowAnimationDebugPanel &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.AnimationDebug) == true)
    {
        m_AnimationDebugPanel.OnImGuiRender(m_Application->GetScene());
    }

    // ========================================================================
    // Scene Hierarchy
    // ========================================================================
    // HierarchyはActive SceneのEntity一覧を表示し、EditorLayerが所有する選択Entityだけを更新します。
    // 選択状態をPanelの外へ置くことで、Inspector / Scene View / Gizmoが同じEntityを利用できます。
    if (m_ShowSceneHierarchyPanel &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.SceneHierarchy) == true)
    {
        m_SceneHierarchyPanel.OnImGuiRender(
            m_Application->GetScene(),
            m_SelectedEntity);
    }

    // ========================================================================
    // Inspector
    // ========================================================================
    // Hierarchyが更新したm_SelectedEntityをそのまま渡します。
    // Inspector側に別の選択状態を作らないため、Hierarchyで選択した同じframeから
    // Component内容が表示され、将来のScene View / Gizmoとも選択対象が一致します。
    if (m_ShowInspectorPanel &&
        IsPanelOwnedByDearImGui(m_PanelUIOwnership.Inspector) == true)
    {
        m_InspectorPanel.OnImGuiRender(m_SelectedEntity);
    }

    EndDockSpace();
}

void EditorLayer::OnRavenUIRender(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    if (m_StatisticsRavenPanel.IsAttached() == true)
    {
        m_StatisticsRavenPanel.SetVisible(m_ShowStatisticsPanel);
        if (m_ShowStatisticsPanel == true)
        {
            m_StatisticsRavenPanel.Update(CaptureStatisticsSnapshot(
                dt, m_Application->GetWindow(), m_Application->GetScene()));
        }
    }

    if (m_AnimationDebugRavenPanel.IsAttached() == true)
    {
        m_AnimationDebugRavenPanel.SetVisible(m_ShowAnimationDebugPanel);
        if (m_ShowAnimationDebugPanel == true)
        {
            m_AnimationDebugRavenPanel.Update(CaptureAnimationDebugSnapshot(
                m_Application->GetScene(),
                m_AnimationDebugRavenPanel.GetSelectedBoneIndex()));
        }
    }

    if (m_SceneHierarchyRavenPanel.IsAttached() == true)
    {
        m_SceneHierarchyRavenPanel.SetVisible(m_ShowSceneHierarchyPanel);
        if (m_ShowSceneHierarchyPanel == true)
        {
            m_SceneHierarchyRavenPanel.Update(CaptureSceneHierarchySnapshot(
                m_Application->GetScene(), m_SelectedEntity));
        }
    }

    if (m_InspectorRavenPanel.IsAttached() == true)
    {
        m_InspectorRavenPanel.SetVisible(m_ShowInspectorPanel);
        if (m_ShowInspectorPanel == true)
        {
            m_InspectorRavenPanel.Update(CaptureInspectorSnapshot(m_SelectedEntity));
        }
    }

    const Window& window = m_Application->GetWindow();
    const math::Vec2 windowLogicalSize(
        static_cast<float>(window.GetWidth()),
        static_cast<float>(window.GetHeight()));
    const math::Vec2 windowFramebufferSize(
        static_cast<float>(window.GetFramebufferWidth()),
        static_cast<float>(window.GetFramebufferHeight()));

    if (m_SceneViewportRavenPanel.IsAttached() == true)
    {
        m_SceneViewportRavenPanel.SetVisible(m_ShowSceneView);
        if (m_ShowSceneView == true && m_SceneFramebuffer != nullptr)
        {
            m_SceneViewportRavenPanel.Update(
                m_SceneFramebuffer->GetColorAttachment());
            EditorViewportRenderTargetExtent extent;
            if (TryCalculateEditorViewportRenderTargetExtent(
                m_SceneViewportRavenPanel.GetViewportLogicalSize(),
                windowLogicalSize,
                windowFramebufferSize,
                extent) == true)
            {
                // Raven UI構築はScene描画後に行われるため、Resizeは次FrameのOnRender()で適用します。
                // Attachment交換前のFrameはUITextureViewのRefが旧TextureのLifetimeを維持します。
                m_SceneViewportWidth = static_cast<float>(extent.Width);
                m_SceneViewportHeight = static_cast<float>(extent.Height);
                m_EditorCamera.SetViewportSize(
                    m_SceneViewportWidth, m_SceneViewportHeight);
            }

            EditorGizmoViewportRect gizmoViewport;
            math::Vec2 gizmoPointerPosition{};
            const std::pair<float, float> mousePosition =
                Input::GetMousePosition();
            if (m_SceneViewportRavenPanel.TryGetGizmoViewportRect(
                    gizmoViewport) == true &&
                m_SceneViewportRavenPanel.TryMapScreenPositionToViewportLocal(
                    math::Vec2(mousePosition.first, mousePosition.second),
                    gizmoPointerPosition) == true)
            {
                // Gizmo計算はViewport local座標で行い、Panelには描画Snapshotだけを渡します。
                // Entity/Scene参照をRetained UI Treeへ保持させないことがLifetime上の境界です。
                m_SceneViewportRavenPanel.SetGizmoDrawData(
                    m_RavenViewportGizmo.BuildDrawData(
                        m_SelectedEntity,
                        m_EditorCamera,
                        gizmoViewport,
                        gizmoPointerPosition));
            }
            else
            {
                m_SceneViewportRavenPanel.SetGizmoDrawData(
                    EditorGizmoDrawData{});
            }
        }
    }

    if (m_GameViewportRavenPanel.IsAttached() == true)
    {
        m_GameViewportRavenPanel.SetVisible(m_ShowGameView);
        if (m_ShowGameView == true && m_GameFramebuffer != nullptr)
        {
            m_GameViewportRavenPanel.Update(
                m_GameFramebuffer->GetColorAttachment());
            EditorViewportRenderTargetExtent extent;
            if (TryCalculateEditorViewportRenderTargetExtent(
                m_GameViewportRavenPanel.GetViewportLogicalSize(),
                windowLogicalSize,
                windowFramebufferSize,
                extent) == true)
            {
                m_GameViewportWidth = static_cast<float>(extent.Width);
                m_GameViewportHeight = static_cast<float>(extent.Height);
            }
        }
    }
}

void EditorLayer::RenderSceneView()
{
    // ========================================================================
    // Scene View Window
    // ========================================================================
    // WindowPaddingを0にすることで、Framebuffer TextureをContentRegionいっぱいに表示します。
    // PushStyleVar()/PopStyleVar()をこのWindowだけに限定し、他PanelのPaddingへ影響させません。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Scene View", &m_ShowSceneView);
    ImGui::PopStyleVar();

    // ========================================================================
    // Scene View input state
    // ========================================================================
    // EditorCameraの入力はScene View上だけで有効にします。
    // Hoverだけでは別WindowがKeyboard focusを持つ場合があるため、Focusも併用して
    // Inspector編集中などにWASDがCameraへ入ることを避けます。
    m_SceneViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    m_SceneViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();

        if (available.x > 0.0f && available.y > 0.0f)
        {
            // ImGui WindowのContentRegionを次frameのFramebufferサイズとして保存します。
            // UI frame中にTextureを作り直さず、OnRender()でResizeすることでGPU Resource操作と
            // UI構築処理の責務を分離しています。
            m_SceneViewportWidth = available.x;
            m_SceneViewportHeight = available.y;

            // Projection行列はGPU Resourceではないため、ContentRegionを取得した時点で更新できます。
            // 次frameのOnRender()では最新Aspect RatioのEditor CameraからScene Viewを描画します。
            m_EditorCamera.SetViewportSize(
                m_SceneViewportWidth,
                m_SceneViewportHeight);

            if (m_SceneFramebuffer != nullptr)
            {
                // 現在のRendererAPIはOpenGLなのでRendererIDはOpenGL Texture IDとして扱えます。
                // この部分はFramebuffer抽象化の中でもまだbackend依存が残っている境界です。
                // DirectX/Vulkan対応時にはTexture/ImGui backendの抽象化と合わせて置き換えます。
                //
                // OpenGL Textureは左下原点ですがDear ImGuiのImageは通常左上から表示するため、
                // UV0=(0,1), UV1=(1,0)としてY方向を反転し、画面を正立させます。
                const ImTextureID textureID = static_cast<ImTextureID>(
                    m_SceneFramebuffer->GetColorAttachmentRendererID());

                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));

                const ImVec2 imageMin = ImGui::GetItemRectMin();
                const ImVec2 imageMax = ImGui::GetItemRectMax();

                // ====================================================================
                // Translate Gizmo
                // ====================================================================
                // GizmoはFramebuffer Textureの上へDear ImGuiのDrawListで重ねます。
                // Editor専用UIなのでゲーム内UI/Rendererには依存させません。
                // 軸をクリックしたframeはScene Pickingを抑止し、Gizmo操作とEntity選択が競合しないようにします。
                const bool gizmoConsumedMouse = RenderTranslateGizmo(
                    m_SelectedEntity,
                    m_EditorCamera,
                    imageMin.x,
                    imageMin.y,
                    imageMax.x,
                    imageMax.y);

                // ====================================================================
                // Scene View Entity Picking
                // ====================================================================
                // Image Itemそのものが左クリックされた時だけPickingします。
                // Window全体のhover判定ではなくImage矩形を使うことで、将来Toolbar等を追加しても
                // UI上のクリックをScene選択として誤認しません。
                if (gizmoConsumedMouse == false
                    && ImGui::IsItemHovered()
                    && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    Scene* activeScene =
                        (m_Application != nullptr) ? m_Application->GetScene() : nullptr;

                    if (activeScene != nullptr
                        && m_SceneFramebuffer->GetColorAttachmentCount() > 1)
                    {
                        const ImVec2 mousePosition = ImGui::GetMousePos();

                        const float imageWidth = imageMax.x - imageMin.x;
                        const float imageHeight = imageMax.y - imageMin.y;

                        if (imageWidth > 0.0f && imageHeight > 0.0f)
                        {
                            const float localX = mousePosition.x - imageMin.x;
                            const float localY = mousePosition.y - imageMin.y;

                            const int framebufferWidth =
                                static_cast<int>(m_SceneFramebuffer->GetWidth());
                            const int framebufferHeight =
                                static_cast<int>(m_SceneFramebuffer->GetHeight());

                            if (framebufferWidth > 0 && framebufferHeight > 0)
                            {
                                // ImGui Imageは左上原点、OpenGL Framebufferは左下原点です。
                                // さらにWindow表示サイズとFramebuffer実サイズが1frameだけ異なる場合にも
                                // 対応できるよう、単純なpixel加算ではなく正規化して実サイズへ変換します。
                                int pixelX = static_cast<int>(
                                    (localX / imageWidth)
                                    * static_cast<float>(framebufferWidth));
                                int pixelY = static_cast<int>(
                                    (1.0f - (localY / imageHeight))
                                    * static_cast<float>(framebufferHeight));

                                pixelX = std::clamp(pixelX, 0, framebufferWidth - 1);
                                pixelY = std::clamp(pixelY, 0, framebufferHeight - 1);

                                PickSceneEntityAt(
                                    static_cast<std::uint32_t>(pixelX),
                                    static_cast<std::uint32_t>(pixelY));
                            }
                        }
                    }
                }
            }
        }
    }

    // ImGui::Begin()の戻り値に関係なく、Beginを呼んだ場合は必ずEndが必要です。
    ImGui::End();
}

void EditorLayer::RenderGameView()
{
    // ========================================================================
    // Game View Window
    // ========================================================================
    // Scene Viewと同じTexture表示経路を持ちますがFramebufferは共有しません。
    // Scene ViewへEditor Camera / Gizmo等を追加しても、Game ViewはRuntime Cameraの
    // 純粋なゲーム表示を保ちます。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Game View", &m_ShowGameView);
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();

        if (available.x > 0.0f && available.y > 0.0f)
        {
            m_GameViewportWidth = available.x;
            m_GameViewportHeight = available.y;

            if (m_GameFramebuffer != nullptr)
            {
                // Scene Viewと同じ理由で、現在のOpenGL backendではTexture IDをImGuiへ渡し、
                // Y方向を反転して表示します。
                const ImTextureID textureID = static_cast<ImTextureID>(
                    m_GameFramebuffer->GetColorAttachmentRendererID());

                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
    }

    ImGui::End();
}

void EditorLayer::OnEvent(Event& event)
{
    // ========================================================================
    // Editor input routing
    // ========================================================================
    // EditorCamera自体はInput pollingですが、Scene ViewがFocusを所有する間のNavigation Keyは
    // Core Event側でも消費します。Inputの物理状態は維持されるためCameraは操作でき、同じKey Eventや
    // Character Eventだけが背後のRuntime Layer / Sceneへ二重配送されることを防ぎます。
    if (m_SceneViewportRavenPanel.IsAttached() == false ||
        m_SceneViewportRavenPanel.HasKeyboardFocus() == false)
    {
        return;
    }

    if (event.GetEventType() == EventType::KeyPressed)
    {
        const KeyPressedEvent& keyEvent = static_cast<const KeyPressedEvent&>(event);
        const int keyCode = keyEvent.GetKeyCode();
        const bool rightMouseDown =
            Input::IsMouseButtonPressed(Mouse::Right) == true;
        const bool controlDown =
            Input::IsKeyPressed(Key::LeftControl) == true ||
            Input::IsKeyPressed(Key::RightControl) == true;

        // Camera Fly中はW/E/Qを移動へ予約します。通常時だけGizmo ModeとSpaceを切り替えます。
        if (keyEvent.IsRepeat() == false && rightMouseDown == false &&
            m_RavenViewportGizmo.IsActive() == false)
        {
            if (keyCode == Key::W)
            {
                SetEditorGizmoOperation(EditorGizmoOperation::Translate);
                event.Handled = true;
            }
            else if (keyCode == Key::E)
            {
                SetEditorGizmoOperation(EditorGizmoOperation::Rotate);
                event.Handled = true;
            }
            else if (keyCode == Key::R)
            {
                SetEditorGizmoOperation(EditorGizmoOperation::Scale);
                event.Handled = true;
            }
            else if (keyCode == Key::Q)
            {
                ToggleEditorGizmoSpace();
                event.Handled = true;
            }
            else if (controlDown == true && keyCode == Key::Z)
            {
                const bool shiftDown =
                    Input::IsKeyPressed(Key::LeftShift) == true ||
                    Input::IsKeyPressed(Key::RightShift) == true;
                if (shiftDown == true)
                {
                    RedoEditorCommand();
                }
                else
                {
                    UndoEditorCommand();
                }
                event.Handled = true;
            }
            else if (controlDown == true && keyCode == Key::Y)
            {
                RedoEditorCommand();
                event.Handled = true;
            }
        }

        if (event.Handled == false)
        {
            event.Handled = IsEditorCameraNavigationKey(keyCode) ||
                (m_RavenViewportGizmo.IsActive() == true &&
                    (IsControlKey(keyCode) == true ||
                        IsEditorGizmoShortcutKey(keyCode) == true));
        }
    }
    else if (event.GetEventType() == EventType::KeyReleased)
    {
        const KeyReleasedEvent& keyEvent = static_cast<const KeyReleasedEvent&>(event);
        event.Handled = IsEditorCameraNavigationKey(keyEvent.GetKeyCode()) ||
            (m_RavenViewportGizmo.IsActive() == true &&
                (IsControlKey(keyEvent.GetKeyCode()) == true ||
                    IsEditorGizmoShortcutKey(keyEvent.GetKeyCode()) == true));
    }
    else if (event.GetEventType() == EventType::CharacterTyped)
    {
        const CharacterTypedEvent& characterEvent =
            static_cast<const CharacterTypedEvent&>(event);
        event.Handled = IsEditorViewportOwnedCharacter(
            characterEvent.GetCodepoint());
    }
}

void EditorLayer::PickSceneEntityAt(
    std::uint32_t pixelX,
    std::uint32_t pixelY)
{
    Scene* activeScene =
        m_Application != nullptr ? m_Application->GetScene() : nullptr;
    if (activeScene == nullptr || m_SceneFramebuffer == nullptr ||
        m_SceneFramebuffer->GetColorAttachmentCount() <= 1 ||
        pixelX >= m_SceneFramebuffer->GetWidth() ||
        pixelY >= m_SceneFramebuffer->GetHeight())
    {
        return;
    }

    const int entityID = m_SceneFramebuffer->ReadPixel(
        1,
        static_cast<int>(pixelX),
        static_cast<int>(pixelY));
    if (entityID <= 0)
    {
        // Geometryが存在しない背景をクリックした場合は選択解除します。
        SetSelectedEntity(Entity{});
        return;
    }

    // Picking AttachmentへはEntityIndexだけを書き込みます。
    // GenerationはScene側の正規データから現在値を取得してEntityを再構築し、
    // Destroy/Index再利用後の古いHandleをEditor選択へ持ち込まないようにします。
    const EntityIndex entityIndex = static_cast<EntityIndex>(entityID);
    try
    {
        const EntityGeneration generation =
            activeScene->GetEntityGeneration(entityIndex);
        Entity pickedEntity(entityIndex, generation, activeScene);
        SetSelectedEntity(activeScene->IsEntityAlive(pickedEntity) == true
            ? pickedEntity
            : Entity{});
    }
    catch (const std::out_of_range&)
    {
        // GPU Bufferへ古いIndexが残った場合でもEditorを落とさず選択解除します。
        // 通常はPicking Pass直後に読むため発生しませんが、安全な境界として扱います。
        SetSelectedEntity(Entity{});
    }
}

void EditorLayer::ValidateSelectedEntity()
{
    if (m_Application == nullptr)
    {
        SetSelectedEntity(Entity{});
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        SetSelectedEntity(Entity{});
        return;
    }

    // Entity::operator bool()はexplicitなので明示的にboolへ変換します。
    // Invalid Entityは「選択なし」として扱い、それ以上のScene/Generation検証は不要です。
    if (static_cast<bool>(m_SelectedEntity) == false)
    {
        return;
    }

    // Entity自身が保持するSceneと現在のActive Sceneを比較します。
    // Index/Generationが偶然一致しても別SceneのEntityは別物なので、Scene切替時に選択を解除します。
    if (m_SelectedEntity.GetScene() != activeScene)
    {
        SetSelectedEntity(Entity{});
        return;
    }

    // Generationまで含めた生存確認です。
    // Destroy後に同じIndexが再利用されても、古いGenerationを持つ選択Entityはここで無効化されます。
    if (activeScene->IsEntityAlive(m_SelectedEntity) == false)
    {
        SetSelectedEntity(Entity{});
    }
}

void EditorLayer::SetSelectedEntity(Entity entity)
{
    const bool changed =
        m_SelectedEntity.GetScene() != entity.GetScene()
        || m_SelectedEntity.GetHandle() != entity.GetHandle();
    if (changed == true)
    {
        // ImGui版は編集開始SnapshotをPanel内部に保持するため、選択境界で明示的に破棄します。
        // Raven UI版は次のSnapshot更新でHandle差を検出し、同じ境界を適用します。
        m_InspectorPanel.ResetEditingState();
    }
    m_SelectedEntity = entity;
}

void EditorLayer::BeginDockSpace()
{
    // ========================================================================
    // Fullscreen DockSpace Host Window
    // ========================================================================
    // Main Viewport全体を覆うHost Windowを作り、その内部へEditor Panel用DockSpaceを配置します。
    // Host Window自身は通常のPanelではなく、MenuBarとDockSpaceを保持するRoot Containerです。
    //
    // 以前はRuntime SceneがMain framebufferへ直接表示されていたため、Host Windowに
    // NoBackground + PassthruCentralNodeを指定して背後のゲーム画面を透過表示していました。
    // 現在はScene/Game映像を専用Framebuffer TextureとしてImGui Windowへ表示するため、
    // その暫定透過処理は不要になっています。
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Host WindowはMain Viewportと一体化させるため、通常WindowのTitleBar / Resize / Move等を
    // 無効にします。DockされたScene ViewやInspector等は独立Windowなので通常どおり操作できます。
    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_MenuBar;
    windowFlags |= ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    windowFlags |= ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize;
    windowFlags |= ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    windowFlags |= ImGuiWindowFlags_NoNavFocus;

    // Main Viewportを余白なしでDock領域として利用します。
    // Style変更はHost WindowのBegin()までに限定し、他Panelへ漏れないよう直後にPopします。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("RavenEditorDockSpaceHost", nullptr, windowFlags);
    m_DockSpaceBegun = true;

    ImGui::PopStyleVar(3);

    // ========================================================================
    // DockSpace Node
    // ========================================================================
    // GetID()から安定したDockSpace IDを生成します。
    // ini保存が有効な場合、Dear ImGuiはこのIDを基準にDock Layoutを保存・復元します。
    const ImGuiID dockSpaceId = ImGui::GetID("RavenEditorDockSpace");

	/* 初期の画面レイアウトは以下のように想定しています。
    ┌─────────────────────────────────────────────────────┐
    │ Menu                                                                                                     │
    ├────────────┬─────────────────────────┬──────────────┤
    │                        │                                                  │                            │
    │  Scene                 │                  Scene View                      │         Inspector          │
    │ Hierarchy              │                  Game View[tab]                  │                            │
    │                        │                                                  │                            │
    ├────────────┴─────────────────────────┴──────────────┤
    │                          Raven Debug / Statistics | Animation Debug[tabs]                                │
    └─────────────────────────────────────────────────────┘
    */
    if (ImGui::DockBuilderGetNode(dockSpaceId) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockSpaceId;

        const ImGuiID leftDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Left,
            0.18f,
            nullptr,
            &centerDockId);

        const ImGuiID rightDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.22f,
            nullptr,
            &centerDockId);

        const ImGuiID bottomDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Down,
            0.25f,
            nullptr,
            &centerDockId);

        // Viewportは中央を最大限利用します。
        // 同じDock Nodeへ登録することでScene/Gameをタブ切り替えにします。
        ImGui::DockBuilderDockWindow("Scene View", centerDockId);
        ImGui::DockBuilderDockWindow("Game View", centerDockId);

        // Entity操作系は左右へ分離します。
        ImGui::DockBuilderDockWindow("Scene Hierarchy", leftDockId);
        ImGui::DockBuilderDockWindow("Inspector", rightDockId);

        // Debug系Windowは画面下部へまとめます。
        ImGui::DockBuilderDockWindow("Raven Debug / Statistics", bottomDockId);
        ImGui::DockBuilderDockWindow("Animation Debug", bottomDockId);

        ImGui::DockBuilderFinish(dockSpaceId);
    }

    // Scene/Game ViewがTexture Windowとして独立したためPassthruCentralNodeは使用しません。
    // DockSpace自身が通常背景を描画することで、Viewport Windowを閉じてもMain Sceneが
    // Host Windowの背後から意図せず透けて見えることを防ぎます。
    ImGui::DockSpace(
        dockSpaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_None);

    // Editor全体のMenuBarはHost Windowへ集約します。
    // 各Panel同士を依存させず、表示状態等のEditor全体操作をここから制御します。
    RenderMenuBar();
}

void EditorLayer::EndDockSpace()
{
    if (m_DockSpaceBegun == false)
    {
        return;
    }

    // ImGui::Begin()の戻り値に関係なくBeginを呼んだ場合は対応するEndが必要です。
    ImGui::End();
    m_DockSpaceBegun = false;
}

void EditorLayer::RenderMenuBar()
{
    if (ImGui::BeginMenuBar() == false)
    {
        return;
    }

    // ========================================================================
    // View menu
    // ========================================================================
    // Editor Windowの表示状態はEditorLayerで一元管理します。
    // Panel自身に他Panelの存在を知らせないことで、Panel間の不要な依存を作りません。
    if (ImGui::BeginMenu("View"))
    {
        // Scene/Game Viewも通常Panelと同じように表示状態を切り替えられます。
        ImGui::MenuItem("Scene View", nullptr, &m_ShowSceneView);
        ImGui::MenuItem("Game View", nullptr, &m_ShowGameView);
        ImGui::Separator();

        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatisticsPanel);
        ImGui::MenuItem("Animation Debug", nullptr, &m_ShowAnimationDebugPanel);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchyPanel);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspectorPanel);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace Raven
