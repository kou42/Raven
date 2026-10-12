#include "Raven/Editor/Tests/InspectorSnapshotSelfTests.h"

#include "Raven/Editor/Panels/InspectorSnapshot.h"
#include "Raven/Scene/Scene.h"

#include <cassert>

namespace Raven::tests
{

void RunInspectorSnapshotSelfTests()
{
    const InspectorSnapshot empty = CaptureInspectorSnapshot(Entity{});
    assert(empty.HasSelection == false);
    assert(empty.SelectionValid == false);

    Scene scene;
    Entity entity = scene.CreateEntity("Inspector Target");
    entity.GetComponent<TransformComponent>().Position = math::Vec3(2.0f, 4.0f, 6.0f);

    RigidBodyComponent rigidBody;
    rigidBody.SetBodyType(BodyType::Kinematic);
    rigidBody.LinearVelocity = math::Vec3(1.0f, 3.0f, 5.0f);
    entity.AddComponent<RigidBodyComponent>(rigidBody);

    const InspectorSnapshot captured = CaptureInspectorSnapshot(entity);
    assert(captured.HasSelection == true);
    assert(captured.SelectionValid == true);
    assert(captured.Handle == entity.GetHandle());
    assert(captured.Tag.has_value() == true);
    assert(*captured.Tag == "Inspector Target");
    assert(captured.Transform.has_value() == true);
    assert(captured.Transform->Position.y == 4.0f);
    assert(captured.Camera.has_value() == false);
    assert(captured.RigidBody.has_value() == true);
    assert(captured.RigidBody->Type == BodyType::Kinematic);
    assert(captured.RigidBody->LinearVelocity.z == 5.0f);
    assert(captured.Collider.has_value() == false);

    scene.DestroyEntity(entity);
    const InspectorSnapshot destroyed = CaptureInspectorSnapshot(entity);
    assert(destroyed.HasSelection == true);
    assert(destroyed.SelectionValid == false);
    assert(static_cast<bool>(destroyed.Handle) == false);
    assert(destroyed.Tag.has_value() == false);
}

} // namespace Raven::tests
