#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/Spatial/CoulombOctree.h"

namespace Raven::ph
{

struct CoulombBarnesHutStatistics
{
    std::uint64_t PairCandidateCount = 0u;
    std::uint64_t ForceEvaluationCount = 0u;
    std::uint64_t VisitedNodeCount = 0u;
    std::uint64_t AcceptedAggregateNodeCount = 0u;
};

class BarnesHutCoulombSolver
{
public:
    void SetTheta(double theta) { m_Theta = theta; }
    double GetTheta() const { return m_Theta; }

    void ComputeForces(
        const std::vector<CoulombOctreeBody>& bodies,
        const CoulombForceSettings& settings,
        std::vector<math::Vec3>& outForces,
        CoulombBarnesHutStatistics* statistics = nullptr) const;

private:
    double m_Theta = 0.5;
};

} // namespace Raven::ph
