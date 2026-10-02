#include "Raven/Physics/Electromagnetism/Spatial/CoulombOctree.h"

#include <cmath>
#include <cstddef>

namespace Raven::ph
{
namespace
{
bool IsValidChargedBody(const CoulombOctreeBody& body)
{
    return body.ChargeCoulombs != 0.0
        && std::isfinite(body.ChargeCoulombs) == true
        && std::isfinite(body.Position[0]) == true
        && std::isfinite(body.Position[1]) == true
        && std::isfinite(body.Position[2]) == true;
}

void AddWeightedPosition(
    std::array<double, 3>& weightedPosition,
    const std::array<double, 3>& position,
    double weight)
{
    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        weightedPosition[axis] += position[axis] * weight;
    }
}

std::array<double, 3> DividePosition(
    const std::array<double, 3>& weightedPosition,
    double weight)
{
    std::array<double, 3> result{};
    if (weight <= 0.0)
    {
        return result;
    }

    for (std::size_t axis = 0u; axis < 3u; ++axis)
    {
        result[axis] = weightedPosition[axis] / weight;
    }
    return result;
}
}

bool CoulombOctreeNode::IsLeaf() const
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

void CoulombOctree::Build(const std::vector<CoulombOctreeBody>& bodies)
{
    Clear();

    std::vector<LongRangeSpatialPoint> spatialPoints;
    spatialPoints.reserve(bodies.size());
    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        if (IsValidChargedBody(bodies[i]) == false)
        {
            continue;
        }

        LongRangeSpatialPoint point{};
        point.Position = bodies[i].Position;
        point.PayloadIndex = static_cast<std::int32_t>(i);
        spatialPoints.push_back(point);
    }

    LongRangeOctree topology;
    topology.Build(spatialPoints);
    m_RootIndex = topology.GetRootIndex();
    if (m_RootIndex < 0)
    {
        return;
    }

    const std::vector<LongRangeOctreeNode>& topologyNodes = topology.GetNodes();
    m_Nodes.resize(topologyNodes.size());
    for (std::size_t nodeIndex = 0u; nodeIndex < topologyNodes.size(); ++nodeIndex)
    {
        const LongRangeOctreeNode& topologyNode = topologyNodes[nodeIndex];
        CoulombOctreeNode& coulombNode = m_Nodes[nodeIndex];
        coulombNode.Center = topologyNode.Center;
        coulombNode.HalfSize = topologyNode.HalfSize;
        coulombNode.Children = topologyNode.Children;
        coulombNode.BodyIndices.reserve(topologyNode.PointIndices.size());

        for (const std::int32_t pointIndex : topologyNode.PointIndices)
        {
            coulombNode.BodyIndices.push_back(
                spatialPoints[static_cast<std::size_t>(pointIndex)].PayloadIndex);
        }
    }

    AccumulateCharge(m_RootIndex, bodies);
}

void CoulombOctree::Clear()
{
    m_Nodes.clear();
    m_RootIndex = -1;
}

void CoulombOctree::AccumulateCharge(
    std::int32_t nodeIndex,
    const std::vector<CoulombOctreeBody>& bodies)
{
    CoulombOctreeNode& node = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    node.PositiveCharge = 0.0;
    node.PositiveCenter = {};
    node.NegativeChargeMagnitude = 0.0;
    node.NegativeCenter = {};

    std::array<double, 3> positiveWeightedCenter{};
    std::array<double, 3> negativeWeightedCenter{};

    if (node.IsLeaf() == true)
    {
        for (const std::int32_t bodyIndex : node.BodyIndices)
        {
            const CoulombOctreeBody& body = bodies[static_cast<std::size_t>(bodyIndex)];
            if (body.ChargeCoulombs > 0.0)
            {
                node.PositiveCharge += body.ChargeCoulombs;
                AddWeightedPosition(
                    positiveWeightedCenter,
                    body.Position,
                    body.ChargeCoulombs);
            }
            else
            {
                const double magnitude = -body.ChargeCoulombs;
                node.NegativeChargeMagnitude += magnitude;
                AddWeightedPosition(negativeWeightedCenter, body.Position, magnitude);
            }
        }
    }
    else
    {
        for (const std::int32_t childIndex : node.Children)
        {
            if (childIndex < 0)
            {
                continue;
            }

            AccumulateCharge(childIndex, bodies);
            const CoulombOctreeNode& child =
                m_Nodes[static_cast<std::size_t>(childIndex)];

            node.PositiveCharge += child.PositiveCharge;
            AddWeightedPosition(
                positiveWeightedCenter,
                child.PositiveCenter,
                child.PositiveCharge);

            node.NegativeChargeMagnitude += child.NegativeChargeMagnitude;
            AddWeightedPosition(
                negativeWeightedCenter,
                child.NegativeCenter,
                child.NegativeChargeMagnitude);
        }
    }

    // 正負をTotalChargeへ相殺してから中心を求めると、ほぼ中性なnodeで中心が発散します。
    // 極性ごとの絶対電荷量と中心を保持し、将来のBarnes-Hut traversalで別々に評価します。
    node.PositiveCenter = DividePosition(positiveWeightedCenter, node.PositiveCharge);
    node.NegativeCenter =
        DividePosition(negativeWeightedCenter, node.NegativeChargeMagnitude);
}

} // namespace Raven::ph
