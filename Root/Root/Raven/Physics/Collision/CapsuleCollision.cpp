#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"

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

math::Vec3 ClosestPointOnOBB(const OBB& box, const math::Vec3& point)
{
    const math::Vec3 local = box.ToLocalPoint(point);
    return box.ToWorldPoint(math::Vec3{
        std::clamp(local.x, -box.HalfExtents.x, box.HalfExtents.x),
        std::clamp(local.y, -box.HalfExtents.y, box.HalfExtents.y),
        std::clamp(local.z, -box.HalfExtents.z, box.HalfExtents.z)
    });
}

float DistanceSquaredSegmentToOBB(
    const OBB& box,
    const math::Vec3& segmentA,
    const math::Vec3& segmentB,
    float t,
    math::Vec3* outSegmentPoint,
    math::Vec3* outBoxPoint)
{
    const math::Vec3 segmentPoint = segmentA + (segmentB - segmentA) * t;
    const math::Vec3 boxPoint = ClosestPointOnOBB(box, segmentPoint);

    if (outSegmentPoint != nullptr)
    {
        *outSegmentPoint = segmentPoint;
    }
    if (outBoxPoint != nullptr)
    {
        *outBoxPoint = boxPoint;
    }

    return (boxPoint - segmentPoint).LengthSq();
}

void ClosestPointsSegmentOBB(
    const OBB& box,
    const math::Vec3& segmentA,
    const math::Vec3& segmentB,
    math::Vec3& outSegmentPoint,
    math::Vec3& outBoxPoint)
{
    // ========================================================================
    // Segment - OBB closest points
    // ========================================================================
    // OBBローカルでは「線分上の点からAABBまでの距離^2」はtに対して凸関数になります。
    // そのため区間[0,1]を三分探索すれば、角・辺・面のどのVoronoi領域に最近接点が
    // 移っても同じ実装で安定して最小点を求められます。
    //
    // Capsule-BoxはRagdoll用途で毎Body数個程度の判定なので、ここでは複雑な分岐を持つ
    // 厳密なsegment-AABBアルゴリズムより、保守性と数値安定性を優先します。
    float left = 0.0f;
    float right = 1.0f;
    for (int iteration = 0; iteration < 28; ++iteration)
    {
        const float t1 = left + (right - left) / 3.0f;
        const float t2 = right - (right - left) / 3.0f;
        const float d1 = DistanceSquaredSegmentToOBB(box, segmentA, segmentB, t1, nullptr, nullptr);
        const float d2 = DistanceSquaredSegmentToOBB(box, segmentA, segmentB, t2, nullptr, nullptr);
        if (d1 <= d2)
        {
            right = t2;
        }
        else
        {
            left = t1;
        }
    }

    const float t = (left + right) * 0.5f;
    DistanceSquaredSegmentToOBB(
        box,
        segmentA,
        segmentB,
        t,
        &outSegmentPoint,
        &outBoxPoint);
}

bool BuildSinglePointManifold(
    Entity entityA,
    Entity entityB,
    const ColliderComponent& colliderA,
    const ColliderComponent& colliderB,
    const math::Vec3& normal,
    const math::Vec3& position,
    float penetration,
    ContactManifold& outManifold)
{
    if (penetration < 0.0f)
    {
        return false;
    }

    ContactPoint point{};
    point.Position = position;
    point.Penetration = penetration;

    outManifold = ContactManifold{};
    outManifold.A = entityA;
    outManifold.B = entityB;
    outManifold.Normal = normal;
    SetCombinedMaterial(colliderA, colliderB, outManifold);
    outManifold.AddPoint(point);
    return true;
}
} // namespace

