#include "Raven/Physics/Collision/AABB.h"

namespace Raven::ph
{

bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB)
{
    const math::Vec3 center = transform.Position + collider.Offset;
    math::Vec3 extents{};

    if (collider.Type == ColliderType::Sphere)
    {
        if (collider.Radius <= 0.0f)
        {
            return false;
        }

        extents = math::Vec3{ collider.Radius, collider.Radius, collider.Radius };
    }
    else if (collider.Type == ColliderType::Box)
    {
        // Boxは現在の設計どおり「回転しないAABB Collider」として扱います。
        // ScaleはCollider寸法とは分離している既存設計を維持します。
        extents = math::Vec3{
            std::abs(collider.HalfExtents.x),
            std::abs(collider.HalfExtents.y),
            std::abs(collider.HalfExtents.z)
        };
    }
    else
    {
        return false;
    }

    outAABB.Min = center - extents;
    outAABB.Max = center + extents;
    return true;
}

} // namespace Raven::ph