#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/AABB.h"
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

void AddFaceContacts(
    const AABB& a,
    const AABB& b,
    int normalAxis,
    float contactPlane,
    float penetration,
    ContactManifold& manifold)
{
    // 最小貫通軸に垂直な2軸について重なり区間を求めます。
    // その矩形の4 cornerがAABB face-face接触のContact Pointになります。
    const float minX = std::max(a.Min.x, b.Min.x);
    const float maxX = std::min(a.Max.x, b.Max.x);
    const float minY = std::max(a.Min.y, b.Min.y);
    const float maxY = std::min(a.Max.y, b.Max.y);
    const float minZ = std::max(a.Min.z, b.Min.z);
    const float maxZ = std::min(a.Max.z, b.Max.z);

    auto add = [&](const math::Vec3& position)
    {
        ContactPoint point{};
        point.Position = position;
        point.Penetration = penetration;
        manifold.AddPoint(point);
    };

    if (normalAxis == 0)
    {
        add({ contactPlane, minY, minZ });
        add({ contactPlane, maxY, minZ });
        add({ contactPlane, maxY, maxZ });
        add({ contactPlane, minY, maxZ });
    }
    else if (normalAxis == 1)
    {
        add({ minX, contactPlane, minZ });
        add({ maxX, contactPlane, minZ });
        add({ maxX, contactPlane, maxZ });
        add({ minX, contactPlane, maxZ });
    }
    else
    {
        add({ minX, minY, contactPlane });
        add({ maxX, minY, contactPlane });
        add({ maxX, maxY, contactPlane });
        add({ minX, maxY, contactPlane });
    }
}
}

bool GenerateBoxBoxManifold(
    Entity boxEntityA,
    const TransformComponent& boxTransformA,
    const ColliderComponent& boxColliderA,
    Entity boxEntityB,
    const TransformComponent& boxTransformB,
    const ColliderComponent& boxColliderB,
    ContactManifold& outManifold)
{
    if (boxColliderA.Type != ColliderType::Box || boxColliderB.Type != ColliderType::Box)
    {
        return false;
    }

    AABB a{};
    AABB b{};
    if (!ComputeColliderAABB(boxTransformA, boxColliderA, a)
        || !ComputeColliderAABB(boxTransformB, boxColliderB, b))
    {
        return false;
    }

    // AABB版SATです。各world axisで投影区間の重なり量を調べます。
    // どれか1軸でも負なら、その軸がSeparating Axisなので衝突していません。
    const float overlapX = std::min(a.Max.x, b.Max.x) - std::max(a.Min.x, b.Min.x);
    const float overlapY = std::min(a.Max.y, b.Max.y) - std::max(a.Min.y, b.Min.y);
    const float overlapZ = std::min(a.Max.z, b.Max.z) - std::max(a.Min.z, b.Min.z);

    if (overlapX < 0.0f || overlapY < 0.0f || overlapZ < 0.0f)
    {
        return false;
    }

    const math::Vec3 centerA = (a.Min + a.Max) * 0.5f;
    const math::Vec3 centerB = (b.Min + b.Max) * 0.5f;
    const math::Vec3 centerDelta = centerB - centerA;

    int normalAxis = 0;
    float penetration = overlapX;
    math::Vec3 normal{ centerDelta.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };

    if (overlapY < penetration)
    {
        normalAxis = 1;
        penetration = overlapY;
        normal = { 0.0f, centerDelta.y >= 0.0f ? 1.0f : -1.0f, 0.0f };
    }
    if (overlapZ < penetration)
    {
        normalAxis = 2;
        penetration = overlapZ;
        normal = { 0.0f, 0.0f, centerDelta.z >= 0.0f ? 1.0f : -1.0f };
    }

    outManifold = ContactManifold{};
    outManifold.A = boxEntityA;
    outManifold.B = boxEntityB;
    outManifold.Normal = normal;
    SetCombinedMaterial(boxColliderA, boxColliderB, outManifold);

    // 接触面は、A側surfaceとB側surfaceの中間を使います。
    // 深い貫通時にもどちらか片側へ偏らず、Debug表示時にも理解しやすい位置です。
    float contactPlane = 0.0f;
    if (normalAxis == 0)
    {
        contactPlane = normal.x > 0.0f
            ? (a.Max.x + b.Min.x) * 0.5f
            : (a.Min.x + b.Max.x) * 0.5f;
    }
    else if (normalAxis == 1)
    {
        contactPlane = normal.y > 0.0f
            ? (a.Max.y + b.Min.y) * 0.5f
            : (a.Min.y + b.Max.y) * 0.5f;
    }
    else
    {
        contactPlane = normal.z > 0.0f
            ? (a.Max.z + b.Min.z) * 0.5f
            : (a.Min.z + b.Max.z) * 0.5f;
    }

    AddFaceContacts(a, b, normalAxis, contactPlane, penetration, outManifold);
    return outManifold.PointCount > 0;
}

} // namespace Raven::ph
