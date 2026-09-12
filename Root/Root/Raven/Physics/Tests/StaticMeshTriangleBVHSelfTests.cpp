#include "Raven/Physics/Tests/StaticMeshTriangleBVHSelfTests.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"
#include "Raven/Physics/Collision/StaticMeshTriangleBVHQuery.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Components.h"

namespace Raven::ph::tests
{
namespace
{

MeshVertex MakeVertex(float x, float y, float z)
{
    MeshVertex vertex{};
    vertex.Position = math::Vec3{ x, y, z };
    return vertex;
}

bool ContainsTriangleIndex(
    const std::vector<uint32_t>& triangleIndices,
    uint32_t triangleIndex)
{
    return std::find(
        triangleIndices.begin(),
        triangleIndices.end(),
        triangleIndex) != triangleIndices.end();
}

void RunStaticMeshBoundsTest()
{
    auto geometry = std::make_shared<MeshGeometry>(
        std::vector<MeshVertex>{ MakeVertex(-3, -2, -1), MakeVertex(4, 5, 6), MakeVertex(0, 2, -4) },
        std::vector<uint32_t>{}, GeometryUsage::Dynamic);
    ColliderComponent collider{};
    collider.Type = ColliderType::StaticMesh;
    collider.StaticMeshGeometry = geometry;
    collider.Offset = math::Vec3{ 2, -1, 3 };
    TransformComponent transform{};
    transform.Position = math::Vec3{ 10, -3, 2 };
    transform.Rotation = math::Vec3{ 0.3f, -0.7f, 1.1f };
    transform.Scale = math::Vec3{ -2, 3, 0.5f };

    // 回転・負の非一様Scale・local Offsetを組み合わせても全頂点を包含します。
    const auto checkBounds = [&]()
    {
        AABB bounds{};
        assert(ComputeColliderAABB(transform, collider, bounds));
        for (const auto& vertex : collider.StaticMeshGeometry->GetVertices())
        {
            const auto local = vertex.Position + collider.Offset;
            const auto world = transform.GetTransform() * math::Vec4{ local.x, local.y, local.z, 1.0f };
            constexpr float tolerance = 1.0e-4f;
            assert(world.x >= bounds.Min.x - tolerance && world.x <= bounds.Max.x + tolerance);
            assert(world.y >= bounds.Min.y - tolerance && world.y <= bounds.Max.y + tolerance);
            assert(world.z >= bounds.Min.z - tolerance && world.z <= bounds.Max.z + tolerance);
        }
    };
    checkBounds();
    auto changed = geometry->GetVertices();
    changed[0].Position = math::Vec3{ -100, 50, 20 };
    assert(geometry->SetVertices(changed));
    checkBounds();
    changed[1].Position = math::Vec3{ 200, -80, -30 };
    assert(geometry->SwapVertices(changed));
    checkBounds();

    // Staticの生成時cacheも同じ境界を返し、Transform変更では再生成不要です。
    collider.StaticMeshGeometry = std::make_shared<MeshGeometry>(geometry->GetVertices(), std::vector<uint32_t>{});
    checkBounds();
    transform.Position.x += 30.0f;
    checkBounds();
    collider.StaticMeshGeometry = std::make_shared<MeshGeometry>();
    AABB bounds{};
    assert(ComputeColliderAABB(transform, collider, bounds) == false);
}

void RunIndexedGeometryBuildTest()
{
    std::vector<MeshVertex> vertices{
        MakeVertex(-3.0f, 0.0f, -1.0f),
        MakeVertex(-1.0f, 0.0f, -1.0f),
        MakeVertex(-2.0f, 0.0f, 1.0f),
        MakeVertex(1.0f, 0.0f, -1.0f),
        MakeVertex(3.0f, 0.0f, -1.0f),
        MakeVertex(2.0f, 0.0f, 1.0f)
    };
    std::vector<uint32_t> indices{
        0u, 1u, 2u,
        3u, 4u, 5u
    };

    MeshGeometry geometry(std::move(vertices), std::move(indices));
    StaticMeshTriangleBVH bvh;
    assert(bvh.Build(geometry, 1u));
    assert(bvh.IsEmpty() == false);
    assert(bvh.GetTriangleCount() == 2u);
    assert(bvh.GetNodeCount() == 3u);

    const StaticMeshTriangleBVH::Node* root = bvh.GetRoot();
    assert(root != nullptr);
    assert(root->IsLeaf() == false);
}

void RunInvalidIndexFilterTest()
{
    std::vector<MeshVertex> vertices{
        MakeVertex(-1.0f, 0.0f, -1.0f),
        MakeVertex(1.0f, 0.0f, -1.0f),
        MakeVertex(0.0f, 0.0f, 1.0f)
    };
    std::vector<uint32_t> indices{
        0u, 1u, 2u,
        0u, 1u, 99u
    };

    MeshGeometry geometry(std::move(vertices), std::move(indices));
    StaticMeshTriangleBVH bvh;
    assert(bvh.Build(geometry));
    assert(bvh.GetTriangleCount() == 1u);

    const StaticMeshTriangleBVH::Triangle* triangle = bvh.GetTriangle(0u);
    assert(triangle != nullptr);
    assert(triangle->IndexA == 0u);
    assert(triangle->IndexB == 1u);
    assert(triangle->IndexC == 2u);
}

void RunAABBQueryTest()
{
    std::vector<MeshVertex> vertices{
        MakeVertex(-4.0f, 0.0f, -1.0f),
        MakeVertex(-2.0f, 0.0f, -1.0f),
        MakeVertex(-3.0f, 0.0f, 1.0f),
        MakeVertex(-1.0f, 0.0f, -1.0f),
        MakeVertex(1.0f, 0.0f, -1.0f),
        MakeVertex(0.0f, 0.0f, 1.0f),
        MakeVertex(2.0f, 0.0f, -1.0f),
        MakeVertex(4.0f, 0.0f, -1.0f),
        MakeVertex(3.0f, 0.0f, 1.0f)
    };

    MeshGeometry geometry(std::move(vertices), {});
    StaticMeshTriangleBVH bvh;
    assert(bvh.Build(geometry, 1u));
    assert(bvh.GetTriangleCount() == 3u);

    std::vector<uint32_t> candidates;
    bvh.QueryAABB(
        math::Vec3{ -0.5f, -0.1f, -0.5f },
        math::Vec3{ 0.5f, 0.1f, 0.5f },
        candidates);

    assert(candidates.size() == 1u);
    assert(ContainsTriangleIndex(candidates, 1u));
    assert(candidates.size() < bvh.GetTriangleCount());
}

void RunRayQueryTest()
{
    std::vector<MeshVertex> vertices{
        MakeVertex(-4.0f, 0.0f, -1.0f),
        MakeVertex(-2.0f, 0.0f, -1.0f),
        MakeVertex(-3.0f, 0.0f, 1.0f),
        MakeVertex(-1.0f, 0.0f, -1.0f),
        MakeVertex(1.0f, 0.0f, -1.0f),
        MakeVertex(0.0f, 0.0f, 1.0f),
        MakeVertex(2.0f, 0.0f, -1.0f),
        MakeVertex(4.0f, 0.0f, -1.0f),
        MakeVertex(3.0f, 0.0f, 1.0f)
    };

    MeshGeometry geometry(std::move(vertices), {});
    StaticMeshTriangleBVH bvh;
    assert(bvh.Build(geometry, 1u));

    std::vector<uint32_t> candidates;
    bvh.QueryRay(
        math::Vec3{ 0.0f, 2.0f, 0.0f },
        math::Vec3{ 0.0f, -1.0f, 0.0f },
        10.0f,
        candidates);

    assert(candidates.size() == 1u);
    assert(ContainsTriangleIndex(candidates, 1u));
    assert(candidates.size() < bvh.GetTriangleCount());

    candidates.clear();
    bvh.QueryRay(
        math::Vec3{ 8.0f, 2.0f, 0.0f },
        math::Vec3{ 0.0f, -1.0f, 0.0f },
        10.0f,
        candidates);
    assert(candidates.empty());
}

void RunGeometryCacheTest()
{
    auto geometry = std::make_shared<MeshGeometry>(
        std::vector<MeshVertex>{
            MakeVertex(-1.0f, 0.0f, -1.0f),
            MakeVertex(1.0f, 0.0f, -1.0f),
            MakeVertex(0.0f, 0.0f, 1.0f)
        },
        std::vector<uint32_t>{});

    const auto first = StaticMeshTriangleBVH::GetOrBuildCached(geometry);
    const auto second = StaticMeshTriangleBVH::GetOrBuildCached(geometry);
    assert(first != nullptr);
    assert(second != nullptr);
    assert(first.get() == second.get());
}

void RunWorldRayQueryTransformTest()
{
    auto geometry = std::make_shared<MeshGeometry>(
        std::vector<MeshVertex>{
            MakeVertex(-1.0f, 0.0f, -1.0f),
            MakeVertex(1.0f, 0.0f, -1.0f),
            MakeVertex(0.0f, 0.0f, 1.0f)
        },
        std::vector<uint32_t>{});

    ColliderComponent collider{};
    collider.Type = ColliderType::StaticMesh;
    collider.StaticMeshGeometry = geometry;
    collider.Offset = math::Vec3{ 0.5f, 0.0f, 0.0f };

    TransformComponent transform{};
    transform.Position = math::Vec3{ 5.0f, 2.0f, 0.0f };
    transform.Scale = math::Vec3{ 2.0f, 1.0f, 1.0f };

    std::shared_ptr<const StaticMeshTriangleBVH> bvh;
    std::vector<uint32_t> candidates;
    assert(QueryStaticMeshBVHByWorldRay(
        math::Vec3{ 6.0f, 5.0f, 0.0f },
        math::Vec3{ 0.0f, -1.0f, 0.0f },
        10.0f,
        transform,
        collider,
        bvh,
        candidates));
    assert(bvh != nullptr);
    assert(candidates.size() == 1u);

    float fraction = 0.0f;
    math::Vec3 normal{};
    assert(RayCastStaticMeshCollider(
        math::Vec3{ 6.0f, 5.0f, 0.0f },
        math::Vec3{ 0.0f, -1.0f, 0.0f },
        10.0f,
        transform,
        collider,
        fraction,
        normal));
    assert(std::abs(fraction - 3.0f) <= 1.0e-5f);
    assert(normal.y > 0.99f);
}

void RunCapsuleBVHManifoldTest()
{
    auto geometry = std::make_shared<MeshGeometry>(
        std::vector<MeshVertex>{
            MakeVertex(-4.0f, 0.0f, -4.0f),
            MakeVertex(4.0f, 0.0f, -4.0f),
            MakeVertex(0.0f, 0.0f, 4.0f)
        },
        std::vector<uint32_t>{});

    TransformComponent meshTransform{};
    ColliderComponent meshCollider{};
    meshCollider.Type = ColliderType::StaticMesh;
    meshCollider.StaticMeshGeometry = geometry;

    TransformComponent capsuleTransform{};
    capsuleTransform.Position = math::Vec3{ 0.0f, 0.4f, 0.0f };

    ColliderComponent capsuleCollider{};
    capsuleCollider.Type = ColliderType::Capsule;
    capsuleCollider.Radius = 0.5f;
    capsuleCollider.HalfLength = 0.1f;

    ContactManifold manifold{};
    assert(GenerateCapsuleStaticMeshBVHManifold(
        Entity{},
        capsuleTransform,
        capsuleCollider,
        Entity{},
        meshTransform,
        meshCollider,
        manifold));
    assert(manifold.PointCount == 1u);
    assert(manifold.Points[0].Penetration > 0.0f);
}

} // namespace

void RunStaticMeshTriangleBVHSelfTests()
{
    RunStaticMeshBoundsTest();
    RunIndexedGeometryBuildTest();
    RunInvalidIndexFilterTest();
    RunAABBQueryTest();
    RunRayQueryTest();
    RunGeometryCacheTest();
    RunWorldRayQueryTransformTest();
    RunCapsuleBVHManifoldTest();
}

} // namespace Raven::ph::tests
