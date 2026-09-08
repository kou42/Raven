#include "Raven/Physics/Collision/StaticMeshTriangleBVH.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::ph
{
namespace
{

math::Vec3 MinComponents(const math::Vec3& a, const math::Vec3& b)
{
    return math::Vec3{
        std::min(a.x, b.x),
        std::min(a.y, b.y),
        std::min(a.z, b.z)
    };
}

math::Vec3 MaxComponents(const math::Vec3& a, const math::Vec3& b)
{
    return math::Vec3{
        std::max(a.x, b.x),
        std::max(a.y, b.y),
        std::max(a.z, b.z)
    };
}

bool BoundsOverlap(
    const math::Vec3& minA,
    const math::Vec3& maxA,
    const math::Vec3& minB,
    const math::Vec3& maxB)
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
        const float originValue = origin[axis];
        const float directionValue = direction[axis];
        const float minValue = boundsMin[axis];
        const float maxValue = boundsMax[axis];

        if (std::abs(directionValue) <= parallelEpsilon)
        {
            if (originValue < minValue || originValue > maxValue)
            {
                return false;
            }
            continue;
        }

        const float inverseDirection = 1.0f / directionValue;
        float t1 = (minValue - originValue) * inverseDirection;
        float t2 = (maxValue - originValue) * inverseDirection;
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

} // namespace

bool StaticMeshTriangleBVH::Build(
    const MeshGeometry& geometry,
    uint32_t maxTrianglesPerLeaf)
{
    Clear();
    m_MaxTrianglesPerLeaf = std::max(maxTrianglesPerLeaf, 1u);

    const auto& vertices = geometry.GetVertices();
    const auto& indices = geometry.GetIndices();
    if (vertices.size() < 3u)
    {
        return false;
    }

    const auto appendTriangle =
        [&](uint32_t indexA, uint32_t indexB, uint32_t indexC)
        {
            if (indexA >= vertices.size()
                || indexB >= vertices.size()
                || indexC >= vertices.size())
            {
                // 壊れたIndexはBVHへ入れません。Query時に毎回範囲外判定を行うより、
                // Static Geometry構築時に一度だけ除外してTreeを正常Triangleだけで保ちます。
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
            appendTriangle(
                static_cast<uint32_t>(index),
                static_cast<uint32_t>(index + 1u),
                static_cast<uint32_t>(index + 2u));
        }
    }

    if (m_Triangles.empty())
    {
        return false;
    }

    m_TriangleOrder.resize(m_Triangles.size());
    std::iota(m_TriangleOrder.begin(), m_TriangleOrder.end(), 0u);

    // Binary BVHのNode数上限は正常Triangle数Nに対して2N-1です。
    // 先にreserveしてBuildNode再帰中のvector再allocationを避けます。
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

uint32_t StaticMeshTriangleBVH::BuildNode(
    uint32_t firstTriangle,
    uint32_t triangleCount)
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

    // Leaf化条件はTriangle数だけでなく、重心が同一点へ潰れて分割不能な場合も含みます。
    // 無理に中央値分割しても空間的な枝刈り効果がなく、Tree深度だけ増えるためです。
    const math::Vec3 centroidExtent = centroidMax - centroidMin;
    const int splitAxis = LongestAxis(centroidExtent);
    const bool centroidCollapsed = centroidExtent[splitAxis] <= 1.0e-8f;
    if (triangleCount <= m_MaxTrianglesPerLeaf || centroidCollapsed == true)
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
    auto begin = m_TriangleOrder.begin() + firstTriangle;
    auto median = m_TriangleOrder.begin() + middle;
    auto end = m_TriangleOrder.begin() + firstTriangle + triangleCount;

    std::nth_element(
        begin,
        median,
        end,
        [&](uint32_t triangleA, uint32_t triangleB)
        {
            return m_Triangles[triangleA].Centroid[splitAxis]
                < m_Triangles[triangleB].Centroid[splitAxis];
        });

    const uint32_t leftChild = BuildNode(firstTriangle, leftCount);
    const uint32_t rightChild = BuildNode(middle, triangleCount - leftCount);

    // 再帰中にm_Nodesが再配置される可能性を考慮し、Node参照は子構築後に取り直します。
    Node& branch = m_Nodes[nodeIndex];
    branch.BoundsMin = boundsMin;
    branch.BoundsMax = boundsMax;
    branch.LeftChild = leftChild;
    branch.RightChild = rightChild;
    branch.FirstTriangle = 0u;
    branch.TriangleCount = 0u;
    return nodeIndex;
}

void StaticMeshTriangleBVH::QueryAABB(
    const math::Vec3& queryMin,
    const math::Vec3& queryMax,
    std::vector<uint32_t>& outTriangleIndices) const
{
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

        const Node& node = m_Nodes[nodeIndex];
        if (BoundsOverlap(
                node.BoundsMin,
                node.BoundsMax,
                normalizedMin,
                normalizedMax) == false)
        {
            continue;
        }

        if (node.IsLeaf())
        {
            for (uint32_t offset = 0u; offset < node.TriangleCount; ++offset)
            {
                const uint32_t triangleIndex = m_TriangleOrder[node.FirstTriangle + offset];
                const Triangle& triangle = m_Triangles[triangleIndex];
                if (BoundsOverlap(
                        triangle.BoundsMin,
                        triangle.BoundsMax,
                        normalizedMin,
                        normalizedMax))
                {
                    outTriangleIndices.push_back(triangleIndex);
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
    std::vector<uint32_t>& outTriangleIndices) const
{
    if (m_Nodes.empty()
        || maxFraction < 0.0f
        || direction.LengthSq() <= 1.0e-12f)
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

        const Node& node = m_Nodes[nodeIndex];
        if (RayIntersectsBounds(
                origin,
                direction,
                maxFraction,
                node.BoundsMin,
                node.BoundsMax) == false)
        {
            continue;
        }

        if (node.IsLeaf())
        {
            for (uint32_t offset = 0u; offset < node.TriangleCount; ++offset)
            {
                const uint32_t triangleIndex = m_TriangleOrder[node.FirstTriangle + offset];
                const Triangle& triangle = m_Triangles[triangleIndex];
                if (RayIntersectsBounds(
                        origin,
                        direction,
                        maxFraction,
                        triangle.BoundsMin,
                        triangle.BoundsMax))
                {
                    outTriangleIndices.push_back(triangleIndex);
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