bool GenerateSphereCapsuleManifold(
    Entity sphereEntity,
    const TransformComponent& sphereTransform,
    const ColliderComponent& sphereCollider,
    Entity capsuleEntity,
    const TransformComponent& capsuleTransform,
    const ColliderComponent& capsuleCollider,
    ContactManifold& outManifold)
{
    if (sphereCollider.Type != ColliderType::Sphere
        || capsuleCollider.Type != ColliderType::Capsule)
    {
        return false;
    }

    const float sphereRadius = std::abs(sphereCollider.Radius);
    Capsule capsule{};
    if (sphereRadius <= 0.0f
        || ComputeCapsule(capsuleTransform, capsuleCollider, capsule) == false)
    {
        return false;
    }

    const math::Vec3 sphereCenter = sphereTransform.Position + sphereCollider.Offset;
    const math::Vec3 capsulePoint = ClosestPointOnSegment(
        capsule.SegmentA,
        capsule.SegmentB,
        sphereCenter);
    const math::Vec3 delta = capsulePoint - sphereCenter;
    const float distanceSquared = delta.LengthSq();
    const float radiusSum = sphereRadius + capsule.Radius;
    if (distanceSquared > radiusSum * radiusSum)
    {
        return false;
    }

    math::Vec3 normal{ 1.0f, 0.0f, 0.0f };
    float distance = 0.0f;
    if (distanceSquared > 1.0e-12f)
    {
        distance = std::sqrt(distanceSquared);
        normal = delta / distance;
    }

    const math::Vec3 sphereSurface = sphereCenter + normal * sphereRadius;
    const math::Vec3 capsuleSurface = capsulePoint - normal * capsule.Radius;
    return BuildSinglePointManifold(
        sphereEntity,
        capsuleEntity,
        sphereCollider,
        capsuleCollider,
        normal,
        (sphereSurface + capsuleSurface) * 0.5f,
        radiusSum - distance,
        outManifold);
}

bool GenerateCapsuleCapsuleManifold(
    Entity capsuleEntityA,
    const TransformComponent& capsuleTransformA,
    const ColliderComponent& capsuleColliderA,
    Entity capsuleEntityB,
    const TransformComponent& capsuleTransformB,
    const ColliderComponent& capsuleColliderB,
    ContactManifold& outManifold)
{
    Capsule capsuleA{};
    Capsule capsuleB{};
    if (ComputeCapsule(capsuleTransformA, capsuleColliderA, capsuleA) == false
        || ComputeCapsule(capsuleTransformB, capsuleColliderB, capsuleB) == false)
    {
        return false;
    }

    math::Vec3 pointA{};
    math::Vec3 pointB{};
    ClosestPointsOnSegments(
        capsuleA.SegmentA,
        capsuleA.SegmentB,
        capsuleB.SegmentA,
        capsuleB.SegmentB,
        pointA,
        pointB);

    const math::Vec3 delta = pointB - pointA;
    const float distanceSquared = delta.LengthSq();
    const float radiusSum = capsuleA.Radius + capsuleB.Radius;
    if (distanceSquared > radiusSum * radiusSum)
    {
        return false;
    }

    math::Vec3 normal{ 1.0f, 0.0f, 0.0f };
    float distance = 0.0f;
    if (distanceSquared > 1.0e-12f)
    {
        distance = std::sqrt(distanceSquared);
        normal = delta / distance;
    }
    else
    {
        const math::Vec3 centerDelta = capsuleB.Center() - capsuleA.Center();
        if (centerDelta.LengthSq() > 1.0e-12f)
        {
            normal = centerDelta.Normalized();
        }
    }

    const math::Vec3 surfaceA = pointA + normal * capsuleA.Radius;
    const math::Vec3 surfaceB = pointB - normal * capsuleB.Radius;
    return BuildSinglePointManifold(
        capsuleEntityA,
        capsuleEntityB,
        capsuleColliderA,
        capsuleColliderB,
        normal,
        (surfaceA + surfaceB) * 0.5f,
        radiusSum - distance,
        outManifold);
}

