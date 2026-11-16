#include "Raven/Editor/Panels/InspectorController.h"
#include "Raven/Editor/Panels/InspectorRavenPanel.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/Command/InspectorEditCommand.h"
#include "Raven/Editor/Command/SetPrimaryCameraCommand.h"
#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Scene/Scene.h"
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace Raven
{
namespace
{
constexpr float CameraProjectionCompareEpsilon = 0.000001f;
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

InspectorController::InspectorController() = default;

void InspectorController::Bind(InspectorRavenPanel& panel, SceneProvider scene, SelectionProvider selection)
{
    m_SceneProvider = std::move(scene);
    m_SelectionProvider = std::move(selection);
    ResetPresentation();
    // 別Controllerへ同じViewを接続し直す場合も、旧Before/Revisionを持ち越しません。
    panel.Update(InspectorSnapshot{});
    panel.SetSnapshotProvider([this]() { return PullSnapshot(); });
    panel.SetTransformPreviewRequestedHandler(
        [this](EntityHandle handle, const InspectorTransformSnapshot& requested)
        {
            return PreviewTransform(handle, requested);
        });
    panel.SetTransformCommitRequestedHandler(
        [this](EntityHandle handle, const InspectorTransformSnapshot& beforeSnapshot)
        {
            return CommitTransform(handle, beforeSnapshot);
        });
    panel.SetCameraPreviewRequestedHandler(
        [this](EntityHandle handle, const InspectorCameraSnapshot& requested)
        {
            return PreviewCamera(handle, requested);
        });
    panel.SetCameraCommitRequestedHandler(
        [this](EntityHandle handle, const InspectorCameraSnapshot& before)
        {
            return CommitCamera(handle, before);
        });
    panel.SetPrimaryCameraRequestedHandler(
        [this](EntityHandle handle)
        {
            return SetPrimaryCamera(handle);
        });
    panel.SetRigidBodyPreviewRequestedHandler(
        [this](EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested)
        {
            return PreviewRigidBody(handle, field, requested);
        });
    panel.SetRigidBodyCommitRequestedHandler(
        [this](EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& before)
        {
            return CommitRigidBody(handle, field, before);
        });
    panel.SetRigidBodyChangeRequestedHandler(
        [this](EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested)
        {
            return ChangeRigidBody(handle, field, requested);
        });
    panel.SetColliderPreviewRequestedHandler(
        [this](EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested)
        {
            return PreviewCollider(handle, field, requested);
        });
    panel.SetColliderCommitRequestedHandler(
        [this](EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& before)
        {
            return CommitCollider(handle, field, before);
        });
    panel.SetColliderChangeRequestedHandler(
        [this](EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested)
        {
            return ChangeCollider(handle, field, requested);
        });
}
void InspectorController::Unbind(InspectorRavenPanel& panel)
{
    panel.SetSnapshotProvider({});
    panel.SetTransformPreviewRequestedHandler({});
    panel.SetTransformCommitRequestedHandler({});
    panel.SetCameraPreviewRequestedHandler({});
    panel.SetCameraCommitRequestedHandler({});
    panel.SetPrimaryCameraRequestedHandler({});
    panel.SetRigidBodyPreviewRequestedHandler({});
    panel.SetRigidBodyCommitRequestedHandler({});
    panel.SetRigidBodyChangeRequestedHandler({});
    panel.SetColliderPreviewRequestedHandler({});
    panel.SetColliderCommitRequestedHandler({});
    panel.SetColliderChangeRequestedHandler({});
    panel.Update(InspectorSnapshot{});
    m_SceneProvider = {};
    m_SelectionProvider = {};
    ResetPresentation();
}
void InspectorController::ResetPresentation()
{
    m_PresentationKnown = false;
    m_PresentationEditable = false;
    m_PresentedScene = nullptr;
    m_PresentedHandle = InvalidEntityHandle;
}
InspectorSnapshot InspectorController::PullSnapshot()
{
    Scene* scene = m_SceneProvider != nullptr ? m_SceneProvider() : nullptr;
    const InspectorSelection selection = m_SelectionProvider != nullptr
        ? m_SelectionProvider() : InspectorSelection{};
    const EntityHandle handle = selection.Primary.GetHandle();
    // Entityのbool変換は旧Sceneを逆参照するため、所属Sceneの一致確認より先に使いません。
    const bool editable = scene != nullptr && selection.Count == 1u &&
        selection.Primary.GetScene() == scene && scene->IsEntityAlive(handle) == true;
    const std::uint64_t epoch = GetEditorCommandHistoryEpoch();
    if (m_PresentationKnown == false || m_PresentedScene != scene || m_PresentedHandle != handle ||
        m_PresentedCount != selection.Count || m_PresentationEditable != editable || m_PresentedHistoryEpoch != epoch)
    {
        // 世代が枯渇した際に以前の編集Sessionと同一視せず、以後は表示/編集を拒否します。
        if (m_SelectionRevision == std::numeric_limits<std::uint64_t>::max())
        {
            ResetPresentation();
            return {};
        }
        ++m_SelectionRevision;
    }
    m_PresentationKnown = true;
    m_PresentationEditable = editable;
    m_PresentedScene = scene;
    m_PresentedHandle = handle;
    m_PresentedCount = selection.Count;
    m_PresentedHistoryEpoch = epoch;
    InspectorSnapshot snapshot = editable == true ? CaptureInspectorSnapshot(Entity(handle, scene)) : InspectorSnapshot{};
    snapshot.HasSelection = selection.Count != 0u;
    snapshot.SelectionCount = selection.Count;
    snapshot.SelectionRevision = m_SelectionRevision;
    return snapshot;
}
Scene* InspectorController::ResolveTarget(EntityHandle handle) const
{
    if (m_PresentationKnown == false || m_PresentationEditable == false ||
        m_SceneProvider == nullptr || m_SelectionProvider == nullptr ||
        m_PresentedHistoryEpoch == 0u || m_PresentedHistoryEpoch != GetEditorCommandHistoryEpoch())
    {
        return nullptr;
    }
    Scene* scene = m_SceneProvider();
    const InspectorSelection selection = m_SelectionProvider();
    // Pull後に選択/Sceneが変わっても、次の描画更新を待たず古いHandle要求を拒否します。
    if (scene == nullptr || scene != m_PresentedScene || selection.Count != 1u ||
        selection.Primary.GetScene() != scene || selection.Primary.GetHandle() != handle ||
        handle != m_PresentedHandle || scene->IsEntityAlive(handle) == false)
    {
        return nullptr;
    }
    return scene;
}

bool InspectorController::PreviewTransform(EntityHandle handle, const InspectorTransformSnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
    TransformComponent transform;
    transform.Position = requested.Position;
    transform.Rotation = requested.Rotation;
    transform.Scale = requested.Scale;
    return activeScene->SetWorldTransform(handle, transform);
}

bool InspectorController::CommitTransform(EntityHandle handle, const InspectorTransformSnapshot& beforeSnapshot)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

bool InspectorController::PreviewCamera(EntityHandle handle, const InspectorCameraSnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
    {
        return false;
    }

    Entity entity(handle, activeScene);
    if (activeScene->IsEntityAlive(entity) == false)
    {
        return false;
    }
    return ApplyCameraProjectionSnapshot(entity, requested);
}

bool InspectorController::CommitCamera(EntityHandle handle, const InspectorCameraSnapshot& before)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

bool InspectorController::SetPrimaryCamera(EntityHandle handle)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
    {
        return false;
    }
    return ExecuteAndRecordEditorCommand(
        std::make_unique<SetPrimaryCameraCommand>(activeScene, handle));
}

bool InspectorController::PreviewRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
    {
        return false;
    }
    return ApplyRigidBodyEditValue(
        Entity(handle, activeScene),
        InspectorRigidBodyEditValue{ field, requested });
}

bool InspectorController::CommitRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& before)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

bool InspectorController::ChangeRigidBody(EntityHandle handle, InspectorRigidBodyField field, const InspectorRigidBodySnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

bool InspectorController::PreviewCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
    {
        return false;
    }
    return ApplyColliderEditValue(
        Entity(handle, activeScene),
        InspectorColliderEditValue{ field, requested });
}

bool InspectorController::CommitCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& before)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

bool InspectorController::ChangeCollider(EntityHandle handle, InspectorColliderField field, const InspectorColliderSnapshot& requested)
{
    Scene* activeScene = ResolveTarget(handle);
    if (activeScene == nullptr)
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
}

} // namespace Raven
