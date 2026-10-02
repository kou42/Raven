#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Spatial/LongRangeOctree.h"

namespace Raven::ph
{

struct CoulombOctreeBody
{
    std::array<double, 3> Position{};
    double ChargeCoulombs = 0.0;
};

struct CoulombOctreeNode
{
    std::array<double, 3> Center{};
    double HalfSize = 0.0;
    std::array<std::int32_t, 8> Children{ -1, -1, -1, -1, -1, -1, -1, -1 };
    std::vector<std::int32_t> BodyIndices;

    double PositiveCharge = 0.0;
    std::array<double, 3> PositiveCenter{};
    double NegativeChargeMagnitude = 0.0;
    std::array<double, 3> NegativeCenter{};

    bool IsLeaf() const;
};

class CoulombOctree
{
public:
    void Build(const std::vector<CoulombOctreeBody>& bodies);
    void Clear();

    const std::vector<CoulombOctreeNode>& GetNodes() const { return m_Nodes; }
    std::int32_t GetRootIndex() const { return m_RootIndex; }

private:
    void AccumulateCharge(
        std::int32_t nodeIndex,
        const std::vector<CoulombOctreeBody>& bodies);

private:
    std::vector<CoulombOctreeNode> m_Nodes;
    std::int32_t m_RootIndex = -1;
};

} // namespace Raven::ph
