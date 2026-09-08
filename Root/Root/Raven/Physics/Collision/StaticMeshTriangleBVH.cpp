#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <unordered_map>

#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::ph
{
namespace
{
math::Vec3 MinComponents(const math::Vec3& a, const math::Vec3& b)
{
    return math::Vec3{ std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
}

math::Vec3 MaxComponents(const math::Vec3& a, const math::Vec3& b)
{
    return math::Vec3{ std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
}

bool BoundsOverlap(const math::Vec3& minA, const math::Vec3& maxA, const math::Vec3& minB, const math::Vec3& maxB)
{
    return maxA.x >= minB.x && minA.x <= maxB.x
        && maxA.y >= minB.y && minA.y <= maxB.y
        && maxA.z >= minB.z && minA.z <= maxB.z;
}

bool RayIntersectsBounds(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const math::Vec3& boundsMin,
    const math::Vec3& boundsMax)
{
    constexpr float parallelEpsilon = 1.0e-8f;
    float tMin = 0.0f;
    float tMax = std::max(maxFraction, 0.0f);

    for (int axis = 0; axis < 3; ++axis)
    {
        const float directionValue = direction[axis];
        if (std::abs(directionValue) <= parallelEpsilon)
        {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis])
            {
                return false;
            }
            continue;
        }

        const float inverseDirection = 1.0f / directionValue;
        float t1 = (boundsMin[axis] - origin[axis]) * inverseDirection;
        float t2 = (boundsMax[axis] - origin[axis]) * inverseDirection;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }
    return true;
}

int LongestAxis(const math::Vec3& extent)
{
    int axis = 0;
    if (extent.y > extent.x)
    {
        axis = 1;
    }
    if (extent.z > extent[axis])
    {
        axis = 2;
    }
    return axis;
}

struct CachedBVHEntry
{
    std::weak_ptr<const MeshGeometry> Geometry;
    std::shared_ptr<const StaticMeshTriangleBVH> BVH;
};

std::mutex& GetBVHCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<const MeshGeometry*, CachedBVHEntry>& GetBVHCache()
{
    static std::unordered_map<const MeshGeometry*, CachedBVHEntry> cache;
    return cache;
}
} // namespace

bool StaticMeshTriangleBVH::Build(const MeshGeometry& geometry, uint32_t maxTrianglesPerLeaf)
{
    Clear();
    m_MaxTrianglesPerLeaf = std::max(maxTrianglesPerLeaf, 1u);

    const auto& vertices = geometry.GetVertices();
    const auto& indices = geometry.GetIndices();
    if (vertices.size() < 3u)
    {
        return false;
    }

    const auto appendTriangle = [&](uint32_t indexA, uint32_t indexB, uint32_t indexC)
    {
        if (indexA >= vertices.size() || indexB >= vertices.size() || indexC >= vertices.size())
        {
            // 壊れたIndexは構築時に除外し、Query hot pathを正常Triangleだけに保ちます。
            return;
        }

        const math::Vec3& a = vertices[indexA].Position;
        const math::Vec3& b = vertices[indexB].Position;
        const math::Vec3& c = vertices[indexC].Position;
        Triangle triangle{};
        triangle.IndexA = indexA;
        triangle.IndexB = indexB;
        triangle.IndexC = indexC;
        triangle.BoundsMin = MinComponents(a, MinComponents(b, c));
        triangle.BoundsMax = MaxComponents(a, MaxComponents(b, c));
        triangle.Centroid = (a + b + c) / 3.0f;
        m_Triangles.push_back(triangle);
    };

    if (indices.empty() == false)
    {
        for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u)
        {
            appendTriangle(indices[index], indices[index + 1u], indices[index + 2u]);
        }
    }
    else
    {
        for (std::size_t index = 0u; index + 2u < vertices.size(); index += 3u)
        {
            appendTriangle(static_cast<uint32_t>(index), static_cast<uint32_t>(index + 1u), static_cast<uint32_t>(index + 2u));
        }
    }

    if (m_Triangles.empty())
    {
        return false;
    }

    m_TriangleOrder.resize(m_Triangles.size());
    std::iota(m_TriangleOrder.begin(), m_TriangleOrder.end(), 0u);
    m_Nodes.reserve(m_Triangles.size() * 2u - 1u);
    BuildNode(0u, static_cast<uint32_t>(m_TriangleOrder.size()));
    return m_Nodes.empty() == false;
}

