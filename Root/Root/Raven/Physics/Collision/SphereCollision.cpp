#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"

namespace Raven::ph
{
namespace
{
void SetCombinedMaterial(
    const ColliderComponent& colliderA,
    const ColliderComponent& colliderB,
    ContactManifold& manifold)
{
    manifold.Restitution = std::min(
        std::max(colliderA.Restitution, 0.0f),
        std::max(colliderB.Restitution, 0.0f));

    manifold.StaticFriction = std::sqrt(
        std::max(colliderA.StaticFriction, 0.0f)
        * std::max(colliderB.StaticFriction, 0.0f));

    manifold.DynamicFriction = std::sqrt(
        std::max(colliderA.DynamicFriction, 0.0f)
        * std::max(colliderB.DynamicFriction, 0.0f));

    manifold.IsTrigger = colliderA.IsTrigger || colliderB.IsTrigger;
}
}

bool GenerateSphereSphereManifold(
    Entity sphereEntityA,
    const TransformComponent& sphereTransformA,
    const ColliderComponent& sphereColliderA,
    Entity sphereEntityB,
    const TransformComponent& sphereTransformB,
    const ColliderComponent& sphereColliderB,
    ContactManifold& outManifold)
{
    if (sphereColliderA.Type != ColliderType::Sphere
        || sphereColliderB.Type != ColliderType::Sphere)
    {
        return false;
    }

    const float radiusA = sphereColliderA.Radius;
    const float radiusB = sphereColliderB.Radius;
    if (radiusA <= 0.0f || radiusB <= 0.0f)
    {
        return false;
    }

    const math::Vec3 centerA =
        sphereTransformA.Position + sphereColliderA.Offset;
    const math::Vec3 centerB =
        sphereTransformB.Position + sphereColliderB.Offset;

    const math::Vec3 centerDelta = centerB - centerA;
    const float distanceSquared = centerDelta.LengthSq();
    const float radiusSum = radiusA + radiusB;

    if (distanceSquared > radiusSum * radiusSum)
    {
        return false;
    }

    // 中心一致時は法線を計算できないため、安定した既定方向を使用します。
    constexpr float CenterEpsilonSquared = 1.0e-12f;
    math::Vec3 normal{ 1.0f, 0.0f, 0.0f };
    float centerDistance = 0.0f;

    if (distanceSquared > CenterEpsilonSquared)
    {
        centerDistance = std::sqrt(distanceSquared);
        normal = centerDelta / centerDistance;
    }

    const math::Vec3 surfacePointA = centerA + normal * radiusA;
    const math::Vec3 surfacePointB = centerB - normal * radiusB;

    ContactPoint point{};
    point.Position = (surfacePointA + surfacePointB) * 0.5f;
    point.Penetration = radiusSum - centerDistance;

    // 出力先が以前の呼び出し結果を保持していても混ざらないよう、毎回初期化します。
    outManifold = ContactManifold{};
    outManifold.A = sphereEntityA;
    outManifold.B = sphereEntityB;
    outManifold.Normal = normal;
    SetCombinedMaterial(sphereColliderA, sphereColliderB, outManifold);
    outManifold.AddPoint(point);

    return true;
}

bool GenerateSpherePlaneManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold)
{
    if (sphereCollider.Type != ColliderType::Sphere
        || planeCollider.Type != ColliderType::Plane)
    {
        return false;
    }

    const float sphereRadius = sphereCollider.Radius;
    if (sphereRadius <= 0.0f)
    {
        return false;
    }

    const float normalLengthSquared = planeCollider.PlaneNormal.LengthSq();
    constexpr float NormalEpsilonSquared = 1.0e-12f;
    if (normalLengthSquared <= NormalEpsilonSquared)
    {
        return false;
    }

    const math::Vec3 planeNormal =
        planeCollider.PlaneNormal / std::sqrt(normalLengthSquared);

    const math::Vec3 sphereCenter =
        sphereTransform.Position + sphereCollider.Offset;
    const math::Vec3 pointOnPlane =
        planeTransform.Position
        + planeCollider.Offset
        + planeNormal * planeCollider.PlaneOffset;

    const float signedDistance =
        math::Vec3::Dot(sphereCenter - pointOnPlane, planeNormal);

    if (signedDistance > sphereRadius)
    {
        return false;
    }

    ContactPoint point{};
    point.Position = sphereCenter - planeNormal * signedDistance;
    point.Penetration = sphereRadius - signedDistance;

    outManifold = ContactManifold{};
    outManifold.A = sphereEntity;
    outManifold.B = planeEntity;

    // Manifold法線はAからBへ向く規約です。
    outManifold.Normal = -planeNormal;
    SetCombinedMaterial(sphereCollider, planeCollider, outManifold);
    outManifold.AddPoint(point);

    return true;
}

} // namespace Raven::ph
