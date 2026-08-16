#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/Capsule.h"
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
        if (ComputeBoxOBB(transform, collider, obb) == false)
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

    if (collider.Type == ColliderType::Capsule)
    {
        Capsule capsule{};
        if (ComputeCapsule(transform, collider, capsule) == false)
        {
            return false;
        }

        // Capsuleの有限部分は中心線分をRadiusだけMinkowski膨張した形です。
        // よって線分両端の各成分min/maxへRadiusを足し引きするだけで、
        // 回転後のCapsuleを完全に包むtight AABBを得られます。
        const math::Vec3 radius{
            capsule.Radius,
            capsule.Radius,
            capsule.Radius
        };
        outAABB.Min = math::Vec3{
            std::min(capsule.SegmentA.x, capsule.SegmentB.x),
            std::min(capsule.SegmentA.y, capsule.SegmentB.y),
            std::min(capsule.SegmentA.z, capsule.SegmentB.z)
        } - radius;
        outAABB.Max = math::Vec3{
            std::max(capsule.SegmentA.x, capsule.SegmentB.x),
            std::max(capsule.SegmentA.y, capsule.SegmentB.y),
            std::max(capsule.SegmentA.z, capsule.SegmentB.z)
        } + radius;
        return true;
    }

    // Planeは無限形状なので有限AABBを持ちません。
    return false;
}

} // namespace Raven::ph
