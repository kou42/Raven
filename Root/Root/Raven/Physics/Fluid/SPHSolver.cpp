#include "Raven/Physics/Fluid/SPHSolver.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Raven/Math/Math.h"
#include "Raven/Physics/Fluid/SPHKernel.h"

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumSmoothingRadius = 1.0e-4f;

void ResolveBoundaryAxis(
    float& position,
    float& velocity,
    float minimum,
    float maximum,
    float restitution)
{
    if (position < minimum)
    {
        position = minimum;
        if (velocity < 0.0f)
        {
            velocity = -velocity * restitution;
        }
        return;
    }

    if (position > maximum)
    {
        position = maximum;
        if (velocity > 0.0f)
        {
            velocity = -velocity * restitution;
        }
    }
}
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
    m_Settings.Viscosity = std::max(0.0f, settings.Viscosity);
    m_Settings.BoundaryParticleRadius = std::max(0.0f, settings.BoundaryParticleRadius);
    m_Settings.BoundaryRestitution = std::clamp(settings.BoundaryRestitution, 0.0f, 1.0f);

    // Boundaryの入力順が逆でもSolver内部では必ずMinimum <= Maximumに正規化します。
    m_Settings.BoundaryMinimum =
    {
        std::min(settings.BoundaryMinimum.x, settings.BoundaryMaximum.x),
        std::min(settings.BoundaryMinimum.y, settings.BoundaryMaximum.y),
        std::min(settings.BoundaryMinimum.z, settings.BoundaryMaximum.z)
    };
    m_Settings.BoundaryMaximum =
    {
        std::max(settings.BoundaryMinimum.x, settings.BoundaryMaximum.x),
        std::max(settings.BoundaryMinimum.y, settings.BoundaryMaximum.y),
        std::max(settings.BoundaryMinimum.z, settings.BoundaryMaximum.z)
    };

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
        // 負圧も式どおり保持し、Pressure Forceの挙動を観察した上でClamp方針を判断します。
        particle.Pressure = m_Settings.PressureStiffness
            * (particle.Density - m_Settings.RestDensity);
    }
}

void SPHSolver::ComputeDensityAndPressure(std::vector<FluidParticle>& particles)
{
    ComputeDensity(particles);
    ComputePressure(particles);
}

void SPHSolver::ComputeForces(std::vector<FluidParticle>& particles)
{
    // Public APIとして単独呼び出しされても現在位置とHashが一致するよう再構築します。
    m_SpatialHash.Build(particles);
    ComputeForcesUsingCurrentGrid(particles);
}

void SPHSolver::ComputeForcesUsingCurrentGrid(std::vector<FluidParticle>& particles) const
{
    const float smoothingRadius = m_Settings.SmoothingRadius;
    const float smoothingRadiusSq = smoothingRadius * smoothingRadius;

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        FluidParticle& particle = particles[particleIndex];
        const float particleMass = std::max(0.0f, particle.Mass);
        particle.Force = m_Settings.Gravity * particleMass;

        if (particleMass <= math::Epsilon || particle.Density <= math::Epsilon)
        {
            continue;
        }

        const float densityI = particle.Density;
        const float inverseDensityISq = 1.0f / (densityI * densityI);

        m_SpatialHash.ForEachNeighborParticle(
            particle.Position,
            smoothingRadius,
            [&](uint32_t neighborIndex)
            {
                if (neighborIndex >= particles.size()
                    || neighborIndex == static_cast<uint32_t>(particleIndex))
                {
                    return;
                }

                const FluidParticle& neighbor = particles[neighborIndex];
                const float neighborMass = std::max(0.0f, neighbor.Mass);
                if (neighborMass <= math::Epsilon || neighbor.Density <= math::Epsilon)
                {
                    return;
                }

                const math::Vec3 displacement = particle.Position - neighbor.Position;
                const float distanceSq = displacement.LengthSq();
                if (distanceSq <= math::Epsilon * math::Epsilon
                    || distanceSq > smoothingRadiusSq)
                {
                    return;
                }

                const float distance = std::sqrt(distanceSq);
                const float densityJ = neighbor.Density;

                // 対称Pressure Force:
                // F_i^p = -m_i sum_j m_j (p_i/rho_i^2 + p_j/rho_j^2) grad W_ij
                // i/jを入れ替えるとgradientの符号だけが反転するため、Pair全体で運動量を保ちやすい形です。
                const math::Vec3 pressureGradient =
                    SPHKernel::EvaluateSpikyGradient(displacement, distance, smoothingRadius);
                const float pressureTerm =
                    particle.Pressure * inverseDensityISq
                    + neighbor.Pressure / (densityJ * densityJ);
                particle.Force += pressureGradient
                    * (-particleMass * neighborMass * pressureTerm);

                // Viscosity Force:
                // F_i^v = m_i * mu * sum_j m_j (v_j-v_i)/rho_j * laplacian W_ij
                // 相対速度を減らす方向へ働き、局所的な速度差を滑らかにします。
                if (m_Settings.Viscosity > 0.0f)
                {
                    const float viscosityLaplacian =
                        SPHKernel::EvaluateViscosityLaplacian(distance, smoothingRadius);
                    const math::Vec3 velocityDifference =
                        neighbor.Velocity - particle.Velocity;
                    particle.Force += velocityDifference
                        * (particleMass
                            * m_Settings.Viscosity
                            * neighborMass
                            / densityJ
                            * viscosityLaplacian);
                }
            });
    }
}

