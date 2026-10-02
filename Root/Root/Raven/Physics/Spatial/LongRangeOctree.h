#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Raven::ph
{

// Gravity / Coulombのような長距離相互作用で共有するOctree topology用の点です。
// Domain固有のMass / Chargeは保持せず、PayloadIndexだけを呼び出し側へ返します。
struct LongRangeSpatialPoint
{
    std::array<double, 3> Position{};
    std::int32_t PayloadIndex = -1;
};

struct LongRangeOctreeNode
{
    std::array<double, 3> Center{};
    double HalfSize = 0.0;
    std::array<std::int32_t, 8> Children{ -1, -1, -1, -1, -1, -1, -1, -1 };
    std::vector<std::int32_t> PointIndices;

    bool IsLeaf() const;
};

class LongRangeOctree
{
public:
    void Build(const std::vector<LongRangeSpatialPoint>& points);
    void Clear();

    const std::vector<LongRangeOctreeNode>& GetNodes() const { return m_Nodes; }
    std::int32_t GetRootIndex() const { return m_RootIndex; }

private:
    std::int32_t CreateNode(const std::array<double, 3>& center, double halfSize);
    void InsertPoint(
        std::int32_t nodeIndex,
        std::int32_t pointIndex,
        const std::vector<LongRangeSpatialPoint>& points,
        std::uint32_t depth);
    void Subdivide(std::int32_t nodeIndex);
    std::int32_t SelectChild(
        const LongRangeOctreeNode& node,
        const std::array<double, 3>& position) const;

private:
    std::vector<LongRangeOctreeNode> m_Nodes;
    std::int32_t m_RootIndex = -1;
};

} // namespace Raven::ph
