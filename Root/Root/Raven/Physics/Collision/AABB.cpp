#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVHQuery.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::ph
{
namespace
{
math::Vec3 TransformStaticMeshPoint(
    const math::Mat4& worldTransform,
    const math::Vec3& colliderOffset,
    const math::Vec3& localPoint)
{
    const math::Vec4 worldPoint = worldTransform * math::Vec4{
        localPoint.x + colliderOffset.x,
        localPoint.y + colliderOffset.y,
        localPoint.z + colliderOffset.z,
        1.0f
    };
    return math::Vec3{ worldPoint.x, worldPoint.y, worldPoint.z };
}

// Moller-Trumbore法で両面Triangleを判定します。
// determinantの符号でBack Faceを除外せず絶対値だけを見ることで、Terrain裏面からの
// Queryでも幾何交差自体は取得できます。返却NormalだけはRayと逆向きへ揃え、
// Ground Query側が常にHit面から外向きの法線として扱えるようにします。
bool RayCastTriangleTwoSided(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& c,
    float& outFraction,
    math::Vec3& outNormal)
{
    constexpr float determinantEpsilon = 1.0e-8f;
    constexpr float normalLengthEpsilon = 1.0e-12f;

    const math::Vec3 edgeAB = b - a;
    const math::Vec3 edgeAC = c - a;
    const math::Vec3 p = math::Vec3::Cross(direction, edgeAC);
    const float determinant = math::Vec3::Dot(edgeAB, p);
    if (std::abs(determinant) <= determinantEpsilon)
    {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    const math::Vec3 fromA = origin - a;
    const float barycentricU = math::Vec3::Dot(fromA, p) * inverseDeterminant;
    if (barycentricU < 0.0f || barycentricU > 1.0f)
    {
        return false;
    }

    const math::Vec3 q = math::Vec3::Cross(fromA, edgeAB);
    const float barycentricV = math::Vec3::Dot(direction, q) * inverseDeterminant;
    if (barycentricV < 0.0f || barycentricU + barycentricV > 1.0f)
    {
        return false;
    }

    const float fraction = math::Vec3::Dot(edgeAC, q) * inverseDeterminant;
    if (fraction < 0.0f || fraction > maxFraction)
    {
        return false;
    }

    math::Vec3 normal = math::Vec3::Cross(edgeAB, edgeAC);
    const float normalLengthSquared = normal.LengthSq();
    if (normalLengthSquared <= normalLengthEpsilon)
    {
        return false;
    }
    normal /= std::sqrt(normalLengthSquared);

    if (math::Vec3::Dot(normal, direction) > 0.0f)
    {
        normal = -normal;
    }

    outFraction = fraction;
    outNormal = normal;
    return true;
}
} // namespace

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

    if (collider.Type == ColliderType::StaticMesh)
    {
        if (collider.StaticMeshGeometry == nullptr)
        {
            return false;
        }

        math::Vec3 localMinimum{};
        math::Vec3 localMaximum{};
        if (collider.StaticMeshGeometry->GetLocalBounds(localMinimum, localMaximum) == false)
        {
            return false;
        }

        // ====================================================================
        // Static Mesh -> World AABB
        // ====================================================================
        // Terrainは回転・非一様Scaleを含むTransformを持てるため、local AABBのMin/Maxだけを
        // 変換する方法ではworld boundsを正しく包めません。8隅すべてをworldへ変換し、
        // 必ずMesh全体を包含する保守的なAABBを作ります。
        //
        // BroadPhase同期はQueryのたびにも呼ばれるため、全頂点変換は常時負荷になります。
        // Static Geometry側の境界再利用により頂点数に依存しない8点の変換へ抑えます。
        // 回転時は従来のtight boundsより広くなる場合がありますが、実Triangle判定はNarrow Phaseが担当します。
        // Offsetは従来どおりlocal-spaceで加算し、負Scale・非一様Scaleも同じ行列で処理します。
        const math::Mat4 worldTransform = transform.GetTransform();
        math::Vec3 minimum = TransformStaticMeshPoint(
            worldTransform,
            collider.Offset,
            localMinimum);
        math::Vec3 maximum = minimum;

        for (uint32_t corner = 1u; corner < 8u; ++corner)
        {
            const math::Vec3 point = TransformStaticMeshPoint(
                worldTransform,
                collider.Offset,
                math::Vec3{
                    (corner & 1u) != 0u ? localMaximum.x : localMinimum.x,
                    (corner & 2u) != 0u ? localMaximum.y : localMinimum.y,
                    (corner & 4u) != 0u ? localMaximum.z : localMinimum.z });
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }

        outAABB.Min = minimum;
        outAABB.Max = maximum;
        return true;
    }

    // Planeは無限形状なので有限AABBを持ちません。
    return false;
}

bool RayCastStaticMeshCollider(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    float& outFraction,
    math::Vec3& outNormal)
{
    if (collider.Type != ColliderType::StaticMesh
        || collider.StaticMeshGeometry == nullptr
        || maxFraction < 0.0f
        || direction.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    const auto& vertices = collider.StaticMeshGeometry->GetVertices();
    const auto& indices = collider.StaticMeshGeometry->GetIndices();
    if (vertices.size() < 3)
    {
        return false;
    }

    const math::Mat4 worldTransform = transform.GetTransform();
    bool hit = false;
    float closestFraction = maxFraction;
    math::Vec3 closestNormal{};

    const auto testTriangle =
        [&](std::size_t indexA, std::size_t indexB, std::size_t indexC)
        {
            if (indexA >= vertices.size()
                || indexB >= vertices.size()
                || indexC >= vertices.size())
            {
                // 壊れたIndexを持つTriangleだけを除外し、他の正常TriangleのQueryは継続します。
                return;
            }

            const math::Vec3 a = TransformStaticMeshPoint(
                worldTransform,
                collider.Offset,
                vertices[indexA].Position);
            const math::Vec3 b = TransformStaticMeshPoint(
                worldTransform,
                collider.Offset,
                vertices[indexB].Position);
            const math::Vec3 c = TransformStaticMeshPoint(
                worldTransform,
                collider.Offset,
                vertices[indexC].Position);

            float fraction = 0.0f;
            math::Vec3 normal{};
            if (RayCastTriangleTwoSided(
                    origin,
                    direction,
                    closestFraction,
                    a,
                    b,
                    c,
                    fraction,
                    normal) == false)
            {
                return;
            }

            hit = true;
            closestFraction = fraction;
            closestNormal = normal;
        };

    // ========================================================================
    // Triangle BVH candidate query
    // ========================================================================
    // GeometryはLocal SpaceでBVH化し、RayだけをLocalへ逆変換します。
    // Query helperが非特異Transformを扱えない場合やBVH Buildに失敗した場合は、
    // 既存の全Triangle走査へfallbackして正しさを維持します。
    std::shared_ptr<const StaticMeshTriangleBVH> bvh;
    std::vector<uint32_t> candidateTriangles;
    if (QueryStaticMeshBVHByWorldRay(
            origin,
            direction,
            maxFraction,
            transform,
            collider,
            bvh,
            candidateTriangles))
    {
        for (uint32_t triangleIndex : candidateTriangles)
        {
            const StaticMeshTriangleBVH::Triangle* triangle = bvh->GetTriangle(triangleIndex);
            if (triangle == nullptr)
            {
                continue;
            }

            testTriangle(triangle->IndexA, triangle->IndexB, triangle->IndexC);
        }
    }
    else if (indices.empty() == false)
    {
        // glTF Terrainの通常経路です。末尾に3未満の不完全Indexがあっても読み越さないよう、
        // index + 2 が範囲内のTriangleだけを評価します。
        for (std::size_t index = 0; index + 2 < indices.size(); index += 3)
        {
            testTriangle(indices[index], indices[index + 1], indices[index + 2]);
        }
    }
    else
    {
        // 非Indexed Geometryは頂点3個を1Triangleとして扱います。
        for (std::size_t index = 0; index + 2 < vertices.size(); index += 3)
        {
            testTriangle(index, index + 1, index + 2);
        }
    }

    if (hit == false)
    {
        return false;
    }

    outFraction = closestFraction;
    outNormal = closestNormal;
    return true;
}

} // namespace Raven::ph