bool GenerateCapsulePlaneManifold(
    Entity capsuleEntity,
    const TransformComponent& capsuleTransform,
    const ColliderComponent& capsuleCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold)
{
    if (planeCollider.Type != ColliderType::Plane)
    {
        return false;
    }

    Capsule capsule{};
    if (ComputeCapsule(capsuleTransform, capsuleCollider, capsule) == false)
    {
        return false;
    }

    const float normalLengthSquared = planeCollider.PlaneNormal.LengthSq();
    if (normalLengthSquared <= 1.0e-12f)
    {
        return false;
    }

    const math::Vec3 planeNormal = planeCollider.PlaneNormal / std::sqrt(normalLengthSquared);
    const math::Vec3 pointOnPlane = planeTransform.Position
        + planeCollider.Offset
        + planeNormal * planeCollider.PlaneOffset;

    const float distanceA = math::Vec3::Dot(capsule.SegmentA - pointOnPlane, planeNormal);
    const float distanceB = math::Vec3::Dot(capsule.SegmentB - pointOnPlane, planeNormal);
    const math::Vec3 segmentPoint = distanceA <= distanceB ? capsule.SegmentA : capsule.SegmentB;
    const float signedDistance = std::min(distanceA, distanceB);
    if (signedDistance > capsule.Radius)
    {
        return false;
    }

    // Manifold規約は A(Capsule) -> B(Plane) なので-planeNormalです。
    const math::Vec3 normal = -planeNormal;
    const math::Vec3 capsuleSurface = segmentPoint - planeNormal * capsule.Radius;
    const math::Vec3 planeSurface = segmentPoint - planeNormal * signedDistance;
    return BuildSinglePointManifold(
        capsuleEntity,
        planeEntity,
        capsuleCollider,
        planeCollider,
        normal,
        (capsuleSurface + planeSurface) * 0.5f,
        capsule.Radius - signedDistance,
        outManifold);
}

bool GenerateCapsuleBoxManifold(
    Entity capsuleEntity,
    const TransformComponent& capsuleTransform,
    const ColliderComponent& capsuleCollider,
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    ContactManifold& outManifold)
{
    Capsule capsule{};
    OBB box{};
    if (ComputeCapsule(capsuleTransform, capsuleCollider, capsule) == false
        || ComputeBoxOBB(boxTransform, boxCollider, box) == false)
    {
        return false;
    }

    math::Vec3 capsulePoint{};
    math::Vec3 boxPoint{};
    ClosestPointsSegmentOBB(
        box,
        capsule.SegmentA,
        capsule.SegmentB,
        capsulePoint,
        boxPoint);

    const math::Vec3 delta = boxPoint - capsulePoint;
    const float distanceSquared = delta.LengthSq();
    if (distanceSquared > capsule.Radius * capsule.Radius)
    {
        return false;
    }

    math::Vec3 normal{};
    float penetration = 0.0f;
    math::Vec3 contactPosition = boxPoint;

    if (distanceSquared > 1.0e-12f)
    {
        const float distance = std::sqrt(distanceSquared);
        normal = delta / distance;
        penetration = capsule.Radius - distance;
    }
    else
    {
        // 中心線分がOBB内部へ入った場合は、最近接segment pointから最短脱出面を選びます。
        // Sphere-Box内部ケースと同じ規約でA(Capsule)->B(Box)法線を構築します。
        const math::Vec3 local = box.ToLocalPoint(capsulePoint);
        int faceAxis = 0;
        float faceSign = local.x >= 0.0f ? 1.0f : -1.0f;
        float faceDistance = box.HalfExtents.x - std::abs(local.x);
        for (int axis = 1; axis < 3; ++axis)
        {
            const float candidate = box.HalfExtents[axis] - std::abs(local[axis]);
            if (candidate < faceDistance)
            {
                faceAxis = axis;
                faceSign = local[axis] >= 0.0f ? 1.0f : -1.0f;
                faceDistance = candidate;
            }
        }

        math::Vec3 localContact = local;
        localContact[faceAxis] = box.HalfExtents[faceAxis] * faceSign;
        contactPosition = box.ToWorldPoint(localContact);
        normal = -(box.Axis[faceAxis] * faceSign);
        penetration = capsule.Radius + faceDistance;
    }

    return BuildSinglePointManifold(
        capsuleEntity,
        boxEntity,
        capsuleCollider,
        boxCollider,
        normal,
        contactPosition,
        penetration,
        outManifold);
}

