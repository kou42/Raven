#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"

#include <optional>
#include <string>

namespace Raven
{

struct InspectorTransformSnapshot
{
    math::Vec3 Position{};
    math::Vec3 Rotation{};
    math::Vec3 Scale{ 1.0f, 1.0f, 1.0f };
};

struct InspectorCameraSnapshot
{
    float VerticalFov = 0.0f;
    float NearClip = 0.0f;
    float FarClip = 0.0f;
    float AspectRatio = 0.0f;
    bool Primary = false;
};

struct InspectorRigidBodySnapshot
{
    BodyType Type = BodyType::Dynamic;
    float Mass = 0.0f;
    math::Vec3 LinearVelocity{};
    math::Vec3 AngularVelocity{};
    float LinearDamping = 0.0f;
    float AngularDamping = 0.0f;
    bool UseGravity = false;
    bool AllowSleep = false;
    bool IsSleeping = false;
};

struct InspectorColliderSnapshot
{
    ColliderType Type = ColliderType::Box;
    math::Vec3 Offset{};
    math::Vec3 HalfExtents{};
    float Radius = 0.0f;
    float HalfLength = 0.0f;
    math::Vec3 PlaneNormal{};
    float PlaneOffset = 0.0f;
    float Restitution = 0.0f;
    float StaticFriction = 0.0f;
    float DynamicFriction = 0.0f;
    bool IsTrigger = false;
};

// Inspector Viewが1frameで表示する値だけを保持します。
// Scene / Component / shared resourceへの参照を持たないため、Scene交換やEntity削除後も
// Retained Widget Treeにdangling参照を残しません。
struct InspectorSnapshot
{
    bool HasSelection = false;
    bool SelectionValid = false;
    EntityHandle Handle = InvalidEntityHandle;
    std::optional<std::string> Tag;
    std::optional<InspectorTransformSnapshot> Transform;
    std::optional<InspectorCameraSnapshot> Camera;
    std::optional<InspectorRigidBodySnapshot> RigidBody;
    std::optional<InspectorColliderSnapshot> Collider;
};

InspectorSnapshot CaptureInspectorSnapshot(Entity selectedEntity);

} // namespace Raven
