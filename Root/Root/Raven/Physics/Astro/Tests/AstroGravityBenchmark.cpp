#include "Raven/Physics/Astro/Tests/AstroGravityBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "Raven/Physics/Astro/Gravity/BarnesHutGravitySolver.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"

namespace Raven::ph::tests
{
namespace
{
struct BenchmarkResult
{
    std::size_t BodyCount = 0u;
    double Theta = 0.0;
    double DirectSolveTimeMs = 0.0;
    double BarnesHutSolveTimeMs = 0.0;
    double TreeBuildTimeMs = 0.0;
    std::uint64_t DirectForceEvaluationCount = 0u;
    std::uint64_t BarnesHutForceEvaluationCount = 0u;
    std::uint64_t VisitedNodeCount = 0u;
    std::uint64_t AcceptedAggregateNodeCount = 0u;
    double MaximumRelativeForceError = 0.0;
    double AverageRelativeForceError = 0.0;
};

std::vector<AstroBodyState> CreateBenchmarkBodies(std::size_t bodyCount)
{
    std::vector<AstroBodyState> bodies(bodyCount);
    // 擬似乱数を使わず決定的な3D分布を作り、実行ごとに同じ入力でSolverを比較します。
    for (std::size_t i = 0u; i < bodyCount; ++i)
    {
        const double index = static_cast<double>(i);
        bodies[i].Mass = 1.0 + static_cast<double>(i % 17u) * 0.05;
        bodies[i].Position = {
            std::sin(index * 0.731) * 500.0 + static_cast<double>(i % 13u) * 0.01,
            std::cos(index * 0.417) * 500.0 + static_cast<double>(i % 19u) * 0.01,
            std::sin(index * 0.193) * std::cos(index * 0.271) * 500.0
        };
    }
    return bodies;
}

BenchmarkResult RunCase(
    std::size_t bodyCount,
    double theta,
    const GravitySolverSettings& settings)
{
    using Clock = std::chrono::steady_clock;

    const std::vector<AstroBodyState> bodies = CreateBenchmarkBodies(bodyCount);
    DirectGravitySolver directSolver;
    BarnesHutGravitySolver barnesHutSolver;
    barnesHutSolver.SetTheta(theta);

    std::vector<AstroVector3> directForces;
    std::vector<AstroVector3> barnesHutForces;
    AstroStatistics directStatistics{};
    AstroStatistics barnesHutStatistics{};

    const auto directBegin = Clock::now();
    directSolver.ComputeForces(bodies, settings, directForces, &directStatistics);
    const auto directEnd = Clock::now();

    const auto barnesHutBegin = Clock::now();
    barnesHutSolver.ComputeForces(
        bodies,
        settings,
        barnesHutForces,
        &barnesHutStatistics);
    const auto barnesHutEnd = Clock::now();

    double maximumRelativeError = 0.0;
    double relativeErrorSum = 0.0;
    std::uint64_t errorSampleCount = 0u;
    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        const double referenceMagnitude = directForces[i].Length();
        if (referenceMagnitude <= 1.0e-12)
        {
            continue;
        }

        const double relativeError =
            (barnesHutForces[i] - directForces[i]).Length() / referenceMagnitude;
        maximumRelativeError = std::max(maximumRelativeError, relativeError);
        relativeErrorSum += relativeError;
        ++errorSampleCount;
    }

    BenchmarkResult result{};
    result.BodyCount = bodyCount;
    result.Theta = theta;
    result.DirectSolveTimeMs =
        std::chrono::duration<double, std::milli>(directEnd - directBegin).count();
    result.BarnesHutSolveTimeMs =
        std::chrono::duration<double, std::milli>(barnesHutEnd - barnesHutBegin).count();
    result.TreeBuildTimeMs = barnesHutStatistics.GravityTreeBuildTimeMs;
    result.DirectForceEvaluationCount = directStatistics.GravityForceEvaluationCount;
    result.BarnesHutForceEvaluationCount = barnesHutStatistics.GravityForceEvaluationCount;
    result.VisitedNodeCount = barnesHutStatistics.GravityVisitedNodeCount;
    result.AcceptedAggregateNodeCount =
        barnesHutStatistics.GravityAcceptedAggregateNodeCount;
    result.MaximumRelativeForceError = maximumRelativeError;
    if (errorSampleCount > 0u)
    {
        result.AverageRelativeForceError =
            relativeErrorSum / static_cast<double>(errorSampleCount);
    }
    return result;
}

void PrintResult(const BenchmarkResult& result)
{
    std::cout
        << std::fixed << std::setprecision(4)
        << "[Astro Benchmark] bodies=" << result.BodyCount
        << " theta=" << result.Theta
        << " direct_ms=" << result.DirectSolveTimeMs
        << " barnes_hut_ms=" << result.BarnesHutSolveTimeMs
        << " tree_ms=" << result.TreeBuildTimeMs
        << " direct_eval=" << result.DirectForceEvaluationCount
        << " bh_eval=" << result.BarnesHutForceEvaluationCount
        << " node_visit=" << result.VisitedNodeCount
        << " aggregate=" << result.AcceptedAggregateNodeCount
        << " max_rel_error=" << result.MaximumRelativeForceError
        << " avg_rel_error=" << result.AverageRelativeForceError
        << '\n';
}
}

int RunAstroGravityBenchmark()
{
    GravitySolverSettings settings{};
    settings.GravitationalConstant = 1.0;
    settings.MinimumDistance = 1.0e-6;

    // 10,000 bodyのDirectは約5千万pairです。通常Self Testには含めず、
    // --benchmark-astro-gravity を明示した場合だけ実行します。
    for (const std::size_t bodyCount : { 100u, 1000u, 10000u })
    {
        for (const double theta : { 0.25, 0.5, 0.75 })
        {
            PrintResult(RunCase(bodyCount, theta, settings));
        }
    }
    return 0;
}

} // namespace Raven::ph::tests
