#include "Raven/Physics/Tests/StaticMeshTriangleBVHSelfTests.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

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

} // namespace

void RunStaticMeshTriangleBVHSelfTests()
{
    RunIndexedGeometryBuildTest();
    RunInvalidIndexFilterTest();
    RunAABBQueryTest();
    RunRayQueryTest();
}

} // namespace Raven::ph::tests
