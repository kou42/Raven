#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Jobs/JobSystem.h"
#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"
#include "Raven/Physics/Collision/StaticSDFCollider.h"
#include "Raven/Physics/Field/DenseSDF.h"
#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"
#include "Raven/Physics/Fluid/SPHKernel.h"
#include "Raven/Physics/Fluid/SPHSettings.h"
#include "Raven/Physics/Fluid/SPHSolver.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{

int RunFluidSPHBenchmark(bool compareCache, bool compareDensity)
{
    constexpr std::size_t workerCount = 4u;
    const std::size_t warmupSteps = (compareCache || compareDensity) ? 5u : 20u;
    const std::size_t sampleSteps = (compareCache || compareDensity) ? 20u : 300u;
    constexpr float deltaTime = 1.0f / 60.0f;
    constexpr std::string_view prefix = "Physics.Fluid.SPH.";
    JobSystem jobSystem(workerCount);
    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.SetEnabled(true);
    profiler.BeginFrame();

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "max_workers=" << workerCount << ",warmup=" << warmupSteps
        << ",samples=" << sampleSteps << ",dt_s=" << deltaTime << '\n';
    struct CacheConfiguration
    {
        std::size_t m_Limit;
        bool m_ShouldReuseRows;
        bool m_ShouldUseJobs = true;
    };
    constexpr std::size_t kilo = 1024u;
    std::vector<CacheConfiguration> configurations = compareCache
        ? std::vector<CacheConfiguration>{
            {0u, false}, {256u * kilo, false}, {256u * kilo, true},
            {512u * kilo, false}, {512u * kilo, true}, {kilo * kilo, false},
            {kilo * kilo, true}, {2u * kilo * kilo, false}, {2u * kilo * kilo, true}}
        : std::vector<CacheConfiguration>{{SPHSettings{}.MaximumCachedNeighbors,
            SPHSettings{}.ReuseCompletedNeighborRowsOnOverflow}};
    if (compareDensity == true)
    {
        configurations.insert(configurations.begin(), {SPHSettings{}.MaximumCachedNeighbors,
            SPHSettings{}.ReuseCompletedNeighborRowsOnOverflow, false});
    }
    struct ParticleConfiguration
    {
        std::size_t m_Count;
        float m_Spacing;
    };
    const std::vector<ParticleConfiguration> layouts = (compareCache || compareDensity)
        ? std::vector<ParticleConfiguration>{{1024u, 0.065f}, {5120u, 0.025f}, {10240u, 0.025f}}
        : std::vector<ParticleConfiguration>{
            {1024u, 0.065f}, {1024u, 0.025f}, {5120u, 0.065f},
            {5120u, 0.025f}, {10240u, 0.065f}, {10240u, 0.025f}};
    for (const auto& [particleCount, spacing] : layouts)
    {
        for (const auto& [cacheLimit, shouldReuseRows, shouldUseJobs] : configurations)
        {
            SPHSettings settings;
            settings.MaximumCachedNeighbors = cacheLimit;
            settings.ReuseCompletedNeighborRowsOnOverflow = shouldReuseRows;
            settings.BoundaryEnabled = true;
            settings.BoundaryMaximum = {3.0f, 3.0f, 3.0f};
            SPHSolver solver(settings);
            std::vector<FluidParticle> initial(particleCount);
            const auto side = static_cast<std::size_t>(std::ceil(std::cbrt(particleCount)));
            for (std::size_t index = 0u; index < particleCount; ++index)
            {
                initial[index].Position = {
                    static_cast<float>(index % side) * spacing,
                    static_cast<float>((index / side) % side) * spacing,
                    static_cast<float>(index / (side * side)) * spacing};
                initial[index].Mass = settings.RestDensity * spacing * spacing * spacing;
            }

            std::vector<FluidParticle> particles;
            std::vector<double> stepMilliseconds;
            stepMilliseconds.reserve(sampleSteps);
            struct Metric
            {
                double m_Sum = 0.0;
                double m_Maximum = 0.0;
                std::size_t m_Count = 0u;

                void Add(double value)
                {
                    m_Sum += value;
                    m_Maximum = std::max(m_Maximum, value);
                    ++m_Count;
                }
            };
            std::map<std::string, Metric> durations;
            std::map<std::string, Metric> counters;
            for (std::size_t sample = 0u; sample < warmupSteps + sampleSteps; ++sample)
            {
                // 粒子配置を毎回復元し、密集条件が時間経過で変わるのを防ぎます。
                // Solverは再利用するため、キャッシュの確保はウォームアップに含まれます。
                particles = initial;
                solver.Step(particles, deltaTime, shouldUseJobs ? &jobSystem : nullptr);
                profiler.BeginFrame();
                if (sample < warmupSteps)
                {
                    continue;
                }
                const CPUProfileFrame& frame = profiler.GetLastFrame();
                for (const CPUProfileResult& result : frame.Results)
                {
                    if (result.Name.starts_with(prefix))
                    {
                        durations[result.Name.substr(prefix.size())].Add(result.DurationMilliseconds);
                    }
                    if (result.Name == "Physics.Fluid.SPH.Step")
                    {
                        stepMilliseconds.push_back(result.DurationMilliseconds);
                    }
                }
                for (const CPUProfileCounter& counter : frame.Counters)
                {
                    if (counter.Name.starts_with(prefix))
                    {
                        counters[counter.Name.substr(prefix.size())].Add(counter.Value);
                    }
                }
            }

            if (stepMilliseconds.size() != sampleSteps)
            {
                std::cerr << "SPH benchmark is missing Step timings.\n";
                return 1;
            }
            std::sort(stepMilliseconds.begin(), stepMilliseconds.end());
            const double median = (stepMilliseconds[sampleSteps / 2u - 1u]
                + stepMilliseconds[sampleSteps / 2u]) * 0.5;
            std::cout << "particles=" << particleCount << ",spacing=" << spacing
                << ",workers=" << (shouldUseJobs ? workerCount : 0u)
                << ",cache_limit=" << cacheLimit << ",reuse_rows=" << shouldReuseRows
                << ",step_median_ms=" << median
                << ",step_p95_ms=" << stepMilliseconds[sampleSteps * 95u / 100u - 1u]
                << ",step_max_ms=" << stepMilliseconds.back() << '\n';

            // 親区間は子区間を含むため、各項目を独立して表示します。
            std::cout << "scope,ms_per_step,ms_per_call,calls_per_step\n";
            for (const auto& [name, metric] : durations)
            {
                std::cout << name << ',' << metric.m_Sum / sampleSteps << ','
                    << metric.m_Sum / metric.m_Count << ','
                    << static_cast<double>(metric.m_Count) / sampleSteps << '\n';
            }
            std::cout << "counter,sum_per_step,mean_per_call,max\n";
            for (const auto& [name, metric] : counters)
            {
                std::cout << name << ',' << metric.m_Sum / sampleSteps << ','
                    << metric.m_Sum / metric.m_Count << ',' << metric.m_Maximum << '\n';
            }
        }
    }
    return 0;
}

