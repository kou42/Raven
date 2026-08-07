#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/AABB.h"

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
    if (sphereColliderA.Type != ColliderType::Sphere || sphereColliderB.Type != ColliderType::Sphere) return false;
    const float radiusA = sphereColliderA.Radius;
    const float radiusB = sphereColliderB.Radius;
    if (radiusA <= 0.0f || radiusB <= 0.0f) return false;

    const math::Vec3 centerA = sphereTransformA.Position + sphereColliderA.Offset;
    const math::Vec3 centerB = sphereTransformB.Position + sphereColliderB.Offset;
    const math::Vec3 centerDelta = centerB - centerA;
    const float distanceSquared = centerDelta.LengthSq();
    const float radiusSum = radiusA + radiusB;
    if (distanceSquared > radiusSum * radiusSum) return false;

    constexpr float CenterEpsilonSquared = 1.0e-12f;
    math::Vec3 normal{ 1.0f, 0.0f, 0.0f };
    float centerDistance = 0.0f;
    if (distanceSquared > CenterEpsilonSquared)
    {
        centerDistance = std::sqrt(distanceSquared);
        normal = centerDelta / centerDistance;
    }

    ContactPoint point{};
    point.Position = ((centerA + normal * radiusA) + (centerB - normal * radiusB)) * 0.5f;
    point.Penetration = radiusSum - centerDistance;

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
    if (sphereCollider.Type != ColliderType::Sphere || planeCollider.Type != ColliderType::Plane) return false;
    const float sphereRadius = sphereCollider.Radius;
    if (sphereRadius <= 0.0f) return false;

    const float normalLengthSquared = planeCollider.PlaneNormal.LengthSq();
    if (normalLengthSquared <= 1.0e-12f) return false;
    const math::Vec3 planeNormal = planeCollider.PlaneNormal / std::sqrt(normalLengthSquared);
    const math::Vec3 sphereCenter = sphereTransform.Position + sphereCollider.Offset;
    const math::Vec3 pointOnPlane = planeTransform.Position + planeCollider.Offset + planeNormal * planeCollider.PlaneOffset;
    const float signedDistance = math::Vec3::Dot(sphereCenter - pointOnPlane, planeNormal);
    if (signedDistance > sphereRadius) return false;

    ContactPoint point{};
    point.Position = sphereCenter - planeNormal * signedDistance;
    point.Penetration = sphereRadius - signedDistance;

    outManifold = ContactManifold{};
    outManifold.A = sphereEntity;
    outManifold.B = planeEntity;
    outManifold.Normal = -planeNormal;
    SetCombinedMaterial(sphereCollider, planeCollider, outManifold);
    outManifold.AddPoint(point);
    return true;
}

bool GenerateSphereBoxManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    ContactManifold& outManifold)
{
    if (sphereCollider.Type != ColliderType::Sphere || boxCollider.Type != ColliderType::Box) return false;

    const float radius = sphereCollider.Radius;
    if (radius <= 0.0f) return false;

    AABB boxBounds{};
    if (!ComputeColliderAABB(boxTransform, boxCollider, boxBounds)) return false;

    const math::Vec3 center = sphereTransform.Position + sphereCollider.Offset;
    const math::Vec3 closest{
        std::clamp(center.x, boxBounds.Min.x, boxBounds.Max.x),
        std::clamp(center.y, boxBounds.Min.y, boxBounds.Max.y),
        std::clamp(center.z, boxBounds.Min.z, boxBounds.Max.z)
    };

    const math::Vec3 sphereToBox = closest - center;
    const float distanceSquared = sphereToBox.LengthSq();
    if (distanceSquared > radius * radius) return false;

    math::Vec3 normal{};
    math::Vec3 contactPosition = closest;
    float penetration = 0.0f;

    if (distanceSquared > 1.0e-12f)
    {
        // Sphere中心がBox外側にある通常ケース。
        // A=Sphere -> B=Box なので、中心から最近接点へ向かう方向がManifold法線です。
        const float distance = std::sqrt(distanceSquared);
        normal = sphereToBox / distance;
        penetration = radius - distance;
    }
    else
    {
        // ====================================================================
        // Sphere中心がBox内部
        // ====================================================================
        // closest == center になるため、最近接点だけでは法線を決められません。
        // 6面までの距離から最も近い面を選びます。
        //
        // SolverはAを -Normal へ押すため、Sphereを外へ出したい方向(exitDirection)の
        // 反対をManifold法線にします。
        float faceDistance = center.x - boxBounds.Min.x;
        math::Vec3 exitDirection{ -1.0f, 0.0f, 0.0f };
        contactPosition = { boxBounds.Min.x, center.y, center.z };

        auto chooseFace = [&](float candidateDistance,
                              const math::Vec3& candidateExit,
                              const math::Vec3& candidatePoint)
        {
            if (candidateDistance < faceDistance)
            {
                faceDistance = candidateDistance;
                exitDirection = candidateExit;
                contactPosition = candidatePoint;
            }
        };

        chooseFace(boxBounds.Max.x - center.x, { 1.0f, 0.0f, 0.0f }, { boxBounds.Max.x, center.y, center.z });
        chooseFace(center.y - boxBounds.Min.y, { 0.0f, -1.0f, 0.0f }, { center.x, boxBounds.Min.y, center.z });
        chooseFace(boxBounds.Max.y - center.y, { 0.0f, 1.0f, 0.0f }, { center.x, boxBounds.Max.y, center.z });
        chooseFace(center.z - boxBounds.Min.z, { 0.0f, 0.0f, -1.0f }, { center.x, center.y, boxBounds.Min.z });
        chooseFace(boxBounds.Max.z - center.z, { 0.0f, 0.0f, 1.0f }, { center.x, center.y, boxBounds.Max.z });

        normal = -exitDirection;

        // Sphere全体をBox外へ出すには、中心から面までの距離にradiusを加えた量を
        // 分離する必要があります。
        penetration = radius + faceDistance;
    }

    ContactPoint point{};
    point.Position = contactPosition;
    point.Penetration = std::max(penetration, 0.0f);

    outManifold = ContactManifold{};
    outManifold.A = sphereEntity;
    outManifold.B = boxEntity;
    outManifold.Normal = normal;
    SetCombinedMaterial(sphereCollider, boxCollider, outManifold);
    outManifold.AddPoint(point);
    return true;
}

} // namespace Raven::ph
