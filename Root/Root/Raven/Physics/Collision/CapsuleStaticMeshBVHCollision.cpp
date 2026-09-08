#include "Raven/Physics/Collision/CollisionDetection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/Capsule.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVHQuery.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::ph
{
namespace
{

math::Vec3 TransformBVHStaticMeshPoint(
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

void SetBVHCombinedMaterial(
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

bool BuildBVHSinglePointManifold(
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
    SetBVHCombinedMaterial(colliderA, colliderB, outManifold);
    outManifold.AddPoint(point);
    return true;
}

AABB ComputeCapsuleWorldAABB(const Capsule& capsule)
{
    const math::Vec3 radius{
        capsule.Radius,
        capsule.Radius,
        capsule.Radius
    };

    AABB bounds{};
    bounds.Min = math::Vec3{
        std::min(capsule.SegmentA.x, capsule.SegmentB.x),
        std::min(capsule.SegmentA.y, capsule.SegmentB.y),
        std::min(capsule.SegmentA.z, capsule.SegmentB.z)
    } - radius;
    bounds.Max = math::Vec3{
        std::max(capsule.SegmentA.x, capsule.SegmentB.x),
        std::max(capsule.SegmentA.y, capsule.SegmentB.y),
        std::max(capsule.SegmentA.z, capsule.SegmentB.z)
    } + radius;
    return bounds;
}

} // namespace

bool GenerateCapsuleStaticMeshBVHManifold(
    Entity capsuleEntity,
    const TransformComponent& capsuleTransform,
    const ColliderComponent& capsuleCollider,
    Entity staticMeshEntity,
    const TransformComponent& staticMeshTransform,
    const ColliderComponent& staticMeshCollider,
    ContactManifold& outManifold)
{
    if (capsuleCollider.Type != ColliderType::Capsule
        || staticMeshCollider.Type != ColliderType::StaticMesh
        || staticMeshCollider.StaticMeshGeometry == nullptr)
    {
        return false;
    }

    Capsule capsule{};
    if (ComputeCapsule(capsuleTransform, capsuleCollider, capsule) == false)
    {
        return false;
    }

    const auto& vertices = staticMeshCollider.StaticMeshGeometry->GetVertices();
    if (vertices.size() < 3u)
    {
        return false;
    }

    // ========================================================================
    // BVH candidate query
    // ========================================================================
    // Capsuleそのものを非一様ScaleのMesh Localへ変換すると楕円断面になり扱いが複雑になります。
    // そこでWorld Capsuleを包むtight AABBを作り、その8 cornerをMesh Localへ戻した保守的AABBで
    // BVHをQueryします。候補は多少増えても、実際に接触し得るTriangleを取りこぼしません。
    std::shared_ptr<const StaticMeshTriangleBVH> bvh;
    std::vector<uint32_t> candidateTriangles;
    const AABB capsuleWorldBounds = ComputeCapsuleWorldAABB(capsule);
    if (QueryStaticMeshBVHByWorldAABB(
            capsuleWorldBounds,
            staticMeshTransform,
            staticMeshCollider,
            bvh,
            candidateTriangles) == false)
    {
        // Singular Transform / Dynamic Geometry / Build失敗では既存の全Triangle走査へ戻します。
        return GenerateCapsuleStaticMeshManifold(
            capsuleEntity,
            capsuleTransform,
            capsuleCollider,
            staticMeshEntity,
            staticMeshTransform,
            staticMeshCollider,
            outManifold);
    }

    RAVEN_PROFILE_SCOPE("Physics.StaticMesh.CapsuleNarrowPhase");

    const math::Mat4 worldTransform = staticMeshTransform.GetTransform();
    const float radiusSquared = capsule.Radius * capsule.Radius;
    bool found = false;
    float bestPenetration = -std::numeric_limits<float>::max();
    math::Vec3 bestNormal{};
    math::Vec3 bestPosition{};

    // SourceTriangleCountはBVH構築時に正常Indexだけへ正規化されたTriangle総数です。
    // CandidateTriangleCountとの比をProfiler上で比較することで、実Terrainに対する
    // BVHの枝刈り効果を全走査時の仕事量を基準に定量化できます。
    const uint64_t sourceTriangleCount = static_cast<uint64_t>(bvh->GetTriangleCount());
    const uint64_t candidateTriangleCount = static_cast<uint64_t>(candidateTriangles.size());
    uint64_t narrowPhaseTriangleTestCount = 0u;
    uint64_t overlapTriangleCount = 0u;

    for (uint32_t triangleIndex : candidateTriangles)
    {
        const StaticMeshTriangleBVH::Triangle* triangle = bvh->GetTriangle(triangleIndex);
        if (triangle == nullptr
            || triangle->IndexA >= vertices.size()
            || triangle->IndexB >= vertices.size()
            || triangle->IndexC >= vertices.size())
        {
            continue;
        }

        const math::Vec3 triangleA = TransformBVHStaticMeshPoint(
            worldTransform,
            staticMeshCollider.Offset,
            vertices[triangle->IndexA].Position);
        const math::Vec3 triangleB = TransformBVHStaticMeshPoint(
            worldTransform,
            staticMeshCollider.Offset,
            vertices[triangle->IndexB].Position);
        const math::Vec3 triangleC = TransformBVHStaticMeshPoint(
            worldTransform,
            staticMeshCollider.Offset,
            vertices[triangle->IndexC].Position);

        const math::Vec3 rawTriangleNormal = math::Vec3::Cross(
            triangleB - triangleA,
            triangleC - triangleA);
        const float triangleNormalLengthSquared = rawTriangleNormal.LengthSq();
        if (triangleNormalLengthSquared <= 1.0e-12f)
        {
            continue;
        }

        ++narrowPhaseTriangleTestCount;

        math::Vec3 capsulePoint{};
        math::Vec3 trianglePoint{};
        ClosestPointsSegmentTriangle(
            capsule.SegmentA,
            capsule.SegmentB,
            triangleA,
            triangleB,
            triangleC,
            capsulePoint,
            trianglePoint);

        const math::Vec3 delta = trianglePoint - capsulePoint;
        const float distanceSquared = delta.LengthSq();
        if (distanceSquared > radiusSquared)
        {
            continue;
        }

        ++overlapTriangleCount;

        float distance = 0.0f;
        math::Vec3 capsuleToMeshNormal{};
        if (distanceSquared > 1.0e-12f)
        {
            distance = std::sqrt(distanceSquared);
            capsuleToMeshNormal = delta / distance;
        }
        else
        {
            // 中心線分がTriangle面を横切る深いOverlapでは最近接差分から法線を作れないため、
            // 既存実装と同じくTriangle windingを基準にCapsule->Mesh方向へ揃えます。
            math::Vec3 obstacleNormal = rawTriangleNormal / std::sqrt(triangleNormalLengthSquared);
            if (math::Vec3::Dot(obstacleNormal, capsule.Center() - trianglePoint) < 0.0f)
            {
                obstacleNormal = -obstacleNormal;
            }
            capsuleToMeshNormal = -obstacleNormal;
        }

        const float penetration = capsule.Radius - distance;
        if (found == true && penetration <= bestPenetration)
        {
            continue;
        }

        const math::Vec3 capsuleSurface = capsulePoint + capsuleToMeshNormal * capsule.Radius;
        found = true;
        bestPenetration = penetration;
        bestNormal = capsuleToMeshNormal;
        bestPosition = (capsuleSurface + trianglePoint) * 0.5f;
    }

    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter("Physics.StaticMesh.Capsule.ManifoldCallCount", 1.0);
    profiler.AddCounter(
        "Physics.StaticMesh.Capsule.SourceTriangleCount",
        static_cast<double>(sourceTriangleCount));
    profiler.AddCounter(
        "Physics.StaticMesh.Capsule.CandidateTriangleCount",
        static_cast<double>(candidateTriangleCount));
    profiler.AddCounter(
        "Physics.StaticMesh.Capsule.NarrowPhaseTriangleTestCount",
        static_cast<double>(narrowPhaseTriangleTestCount));
    profiler.AddCounter(
        "Physics.StaticMesh.Capsule.OverlapTriangleCount",
        static_cast<double>(overlapTriangleCount));

    if (found == false)
    {
        return false;
    }

    return BuildBVHSinglePointManifold(
        capsuleEntity,
        staticMeshEntity,
        capsuleCollider,
        staticMeshCollider,
        bestNormal,
        bestPosition,
        bestPenetration,
        outManifold);
}

} // namespace Raven::ph
