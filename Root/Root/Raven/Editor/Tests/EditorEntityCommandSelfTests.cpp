#include "Raven/Editor/Tests/EditorEntityCommandSelfTests.h"

#include "Raven/Editor/Command/DeleteEntityCommand.h"
#include "Raven/Editor/EditorCommandHistory.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <memory>

namespace Raven::tests
{

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
    auto command = std::make_unique<DeleteEntityCommand>(entity);
    DeleteEntityCommand* recordedCommand = command.get();
    assert(ExecuteAndRecordEditorCommand(std::move(command)) == true);
    assert(scene.IsEntityAlive(deletedHandle) == false);
    assert(static_cast<bool>(recordedCommand->GetRestoredEntity()) == false);

    assert(UndoEditorCommand() == true);
    Entity restored = recordedCommand->GetRestoredEntity();
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
    assert(recordedCommand->GetRestoredEntity().GetHandle() != restoredHandle);

    // Sceneはstack上の一時Objectなので、終了前に非所有参照とCommand Snapshotを破棄します。
    SetEditorCommandHistoryScene(nullptr);
}

} // namespace Raven::tests
