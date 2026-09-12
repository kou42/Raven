#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"
#include "Raven/Physics/Fluid/SPHKernel.h"
#include "Raven/Physics/Fluid/SPHSettings.h"
#include "Raven/Physics/Fluid/SPHSolver.h"

namespace Raven::ph::tests
{

void RunFluidSPHSelfTests()
{
    // ------------------------------------------------------------------------
    // 1. SPH Kernel support / direction
    // ------------------------------------------------------------------------
    {
        const float h = 1.0f;
        const float centerValue = SPHKernel::EvaluatePoly6Density(0.0f, h);
        const float boundaryValue = SPHKernel::EvaluatePoly6Density(1.0f, h);
        const float outsideValue = SPHKernel::EvaluatePoly6Density(1.01f, h);

        assert(centerValue > 0.0f);
        assert(std::abs(boundaryValue) <= 1.0e-6f);
        assert(std::abs(outsideValue) <= 1.0e-6f);

        const math::Vec3 gradient = SPHKernel::EvaluateSpikyGradient(
            math::Vec3{ 0.5f, 0.0f, 0.0f },
            0.5f,
            h);
        assert(gradient.x < 0.0f);
        assert(std::abs(gradient.y) <= 1.0e-6f);
        assert(std::abs(gradient.z) <= 1.0e-6f);

        const math::Vec3 centerGradient = SPHKernel::EvaluateSpikyGradient(
            math::Vec3{},
            0.0f,
            h);
        assert(centerGradient.LengthSq() <= 1.0e-6f);
        assert(SPHKernel::EvaluateViscosityLaplacian(0.5f, h) > 0.0f);
        assert(std::abs(SPHKernel::EvaluateViscosityLaplacian(1.1f, h)) <= 1.0e-6f);
    }

    // ------------------------------------------------------------------------
    // 2. Radius larger than CellSize
    // ------------------------------------------------------------------------
    // ParticleSpatialHashGridは検索半径からCell範囲を計算するため、Fluid Adapterでも
    // radius > CellSizeのNeighborを取りこぼさないことを確認します。
    {
        FluidSpatialHashGrid grid(1.0f);
        std::vector<FluidParticle> particles(3u);
        particles[0].Position = { 0.1f, 0.0f, 0.0f };
        particles[1].Position = { 2.1f, 0.0f, 0.0f };
        particles[2].Position = { 4.1f, 0.0f, 0.0f };
        grid.Build(particles);

        bool foundParticle0 = false;
        bool foundParticle1 = false;
        bool foundParticle2 = false;
        grid.ForEachNeighborParticle(
            particles[0].Position,
            2.1f,
            [&](uint32_t particleIndex)
            {
                if (particleIndex == 0u)
                {
                    foundParticle0 = true;
                }
                else if (particleIndex == 1u)
                {
                    foundParticle1 = true;
                }
                else if (particleIndex == 2u)
                {
                    foundParticle2 = true;
                }
            });

        assert(foundParticle0);
        assert(foundParticle1);
        assert(foundParticle2 == false);
    }

    // ------------------------------------------------------------------------
    // 3. SPH Density includes self and exact radius filter
    // ------------------------------------------------------------------------
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

        const float expectedDensity0 =
            particles[0].Mass * SPHKernel::EvaluatePoly6Density(0.0f, 1.0f)
            + particles[1].Mass * SPHKernel::EvaluatePoly6Density(0.25f, 1.0f);

        assert(std::abs(particles[0].Density - expectedDensity0) <= 1.0e-5f);
        assert(std::abs(
            particles[0].Pressure
            - settings.PressureStiffness * (expectedDensity0 - settings.RestDensity))
            <= 1.0e-4f);
    }

    // ------------------------------------------------------------------------
    // 4. Symmetric Pressure Force
    // ------------------------------------------------------------------------
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = math::Vec3{};

        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(2u);
        particles[0].Position = { -0.25f, 0.0f, 0.0f };
        particles[1].Position = { 0.25f, 0.0f, 0.0f };
        particles[0].Density = 1.0f;
        particles[1].Density = 1.0f;
        particles[0].Pressure = 1.0f;
        particles[1].Pressure = 1.0f;
        particles[0].Mass = 1.0f;
        particles[1].Mass = 1.0f;

        solver.ComputeForces(particles);

        // 正圧なので2 Particleは互いに押し離されます。
        assert(particles[0].Force.x < 0.0f);
        assert(particles[1].Force.x > 0.0f);
        assert(std::abs(particles[0].Force.x + particles[1].Force.x) <= 1.0e-4f);
    }

    // ------------------------------------------------------------------------
    // 5. Viscosity reduces relative velocity
    // ------------------------------------------------------------------------
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
        particles[0].Density = 1.0f;
        particles[1].Density = 1.0f;
        particles[0].Pressure = 0.0f;
        particles[1].Pressure = 0.0f;

        solver.ComputeForces(particles);

        assert(particles[0].Force.x < 0.0f);
        assert(particles[1].Force.x > 0.0f);
        assert(std::abs(particles[0].Force.x + particles[1].Force.x) <= 1.0e-4f);
    }

    // ------------------------------------------------------------------------
    // 6. Gravity + Semi-Implicit Euler
    // ------------------------------------------------------------------------
    {
        SPHSettings settings{};
        settings.SmoothingRadius = 1.0f;
        settings.PressureStiffness = 0.0f;
        settings.Viscosity = 0.0f;
        settings.Gravity = { 0.0f, -10.0f, 0.0f };
        settings.BoundaryEnabled = false;

        SPHSolver solver(settings);
        std::vector<FluidParticle> particles(1u);
        particles[0].Mass = 2.0f;

        solver.Step(particles, 0.1f);

        assert(std::abs(particles[0].Velocity.y + 1.0f) <= 1.0e-5f);
        assert(std::abs(particles[0].Position.y + 0.1f) <= 1.0e-5f);
    }

    // ------------------------------------------------------------------------
    // 7. Box Boundary
    // ------------------------------------------------------------------------
    {
        SPHSettings settings{};
        settings.BoundaryEnabled = true;
        settings.BoundaryMinimum = { -1.0f, -1.0f, -1.0f };
        settings.BoundaryMaximum = { 1.0f, 1.0f, 1.0f };
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
}

} // namespace Raven::ph::tests