void RunFluidSPHSelfTests()
{
    {
        SPHSettings settings;
        settings.SmoothingRadius = 0.125f;
        settings.StableTimeStepEnabled = false;
        SPHSolver solver(settings);
        SPHSolver reference(settings);

        const auto checkStep = [&](std::vector<FluidParticle>& particles)
        {
            constexpr float deltaTime = 1.0e-4f;
            auto expected = particles;
            // 単独APIはGridを探索するため、Substep内の近傍再利用結果と比較できます。
            reference.ComputeDensityAndPressure(expected);
            reference.ComputeForces(expected);
            reference.Integrate(expected, deltaTime);
            reference.ResolveBoundary(expected);
            solver.Step(particles, deltaTime);

            for (std::size_t index = 0; index < particles.size(); ++index)
            {
                const auto& actual = particles[index];
                const auto& expectedParticle = expected[index];
                assert(std::isfinite(actual.Density));
                assert(std::isfinite(actual.Pressure));
                assert(actual.Density == expectedParticle.Density);
                assert(actual.Pressure == expectedParticle.Pressure);
                assert((actual.Force - expectedParticle.Force).LengthSq() == 0.0f);
                assert((actual.Velocity - expectedParticle.Velocity).LengthSq() == 0.0f);
                assert((actual.Position - expectedParticle.Position).LengthSq() == 0.0f);
            }
        };

        std::vector<FluidParticle> emptyParticles;
        checkStep(emptyParticles);
        assert(solver.GetLastSubstepCount() == 0u);
        std::vector<FluidParticle> singleParticle(1);
        checkStep(singleParticle);
        assert(solver.GetLastSubstepCount() == 1u);

        std::vector<FluidParticle> particles(6);
        particles[1].Position.x = 0.0625f;
        particles[1].Velocity.y = 0.5f;
        particles[2].Position.x = -0.0625f;
        particles[2].Mass = 0.0f;
        particles[3].Position.x = 0.125f;
        particles[4].Position.x = 0.5f;
        checkStep(particles);

        // 全粒子が異なる位置で互いの半径内に入り、近傍バッファの上限を超えます。
        std::vector<FluidParticle> denseParticles(514);
        for (std::size_t index = 0; index < denseParticles.size(); ++index)
        {
            denseParticles[index].Position.x = static_cast<float>(index) * 0.000125f;
        }
        checkStep(denseParticles);

        // 上限超過の後も、移動済みの粒子から新しいSubstep用の近傍を構築します。
        particles[4].Position.x = 0.03125f;
        checkStep(particles);

        // 同じSolverで上限を変更し、完成済みの行と途中の行を切り分けます。
        std::vector<FluidParticle> cluster(4u);
        for (std::size_t index = 0u; index < cluster.size(); ++index)
        {
            cluster[index].Position.x = static_cast<float>(index) * 0.01f;
        }
        settings.ReuseCompletedNeighborRowsOnOverflow = true;
        CPUProfiler& profiler = CPUProfiler::Get();
        profiler.BeginFrame();
        const auto checkCapacity = [&](std::size_t limit, double expectedRows, double expectedOverflow)
        {
            settings.MaximumCachedNeighbors = limit;
            solver.SetSettings(settings);
            auto inputs = cluster;
            checkStep(inputs);
            profiler.BeginFrame();
            const auto readCounter = [&](std::string_view name)
            {
                for (const auto& counter : profiler.GetLastFrame().Counters)
                {
                    if (counter.Name == name)
                    {
                        return counter.Value;
                    }
                }
                return -1.0;
            };
            assert(readCounter("Physics.Fluid.SPH.NeighborCacheCompletedRowCount") == expectedRows);
            assert(readCounter("Physics.Fluid.SPH.NeighborCacheOverflow") == expectedOverflow);
            const double bytes = readCounter("Physics.Fluid.SPH.NeighborCacheCapacityBytes");
            assert(bytes >= 0.0);
            assert(bytes <= limit * sizeof(float) * 2u + (cluster.size() + 1u) * sizeof(std::size_t));
            if (limit == 0u)
            {
                assert(bytes == 0.0);
            }
        };
        checkCapacity(12u, 4.0, 0.0);
        checkCapacity(5u, 1.0, 1.0);
        checkCapacity(2u, 0.0, 1.0);
        checkCapacity(0u, 0.0, 0.0);
        checkCapacity(12u, 4.0, 0.0);
    }

    {
        SPHSettings settings;
        settings.SmoothingRadius = 0.125f;
        settings.StableTimeStepEnabled = false;
        SPHSolver serialSolver(settings);
        SPHSolver parallelSolver(settings);
        JobSystem jobSystem(2u);
        CPUProfiler& profiler = CPUProfiler::Get();

        const auto readForceCounters = [&]()
        {
            std::array<double, 3> counters{-1.0, -1.0, -1.0};
            for (const CPUProfileCounter& counter : profiler.GetLastFrame().Counters)
            {
                if (counter.Name == "Physics.Fluid.SPH.ForceNeighborCandidateCount")
                {
                    counters[0] = counter.Value;
                }
                if (counter.Name == "Physics.Fluid.SPH.ForceNeighborAcceptedCount")
                {
                    counters[1] = counter.Value;
                }
                if (counter.Name == "Physics.Fluid.SPH.ForceJobCount")
                {
                    counters[2] = counter.Value;
                }
            }
            return counters;
        };

        const auto checkParallelStep = [&](const std::vector<FluidParticle>& initial,
            bool expectsCache, double expectedJobCount)
        {
            constexpr float deltaTime = 1.0e-4f;
            auto expected = initial;
            auto actual = initial;
            profiler.BeginFrame();
            serialSolver.Step(expected, deltaTime);
            profiler.BeginFrame();
            const auto serialCounters = readForceCounters();
            parallelSolver.Step(actual, deltaTime, &jobSystem);
            profiler.BeginFrame();
            const auto parallelCounters = readForceCounters();

            const auto readCounter = [&](std::string_view name)
            {
                for (const CPUProfileCounter& counter : profiler.GetLastFrame().Counters)
                {
                    if (counter.Name == name)
                    {
                        return counter.Value;
                    }
                }
                return -1.0;
            };
            const double cachedCount = readCounter("Physics.Fluid.SPH.NeighborCacheCount");
            const double capacityBytes = readCounter("Physics.Fluid.SPH.NeighborCacheCapacityBytes");
            assert(capacityBytes >= cachedCount * (sizeof(uint32_t) + sizeof(float)));
            assert(readCounter("Physics.Fluid.SPH.NeighborCacheOverflow") == (expectsCache ? 0.0 : 1.0));
            assert(readCounter("Physics.Fluid.SPH.ForceNeighborCacheUsed") == (expectsCache ? 1.0 : 0.0));
            assert(readCounter("Physics.Fluid.SPH.DensityJobCount") == expectedJobCount);
            assert(capacityBytes <= settings.MaximumCachedNeighbors * (sizeof(uint32_t) + sizeof(float))
                + (initial.size() + expectedJobCount + 1.0) * sizeof(std::size_t));
            if (expectsCache == true)
            {
                assert(cachedCount == parallelCounters[1]);
            }
            else
            {
                assert(cachedCount >= settings.MaximumCachedNeighbors / (expectedJobCount + 1.0));
            }

            const double minimumJobTime = readCounter("Physics.Fluid.SPH.ForceJobMinMilliseconds");
            const double maximumJobTime = readCounter("Physics.Fluid.SPH.ForceJobMaxMilliseconds");
            if (expectedJobCount > 0.0)
            {
                assert(minimumJobTime >= 0.0);
                assert(maximumJobTime >= minimumJobTime);
            }
            else
            {
                assert(minimumJobTime == -1.0);
                assert(maximumJobTime == -1.0);
            }

            assert(serialCounters[1] > 0.0);
            assert((serialCounters[0] == serialCounters[1]) == expectsCache);
            assert(serialCounters[0] == parallelCounters[0]);
            assert(serialCounters[1] == parallelCounters[1]);
            assert(serialCounters[2] == 0.0);
            assert(parallelCounters[2] == expectedJobCount);
            for (std::size_t index = 0u; index < initial.size(); ++index)
            {
                assert(actual[index].Density == expected[index].Density);
                assert(actual[index].Pressure == expected[index].Pressure);
                assert((actual[index].Force - expected[index].Force).LengthSq() == 0.0f);
                assert((actual[index].Velocity - expected[index].Velocity).LengthSq() == 0.0f);
                assert((actual[index].Position - expected[index].Position).LengthSq() == 0.0f);
            }
        };

        std::vector<FluidParticle> particles(1024);
        for (std::size_t index = 0u; index < particles.size(); ++index)
        {
            particles[index].Position = {
                static_cast<float>(index % 16u) * 0.0625f,
                static_cast<float>((index / 16u) % 8u) * 0.0625f,
                static_cast<float>(index / 128u) * 0.0625f};
            particles[index].Velocity.y = static_cast<float>(index % 16u) * 0.03125f;
        }
        checkParallelStep(std::vector<FluidParticle>(particles.begin(), particles.begin() + 6), true, 0.0);
        checkParallelStep(particles, true, 1.0);
        checkParallelStep(std::vector<FluidParticle>(particles.begin(), particles.begin() + 6), true, 0.0);
        checkParallelStep(particles, true, 1.0);

        // 514個の密集群だけでキャッシュ上限を超え、疎な範囲と混在するケースも照合します。
        for (std::size_t index = 0u; index < 514u; ++index)
        {
            particles[index].Position = {2.0f + static_cast<float>(index) * 0.000125f, 0.0f, 0.0f};
        }
        checkParallelStep(particles, false, 1.0);

        auto expected = particles;
        serialSolver.Step(expected, 1.0e-4f);
        profiler.BeginFrame();
        settings.ReuseCompletedNeighborRowsOnOverflow = true;
        parallelSolver.SetSettings(settings);
        parallelSolver.Step(particles, 1.0e-4f, &jobSystem);
        profiler.BeginFrame();
        const auto partialCounters = readForceCounters();
        assert(partialCounters[0] > partialCounters[1]);
        assert(partialCounters[2] == 1.0);
        for (std::size_t index = 0u; index < particles.size(); ++index)
        {
            assert(particles[index].Density == expected[index].Density);
            assert(particles[index].Pressure == expected[index].Pressure);
            assert((particles[index].Force - expected[index].Force).LengthSq() == 0.0f);
            assert((particles[index].Velocity - expected[index].Velocity).LengthSq() == 0.0f);
            assert((particles[index].Position - expected[index].Position).LengthSq() == 0.0f);
        }

        settings.MaximumCachedNeighbors = 0u;
        parallelSolver.SetSettings(settings);
        auto noCache = expected;
        auto serialNoCache = expected;
        serialSolver.Step(serialNoCache, 1.0e-4f);
        profiler.BeginFrame();
        parallelSolver.Step(noCache, 1.0e-4f, &jobSystem);
        profiler.BeginFrame();
        for (const CPUProfileCounter& counter : profiler.GetLastFrame().Counters)
        {
            if (counter.Name == "Physics.Fluid.SPH.NeighborCacheCapacityBytes")
            {
                assert(counter.Value == 0.0);
            }
        }
        for (std::size_t index = 0u; index < noCache.size(); ++index)
        {
            assert(noCache[index].Density == serialNoCache[index].Density);
            assert(noCache[index].Pressure == serialNoCache[index].Pressure);
            assert((noCache[index].Force - serialNoCache[index].Force).LengthSq() == 0.0f);
            assert((noCache[index].Position - serialNoCache[index].Position).LengthSq() == 0.0f);
            assert((noCache[index].Velocity - serialNoCache[index].Velocity).LengthSq() == 0.0f);
        }
    }

    {
        Scene scene;
        const auto source = DenseSDFSource::CreateSphere({}, 1.0f);
        auto obstacle = scene.CreateEntity("SPH signed distance boundary");
        obstacle.AddComponent<StaticSDFColliderComponent>().Boundary =
            StaticSDFBoundary::FromAnalytic(CreateRef<DenseSDFSource>(*source), 0.01f);
        SPHSettings settings{};
        settings.Gravity = {};
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1);
        particles[0].Position = {1.05f, 0, 0};
        particles[0].Velocity = {-1.0f, 0, 0};
        solver.Step(particles, 1.0f / 60.0f);
        FluidStaticColliderCoupling coupling({0.1f, 0.0f});
        coupling.ResolveScene(scene, particles);
        assert(coupling.GetLastStatistics().ResolvedContactCount == 1);
        assert(std::abs(particles[0].Position.x - 1.1f) < 1.0e-5f);
        assert(std::abs(particles[0].Velocity.x) < 1.0e-5f);
    }
    {
        const float h = 1.0f;
        assert(SPHKernel::EvaluatePoly6Density(0.0f, h) > 0.0f);
        assert(std::abs(SPHKernel::EvaluatePoly6Density(1.0f, h)) <= 1.0e-6f);
        const math::Vec3 gradient = SPHKernel::EvaluateSpikyGradient(
            { 0.5f, 0.0f, 0.0f },
            0.5f,
            h);
        assert(gradient.x < 0.0f);
        assert(SPHKernel::EvaluateViscosityLaplacian(0.5f, h) > 0.0f);
    }

    // 負座標・Cell境界を含め、通常検索と共有検索のVisit順が一致することを確認します。
    {
        std::vector<FluidParticle> particles(6u);
        particles[0].Position = { -1.0f, 0.0f, 0.0f };
        particles[1].Position = { 0.0f, -0.5f, 0.0f };
        particles[2].Position = { -0.25f, 0.0f, -0.5f };
        particles[3].Position = { -0.875f, 0.0f, 0.0f };
        particles[4].Position = { 0.5f, 0.5f, 0.0f };
        particles[5].Position = { 1.5f, 0.0f, 0.0f };
        constexpr float radius = 1.0f;
        constexpr float cellSizes[] = { 1.0f, 0.5f, 0.25f };
        for (float cellSize : cellSizes)
        {
            FluidSpatialHashGrid grid(cellSize);
            const auto compareQueries = [&]()
            {
                for (const auto& particle : particles)
                {
                    std::vector<uint32_t> expected;
                    std::vector<uint32_t> actual;
                    grid.ForEachNeighborParticle(particle.Position, radius,
                        [&](uint32_t index) { expected.push_back(index); });
                    grid.ForEachPreparedNeighborParticle(particle.Position, radius,
                        [&](uint32_t index) { actual.push_back(index); });
                    assert(actual == expected);
                }
            };
            grid.Build(particles);
            grid.PrepareNeighborBuckets(radius);
            compareQueries();

            // 粒子を別Cellへ移して再Buildした後も、古い共有結果を参照しないことを確認します。
            particles[5].Position.x -= 0.5f;
            grid.Build(particles);
            compareQueries();
            grid.PrepareNeighborBuckets(radius);
            compareQueries();
        }
    }

    // Neighbor QueryはCellSizeより大きな検索半径でも必要Cellを走査します。
    {
        FluidSpatialHashGrid grid(1.0f);
        std::vector<FluidParticle> particles(3u);
        particles[0].Position = { 0.1f, 0.0f, 0.0f };
        particles[1].Position = { 2.1f, 0.0f, 0.0f };
        particles[2].Position = { 4.1f, 0.0f, 0.0f };
        grid.Build(particles);

        bool found0 = false;
        bool found1 = false;
        bool found2 = false;
        grid.ForEachNeighborParticle(particles[0].Position, 2.1f, [&](uint32_t index)
        {
            if (index == 0u)
            {
                found0 = true;
            }
            else if (index == 1u)
            {
                found1 = true;
            }
            else if (index == 2u)
            {
                found2 = true;
            }
        });

        assert(found0);
        assert(found1);
        assert(found2 == false);
    }

    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.RestDensity = 1.0f;
        settings.PressureStiffness = 10.0f;

        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(3u);
        particles[0].Position = { 0.0f, 0.0f, 0.0f };
        particles[1].Position = { 0.5f, 0.0f, 0.0f };
        particles[2].Position = { 1.5f, 0.0f, 0.0f };
        particles[0].Mass = 1.0f;
        particles[1].Mass = 2.0f;
        particles[2].Mass = 4.0f;

        solver.ComputeDensityAndPressure(particles);
        const float expected =
            particles[0].Mass * SPHKernel::EvaluatePoly6Density(0.0f, 1.0f)
            + particles[1].Mass * SPHKernel::EvaluatePoly6Density(0.25f, 1.0f);
        assert(std::abs(particles[0].Density - expected) <= 1.0e-5f);
    }

    // Spatial Hash CellSizeはBroad Phase最適化パラメータです。
    // hを固定したままCellSizeを0.75h / 1.0h / 1.25hへ変更しても、
    // 厳密なsupport radius判定後のDensityは一致しなければなりません。
    {
        std::vector<FluidParticle> sourceParticles(4u);
        sourceParticles[0].Position = { 0.0f, 0.0f, 0.0f };
        sourceParticles[1].Position = { 0.45f, 0.0f, 0.0f };
        sourceParticles[2].Position = { 0.95f, 0.0f, 0.0f };
        sourceParticles[3].Position = { 1.25f, 0.0f, 0.0f };
        sourceParticles[0].Mass = 1.0f;
        sourceParticles[1].Mass = 2.0f;
        sourceParticles[2].Mass = 3.0f;
        sourceParticles[3].Mass = 4.0f;

        const float cellSizeScales[] = { 0.75f, 1.0f, 1.25f };
        std::vector<float> referenceDensities;

        for (float cellSizeScale : cellSizeScales)
        {
            SPHSettings settings{};
            settings.SmoothingRadius = 1.0f;
            settings.SpatialHashCellSizeScale = cellSizeScale;
            settings.PressureStiffness = 0.0f;

            SPHSolver solver(settings);
            std::vector<FluidParticle> particles = sourceParticles;
            solver.ComputeDensity(particles);

            if (referenceDensities.empty())
            {
                referenceDensities.reserve(particles.size());
                for (const FluidParticle& particle : particles)
                {
                    referenceDensities.push_back(particle.Density);
                }
                continue;
            }

            assert(referenceDensities.size() == particles.size());
            for (std::size_t i = 0u; i < particles.size(); ++i)
            {
                assert(std::abs(particles[i].Density - referenceDensities[i]) <= 1.0e-5f);
            }
        }
    }

    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = math::Vec3{};
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(2u);
        particles[0].Position = { -0.25f, 0.0f, 0.0f };
        particles[1].Position = { 0.25f, 0.0f, 0.0f };
        particles[0].Density = particles[1].Density = 1.0f;
        particles[0].Pressure = particles[1].Pressure = 1.0f;
        solver.ComputeForces(particles);
        assert(particles[0].Force.x < 0.0f);
        assert(particles[1].Force.x > 0.0f);
    }

    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.Viscosity = 1.0f;
        settings.Gravity = math::Vec3{};
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(2u);
        particles[0].Position = { -0.25f, 0.0f, 0.0f };
        particles[1].Position = { 0.25f, 0.0f, 0.0f };
        particles[0].Velocity = { 1.0f, 0.0f, 0.0f };
        particles[1].Velocity = { -1.0f, 0.0f, 0.0f };
        particles[0].Density = particles[1].Density = 1.0f;
        solver.ComputeForces(particles);
        assert(particles[0].Force.x < 0.0f);
        assert(particles[1].Force.x > 0.0f);
    }

    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = { 0.0f, -10.0f, 0.0f };
        settings.StableTimeStepEnabled = false;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Mass = 2.0f;
        solver.Step(particles, 0.1f);
        assert(std::abs(particles[0].Velocity.y + 1.0f) <= 1.0e-5f);
        assert(solver.GetLastSubstepCount() == 1u);
    }

    {
        SPHSettings settings{};
        settings.BoundaryEnabled = true;
        settings.BoundaryParticleRadius = 0.1f;
        settings.BoundaryRestitution = 0.5f;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Position = { 1.2f, 0.0f, 0.0f };
        particles[0].Velocity = { 2.0f, 0.0f, 0.0f };
        solver.ResolveBoundary(particles);
        assert(std::abs(particles[0].Position.x - 0.9f) <= 1.0e-6f);
        assert(std::abs(particles[0].Velocity.x + 1.0f) <= 1.0e-6f);
    }

    // Velocity CFL: dt = 0.5 * h / |v| = 0.05。
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.CFLFactor = 0.5f;
        settings.AccelerationTimeStepFactor = 0.0f;
        settings.MinimumTimeStep = 0.0f;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Velocity = { 10.0f, 0.0f, 0.0f };
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) - 0.05f) <= 1.0e-6f);
    }

    // Pressure stiffness: c=sqrt(k)=10、dt=0.05。
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 100.0f;
        settings.CFLFactor = 0.5f;
        settings.AccelerationTimeStepFactor = 0.0f;
        settings.MinimumTimeStep = 0.0f;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) - 0.05f) <= 1.0e-6f);
    }

    // Acceleration condition: |a|=16、dt=0.25*sqrt(1/16)=0.0625。
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.CFLFactor = 0.0f;
        settings.AccelerationTimeStepFactor = 0.25f;
        settings.MinimumTimeStep = 0.0f;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Mass = 2.0f;
        particles[0].Force = { 32.0f, 0.0f, 0.0f };
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) - 0.0625f) <= 1.0e-6f);
    }

    // 0.12秒を0.05 + 0.05 + 0.02へ分割します。
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = math::Vec3{};
        settings.CFLFactor = 0.5f;
        settings.AccelerationTimeStepFactor = 0.0f;
        settings.MinimumTimeStep = 0.0f;
        settings.MaximumSubsteps = 8u;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Velocity = { 10.0f, 0.0f, 0.0f };
        solver.Step(particles, 0.12f);
        assert(solver.GetLastSubstepCount() == 3u);
        assert(std::abs(solver.GetLastMinimumSubstepDeltaTime() - 0.02f) <= 1.0e-5f);
        assert(solver.WasLastSubstepLimitReached() == false);
    }

    // 上限2回では0.12秒を安全な0.05刻みだけで消化できないため、上限到達状態を記録します。
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = math::Vec3{};
        settings.CFLFactor = 0.5f;
        settings.AccelerationTimeStepFactor = 0.0f;
        settings.MinimumTimeStep = 0.0f;
        settings.MaximumSubsteps = 2u;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Velocity = { 10.0f, 0.0f, 0.0f };
        solver.Step(particles, 0.12f);
        assert(solver.GetLastSubstepCount() == 2u);
        assert(solver.WasLastSubstepLimitReached());
    }

    // 統合された粒子走査を、公開APIで各処理を分けた参照手順と比較します。
    {
        SPHSettings settings{};
        settings.BoundaryEnabled = true;
        settings.BoundaryRestitution = 0.25f;
        settings.SmoothingRadius = 0.2f;
        settings.MaximumSubsteps = 4u;
        SPHSolver solver(settings);
        SPHSolver reference(settings);
        JobSystem jobs(2u);
        std::vector<FluidParticle> actual(1024u);
        for (std::size_t index = 0u; index < actual.size(); ++index)
        {
            actual[index].Position = {
                static_cast<float>(index % 16u) * 0.0625f - 0.5f,
                static_cast<float>((index / 16u) % 8u) * 0.0625f,
                static_cast<float>(index / 128u) * 0.0625f};
            actual[index].Mass = 0.1f;
        }
        actual[0].Mass = 0.0f;
        actual[0].Position.x = 1.2f;
        actual[0].Velocity.x = 20.0f;
        auto expected = actual;

        for (const float deltaTime : {5.0e-3f, 3.0e-2f})
        {
            float remainingTime = deltaTime;
            float minimumDt = deltaTime;
            uint32_t substeps = 0u;
            bool hasReachedLimit = false;
            while (remainingTime > math::Epsilon && substeps < settings.MaximumSubsteps)
            {
                float dt = reference.ComputeStableTimeStep(expected, remainingTime);
                if (substeps + 1u == settings.MaximumSubsteps && dt + math::Epsilon < remainingTime)
                {
                    dt = remainingTime;
                    hasReachedLimit = true;
                }
                reference.ComputeDensityAndPressure(expected);
                reference.ComputeForces(expected);
                reference.Integrate(expected, dt);
                reference.ResolveBoundary(expected);
                remainingTime -= dt;
                minimumDt = std::min(minimumDt, dt);
                ++substeps;
            }

            solver.Step(actual, deltaTime, &jobs);
            assert(solver.GetLastSubstepCount() == substeps);
            assert(solver.GetLastMinimumSubstepDeltaTime() == minimumDt);
            assert(solver.WasLastSubstepLimitReached() == hasReachedLimit);
            assert(actual[0].Position.x == 1.0f - settings.BoundaryParticleRadius);
            for (std::size_t index = 0u; index < actual.size(); ++index)
            {
                assert(actual[index].Density == expected[index].Density);
                assert(actual[index].Pressure == expected[index].Pressure);
                assert((actual[index].Force - expected[index].Force).LengthSq() == 0.0f);
                assert((actual[index].Position - expected[index].Position).LengthSq() == 0.0f);
                assert((actual[index].Velocity - expected[index].Velocity).LengthSq() == 0.0f);
            }

            // 次のStep開始時は外部から加えられた速度変更をCFLへ反映します。
            actual[0].Velocity.y = 100.0f;
            expected[0].Velocity.y = 100.0f;
        }
    }

    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.MinimumTimeStep = 0.0f;
        settings.CFLFactor = 0.5f;
        settings.AccelerationTimeStepFactor = 0.25f;
        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(2u);
        particles[0].Velocity = {3.0e20f, 4.0e20f, 0.0f};
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) / 1.0e-21f - 1.0f) < 1.0e-6f);
        particles[0].Velocity = {};
        particles[0].Force = {3.0e20f, 4.0e20f, 0.0f};
        particles[0].Mass = 1.0e20f;
        particles[1].Force = {8.0f, 0.0f, 0.0f};
        particles[1].Mass = 0.5f;
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) - 0.0625f) < 1.0e-6f);
        particles.resize(1u);
        assert(std::abs(solver.ComputeStableTimeStep(particles, 1.0f) - 0.25f / std::sqrt(5.0f)) < 1.0e-6f);
    }

    // Fluid Particle <-> Sphere Static Collider:
    // Particle半径を含む表面まで押し出し、内向き速度だけを反発させます。
    {
        FluidStaticColliderCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.Restitution = 0.5f;
        FluidStaticColliderCoupling coupling(settings);

        TransformComponent transform{};
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 0.5f;
        collider.Restitution = 0.25f;

        FluidParticle particle{};
        particle.Position = { 0.25f, 0.0f, 0.0f };
        particle.Velocity = { -2.0f, 0.0f, 0.0f };

        assert(coupling.ResolveParticleAgainstCollider(particle, transform, collider));
        assert(std::abs(particle.Position.x - 0.6f) <= 1.0e-6f);
        assert(std::abs(particle.Velocity.x - 0.5f) <= 1.0e-6f);
    }

    // Fluid Particle <-> Box Static Collider:
    // Particle中心がBox内部にある場合は最寄り面 + Particle Radiusまで押し出します。
    {
        FluidStaticColliderCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        settings.Restitution = 0.0f;
        FluidStaticColliderCoupling coupling(settings);

        TransformComponent transform{};
        ColliderComponent collider{};
        collider.Type = ColliderType::Box;
        collider.HalfExtents = { 0.5f, 0.5f, 0.5f };

        FluidParticle particle{};
        particle.Position = { 0.2f, 0.0f, 0.0f };
        particle.Velocity = { -1.0f, 0.0f, 0.0f };

        assert(coupling.ResolveParticleAgainstCollider(particle, transform, collider));
        assert(std::abs(particle.Position.x - 0.6f) <= 1.0e-6f);
        assert(std::abs(particle.Velocity.x) <= 1.0e-6f);
    }

    // Trigger ColliderはFluidの物理Constraintとして扱いません。
    {
        FluidStaticColliderCouplingSettings settings{};
        settings.ParticleRadius = 0.1f;
        FluidStaticColliderCoupling coupling(settings);

        TransformComponent transform{};
        ColliderComponent collider{};
        collider.Type = ColliderType::Sphere;
        collider.Radius = 1.0f;
        collider.IsTrigger = true;

        FluidParticle particle{};
        particle.Position = { 0.25f, 0.0f, 0.0f };
        const math::Vec3 originalPosition = particle.Position;

        assert(coupling.ResolveParticleAgainstCollider(particle, transform, collider) == false);
        assert((particle.Position - originalPosition).LengthSq() <= 1.0e-12f);
    }
}

} // namespace Raven::ph::tests
