#pragma once

#include <cstdint>

namespace Raven::ph
{

enum class CoulombSolverKind
{
    Direct,
    BarnesHut
};

struct ElectromagneticStatistics
{
    CoulombSolverKind CoulombSolver = CoulombSolverKind::Direct;
    std::uint64_t ActiveChargedBodyCount = 0u;
    std::uint64_t CoulombPairCandidateCount = 0u;
    std::uint64_t CoulombForceEvaluationCount = 0u;
    std::uint64_t CoulombVisitedNodeCount = 0u;
    std::uint64_t CoulombAcceptedAggregateNodeCount = 0u;

    double CoulombStateCollectionTimeMs = 0.0;
    double CoulombSolveTimeMs = 0.0;
    double CoulombForceFeedbackTimeMs = 0.0;

    void Clear()
    {
        CoulombSolver = CoulombSolverKind::Direct;
        ActiveChargedBodyCount = 0u;
        CoulombPairCandidateCount = 0u;
        CoulombForceEvaluationCount = 0u;
        CoulombVisitedNodeCount = 0u;
        CoulombAcceptedAggregateNodeCount = 0u;
        CoulombStateCollectionTimeMs = 0.0;
        CoulombSolveTimeMs = 0.0;
        CoulombForceFeedbackTimeMs = 0.0;
    }
};

} // namespace Raven::ph
