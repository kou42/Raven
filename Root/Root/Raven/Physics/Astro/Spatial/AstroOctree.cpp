#include "Raven/Physics/Astro/Spatial/AstroOctree.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven::ph
{
namespace
{
constexpr std::uint32_t MaxOctreeDepth = 64u;
constexpr double MinimumNodeHalfSize = 1.0e-12;

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

    bool hasBody = false;
    AstroVector3 minimum{};
    AstroVector3 maximum{};
    for (const AstroBodyState& body : bodies)
    {
        if (IsValidSourceBody(body) == false)
        {
            continue;
        }

        if (hasBody == false)
        {
            minimum = body.Position;
            maximum = body.Position;
            hasBody = true;
            continue;
        }

        minimum.x = std::min(minimum.x, body.Position.x);
        minimum.y = std::min(minimum.y, body.Position.y);
        minimum.z = std::min(minimum.z, body.Position.z);
        maximum.x = std::max(maximum.x, body.Position.x);
        maximum.y = std::max(maximum.y, body.Position.y);
        maximum.z = std::max(maximum.z, body.Position.z);
    }

    if (hasBody == false)
    {
        return;
    }

    const AstroVector3 center = (minimum + maximum) * 0.5;
    const double extent = std::max({
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z
    });
    // 全点が同一点でもroot volumeを持たせます。重複点はdepth上限で打ち切ります。
    const double halfSize = std::max(extent * 0.5, MinimumNodeHalfSize);
    m_RootIndex = CreateNode(center, halfSize * 1.000001);

    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        if (IsValidSourceBody(bodies[i]) == true)
        {
            InsertBody(m_RootIndex, static_cast<std::int32_t>(i), bodies, 0u);
        }
    }

    AccumulateMass(m_RootIndex, bodies);
}

void AstroOctree::Clear()
{
    m_Nodes.clear();
    m_RootIndex = -1;
}

std::int32_t AstroOctree::CreateNode(const AstroVector3& center, double halfSize)
{
    AstroOctreeNode node{};
    node.Center = center;
    node.HalfSize = halfSize;
    m_Nodes.push_back(node);
    return static_cast<std::int32_t>(m_Nodes.size() - 1u);
}

void AstroOctree::Subdivide(std::int32_t nodeIndex)
{
    const AstroOctreeNode parent = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    const double childHalfSize = parent.HalfSize * 0.5;

    for (std::int32_t childIndex = 0; childIndex < 8; ++childIndex)
    {
        const AstroVector3 offset{
            (childIndex & 1) != 0 ? childHalfSize : -childHalfSize,
            (childIndex & 2) != 0 ? childHalfSize : -childHalfSize,
            (childIndex & 4) != 0 ? childHalfSize : -childHalfSize
        };
        m_Nodes[static_cast<std::size_t>(nodeIndex)].Children[static_cast<std::size_t>(childIndex)] =
            CreateNode(parent.Center + offset, childHalfSize);
    }
}

std::int32_t AstroOctree::SelectChild(
    const AstroOctreeNode& node,
    const AstroVector3& position) const
{
    std::int32_t index = 0;
    if (position.x >= node.Center.x) { index |= 1; }
    if (position.y >= node.Center.y) { index |= 2; }
    if (position.z >= node.Center.z) { index |= 4; }
    return node.Children[static_cast<std::size_t>(index)];
}

void AstroOctree::InsertBody(
    std::int32_t nodeIndex,
    std::int32_t bodyIndex,
    const std::vector<AstroBodyState>& bodies,
    std::uint32_t depth)
{
    AstroOctreeNode& node = m_Nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.IsLeaf() == true && node.BodyIndex < 0)
    {
        node.BodyIndex = bodyIndex;
        return;
    }

    if (depth >= MaxOctreeDepth || node.HalfSize <= MinimumNodeHalfSize)
    {
        // 完全重複位置は1 leafへ代表Bodyを保持します。aggregate massは後段で全Bodyを
        // 再集約するため、無限再帰を避けることを優先します。
        return;
    }

    if (node.IsLeaf() == true)
    {
        const std::int32_t previousBodyIndex = node.BodyIndex;
        node.BodyIndex = -1;
        Subdivide(nodeIndex);

        const std::int32_t previousChild =
            SelectChild(m_Nodes[static_cast<std::size_t>(nodeIndex)],
                bodies[static_cast<std::size_t>(previousBodyIndex)].Position);
        InsertBody(previousChild, previousBodyIndex, bodies, depth + 1u);
    }

    const std::int32_t child =
        SelectChild(m_Nodes[static_cast<std::size_t>(nodeIndex)],
            bodies[static_cast<std::size_t>(bodyIndex)].Position);
    InsertBody(child, bodyIndex, bodies, depth + 1u);
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
        if (node.BodyIndex >= 0)
        {
            const AstroBodyState& body = bodies[static_cast<std::size_t>(node.BodyIndex)];
            node.TotalMass = body.Mass;
            node.CenterOfMass = body.Position;
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
