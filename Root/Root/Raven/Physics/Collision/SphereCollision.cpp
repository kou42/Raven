#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{
namespace
{
// 物理マテリアル合成則:
// - Restitutionは過大反発を避けるため小さい方を採用
// - 摩擦係数は幾何平均で滑り量の極端な偏りを抑制
// - Triggerはどちらか片方でも有効なら非拘束接触として扱う
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

// Sphere-Sphere判定:
// 中心距離と半径和の比較で接触を判定し、法線はA->B方向で定義します。
// 重なり中心が同一点に近い退化ケースでは、固定法線で安定化します。
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

// Sphere-Plane判定:
// 球中心と平面の符号付き距離を使って貫通量を算出します。
// 法線はマニホールド規約に合わせて A(Sphere) -> B(Plane) 向きへ揃えます。
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

// Sphere-Box判定:
// 1) OBBローカル空間で最近接点を求める
// 2) 球中心との距離で接触判定
// 3) 球中心がOBB内部なら最短脱出面を使って法線を決定
// という3段構成で、回転Boxにも安定に対応します。
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

    OBB box{};
    if (!ComputeBoxOBB(boxTransform, boxCollider, box)) return false;

    const math::Vec3 sphereCenter = sphereTransform.Position + sphereCollider.Offset;

    // ========================================================================
    // Sphere - OBB closest point
    // ========================================================================
    // Sphere中心をBoxローカル空間へ落とせば、回転していても問題は単純な
    // local AABBへのclampになります。得られたlocal closest pointを再びworldへ戻します。
    const math::Vec3 localCenter = box.ToLocalPoint(sphereCenter);
    const math::Vec3 localClosest{
        std::clamp(localCenter.x, -box.HalfExtents.x, box.HalfExtents.x),
        std::clamp(localCenter.y, -box.HalfExtents.y, box.HalfExtents.y),
        std::clamp(localCenter.z, -box.HalfExtents.z, box.HalfExtents.z)
    };
    math::Vec3 contactPosition = box.ToWorldPoint(localClosest);

    const math::Vec3 sphereToBox = contactPosition - sphereCenter;
    const float distanceSquared = sphereToBox.LengthSq();
    if (distanceSquared > radius * radius) return false;

    math::Vec3 normal{};
    float penetration = 0.0f;

    if (distanceSquared > 1.0e-12f)
    {
        const float distance = std::sqrt(distanceSquared);
        normal = sphereToBox / distance; // A=Sphere -> B=Box
        penetration = radius - distance;
    }
    else
    {
        // Sphere中心がOBB内部の場合は6面までのローカル距離を比較します。
        // 最短の面を脱出面とし、そのworld方向の反対をA->B法線にします。
        int faceAxis = 0;
        float faceSign = localCenter.x >= 0.0f ? 1.0f : -1.0f;
        float faceDistance = box.HalfExtents.x - std::abs(localCenter.x);

        for (int axis = 1; axis < 3; ++axis)
        {
            const float candidateDistance = box.HalfExtents[axis] - std::abs(localCenter[axis]);
            if (candidateDistance < faceDistance)
            {
                faceAxis = axis;
                faceSign = localCenter[axis] >= 0.0f ? 1.0f : -1.0f;
                faceDistance = candidateDistance;
            }
        }

        math::Vec3 localContact = localCenter;
        localContact[faceAxis] = box.HalfExtents[faceAxis] * faceSign;
        contactPosition = box.ToWorldPoint(localContact);

        const math::Vec3 exitDirection = box.Axis[faceAxis] * faceSign;
        normal = -exitDirection;
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
