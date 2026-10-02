#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Astro/Gravity/GravitySolver.h"

namespace Raven::ph
{

struct AstroOctreeNode
{
    AstroVector3 Center{};
    double HalfSize = 0.0;
    double TotalMass = 0.0;
    AstroVector3 CenterOfMass{};
    std::array<std::int32_t, 8> Children{ -1, -1, -1, -1, -1, -1, -1, -1 };
    std::vector<std::int32_t> BodyIndices;

    bool IsLeaf() const;
};

class AstroOctree
{
public:
    void Build(const std::vector<AstroBodyState>& bodies);
    void Clear();

    const std::vector<AstroOctreeNode>& GetNodes() const { return m_Nodes; }
    std::int32_t GetRootIndex() const { return m_RootIndex; }

private:
    void AccumulateMass(std::int32_t nodeIndex, const std::vector<AstroBodyState>& bodies);

private:
    std::vector<AstroOctreeNode> m_Nodes;
    std::int32_t m_RootIndex = -1;
};

} // namespace Raven::ph
