#include "Raven/Editor/Tests/EditorEntityCommandSelfTests.h"

#include "Raven/Editor/Command/DeleteEntityCommand.h"
#include "Raven/Editor/Command/ReparentEntityCommand.h"
#include "Raven/Editor/Panels/SceneHierarchySnapshot.h"
#include "Raven/Editor/Command/InspectorEditCommand.h"
#include "Raven/Editor/Command/SetPrimaryCameraCommand.h"
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

    // SG1: Reparent履歴と親削除UndoのHandle remapを同じ履歴上で検証します。
    {
        Scene hierarchy;
        Entity firstParent = hierarchy.CreateEntity("FirstParent");
        Entity secondParent = hierarchy.CreateEntity("SecondParent");
        Entity child = hierarchy.CreateEntity("Child");
        assert(hierarchy.SetEntityParent(child.GetHandle(), firstParent.GetHandle()) == true);
        child.GetComponent<TransformComponent>().Position = math::Vec3(7.0f, 8.0f, 9.0f);
        SetEditorCommandHistoryScene(&hierarchy);
        assert(ExecuteAndRecordEditorCommand(std::make_unique<ReparentEntityCommand>(
            &hierarchy, child.GetHandle(), secondParent.GetHandle())) == true);
        assert(UndoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == firstParent.GetHandle());
        assert(RedoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == secondParent.GetHandle());
        const EntityHandle oldParent = secondParent.GetHandle();
        auto deleteParent = std::make_unique<DeleteEntityCommand>(&hierarchy, secondParent);
        DeleteEntityCommand* recordedDelete = deleteParent.get();
        assert(ExecuteAndRecordEditorCommand(std::move(deleteParent)) == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == InvalidEntityHandle);
        assert(CaptureSceneHierarchySnapshot(&hierarchy, secondParent).Selected == InvalidEntityHandle);
        assert(UndoEditorCommand() == true);
        EntityHandle oldHandle;
        EntityHandle newHandle;
        assert(recordedDelete->GetRestoredEntityHandles(oldHandle, newHandle) == true);
        assert(oldHandle == oldParent && newHandle != oldParent);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == newHandle);
        assert(CaptureSceneHierarchySnapshot(&hierarchy, secondParent).Selected == InvalidEntityHandle);
        assert(CaptureSceneHierarchySnapshot(&hierarchy, Entity(newHandle, &hierarchy)).Selected == newHandle);
        const auto snapshot = CaptureSceneHierarchySnapshot(&hierarchy, child);
        assert(snapshot.Selected == child.GetHandle());
        assert(snapshot.Nodes.size() == 3u);
        bool childFound = false;
        for (const auto& node : snapshot.Nodes)
        {
            if (node.Handle == child.GetHandle())
            {
                childFound = true;
                assert(node.Parent == newHandle);
                assert(node.DisplayName == "Child");
            }
        }
        assert(childFound == true);
        // Reparentのm_Afterも復元された親Handleへ更新済みであることを確認します。
        assert(UndoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == firstParent.GetHandle());
        assert(RedoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == newHandle);
        assert(RedoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == InvalidEntityHandle);
        assert(UndoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) != newHandle);
        assert(child.GetComponent<TransformComponent>().Position == math::Vec3(7.0f, 8.0f, 9.0f));
        // 削除中に別の親へ移された子を、Undoが勝手に奪い返さない契約です。
        assert(RedoEditorCommand() == true);
        assert(hierarchy.SetEntityParent(child.GetHandle(), firstParent.GetHandle()) == true);
        assert(UndoEditorCommand() == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == firstParent.GetHandle());
        Scene other;
        Entity foreign = other.CreateEntity("Foreign");
        DeleteEntityCommand foreignDelete(&hierarchy, foreign);
        assert(foreignDelete.CanExecute(hierarchy) == false);
        assert(CaptureSceneHierarchySnapshot(&hierarchy, foreign).Selected == InvalidEntityHandle);
        SetEditorCommandHistoryScene(nullptr);
    }

    // 保存していた親が削除・Index再利用されても、Undoが別Entityへ接続しないことを確認します。
    {
        Scene hierarchy;
        Entity grandparent = hierarchy.CreateEntity("Grandparent");
        Entity parent = hierarchy.CreateEntity("Parent");
        Entity child = hierarchy.CreateEntity("Child");
        assert(hierarchy.SetEntityParent(parent.GetHandle(), grandparent.GetHandle()) == true);
        assert(hierarchy.SetEntityParent(child.GetHandle(), parent.GetHandle()) == true);
        DeleteEntityCommand deletion(&hierarchy, parent);
        assert(deletion.Execute(hierarchy) == true);
        hierarchy.DestroyEntity(grandparent);
        Entity reusedGrandparent = hierarchy.CreateEntity("UnrelatedReusedIndex");
        assert(reusedGrandparent.GetIndex() == grandparent.GetIndex());
        assert(deletion.Undo(hierarchy) == true);
        EntityHandle before;
        EntityHandle after;
        assert(deletion.GetRestoredEntityHandles(before, after) == true);
        assert(hierarchy.GetEntityParent(after) == InvalidEntityHandle);
        assert(Entity(after, &hierarchy).GetComponent<RelationshipComponent>().Parent == InvalidEntityHandle);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == after);
        assert(hierarchy.GetEntityChildren(reusedGrandparent.GetHandle()).empty() == true);
    }

    // SG2: Local authoringを保存し、親変更とWorld編集の履歴を混同しません。
    {
        Scene hierarchy;
        Entity first = hierarchy.CreateEntity("First");
        Entity second = hierarchy.CreateEntity("Second");
        Entity child = hierarchy.CreateEntity("LocalChild");
        first.GetComponent<TransformComponent>().Position.x = 10;
        second.GetComponent<TransformComponent>().Position.x = -20;
        child.GetComponent<TransformComponent>().Position.x = 3;
        assert(hierarchy.SetEntityParent(child.GetHandle(), first.GetHandle()) == true);
        assert(hierarchy.EnableLocalTransform(child.GetHandle()) == true);
        SetEditorCommandHistoryScene(&hierarchy);
        assert(ExecuteAndRecordEditorCommand(std::make_unique<ReparentEntityCommand>(
            &hierarchy, child.GetHandle(), second.GetHandle(), ReparentTransformMode::KeepLocal)) == true);
        assert(std::fabs(child.GetComponent<TransformComponent>().Position.x + 27) < 0.001f);
        assert(UndoEditorCommand() == true);
        assert(std::fabs(child.GetComponent<TransformComponent>().Position.x - 3) < 0.001f);
        assert(RedoEditorCommand() == true);
        assert(std::fabs(child.GetComponent<LocalTransformComponent>().Value.Position.x + 7) < 0.001f);
        const auto before = child.GetComponent<TransformComponent>();
        auto requested = before;
        requested.Position.x = 7;
        assert(ExecuteAndRecordEditorCommand(std::make_unique<TransformCommand>(child, before, requested)) == true);
        assert(std::fabs(child.GetComponent<LocalTransformComponent>().Value.Position.x - 27) < 0.001f);
        assert(UndoEditorCommand() == true);
        assert(std::fabs(child.GetComponent<TransformComponent>().Position.x + 27) < 0.001f);
        assert(RedoEditorCommand() == true);
        const auto inspector = CaptureInspectorSnapshot(child);
        assert(inspector.Transform.has_value() == true);
        assert(std::fabs(inspector.Transform->Position.x - 7) < 0.001f);
        assert(hierarchy.CanSnapshotEntityComponents(child.GetHandle()) == true);
        SetEditorCommandHistoryScene(nullptr);
    }
    {
        Scene hierarchy;
        Entity ancestor = hierarchy.CreateEntity("Ancestor");
        Entity parent = hierarchy.CreateEntity("LocalParent");
        Entity child = hierarchy.CreateEntity("LocalChild");
        ancestor.GetComponent<TransformComponent>().Position.x = 10;
        parent.GetComponent<TransformComponent>().Position.x = 5;
        child.GetComponent<TransformComponent>().Position.x = 2;
        assert(hierarchy.SetEntityParent(parent.GetHandle(), ancestor.GetHandle()) == true);
        assert(hierarchy.EnableLocalTransform(parent.GetHandle(), ReparentTransformMode::KeepLocal) == true);
        assert(hierarchy.SetEntityParent(child.GetHandle(), parent.GetHandle()) == true);
        assert(hierarchy.EnableLocalTransform(child.GetHandle(), ReparentTransformMode::KeepLocal) == true);
        DeleteEntityCommand deletion(&hierarchy, parent);
        assert(deletion.Execute(hierarchy) == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == InvalidEntityHandle);
        assert(std::fabs(child.GetComponent<TransformComponent>().Position.x - 17) < 0.001f);
        assert(deletion.Undo(hierarchy) == true);
        EntityHandle oldHandle, restoredHandle;
        assert(deletion.GetRestoredEntityHandles(oldHandle, restoredHandle) == true);
        assert(hierarchy.GetEntityParent(child.GetHandle()) == restoredHandle);
        assert(std::fabs(child.GetComponent<LocalTransformComponent>().Value.Position.x - 2) < 0.001f);
        assert(deletion.Redo(hierarchy) == true);
        hierarchy.DestroyEntity(ancestor);
        hierarchy.CreateEntity("UnrelatedReusedAncestor");
        assert(deletion.Undo(hierarchy) == true);
        assert(deletion.GetRestoredEntityHandles(oldHandle, restoredHandle) == true);
        assert(hierarchy.GetEntityParent(restoredHandle) == InvalidEntityHandle);
        assert(std::fabs(hierarchy.GetComponent<TransformComponent>(restoredHandle.m_Index).Position.x - 15) < 0.001f);
        assert(std::fabs(child.GetComponent<TransformComponent>().Position.x - 17) < 0.001f);
    }

    Scene cameraScene;
    Entity firstCamera = cameraScene.CreateEntity("First Camera");
    firstCamera.AddComponent<CameraComponent>();
    Entity secondCamera = cameraScene.CreateEntity("Second Camera");
    secondCamera.AddComponent<CameraComponent>();
    firstCamera.GetComponent<CameraComponent>().Primary = true;
    secondCamera.GetComponent<CameraComponent>().Primary = false;

    SetEditorCommandHistoryScene(&cameraScene);
    assert(ExecuteAndRecordEditorCommand(
        std::make_unique<SetPrimaryCameraCommand>(
            &cameraScene, secondCamera.GetHandle())) == true);
    assert(firstCamera.GetComponent<CameraComponent>().Primary == false);
    assert(secondCamera.GetComponent<CameraComponent>().Primary == true);
    assert(UndoEditorCommand() == true);
    assert(firstCamera.GetComponent<CameraComponent>().Primary == true);
    assert(secondCamera.GetComponent<CameraComponent>().Primary == false);
    assert(RedoEditorCommand() == true);
    assert(firstCamera.GetComponent<CameraComponent>().Primary == false);
    assert(secondCamera.GetComponent<CameraComponent>().Primary == true);
    SetEditorCommandHistoryScene(nullptr);
}

} // namespace Raven::tests
