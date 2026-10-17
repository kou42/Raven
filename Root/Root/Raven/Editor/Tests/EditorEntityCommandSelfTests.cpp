#include "Raven/Editor/Tests/EditorEntityCommandSelfTests.h"

#include "Raven/Editor/Command/DeleteEntityCommand.h"
#include "Raven/Editor/Command/InspectorEditCommand.h"
#include "Raven/Editor/Command/TransformCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Editor/Panels/InspectorSnapshot.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <cmath>
#include <memory>

namespace Raven::tests
{
namespace
{

bool ValidateCameraProjection(Entity entity)
{
    return entity.HasComponent<CameraComponent>();
}

bool ApplyCameraProjection(Entity entity, const InspectorCameraSnapshot& value)
{
    if (ValidateCameraProjection(entity) == false)
    {
        return false;
    }
    entity.GetComponent<CameraComponent>().Camera.SetPerspective(
        value.VerticalFov, value.NearClip, value.FarClip);
    return true;
}

bool CameraProjectionEqual(
    const InspectorCameraSnapshot& left,
    const InspectorCameraSnapshot& right)
{
    constexpr float Epsilon = 0.000001f;
    return std::fabs(left.VerticalFov - right.VerticalFov) <= Epsilon
        && std::fabs(left.NearClip - right.NearClip) <= Epsilon
        && std::fabs(left.FarClip - right.FarClip) <= Epsilon;
}

} // namespace

void RunEditorEntityCommandSelfTests()
{
    Scene scene;
    Entity entity = scene.CreateEntity("Delete Target");
    const EntityHandle deletedHandle = entity.GetHandle();

    TransformComponent expectedTransform;
    expectedTransform.Position = math::Vec3(1.0f, 2.0f, 3.0f);
    expectedTransform.Rotation = math::Vec3(0.1f, 0.2f, 0.3f);
    expectedTransform.Scale = math::Vec3(2.0f, 3.0f, 4.0f);
    entity.GetComponent<TransformComponent>() = expectedTransform;

    RigidBodyComponent expectedRigidBody;
    expectedRigidBody.SetMass(8.0f);
    expectedRigidBody.LinearVelocity = math::Vec3(4.0f, 5.0f, 6.0f);
    entity.AddComponent<RigidBodyComponent>(expectedRigidBody);

    ColliderComponent expectedCollider;
    expectedCollider.Type = ColliderType::Capsule;
    expectedCollider.Radius = 0.75f;
    expectedCollider.HalfLength = 1.25f;
    entity.AddComponent<ColliderComponent>(expectedCollider);

    SetEditorCommandHistoryScene(&scene);
    auto command = std::make_unique<DeleteEntityCommand>(&scene, entity);
    DeleteEntityCommand* recordedCommand = command.get();
    assert(ExecuteAndRecordEditorCommand(std::move(command)) == true);
    assert(scene.IsEntityAlive(deletedHandle) == false);
    EntityHandle remappedBefore{};
    EntityHandle remappedAfter{};
    assert(recordedCommand->GetRestoredEntityHandles(
        remappedBefore, remappedAfter) == false);

    assert(UndoEditorCommand() == true);
    assert(recordedCommand->GetRestoredEntityHandles(
        remappedBefore, remappedAfter) == true);
    assert(remappedBefore == deletedHandle);
    Entity restored(remappedAfter, &scene);
    assert(static_cast<bool>(restored) == true);
    assert(scene.IsEntityAlive(restored) == true);
    assert(restored.GetHandle() != deletedHandle);
    assert(restored.GetComponent<TagComponent>().Tag == "Delete Target");

    const TransformComponent& actualTransform = restored.GetComponent<TransformComponent>();
    assert(actualTransform.Position.x == expectedTransform.Position.x);
    assert(actualTransform.Position.y == expectedTransform.Position.y);
    assert(actualTransform.Position.z == expectedTransform.Position.z);
    assert(actualTransform.Rotation.x == expectedTransform.Rotation.x);
    assert(actualTransform.Scale.z == expectedTransform.Scale.z);

    assert(restored.HasComponent<RigidBodyComponent>() == true);
    const RigidBodyComponent& actualRigidBody = restored.GetComponent<RigidBodyComponent>();
    assert(actualRigidBody.Mass == expectedRigidBody.Mass);
    assert(actualRigidBody.InverseMass == expectedRigidBody.InverseMass);
    assert(actualRigidBody.LinearVelocity.y == expectedRigidBody.LinearVelocity.y);

    assert(restored.HasComponent<ColliderComponent>() == true);
    const ColliderComponent& actualCollider = restored.GetComponent<ColliderComponent>();
    assert(actualCollider.Type == expectedCollider.Type);
    assert(actualCollider.Radius == expectedCollider.Radius);
    assert(actualCollider.HalfLength == expectedCollider.HalfLength);

    const EntityHandle restoredHandle = restored.GetHandle();
    assert(RedoEditorCommand() == true);
    assert(scene.IsEntityAlive(restoredHandle) == false);
    assert(UndoEditorCommand() == true);
    assert(recordedCommand->GetRestoredEntityHandles(
        remappedBefore, remappedAfter) == true);
    assert(remappedBefore == restoredHandle);
    assert(remappedAfter != restoredHandle);

    restored = Entity(remappedAfter, &scene);
    ClearEditorCommandHistory();
    const TransformComponent beforePreview =
        restored.GetComponent<TransformComponent>();
    TransformComponent afterPreview = beforePreview;
    afterPreview.Position = math::Vec3(10.0f, 20.0f, 30.0f);

    // Raven UI InspectorはPreviewを既に適用してから、Focus喪失時にCommandだけを記録します。
    restored.GetComponent<TransformComponent>() = afterPreview;
    assert(RecordAlreadyExecutedEditorCommand(
        std::make_unique<TransformCommand>(
            restored, beforePreview, afterPreview)) == true);
    assert(restored.GetComponent<TransformComponent>().Position.x == 10.0f);
    assert(UndoEditorCommand() == true);
    assert(restored.GetComponent<TransformComponent>().Position.x
        == beforePreview.Position.x);
    assert(RedoEditorCommand() == true);
    assert(restored.GetComponent<TransformComponent>().Position.z == 30.0f);

    ClearEditorCommandHistory();
    restored.AddComponent<CameraComponent>();
    CameraComponent& cameraComponent = restored.GetComponent<CameraComponent>();
    const InspectorCameraSnapshot cameraBefore{
        cameraComponent.Camera.GetPerspectiveVerticalFov(),
        cameraComponent.Camera.GetPerspectiveNearClip(),
        cameraComponent.Camera.GetPerspectiveFarClip(),
        cameraComponent.Camera.GetAspectRatio(),
        cameraComponent.Primary };
    InspectorCameraSnapshot cameraAfter = cameraBefore;
    cameraAfter.VerticalFov = 1.0f;
    cameraAfter.NearClip = 0.25f;
    cameraAfter.FarClip = 2500.0f;
    assert(ApplyCameraProjection(restored, cameraAfter) == true);
    assert(RecordAlreadyExecutedEditorCommand(
        std::make_unique<InspectorEditCommand<InspectorCameraSnapshot>>(
            restored,
            cameraBefore,
            cameraAfter,
            &ValidateCameraProjection,
            &ApplyCameraProjection,
            &CameraProjectionEqual)) == true);
    assert(UndoEditorCommand() == true);
    assert(cameraComponent.Camera.GetPerspectiveNearClip() == cameraBefore.NearClip);
    assert(RedoEditorCommand() == true);
    assert(cameraComponent.Camera.GetPerspectiveFarClip() == cameraAfter.FarClip);

    // Generationが変わった後は、古い編集Commandを同じIndexの別Entityへ適用しません。
    scene.DestroyEntity(restored);
    scene.CreateEntity("Reused Index Candidate");
    assert(UndoEditorCommand() == false);

    // Sceneはstack上の一時Objectなので、終了前に非所有参照とCommand Snapshotを破棄します。
    SetEditorCommandHistoryScene(nullptr);
}

} // namespace Raven::tests
