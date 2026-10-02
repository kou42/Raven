#include "Raven/Physics/Tests/CoulombBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "Raven/Physics/Electromagnetism/BarnesHutCoulombSolver.h"

namespace Raven::ph::tests
{
namespace
{
constexpr std::uint32_t kWarmupCount = 1u;
constexpr std::uint32_t kMeasurementCount = 3u;

struct SolverMeasurement
{
    double SolveTimeMs = 0.0;
    double TreeBuildTimeMs = 0.0;
    std::vector<math::Vec3> Forces;
    CoulombBarnesHutStatistics Statistics{};
};

std::vector<CoulombOctreeBody> CreateBodies(std::size_t bodyCount)
{
    std::vector<CoulombOctreeBody> bodies(bodyCount);
    for (std::size_t i = 0u; i < bodyCount; ++i)
    {
        const double index = static_cast<double>(i);
        bodies[i].Position = {
            std::sin(index * 0.731) * 500.0 + static_cast<double>(i % 13u) * 0.01,
            std::cos(index * 0.417) * 500.0 + static_cast<double>(i % 19u) * 0.01,
            std::sin(index * 0.193) * std::cos(index * 0.271) * 500.0
        };
        // 正負が混在する分布で極性別aggregateの実コストと誤差を測ります。
        const double magnitude = 1.0e-6 * (1.0 + static_cast<double>(i % 7u) * 0.05);
        bodies[i].ChargeCoulombs = (i % 2u == 0u) ? magnitude : -magnitude * 0.8;
    }
    return bodies;
}

double Median(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    return samples.empty() == true ? 0.0 : samples[samples.size() / 2u];
}

void ComputeDirect(
    const std::vector<CoulombOctreeBody>& bodies,
    const CoulombForceSettings& settings,
    std::vector<math::Vec3>& outForces)
{
    outForces.assign(bodies.size(), math::Vec3{});
    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        const math::Vec3 a{
            static_cast<float>(bodies[i].Position[0]),
            static_cast<float>(bodies[i].Position[1]),
            static_cast<float>(bodies[i].Position[2])
        };
        for (std::size_t j = i + 1u; j < bodies.size(); ++j)
        {
            const math::Vec3 b{
                static_cast<float>(bodies[j].Position[0]),
                static_cast<float>(bodies[j].Position[1]),
                static_cast<float>(bodies[j].Position[2])
            };
            const math::Vec3 forceOnB = ComputeCoulombForce(
                a, bodies[i].ChargeCoulombs,
                b, bodies[j].ChargeCoulombs,
                settings);
            outForces[i] -= forceOnB;
            outForces[j] += forceOnB;
        }
    }
}

SolverMeasurement MeasureDirect(
    const std::vector<CoulombOctreeBody>& bodies,
    const CoulombForceSettings& settings)
{
    using Clock = std::chrono::steady_clock;
    std::vector<math::Vec3> forces;
    // RuntimeのSolve計時では出力配列を事前確保しているため、warm-up後の容量を再利用し、
    // Directだけに毎sampleの動的確保コストが混ざることを防ぎます。
    for (std::uint32_t i = 0u; i < kWarmupCount; ++i)
    {
        ComputeDirect(bodies, settings, forces);
    }

    std::vector<double> solveTimes;
    for (std::uint32_t i = 0u; i < kMeasurementCount; ++i)
    {
        const auto begin = Clock::now();
        ComputeDirect(bodies, settings, forces);
        const auto end = Clock::now();
        solveTimes.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }

    SolverMeasurement result{};
    result.SolveTimeMs = Median(std::move(solveTimes));
    result.Forces = std::move(forces);
    return result;
}

SolverMeasurement MeasureBarnesHut(
    const std::vector<CoulombOctreeBody>& bodies,
    const CoulombForceSettings& settings,
    double theta)
{
    using Clock = std::chrono::steady_clock;
    BarnesHutCoulombSolver solver;
    solver.SetTheta(theta);
    std::vector<math::Vec3> forces;
    for (std::uint32_t i = 0u; i < kWarmupCount; ++i)
    {
        solver.ComputeForces(bodies, settings, forces, nullptr);
    }

    std::vector<double> solveTimes;
    std::vector<double> treeTimes;
    CoulombBarnesHutStatistics statistics{};
    for (std::uint32_t i = 0u; i < kMeasurementCount; ++i)
    {
        const auto begin = Clock::now();
        solver.ComputeForces(bodies, settings, forces, &statistics);
        const auto end = Clock::now();
        solveTimes.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        treeTimes.push_back(statistics.TreeBuildTimeMs);
    }

    SolverMeasurement result{};
    result.SolveTimeMs = Median(std::move(solveTimes));
    result.TreeBuildTimeMs = Median(std::move(treeTimes));
    result.Forces = std::move(forces);
    result.Statistics = statistics;
    return result;
}
}

int RunCoulombBenchmark()
{
    CoulombForceSettings settings{};
    std::cout << "[Coulomb Benchmark] warmup=" << kWarmupCount
              << " samples=" << kMeasurementCount << " timing=median\n";

    for (const std::size_t bodyCount : {
        100u, 500u, 750u, 1000u, 1250u, 1500u, 1750u, 2000u,
        2500u, 3000u, 5000u, 7500u, 10000u, 12500u, 13500u, 14000u, 15000u })
    {
        const std::vector<CoulombOctreeBody> bodies = CreateBodies(bodyCount);
        const SolverMeasurement direct = MeasureDirect(bodies, settings);

        for (const double theta : { 0.25, 0.5, 0.75 })
        {
            const SolverMeasurement bh = MeasureBarnesHut(bodies, settings, theta);
            double maxError = 0.0;
            double errorSum = 0.0;
            std::uint64_t errorCount = 0u;
            for (std::size_t i = 0u; i < bodies.size(); ++i)
            {
                const double reference = static_cast<double>(direct.Forces[i].Length());
                if (reference <= 1.0e-12)
                {
                    continue;
                }
                const double error =
                    static_cast<double>((bh.Forces[i] - direct.Forces[i]).Length()) / reference;
                maxError = std::max(maxError, error);
                errorSum += error;
                ++errorCount;
            }

            std::cout << std::fixed << std::setprecision(4)
                << "[Coulomb Benchmark] bodies=" << bodyCount
                << " theta=" << theta
                << " direct_ms=" << direct.SolveTimeMs
                << " barnes_hut_ms=" << bh.SolveTimeMs
                << " tree_ms=" << bh.TreeBuildTimeMs
                << " bh_eval=" << bh.Statistics.ForceEvaluationCount
                << " node_visit=" << bh.Statistics.VisitedNodeCount
                << " aggregate=" << bh.Statistics.AcceptedAggregateNodeCount
                << " max_rel_error=" << maxError
                << " avg_rel_error="
                << (errorCount > 0u ? errorSum / static_cast<double>(errorCount) : 0.0)
                << '\n';
        }
    }
    return 0;
}

} // namespace Raven::ph::tests