void StaticMeshTriangleBVH::Clear()
{
    m_Triangles.clear();
    m_TriangleOrder.clear();
    m_Nodes.clear();
    m_MaxTrianglesPerLeaf = DefaultMaxTrianglesPerLeaf;
}

std::shared_ptr<const StaticMeshTriangleBVH> StaticMeshTriangleBVH::GetOrBuildCached(
    const std::shared_ptr<const MeshGeometry>& geometry)
{
    if (geometry == nullptr || geometry->GetGeometryUsage() != GeometryUsage::Static)
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(GetBVHCacheMutex());
    auto& cache = GetBVHCache();
    const MeshGeometry* geometryKey = geometry.get();
    auto found = cache.find(geometryKey);
    if (found != cache.end())
    {
        const std::shared_ptr<const MeshGeometry> cachedGeometry = found->second.Geometry.lock();
        if (cachedGeometry != nullptr && cachedGeometry.get() == geometryKey && found->second.BVH != nullptr)
        {
            return found->second.BVH;
        }
        cache.erase(found);
    }

    auto bvh = std::make_shared<StaticMeshTriangleBVH>();
    if (bvh->Build(*geometry) == false)
    {
        return nullptr;
    }

    CachedBVHEntry entry{};
    entry.Geometry = geometry;
    entry.BVH = bvh;
    cache.emplace(geometryKey, std::move(entry));

    for (auto iterator = cache.begin(); iterator != cache.end();)
    {
        if (iterator->second.Geometry.expired())
        {
            iterator = cache.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
    return bvh;
}

uint32_t StaticMeshTriangleBVH::BuildNode(uint32_t firstTriangle, uint32_t triangleCount)
{
    const uint32_t nodeIndex = static_cast<uint32_t>(m_Nodes.size());
    m_Nodes.push_back(Node{});

    const Triangle& first = m_Triangles[m_TriangleOrder[firstTriangle]];
    math::Vec3 boundsMin = first.BoundsMin;
    math::Vec3 boundsMax = first.BoundsMax;
    math::Vec3 centroidMin = first.Centroid;
    math::Vec3 centroidMax = first.Centroid;
    for (uint32_t offset = 1u; offset < triangleCount; ++offset)
    {
        const Triangle& triangle = m_Triangles[m_TriangleOrder[firstTriangle + offset]];
        boundsMin = MinComponents(boundsMin, triangle.BoundsMin);
        boundsMax = MaxComponents(boundsMax, triangle.BoundsMax);
        centroidMin = MinComponents(centroidMin, triangle.Centroid);
        centroidMax = MaxComponents(centroidMax, triangle.Centroid);
    }

    const math::Vec3 centroidExtent = centroidMax - centroidMin;
    const int splitAxis = LongestAxis(centroidExtent);
    if (triangleCount <= m_MaxTrianglesPerLeaf || centroidExtent[splitAxis] <= 1.0e-8f)
    {
        Node& leaf = m_Nodes[nodeIndex];
        leaf.BoundsMin = boundsMin;
        leaf.BoundsMax = boundsMax;
        leaf.FirstTriangle = firstTriangle;
        leaf.TriangleCount = triangleCount;
        return nodeIndex;
    }

    const uint32_t leftCount = triangleCount / 2u;
    const uint32_t middle = firstTriangle + leftCount;
    std::nth_element(
        m_TriangleOrder.begin() + firstTriangle,
        m_TriangleOrder.begin() + middle,
        m_TriangleOrder.begin() + firstTriangle + triangleCount,
        [&](uint32_t triangleA, uint32_t triangleB)
        {
            return m_Triangles[triangleA].Centroid[splitAxis] < m_Triangles[triangleB].Centroid[splitAxis];
        });

    const uint32_t leftChild = BuildNode(firstTriangle, leftCount);
    const uint32_t rightChild = BuildNode(middle, triangleCount - leftCount);
    Node& branch = m_Nodes[nodeIndex];
    branch.BoundsMin = boundsMin;
    branch.BoundsMax = boundsMax;
    branch.LeftChild = leftChild;
    branch.RightChild = rightChild;
    return nodeIndex;
}

void StaticMeshTriangleBVH::QueryAABB(
    const math::Vec3& queryMin,
    const math::Vec3& queryMax,
    std::vector<uint32_t>& outTriangleIndices,
    QueryStatistics* statistics) const
{
    if (statistics != nullptr)
    {
        statistics->Clear();
    }
    if (m_Nodes.empty())
    {
        return;
    }

    const math::Vec3 normalizedMin = MinComponents(queryMin, queryMax);
    const math::Vec3 normalizedMax = MaxComponents(queryMin, queryMax);
    std::vector<uint32_t> stack;
    stack.reserve(32u);
    stack.push_back(0u);

    while (stack.empty() == false)
    {
        const uint32_t nodeIndex = stack.back();
        stack.pop_back();
        if (statistics != nullptr)
        {
            ++statistics->VisitedNodeCount;
        }

        const Node& node = m_Nodes[nodeIndex];
        if (BoundsOverlap(node.BoundsMin, node.BoundsMax, normalizedMin, normalizedMax) == false)
        {
            if (statistics != nullptr)
            {
                ++statistics->RejectedNodeCount;
            }
            continue;
        }

        if (node.IsLeaf())
        {
            if (statistics != nullptr)
            {
                ++statistics->VisitedLeafCount;
            }
            for (uint32_t offset = 0u; offset < node.TriangleCount; ++offset)
            {
                const uint32_t triangleIndex = m_TriangleOrder[node.FirstTriangle + offset];
                const Triangle& triangle = m_Triangles[triangleIndex];
                if (statistics != nullptr)
                {
                    ++statistics->TestedTriangleBoundsCount;
                }
                if (BoundsOverlap(triangle.BoundsMin, triangle.BoundsMax, normalizedMin, normalizedMax))
                {
                    outTriangleIndices.push_back(triangleIndex);
                }
                else if (statistics != nullptr)
                {
                    ++statistics->RejectedTriangleBoundsCount;
                }
            }
            continue;
        }

        if (node.LeftChild != InvalidNode)
        {
            stack.push_back(node.LeftChild);
        }
        if (node.RightChild != InvalidNode)
        {
            stack.push_back(node.RightChild);
        }
    }
}

void StaticMeshTriangleBVH::QueryRay(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    std::vector<uint32_t>& outTriangleIndices,
    QueryStatistics* statistics) const
{
    if (statistics != nullptr)
    {
        statistics->Clear();
    }
    if (m_Nodes.empty() || maxFraction < 0.0f || direction.LengthSq() <= 1.0e-12f)
    {
        return;
    }

    std::vector<uint32_t> stack;
    stack.reserve(32u);
    stack.push_back(0u);
    while (stack.empty() == false)
    {
        const uint32_t nodeIndex = stack.back();
        stack.pop_back();
        if (statistics != nullptr)
        {
            ++statistics->VisitedNodeCount;
        }

        const Node& node = m_Nodes[nodeIndex];
        if (RayIntersectsBounds(origin, direction, maxFraction, node.BoundsMin, node.BoundsMax) == false)
        {
            if (statistics != nullptr)
            {
                ++statistics->RejectedNodeCount;
            }
            continue;
        }

        if (node.IsLeaf())
        {
            if (statistics != nullptr)
            {
                ++statistics->VisitedLeafCount;
            }
            for (uint32_t offset = 0u; offset < node.TriangleCount; ++offset)
            {
                const uint32_t triangleIndex = m_TriangleOrder[node.FirstTriangle + offset];
                const Triangle& triangle = m_Triangles[triangleIndex];
                if (statistics != nullptr)
                {
                    ++statistics->TestedTriangleBoundsCount;
                }
                if (RayIntersectsBounds(origin, direction, maxFraction, triangle.BoundsMin, triangle.BoundsMax))
                {
                    outTriangleIndices.push_back(triangleIndex);
                }
                else if (statistics != nullptr)
                {
                    ++statistics->RejectedTriangleBoundsCount;
                }
            }
            continue;
        }

        if (node.LeftChild != InvalidNode)
        {
            stack.push_back(node.LeftChild);
        }
        if (node.RightChild != InvalidNode)
        {
            stack.push_back(node.RightChild);
        }
    }
}

} // namespace Raven::ph