bool RayCastCapsule(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const Capsule& capsule,
    float& outFraction,
    math::Vec3& outNormal)
{
    // ========================================================================
    // Ray - Capsule
    // ========================================================================
    // Capsuleを「有限円柱 + 両端Sphere」として解きます。
    // fractionはdirectionを正規化しない元のRayパラメータを維持するため、
    // origin + direction * fraction をそのままPhysicsRayCastHitへ使えます。
    const math::Vec3 ba = capsule.SegmentB - capsule.SegmentA;
    const math::Vec3 oa = origin - capsule.SegmentA;
    const float baba = math::Vec3::Dot(ba, ba);
    const float bard = math::Vec3::Dot(ba, direction);
    const float baoa = math::Vec3::Dot(ba, oa);
    const float rdoa = math::Vec3::Dot(direction, oa);
    const float oaoa = math::Vec3::Dot(oa, oa);
    const float directionLengthSquared = direction.LengthSq();

    if (directionLengthSquared <= 1.0e-12f || capsule.Radius <= 0.0f)
    {
        return false;
    }

    // Ray始点がCapsule内部なら即時Hitとして扱います。
    const math::Vec3 closestAtOrigin = ClosestPointOnSegment(
        capsule.SegmentA,
        capsule.SegmentB,
        origin);
    if ((origin - closestAtOrigin).LengthSq() <= capsule.Radius * capsule.Radius)
    {
        outFraction = 0.0f;
        const math::Vec3 delta = origin - closestAtOrigin;
        outNormal = delta.LengthSq() > 1.0e-12f
            ? delta.Normalized()
            : -direction.Normalized();
        return true;
    }

    float bestFraction = maxFraction + 1.0f;
    math::Vec3 bestNormal{};

    if (baba > 1.0e-12f)
    {
        const float a = baba * directionLengthSquared - bard * bard;
        const float b = baba * rdoa - baoa * bard;
        const float c = baba * oaoa - baoa * baoa - capsule.Radius * capsule.Radius * baba;
        const float h = b * b - a * c;

        if (std::abs(a) > 1.0e-12f && h >= 0.0f)
        {
            const float fraction = (-b - std::sqrt(h)) / a;
            const float y = baoa + fraction * bard;
            if (fraction >= 0.0f
                && fraction <= maxFraction
                && y > 0.0f
                && y < baba)
            {
                const math::Vec3 hitPoint = origin + direction * fraction;
                const math::Vec3 axisPoint = capsule.SegmentA + ba * (y / baba);
                bestFraction = fraction;
                bestNormal = (hitPoint - axisPoint).Normalized();
            }
        }
    }

    // 両端半球をRay-Sphereとして評価し、円柱Hitより手前なら採用します。
    const math::Vec3 sphereCenters[2]{ capsule.SegmentA, capsule.SegmentB };
    for (const math::Vec3& center : sphereCenters)
    {
        const math::Vec3 m = origin - center;
        const float b = math::Vec3::Dot(m, direction);
        const float c = math::Vec3::Dot(m, m) - capsule.Radius * capsule.Radius;
        const float discriminant = b * b - directionLengthSquared * c;
        if (discriminant < 0.0f)
        {
            continue;
        }

        const float fraction = (-b - std::sqrt(discriminant)) / directionLengthSquared;
        if (fraction < 0.0f || fraction > maxFraction || fraction >= bestFraction)
        {
            continue;
        }

        bestFraction = fraction;
        bestNormal = (origin + direction * fraction - center).Normalized();
    }

    if (bestFraction > maxFraction)
    {
        return false;
    }

    outFraction = bestFraction;
    outNormal = bestNormal;
    return true;
}

} // namespace Raven::ph
