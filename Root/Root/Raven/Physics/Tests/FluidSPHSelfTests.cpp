#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
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
                assert(actual.Density == expectedParticle.Density);
                assert(actual.Pressure == expectedParticle.Pressure);
                assert((actual.Force - expectedParticle.Force).LengthSq() == 0.0f);
                assert((actual.Velocity - expectedParticle.Velocity).LengthSq() == 0.0f);
                assert((actual.Position - expectedParticle.Position).LengthSq() == 0.0f);
            }
        };

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

        // 514個の密集群だけでキャッシュ上限を超え、疎な範囲と混在するケースも照合します。
        for (std::size_t index = 0u; index < 514u; ++index)
        {
            particles[index].Position = {2.0f + static_cast<float>(index) * 0.000125f, 0.0f, 0.0f};
        }
        checkParallelStep(particles, false, 1.0);
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
