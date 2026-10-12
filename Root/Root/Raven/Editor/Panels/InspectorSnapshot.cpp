#include "Raven/Editor/Panels/InspectorSnapshot.h"

#include "Raven/Scene/Scene.h"

namespace Raven
{

InspectorSnapshot CaptureInspectorSnapshot(Entity selectedEntity)
{
    InspectorSnapshot snapshot{};
    // Entity::operator bool()は生存確認まで含むため、破棄済み選択を「選択なし」と
    // 区別するにはHandleとScene pointerを明示的に確認します。
    snapshot.HasSelection = selectedEntity.GetScene() != nullptr
        && selectedEntity.GetHandle().IsValid() == true;
    if (snapshot.HasSelection == false)
    {
        return snapshot;
    }

    Scene* scene = selectedEntity.GetScene();
    if (scene == nullptr || scene->IsEntityAlive(selectedEntity) == false)
    {
        return snapshot;
    }

    snapshot.SelectionValid = true;
    snapshot.Handle = selectedEntity.GetHandle();

    if (selectedEntity.HasComponent<TagComponent>() == true)
    {
        snapshot.Tag = selectedEntity.GetComponent<TagComponent>().Tag;
    }
    if (selectedEntity.HasComponent<TransformComponent>() == true)
    {
        const TransformComponent& component =
            selectedEntity.GetComponent<TransformComponent>();
        snapshot.Transform = InspectorTransformSnapshot{
            component.Position, component.Rotation, component.Scale };
    }
    if (selectedEntity.HasComponent<CameraComponent>() == true)
    {
        const CameraComponent& component =
            selectedEntity.GetComponent<CameraComponent>();
        snapshot.Camera = InspectorCameraSnapshot{
            component.Camera.GetPerspectiveVerticalFov(),
            component.Camera.GetPerspectiveNearClip(),
            component.Camera.GetPerspectiveFarClip(),
            component.Camera.GetAspectRatio(),
            component.Primary };
    }
    if (selectedEntity.HasComponent<RigidBodyComponent>() == true)
    {
        const RigidBodyComponent& component =
            selectedEntity.GetComponent<RigidBodyComponent>();
        snapshot.RigidBody = InspectorRigidBodySnapshot{
            component.Type,
            component.Mass,
            component.LinearVelocity,
            component.AngularVelocity,
            component.LinearDamping,
            component.AngularDamping,
            component.UseGravity,
            component.AllowSleep,
            component.IsSleeping };
    }
    if (selectedEntity.HasComponent<ColliderComponent>() == true)
    {
        const ColliderComponent& component =
            selectedEntity.GetComponent<ColliderComponent>();
        snapshot.Collider = InspectorColliderSnapshot{
            component.Type,
            component.Offset,
            component.HalfExtents,
            component.Radius,
            component.HalfLength,
            component.PlaneNormal,
            component.PlaneOffset,
            component.Restitution,
            component.StaticFriction,
            component.DynamicFriction,
            component.IsTrigger };
    }
    return snapshot;
}

} // namespace Raven
