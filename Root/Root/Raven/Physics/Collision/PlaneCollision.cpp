#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{
namespace
{
// Planeとの接触でも他のNarrow Phaseと同じ物理マテリアル合成則を使用します。
void SetCombinedMaterial(
    const ColliderComponent& colliderA,
    const ColliderComponent& colliderB,
    ContactManifold& manifold)
{
    manifold.Restitution = std::min(std::max(colliderA.Restitution, 0.0f), std::max(colliderB.Restitution, 0.0f));
    manifold.StaticFriction = std::sqrt(std::max(colliderA.StaticFriction, 0.0f) * std::max(colliderB.StaticFriction, 0.0f));
    manifold.DynamicFriction = std::sqrt(std::max(colliderA.DynamicFriction, 0.0f) * std::max(colliderB.DynamicFriction, 0.0f));
    manifold.IsTrigger = colliderA.IsTrigger || colliderB.IsTrigger;
}
}

bool GenerateBoxPlaneManifold(
    Entity boxEntity,
    const TransformComponent& boxTransform,
    const ColliderComponent& boxCollider,
    Entity planeEntity,
    const TransformComponent& planeTransform,
    const ColliderComponent& planeCollider,
    ContactManifold& outManifold)
{
    if (boxCollider.Type != ColliderType::Box || planeCollider.Type != ColliderType::Plane)
        return false;

    const float normalLengthSquared = planeCollider.PlaneNormal.LengthSq();
    if (normalLengthSquared <= 1.0e-12f)
        return false;

    OBB box{};
    if (!ComputeBoxOBB(boxTransform, boxCollider, box))
        return false;

    const math::Vec3 planeNormal = planeCollider.PlaneNormal / std::sqrt(normalLengthSquared);
    const math::Vec3 pointOnPlane = planeTransform.Position + planeCollider.Offset
        + planeNormal * planeCollider.PlaneOffset;

    outManifold = ContactManifold{};
    outManifold.A = boxEntity;
    outManifold.B = planeEntity;
    // Ravenの既存Sphere-Planeと同じく、法線はA(Box)からB(Plane)へ向けます。
    outManifold.Normal = -planeNormal;
    SetCombinedMaterial(boxCollider, planeCollider, outManifold);

    // 回転OBBの8頂点をワールド空間で調べます。
    // 平面の負側へ入った頂点が実際の支持候補となり、最大4点をManifoldへ保存します。
    for (int vertexIndex = 0; vertexIndex < 8; ++vertexIndex)
    {
        const float sx = (vertexIndex & 1) ? 1.0f : -1.0f;
        const float sy = (vertexIndex & 2) ? 1.0f : -1.0f;
        const float sz = (vertexIndex & 4) ? 1.0f : -1.0f;

        const math::Vec3 vertex = box.Center
            + box.Axis[0] * (box.HalfExtents.x * sx)
            + box.Axis[1] * (box.HalfExtents.y * sy)
            + box.Axis[2] * (box.HalfExtents.z * sz);

        const float signedDistance = math::Vec3::Dot(vertex - pointOnPlane, planeNormal);
        if (signedDistance > 0.0f)
            continue;

        ContactPoint point{};
        // Solverが平面上の正しいlever armを使えるよう、接触位置をPlaneへ射影します。
        point.Position = vertex - planeNormal * signedDistance;
        point.Penetration = -signedDistance;
        point.Feature.TypeA = ContactFeatureType::Vertex;
        point.Feature.IndexA = static_cast<uint8_t>(vertexIndex);
        point.Feature.TypeB = ContactFeatureType::Face;
        point.Feature.IndexB = 0;
        outManifold.AddPoint(point);
    }

    return outManifold.PointCount > 0;
}

} // namespace Raven::ph
