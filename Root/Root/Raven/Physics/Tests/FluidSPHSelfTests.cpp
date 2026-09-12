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
    {
        const float h = 1.0f;
        assert(SPHKernel::EvaluatePoly6Density(0.0f, h) > 0.0f);
        assert(std::abs(SPHKernel::EvaluatePoly6Density(1.0f, h)) <= 1.0e-6f);
        const math::Vec3 gradient = SPHKernel::EvaluateSpikyGradient({ 0.5f, 0.0f, 0.0f }, 0.5f, h);
        assert(gradient.x < 0.0f);
        assert(SPHKernel::EvaluateViscosityLaplacian(0.5f, h) > 0.0f);
    }
    {
        FluidSpatialHashGrid grid(1.0f);
        std::vector<FluidParticle> particles(3u);
        particles[0].Position = { 0.1f, 0.0f, 0.0f };
        particles[1].Position = { 2.1f, 0.0f, 0.0f };
        particles[2].Position = { 4.1f, 0.0f, 0.0f };
        grid.Build(particles);
        bool found0 = false; bool found1 = false; bool found2 = false;
        grid.ForEachNeighborParticle(particles[0].Position, 2.1f, [&](uint32_t index)
        {
            if (index == 0u) { found0 = true; }
            else if (index == 1u) { found1 = true; }
            else if (index == 2u) { found2 = true; }
        });
        assert(found0); assert(found1); assert(found2 == false);
    }
    {
        SPHSettings settings{}; settings.SmoothingRadius = 1.0f; settings.RestDensity = 1.0f; settings.PressureStiffness = 10.0f;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(3u);
        particles[0].Position = {0.0f,0.0f,0.0f}; particles[1].Position = {0.5f,0.0f,0.0f}; particles[2].Position = {1.5f,0.0f,0.0f};
        particles[0].Mass=1.0f; particles[1].Mass=2.0f; particles[2].Mass=4.0f;
        solver.ComputeDensityAndPressure(particles);
        const float expected = particles[0].Mass*SPHKernel::EvaluatePoly6Density(0.0f,1.0f)+particles[1].Mass*SPHKernel::EvaluatePoly6Density(0.25f,1.0f);
        assert(std::abs(particles[0].Density-expected)<=1.0e-5f);
    }
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.Viscosity=0.0f; settings.Gravity=math::Vec3{};
        SPHSolver solver(settings); std::vector<FluidParticle> particles(2u);
        particles[0].Position={-0.25f,0.0f,0.0f}; particles[1].Position={0.25f,0.0f,0.0f};
        particles[0].Density=particles[1].Density=1.0f; particles[0].Pressure=particles[1].Pressure=1.0f;
        solver.ComputeForces(particles);
        assert(particles[0].Force.x<0.0f); assert(particles[1].Force.x>0.0f);
    }
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.Viscosity=1.0f; settings.Gravity=math::Vec3{};
        SPHSolver solver(settings); std::vector<FluidParticle> particles(2u);
        particles[0].Position={-0.25f,0.0f,0.0f}; particles[1].Position={0.25f,0.0f,0.0f};
        particles[0].Velocity={1.0f,0.0f,0.0f}; particles[1].Velocity={-1.0f,0.0f,0.0f};
        particles[0].Density=particles[1].Density=1.0f;
        solver.ComputeForces(particles); assert(particles[0].Force.x<0.0f); assert(particles[1].Force.x>0.0f);
    }
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=0.0f; settings.Viscosity=0.0f;
        settings.Gravity={0.0f,-10.0f,0.0f}; settings.StableTimeStepEnabled=false;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Mass=2.0f;
        solver.Step(particles,0.1f); assert(std::abs(particles[0].Velocity.y+1.0f)<=1.0e-5f); assert(solver.GetLastSubstepCount()==1u);
    }
    {
        SPHSettings settings{}; settings.BoundaryEnabled=true; settings.BoundaryParticleRadius=0.1f; settings.BoundaryRestitution=0.5f;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Position={1.2f,0.0f,0.0f}; particles[0].Velocity={2.0f,0.0f,0.0f};
        solver.ResolveBoundary(particles); assert(std::abs(particles[0].Position.x-0.9f)<=1.0e-6f); assert(std::abs(particles[0].Velocity.x+1.0f)<=1.0e-6f);
    }
    // Velocity CFL: dt = 0.5 * h / |v| = 0.05。
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=0.0f; settings.CFLFactor=0.5f;
        settings.AccelerationTimeStepFactor=0.0f; settings.MinimumTimeStep=0.0f;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Velocity={10.0f,0.0f,0.0f};
        assert(std::abs(solver.ComputeStableTimeStep(particles,1.0f)-0.05f)<=1.0e-6f);
    }
    // Pressure stiffness: c=sqrt(k)=10、dt=0.05。
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=100.0f; settings.CFLFactor=0.5f;
        settings.AccelerationTimeStepFactor=0.0f; settings.MinimumTimeStep=0.0f;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u);
        assert(std::abs(solver.ComputeStableTimeStep(particles,1.0f)-0.05f)<=1.0e-6f);
    }
    // Acceleration condition: |a|=16、dt=0.25*sqrt(1/16)=0.0625。
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=0.0f; settings.CFLFactor=0.0f;
        settings.AccelerationTimeStepFactor=0.25f; settings.MinimumTimeStep=0.0f;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Mass=2.0f; particles[0].Force={32.0f,0.0f,0.0f};
        assert(std::abs(solver.ComputeStableTimeStep(particles,1.0f)-0.0625f)<=1.0e-6f);
    }
    // 0.12秒を0.05 + 0.05 + 0.02へ分割します。
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=0.0f; settings.Viscosity=0.0f;
        settings.Gravity=math::Vec3{}; settings.CFLFactor=0.5f; settings.AccelerationTimeStepFactor=0.0f;
        settings.MinimumTimeStep=0.0f; settings.MaximumSubsteps=8u;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Velocity={10.0f,0.0f,0.0f};
        solver.Step(particles,0.12f); assert(solver.GetLastSubstepCount()==3u);
        assert(std::abs(solver.GetLastMinimumSubstepDeltaTime()-0.02f)<=1.0e-5f); assert(solver.WasLastSubstepLimitReached()==false);
    }
    // 上限2回では0.12秒を安全な0.05刻みだけで消化できないため、上限到達状態を記録します。
    {
        SPHSettings settings{}; settings.SmoothingRadius=1.0f; settings.PressureStiffness=0.0f; settings.Viscosity=0.0f;
        settings.Gravity=math::Vec3{}; settings.CFLFactor=0.5f; settings.AccelerationTimeStepFactor=0.0f;
        settings.MinimumTimeStep=0.0f; settings.MaximumSubsteps=2u;
        SPHSolver solver(settings); std::vector<FluidParticle> particles(1u); particles[0].Velocity={10.0f,0.0f,0.0f};
        solver.Step(particles,0.12f); assert(solver.GetLastSubstepCount()==2u); assert(solver.WasLastSubstepLimitReached());
    }
}

} // namespace Raven::ph::tests