void SPHSolver::Integrate(
    std::vector<FluidParticle>& particles,
    float deltaTime) const
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    for (FluidParticle& particle : particles)
    {
        const float particleMass = std::max(0.0f, particle.Mass);
        if (particleMass <= math::Epsilon)
        {
            continue;
        }

        // Semi-Implicit Euler:
        // a = F/m, v_{n+1} = v_n + a*dt, x_{n+1} = x_n + v_{n+1}*dt
        // Explicit Eulerより位置更新に新しい速度を使うため、ゲーム物理で扱いやすい安定性を得られます。
        const math::Vec3 acceleration = particle.Force / particleMass;
        particle.Velocity += acceleration * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SPHSolver::ResolveBoundary(std::vector<FluidParticle>& particles) const
{
    if (m_Settings.BoundaryEnabled == false)
    {
        return;
    }

    const float radius = m_Settings.BoundaryParticleRadius;
    const float restitution = m_Settings.BoundaryRestitution;

    math::Vec3 minimum = m_Settings.BoundaryMinimum + math::Vec3(radius);
    math::Vec3 maximum = m_Settings.BoundaryMaximum - math::Vec3(radius);

    // Particle直径より狭いAxisでは有効領域が反転するため、Box中央へ潰してNaNや振動を防ぎます。
    if (minimum.x > maximum.x)
    {
        const float center = (m_Settings.BoundaryMinimum.x + m_Settings.BoundaryMaximum.x) * 0.5f;
        minimum.x = center;
        maximum.x = center;
    }
    if (minimum.y > maximum.y)
    {
        const float center = (m_Settings.BoundaryMinimum.y + m_Settings.BoundaryMaximum.y) * 0.5f;
        minimum.y = center;
        maximum.y = center;
    }
    if (minimum.z > maximum.z)
    {
        const float center = (m_Settings.BoundaryMinimum.z + m_Settings.BoundaryMaximum.z) * 0.5f;
        minimum.z = center;
        maximum.z = center;
    }

    for (FluidParticle& particle : particles)
    {
        ResolveBoundaryAxis(
            particle.Position.x,
            particle.Velocity.x,
            minimum.x,
            maximum.x,
            restitution);
        ResolveBoundaryAxis(
            particle.Position.y,
            particle.Velocity.y,
            minimum.y,
            maximum.y,
            restitution);
        ResolveBoundaryAxis(
            particle.Position.z,
            particle.Velocity.z,
            minimum.z,
            maximum.z,
            restitution);
    }
}

void SPHSolver::Step(
    std::vector<FluidParticle>& particles,
    float deltaTime)
{
    if (deltaTime <= 0.0f || particles.empty())
    {
        return;
    }

    // 位置依存量 -> 力 -> 時間積分 -> 境界、という物理データフローを明示します。
    ComputeDensityAndPressure(particles);
    ComputeForcesUsingCurrentGrid(particles);
    Integrate(particles, deltaTime);
    ResolveBoundary(particles);
}

} // namespace ph
} // namespace Raven
