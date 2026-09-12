#include "Raven/Physics/Fluid/SPHSolver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "Raven/Physics/Fluid/SPHKernel.h"

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumSmoothingRadius = 1.0e-4f;
}

SPHSolver::SPHSolver(const SPHSettings& settings)
    : m_SpatialHash(settings.SmoothingRadius)
{
    SetSettings(settings);
}

void SPHSolver::SetSettings(const SPHSettings& settings)
{
    m_Settings = settings;
    m_Settings.SmoothingRadius = std::max(
        settings.SmoothingRadius,
        MinimumSmoothingRadius);
    m_Settings.RestDensity = std::max(0.0f, settings.RestDensity);
    m_Settings.PressureStiffness = std::max(0.0f, settings.PressureStiffness);

    // SPHではCellSizeをsupport radius hへ合わせると、通常は3x3x3近傍だけで候補を得られます。
    // Core側はh > CellSizeにも対応していますが、ここでは最も素直な構成を標準にします。
    m_SpatialHash.SetCellSize(m_Settings.SmoothingRadius);
}

void SPHSolver::ComputeDensity(std::vector<FluidParticle>& particles)
{
    m_SpatialHash.Build(particles);

    const float smoothingRadius = m_Settings.SmoothingRadius;
    const float smoothingRadiusSq = smoothingRadius * smoothingRadius;

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        FluidParticle& particle = particles[particleIndex];
        float density = 0.0f;

        // rho_i = sum_j m_j W(|x_i - x_j|, h)
        // Neighbor Queryは候補Cellだけを返すため、support radiusの厳密な球判定はここで行います。
        // j == i もW(0, h)として密度へ寄与するため、self particleは除外しません。
        m_SpatialHash.ForEachNeighborParticle(
            particle.Position,
            smoothingRadius,
            [&](uint32_t neighborIndex)
            {
                if (neighborIndex >= particles.size())
                {
                    return;
                }

                const FluidParticle& neighbor = particles[neighborIndex];
                const math::Vec3 delta = particle.Position - neighbor.Position;
                const float distanceSq = delta.LengthSq();
                if (distanceSq > smoothingRadiusSq)
                {
                    return;
                }

                const float neighborMass = std::max(0.0f, neighbor.Mass);
                density += neighborMass
                    * SPHKernel::EvaluatePoly6Density(distanceSq, smoothingRadius);
            });

        particle.Density = density;
    }
}

void SPHSolver::ComputePressure(std::vector<FluidParticle>& particles) const
{
    for (FluidParticle& particle : particles)
    {
        // 最初のEquation of Stateは線形形 p = k(rho - rho_0) とします。
        // 負圧も式どおり保持し、後続のPressure Force実装で安定性を評価して必要ならClamp方針を決めます。
        particle.Pressure = m_Settings.PressureStiffness
            * (particle.Density - m_Settings.RestDensity);
    }
}

void SPHSolver::ComputeDensityAndPressure(std::vector<FluidParticle>& particles)
{
    ComputeDensity(particles);
    ComputePressure(particles);
}

} // namespace ph
} // namespace Raven
