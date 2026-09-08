#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "Raven/Math/MathVector.h"

namespace Raven
{
class MeshGeometry;
}

namespace Raven::ph
{

// ============================================================================
// StaticMeshTriangleBVH
// ============================================================================
// StaticMesh ColliderのTriangle候補数を削減するためのローカル空間BVHです。
// GeometryのPositionをそのまま使って構築し、Entity Transform / Collider Offsetは
// Query側でローカル空間へ変換して扱います。
class StaticMeshTriangleBVH
{
public:
    static constexpr uint32_t InvalidNode = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t DefaultMaxTrianglesPerLeaf = 4u;

    struct Triangle
    {
        uint32_t IndexA = 0u;
        uint32_t IndexB = 0u;
        uint32_t IndexC = 0u;
        math::Vec3 BoundsMin{};
        math::Vec3 BoundsMax{};
        math::Vec3 Centroid{};
    };

    struct Node
    {
        math::Vec3 BoundsMin{};
        math::Vec3 BoundsMax{};
        uint32_t LeftChild = InvalidNode;
        uint32_t RightChild = InvalidNode;
        uint32_t FirstTriangle = 0u;
        uint32_t TriangleCount = 0u;

        bool IsLeaf() const
        {
            return TriangleCount > 0u;
        }
    };

    // Queryの枝刈り効率を計測するための統計です。
    // CandidateTriangleCountだけではTree内部でどれだけNodeを辿ったか分からないため、
    // SAH等の分割戦略を比較する前段としてTraversalそのものを可視化します。
    struct QueryStatistics
    {
        uint64_t VisitedNodeCount = 0u;
        uint64_t RejectedNodeCount = 0u;
        uint64_t VisitedLeafCount = 0u;
        uint64_t TestedTriangleBoundsCount = 0u;
        uint64_t RejectedTriangleBoundsCount = 0u;

        void Clear()
        {
            VisitedNodeCount = 0u;
            RejectedNodeCount = 0u;
            VisitedLeafCount = 0u;
            TestedTriangleBoundsCount = 0u;
            RejectedTriangleBoundsCount = 0u;
        }
    };

    bool Build(
        const MeshGeometry& geometry,
        uint32_t maxTrianglesPerLeaf = DefaultMaxTrianglesPerLeaf);
    void Clear();

    bool IsEmpty() const { return m_Nodes.empty(); }
    std::size_t GetNodeCount() const { return m_Nodes.size(); }
    std::size_t GetTriangleCount() const { return m_Triangles.size(); }
    uint32_t GetMaxTrianglesPerLeaf() const { return m_MaxTrianglesPerLeaf; }

    const Node* GetRoot() const
    {
        return m_Nodes.empty() ? nullptr : &m_Nodes.front();
    }

    const Triangle* GetTriangle(uint32_t triangleIndex) const
    {
        if (triangleIndex >= m_Triangles.size())
        {
            return nullptr;
        }
        return &m_Triangles[triangleIndex];
    }

    // Static Geometry単位でBVHを共有します。Dynamic GeometryはCache対象外です。
    static std::shared_ptr<const StaticMeshTriangleBVH> GetOrBuildCached(
        const std::shared_ptr<const MeshGeometry>& geometry);

    // statisticsは任意です。指定した場合はQuery開始時にClearして今回分だけを返します。
    void QueryAABB(
        const math::Vec3& queryMin,
        const math::Vec3& queryMax,
        std::vector<uint32_t>& outTriangleIndices,
        QueryStatistics* statistics = nullptr) const;

    void QueryRay(
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        std::vector<uint32_t>& outTriangleIndices,
        QueryStatistics* statistics = nullptr) const;

private:
    uint32_t BuildNode(uint32_t firstTriangle, uint32_t triangleCount);

    std::vector<Triangle> m_Triangles;
    std::vector<uint32_t> m_TriangleOrder;
    std::vector<Node> m_Nodes;
    uint32_t m_MaxTrianglesPerLeaf = DefaultMaxTrianglesPerLeaf;
};

} // namespace Raven::ph
