#pragma once

#include <cstdint>

namespace Raven::ph
{

enum class AstroGravitySolverKind
{
    Direct,
    BarnesHut,
    Custom
};

struct AstroStatistics
{
    AstroGravitySolverKind SolverKind = AstroGravitySolverKind::Direct;
    std::uint64_t ActiveBodyCount = 0u;
    std::uint64_t GravityPairCandidateCount = 0u;
    std::uint64_t GravityForceEvaluationCount = 0u;
    std::uint64_t GravityVisitedNodeCount = 0u;
    std::uint64_t GravityAcceptedAggregateNodeCount = 0u;
    bool GravitySolveExecuted = false;
    bool CachedGravityForceUsed = false;
    bool FarGravitySolveExecuted = false;
    bool CachedFarGravityForceUsed = false;
    std::uint64_t NearGravityPairEvaluationCount = 0u;

    double StateCollectionTimeMs = 0.0;
    double GravityTreeBuildTimeMs = 0.0;
    double GravitySolveTimeMs = 0.0;
    double ForceFeedbackTimeMs = 0.0;

    void Clear()
    {
        SolverKind = AstroGravitySolverKind::Direct;
        ActiveBodyCount = 0u;
        GravityPairCandidateCount = 0u;
        GravityForceEvaluationCount = 0u;
        GravityVisitedNodeCount = 0u;
        GravityAcceptedAggregateNodeCount = 0u;
        GravitySolveExecuted = false;
        CachedGravityForceUsed = false;
        FarGravitySolveExecuted = false;
        CachedFarGravityForceUsed = false;
        NearGravityPairEvaluationCount = 0u;
        StateCollectionTimeMs = 0.0;
        GravityTreeBuildTimeMs = 0.0;
        GravitySolveTimeMs = 0.0;
        ForceFeedbackTimeMs = 0.0;
    }
};

} // namespace Raven::ph
