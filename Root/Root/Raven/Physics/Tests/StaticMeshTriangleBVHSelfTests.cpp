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
    RunIndexedGeometryBuildTest();
    RunInvalidIndexFilterTest();
    RunAABBQueryTest();
    RunRayQueryTest();
    RunGeometryCacheTest();
    RunWorldRayQueryTransformTest();
    RunCapsuleBVHManifoldTest();
}

} // namespace Raven::ph::tests
