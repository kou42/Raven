#include "Raven/Physics/Astro/Tests/AstroGravityBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "Raven/Physics/Astro/Gravity/BarnesHutGravitySolver.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"

namespace Raven::ph::tests
{
namespace
{
constexpr std::uint32_t kWarmupCount = 1u;
constexpr std::uint32_t kMeasurementCount = 3u;

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

struct SolverMeasurement
{
    double SolveTimeMs = 0.0;
    double TreeBuildTimeMs = 0.0;
    std::vector<AstroVector3> Forces;
    AstroStatistics Statistics{};
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

double ComputeMedian(std::vector<double> samples)
{
    if (samples.empty() == true)
    {
        return 0.0;
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

SolverMeasurement MeasureSolver(
    const GravitySolver& solver,
    const std::vector<AstroBodyState>& bodies,
    const GravitySolverSettings& settings)
{
    using Clock = std::chrono::steady_clock;

    std::vector<AstroVector3> forces;
    for (std::uint32_t warmupIndex = 0u; warmupIndex < kWarmupCount; ++warmupIndex)
    {
        // 出力vector容量、Octree内部allocation、命令cacheの初回差を計測値から分離します。
        solver.ComputeForces(bodies, settings, forces, nullptr);
    }

    std::vector<double> solveTimes;
    std::vector<double> treeBuildTimes;
    solveTimes.reserve(kMeasurementCount);
    treeBuildTimes.reserve(kMeasurementCount);

    AstroStatistics statistics{};
    for (std::uint32_t sampleIndex = 0u; sampleIndex < kMeasurementCount; ++sampleIndex)
    {
        statistics.Clear();
        const auto begin = Clock::now();
        solver.ComputeForces(bodies, settings, forces, &statistics);
        const auto end = Clock::now();

        solveTimes.push_back(
            std::chrono::duration<double, std::milli>(end - begin).count());
        treeBuildTimes.push_back(statistics.GravityTreeBuildTimeMs);
    }

    SolverMeasurement measurement{};
    measurement.SolveTimeMs = ComputeMedian(std::move(solveTimes));
    measurement.TreeBuildTimeMs = ComputeMedian(std::move(treeBuildTimes));
    measurement.Forces = std::move(forces);
    measurement.Statistics = statistics;
    return measurement;
}

BenchmarkResult BuildResult(
    const std::vector<AstroBodyState>& bodies,
    double theta,
    const SolverMeasurement& directMeasurement,
    const SolverMeasurement& barnesHutMeasurement)
{
    double maximumRelativeError = 0.0;
    double relativeErrorSum = 0.0;
    std::uint64_t errorSampleCount = 0u;
    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        const double referenceMagnitude = directMeasurement.Forces[i].Length();
        if (referenceMagnitude <= 1.0e-12)
        {
            continue;
        }

        const double relativeError =
            (barnesHutMeasurement.Forces[i] - directMeasurement.Forces[i]).Length()
            / referenceMagnitude;
        maximumRelativeError = std::max(maximumRelativeError, relativeError);
        relativeErrorSum += relativeError;
        ++errorSampleCount;
    }

    BenchmarkResult result{};
    result.BodyCount = bodies.size();
    result.Theta = theta;
    result.DirectSolveTimeMs = directMeasurement.SolveTimeMs;
    result.BarnesHutSolveTimeMs = barnesHutMeasurement.SolveTimeMs;
    result.TreeBuildTimeMs = barnesHutMeasurement.TreeBuildTimeMs;
    result.DirectForceEvaluationCount =
        directMeasurement.Statistics.GravityForceEvaluationCount;
    result.BarnesHutForceEvaluationCount =
        barnesHutMeasurement.Statistics.GravityForceEvaluationCount;
    result.VisitedNodeCount = barnesHutMeasurement.Statistics.GravityVisitedNodeCount;
    result.AcceptedAggregateNodeCount =
        barnesHutMeasurement.Statistics.GravityAcceptedAggregateNodeCount;
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

    std::cout
        << "[Astro Benchmark] warmup=" << kWarmupCount
        << " samples=" << kMeasurementCount
        << " timing=median\n";

    // 10,000 bodyのDirectは約5千万pairです。通常Self Testには含めず、
    // --benchmark-astro-gravity を明示した場合だけ実行します。
    // 基本規模にcrossover探索用の中間点を加え、DirectからBarnes-Hutへ
    // 切り替えるbody数を同じ決定的配置のまま判断できるようにします。
    for (const std::size_t bodyCount : { 100u, 1000u, 2000u, 3000u, 5000u, 10000u })
    {
        const std::vector<AstroBodyState> bodies = CreateBenchmarkBodies(bodyCount);
        DirectGravitySolver directSolver;
        const SolverMeasurement directMeasurement = MeasureSolver(
            directSolver,
            bodies,
            settings);

        for (const double theta : { 0.25, 0.5, 0.75 })
        {
            BarnesHutGravitySolver barnesHutSolver;
            barnesHutSolver.SetTheta(theta);
            const SolverMeasurement barnesHutMeasurement = MeasureSolver(
                barnesHutSolver,
                bodies,
                settings);
            PrintResult(BuildResult(
                bodies,
                theta,
                directMeasurement,
                barnesHutMeasurement));
        }
    }
    return 0;
}

} // namespace Raven::ph::tests
