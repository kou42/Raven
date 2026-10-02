#include "Raven/Physics/Spatial/LongRangeOctree.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
namespace
{
constexpr std::uint32_t MaxOctreeDepth = 64u;
constexpr double MinimumNodeHalfSize = 1.0e-12;

bool IsFinitePosition(const std::array<double, 3>& position)
{
    return std::isfinite(position[0]) == true
        && std::isfinite(position[1]) == true
        && std::isfinite(position[2]) == true;
}
}

bool LongRangeOctreeNode::IsLeaf() const
{
    for (const std::int32_t child : Children)
    {
        if (child >= 0)
        {
            return false;
        }
    }
    return true;
}

void LongRangeOctree::Build(const std::vector<LongRangeSpatialPoint>& points)
{
    Clear();

    bool hasPoint = false;
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
    for (const LongRangeSpatialPoint& point : points)
    {
        if (IsFinitePosition(point.Position) == false)
        {
            continue;
        }

        if (hasPoint == false)
        {
            minimum = point.Position;
            maximum = point.Position;
            hasPoint = true;
            continue;
        }

        for (std::size_t axis = 0u; axis < 3u; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], point.Position[axis]);
            maximum[axis] = std::max(maximum[axis], point.Position[axis]);
        }
    }

    if (hasPoint == false)
    {
        return;
    }

    std::array<double, 3> center{};
    double extent = 0.0;
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        center[axis] = (minimum[axis] + maximum[axis]) * 0.5;
        extent = std::max(extent, maximum[axis] - minimum[axis]);
    }

    // 全点が同位置でも有限volumeを持たせ、重複点はdepth上限で同一leafへ保持します。
    const double halfSize = std::max(extent * 0.5, MinimumNodeHalfSize);
    m_RootIndex = CreateNode(center, halfSize * 1.000001);

    for (std::size_t i = 0u; i < points.size(); ++i)
    {
        if (IsFinitePosition(points[i].Position) == true)
        {
            InsertPoint(m_RootIndex, static_cast<std::int32_t>(i), points, 0u);
        }
    }
}

void LongRangeOctree::Clear()
{
    m_Nodes.clear();
    m_RootIndex = -1;
}

std::int32_t LongRangeOctree::CreateNode(
    const std::array<double, 3>& center,
    double halfSize)
{
    LongRangeOctreeNode node{};
    node.Center = center;
    node.HalfSize = halfSize;
    m_Nodes.push_back(node);
    return static_cast<std::int32_t>(m_Nodes.size() - 1u);
}

void LongRangeOctree::Subdivide(std::int32_t nodeIndex)
{
    const LongRangeOctreeNode parent = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    const double childHalfSize = parent.HalfSize * 0.5;

    for (std::int32_t childIndex = 0; childIndex < 8; ++childIndex)
    {
        std::array<double, 3> center = parent.Center;
        center[0] += (childIndex & 1) != 0 ? childHalfSize : -childHalfSize;
        center[1] += (childIndex & 2) != 0 ? childHalfSize : -childHalfSize;
        center[2] += (childIndex & 4) != 0 ? childHalfSize : -childHalfSize;

        m_Nodes[static_cast<std::size_t>(nodeIndex)].Children[static_cast<std::size_t>(childIndex)] =
            CreateNode(center, childHalfSize);
    }
}

std::int32_t LongRangeOctree::SelectChild(
    const LongRangeOctreeNode& node,
    const std::array<double, 3>& position) const
{
    std::int32_t index = 0;
    if (position[0] >= node.Center[0])
    {
        index |= 1;
    }
    if (position[1] >= node.Center[1])
    {
        index |= 2;
    }
    if (position[2] >= node.Center[2])
    {
        index |= 4;
    }
    return node.Children[static_cast<std::size_t>(index)];
}

void LongRangeOctree::InsertPoint(
    std::int32_t nodeIndex,
    std::int32_t pointIndex,
    const std::vector<LongRangeSpatialPoint>& points,
    std::uint32_t depth)
{
    LongRangeOctreeNode& node = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.IsLeaf() == true && node.PointIndices.empty() == true)
    {
        node.PointIndices.push_back(pointIndex);
        return;
    }

    if (depth >= MaxOctreeDepth || node.HalfSize <= MinimumNodeHalfSize)
    {
        node.PointIndices.push_back(pointIndex);
        return;
    }

    if (node.IsLeaf() == true)
    {
        const std::vector<std::int32_t> previousPointIndices = node.PointIndices;
        node.PointIndices.clear();
        Subdivide(nodeIndex);

        for (const std::int32_t previousPointIndex : previousPointIndices)
        {
            const std::int32_t previousChild =
                SelectChild(
                    m_Nodes[static_cast<std::size_t>(nodeIndex)],
                    points[static_cast<std::size_t>(previousPointIndex)].Position);
            InsertPoint(previousChild, previousPointIndex, points, depth + 1u);
        }
    }

    const std::int32_t child =
        SelectChild(
            m_Nodes[static_cast<std::size_t>(nodeIndex)],
            points[static_cast<std::size_t>(pointIndex)].Position);
    InsertPoint(child, pointIndex, points, depth + 1u);
}

} // namespace Raven::ph
