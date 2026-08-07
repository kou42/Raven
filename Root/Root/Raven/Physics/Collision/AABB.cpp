#include <algorithm>
#include <cmath>

#include "Raven/Physics/Collision/AABB.h"

namespace Raven::ph
{

bool AABB::Overlaps(const AABB& other) const
{
    // 分離軸が1つでも存在すれば交差していません。
    // AABBでは候補となる分離軸はX/Y/Zの3本だけなので、非常に安価に判定できます。
    if (Max.x < other.Min.x || Min.x > other.Max.x)
    {
        return false;
    }

    if (Max.y < other.Min.y || Min.y > other.Max.y)
    {
        return false;
    }

    if (Max.z < other.Min.z || Min.z > other.Max.z)
    {
        return false;
    }

    return true;
}

bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB)
{
    const math::Vec3 center = transform.Position + collider.Offset;

    switch (collider.Type)
    {
    case ColliderType::Sphere:
    {
        if (collider.Radius <= 0.0f)
        {
            return false;
        }

        // Sphereを包むAABBは、各軸へRadiusだけ広げれば求められます。
        // Narrow Phaseの現在のSphere実装と意味を揃えるため、Transform::Scaleは
        // ここではRadiusへ掛けません。
        const math::Vec3 extents{
            collider.Radius,
            collider.Radius,
            collider.Radius
        };

        outAABB.Min = center - extents;
        outAABB.Max = center + extents;
        return true;
    }

    case ColliderType::Box:
    {
        // HalfExtentsに負値が入ってもMin/Maxが反転しないよう絶対値化します。
        // 現段階のBoxは回転しないAABBとして扱う設計なので、OBB変換は行いません。
        const math::Vec3 extents{
            std::abs(collider.HalfExtents.x),
            std::abs(collider.HalfExtents.y),
            std::abs(collider.HalfExtents.z)
        };

        outAABB.Min = center - extents;
        outAABB.Max = center + extents;
        return true;
    }

    case ColliderType::Plane:
        // Planeは無限に広がるため有限AABBへ入れません。
        // PhysicsWorld側でSphere-Plane Narrow Phaseを別途処理します。
        return false;
    }

    return false;
}

} // namespace Raven::ph
