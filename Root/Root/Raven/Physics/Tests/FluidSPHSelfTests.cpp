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
    // 1. Poly6 support radius
    // ------------------------------------------------------------------------
    {
        const float h = 1.0f;
        const float centerValue = SPHKernel::EvaluatePoly6Density(0.0f, h);
        const float boundaryValue = SPHKernel::EvaluatePoly6Density(1.0f, h);
        const float outsideValue = SPHKernel::EvaluatePoly6Density(1.01f, h);

        assert(centerValue > 0.0f);
        assert(std::abs(boundaryValue) <= 1.0e-6f);
        assert(std::abs(outsideValue) <= 1.0e-6f);
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
}

} // namespace Raven::ph::tests
