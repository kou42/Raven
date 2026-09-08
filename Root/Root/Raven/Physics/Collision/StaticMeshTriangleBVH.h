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
//
// GeometryのPositionをそのまま使って構築し、Entity Transform / Collider Offsetは
// Query側でローカル空間へ変換して扱う想定です。Static Geometryは生成後に不変という
// MeshGeometryの契約に合わせ、Refitではなく一度BuildしたTreeを再利用します。
//
// Nodeはvector上へ連続配置し、Leafはm_TriangleOrderの連続区間を参照します。
// PointerをNode内に持たないため、vector再配置が起きてもTree構造が壊れません。
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

    // StaticMeshGeometry単位でBVHを1回だけ構築し、同じGeometryを参照するCollider間で共有します。
    // CacheはGeometryをweak_ptrで追跡するため、Scene破棄後にGeometry寿命を不必要に延長しません。
    // Build失敗時はnullptrを返し、呼び出し側が従来の全Triangle走査へfallbackできます。
    static std::shared_ptr<const StaticMeshTriangleBVH> GetOrBuildCached(
        const std::shared_ptr<const MeshGeometry>& geometry);

    // Query AABBと重なるLeaf TriangleのIDをoutTriangleIndicesへ追記します。
    // 返却IDはGetTriangle()へ渡せるBVH内部Triangle IDです。
    void QueryAABB(
        const math::Vec3& queryMin,
        const math::Vec3& queryMax,
        std::vector<uint32_t>& outTriangleIndices) const;

    // Rayと交差するNodeだけを辿り、候補Triangle IDを追記します。
    // Triangleそのものとの交差判定は呼び出し側で行います。
    void QueryRay(
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        std::vector<uint32_t>& outTriangleIndices) const;

private:
    uint32_t BuildNode(uint32_t firstTriangle, uint32_t triangleCount);

    std::vector<Triangle> m_Triangles;
    std::vector<uint32_t> m_TriangleOrder;
    std::vector<Node> m_Nodes;
    uint32_t m_MaxTrianglesPerLeaf = DefaultMaxTrianglesPerLeaf;
};

} // namespace Raven::ph
