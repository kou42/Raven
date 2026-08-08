#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/OBB.h"

namespace Raven::ph
{

bool ComputeColliderAABB(
    const TransformComponent& transform,
    const ColliderComponent& collider,
    AABB& outAABB)
{
    if (collider.Type == ColliderType::Sphere)
    {
        if (collider.Radius <= 0.0f)
        {
            return false;
        }

        // Sphereは回転の影響を受けないため従来どおりです。
        // Collider Offsetも既存挙動との互換性を維持します。
        const math::Vec3 center = transform.Position + collider.Offset;
        const math::Vec3 extents{ collider.Radius, collider.Radius, collider.Radius };
        outAABB.Min = center - extents;
        outAABB.Max = center + extents;
        return true;
    }

    if (collider.Type == ColliderType::Box)
    {
        OBB obb{};
        if (!ComputeBoxOBB(transform, collider, obb))
        {
            return false;
        }

        // ====================================================================
        // OBB -> Broad Phase AABB
        // ====================================================================
        // Dynamic AABB Tree自体はAxis-Alignedのまま維持します。
        // OBBの各ローカル軸がworld X/Y/Zへどれだけ投影されるかを足し合わせると、
        // 回転Boxを完全に包むtight AABBのhalf extentsを得られます。
        //
        //   ex = |Ax.x|*hx + |Ay.x|*hy + |Az.x|*hz
        //
        // Y/Zも同様です。これによりBroad Phaseは既存実装を一切OBB化せず、
        // Narrow Phaseだけを高精度なOBB判定へ移行できます。
        const math::Vec3 extents{
            std::abs(obb.Axis[0].x) * obb.HalfExtents.x
                + std::abs(obb.Axis[1].x) * obb.HalfExtents.y
                + std::abs(obb.Axis[2].x) * obb.HalfExtents.z,
            std::abs(obb.Axis[0].y) * obb.HalfExtents.x
                + std::abs(obb.Axis[1].y) * obb.HalfExtents.y
                + std::abs(obb.Axis[2].y) * obb.HalfExtents.z,
            std::abs(obb.Axis[0].z) * obb.HalfExtents.x
                + std::abs(obb.Axis[1].z) * obb.HalfExtents.y
                + std::abs(obb.Axis[2].z) * obb.HalfExtents.z
        };

        outAABB.Min = obb.Center - extents;
        outAABB.Max = obb.Center + extents;
        return true;
    }

    // Planeは無限形状なので有限AABBを持ちません。
    return false;
}

} // namespace Raven::ph
