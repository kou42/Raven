#include "Raven/Physics/Astro/Spatial/AstroOctree.h"

#include <cmath>

#include "Raven/Physics/Spatial/LongRangeOctree.h"

namespace Raven::ph
{
namespace
{
bool IsValidSourceBody(const AstroBodyState& body)
{
    return body.GenerateGravity == true
        && std::isfinite(body.Mass) == true
        && body.Mass > 0.0
        && std::isfinite(body.Position.x) == true
        && std::isfinite(body.Position.y) == true
        && std::isfinite(body.Position.z) == true;
}
}

bool AstroOctreeNode::IsLeaf() const
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

void AstroOctree::Build(const std::vector<AstroBodyState>& bodies)
{
    Clear();

    std::vector<LongRangeSpatialPoint> spatialPoints;
    spatialPoints.reserve(bodies.size());
    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        const AstroBodyState& body = bodies[i];
        if (IsValidSourceBody(body) == false)
        {
            continue;
        }

        LongRangeSpatialPoint point{};
        point.Position = { body.Position.x, body.Position.y, body.Position.z };
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

    // Octreeの分割規則はGravity/Coulomb共通層へ委譲し、Astro側では質量集約だけを保持します。
    // これによりCoulombをBarnes-Hut化しても、Domain固有のMass/Chargeを同じNode型へ混在させません。
    const std::vector<LongRangeOctreeNode>& topologyNodes = topology.GetNodes();
    m_Nodes.resize(topologyNodes.size());
    for (std::size_t nodeIndex = 0u; nodeIndex < topologyNodes.size(); ++nodeIndex)
    {
        const LongRangeOctreeNode& topologyNode = topologyNodes[nodeIndex];
        AstroOctreeNode& astroNode = m_Nodes[nodeIndex];
        astroNode.Center = {
            topologyNode.Center[0],
            topologyNode.Center[1],
            topologyNode.Center[2]
        };
        astroNode.HalfSize = topologyNode.HalfSize;
        astroNode.Children = topologyNode.Children;
        astroNode.BodyIndices.reserve(topologyNode.PointIndices.size());

        for (const std::int32_t pointIndex : topologyNode.PointIndices)
        {
            astroNode.BodyIndices.push_back(
                spatialPoints[static_cast<std::size_t>(pointIndex)].PayloadIndex);
        }
    }

    AccumulateMass(m_RootIndex, bodies);
}

void AstroOctree::Clear()
{
    m_Nodes.clear();
    m_RootIndex = -1;
}

void AstroOctree::AccumulateMass(
    std::int32_t nodeIndex,
    const std::vector<AstroBodyState>& bodies)
{
    AstroOctreeNode& node = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    node.TotalMass = 0.0;
    node.CenterOfMass = AstroVector3{};

    if (node.IsLeaf() == true)
    {
        AstroVector3 weightedCenter{};
        for (const std::int32_t bodyIndex : node.BodyIndices)
        {
            const AstroBodyState& body = bodies[static_cast<std::size_t>(bodyIndex)];
            node.TotalMass += body.Mass;
            weightedCenter += body.Position * body.Mass;
        }
        if (node.TotalMass > 0.0)
        {
            node.CenterOfMass = weightedCenter / node.TotalMass;
        }
        return;
    }

    AstroVector3 weightedCenter{};
    for (const std::int32_t childIndex : node.Children)
    {
        if (childIndex < 0)
        {
            continue;
        }

        AccumulateMass(childIndex, bodies);
        const AstroOctreeNode& child = m_Nodes[static_cast<std::size_t>(childIndex)];
        node.TotalMass += child.TotalMass;
        weightedCenter += child.CenterOfMass * child.TotalMass;
    }

    if (node.TotalMass > 0.0)
    {
        node.CenterOfMass = weightedCenter / node.TotalMass;
    }
}

} // namespace Raven::ph
