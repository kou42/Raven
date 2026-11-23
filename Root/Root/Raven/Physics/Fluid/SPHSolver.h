#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"
#include "Raven/Physics/Fluid/SPHSettings.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SPH Solver
// ============================================================================
// Density -> Pressure -> Force -> Integration -> Boundary の順で1 Substepを進めます。
// Frame deltaTimeはStable Time Stepに基づいて必要な回数へ分割します。
class SPHSolver
{
public:
    explicit SPHSolver(const SPHSettings& settings = SPHSettings{});

    void SetSettings(const SPHSettings& settings);
    const SPHSettings& GetSettings() const { return m_Settings; }

    void ComputeDensity(std::vector<FluidParticle>& particles);
    void ComputePressure(std::vector<FluidParticle>& particles) const;
    void ComputeDensityAndPressure(std::vector<FluidParticle>& particles);
    void ComputeForces(std::vector<FluidParticle>& particles);

    // 現在の速度・Force・SPH剛性から、次のSubstepで安全側となる時間刻みを推定します。
    float ComputeStableTimeStep(
        const std::vector<FluidParticle>& particles,
        float maximumDeltaTime) const;

    void Integrate(std::vector<FluidParticle>& particles, float deltaTime) const;
    void ResolveBoundary(std::vector<FluidParticle>& particles) const;
    void Step(std::vector<FluidParticle>& particles, float deltaTime);

    uint32_t GetLastSubstepCount() const { return m_LastSubstepCount; }
    float GetLastMinimumSubstepDeltaTime() const { return m_LastMinimumSubstepDeltaTime; }
    bool WasLastSubstepLimitReached() const { return m_LastSubstepLimitReached; }

private:
    struct CachedNeighbor
    {
        uint32_t ParticleIndex;
        float DistanceSquared;
    };

    bool ComputeDensity(std::vector<FluidParticle>& particles, bool shouldCacheNeighbors);
    void ComputeForcesUsingCurrentGrid(
        std::vector<FluidParticle>& particles, bool hasCachedNeighbors) const;
    void AdvanceSubstep(std::vector<FluidParticle>& particles, float deltaTime);

private:
    SPHSettings m_Settings{};
    FluidSpatialHashGrid m_SpatialHash;
    // 各行の近傍はGridの走査順を維持し、位置を更新する前のForce計算だけで使用します。
    std::vector<CachedNeighbor> m_CachedNeighbors;
    std::vector<std::size_t> m_CachedNeighborOffsets;
    uint32_t m_LastSubstepCount = 0u;
    float m_LastMinimumSubstepDeltaTime = 0.0f;
    bool m_LastSubstepLimitReached = false;
};

} // namespace ph
} // namespace Raven
