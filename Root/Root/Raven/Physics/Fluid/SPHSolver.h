#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"
#include "Raven/Physics/Fluid/SPHKernel.h"
#include "Raven/Physics/Fluid/SPHSettings.h"

namespace Raven
{
class JobSystem;

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
    // JobSystemはこの呼び出し中だけ借用します。同じPoolのWorkerからは渡さないでください。
    void Step(std::vector<FluidParticle>& particles, float deltaTime, JobSystem* pJobSystem = nullptr);

    uint32_t GetLastSubstepCount() const { return m_LastSubstepCount; }
    float GetLastMinimumSubstepDeltaTime() const { return m_LastMinimumSubstepDeltaTime; }
    bool WasLastSubstepLimitReached() const { return m_LastSubstepLimitReached; }

private:
    struct CachedNeighbor
    {
        uint32_t ParticleIndex;
        float DistanceSquared;
    };

    struct NeighborForceTerms
    {
        float Mass = 0.0f;
        float PressureOverDensitySquared = 0.0f;
        float InverseDensity = 0.0f;
    };

    struct ForceNeighborCounts
    {
        uint64_t m_Candidates = 0u;
        uint64_t m_Accepted = 0u;
        double m_Milliseconds = 0.0;
    };

    struct DensityJobBuffer
    {
        std::vector<CachedNeighbor> m_Neighbors;
        std::vector<std::size_t> m_Offsets;
        std::size_t m_RangeBegin = 0u;
        std::size_t m_RangeEnd = 0u;
        std::size_t m_CacheLimit = 0u;
        std::size_t m_CachedRowCount = 0u;
        uint64_t m_Candidates = 0u;
        uint64_t m_Accepted = 0u;
        bool m_HasOverflowed = false;
        bool m_IsVerlet = false;
    };

    enum class NeighborSearch
    {
        Grid,
        BuildVerlet,
        ReuseVerlet
    };

    struct VerletStepState
    {
        float m_SearchRadius = 0.0f;
        double m_MaximumDisplacementSquared = 0.0;
        bool m_HasValidList = false;
    };

    struct ParticleStatistics
    {
        void Accumulate(const FluidParticle& particle, float mass);
        float GetMaximumSpeed() const;
        float GetMaximumAcceleration() const;

        float m_MaximumDensity = 0.0f;
        float m_MaximumAbsolutePressure = 0.0f;
        double m_MaximumSpeedSquared = 0.0;
        double m_MaximumAccelerationForceSquared = 0.0;
        double m_MaximumAccelerationMassSquared = 1.0;
        float m_MaximumAccelerationMass = 1.0f;
    };

    static constexpr std::size_t MaximumWorkerJobs = 7u;
    std::span<const DensityJobBuffer> ComputeDensity(
        std::vector<FluidParticle>& particles, bool isSubstep, JobSystem* pJobSystem,
        VerletStepState* pVerlet = nullptr);
    void ComputeDensityRange(std::vector<FluidParticle>& particles, DensityJobBuffer& buffer,
        std::span<NeighborForceTerms> forceTerms, NeighborSearch search, float searchRadius) const;
    void ComputeForcesUsingCurrentGrid(std::vector<FluidParticle>& particles,
        std::span<const DensityJobBuffer> cachedRanges, JobSystem* pJobSystem);
    ForceNeighborCounts ComputeForceRange(std::vector<FluidParticle>& particles,
        std::size_t rangeBegin, std::size_t rangeEnd,
        std::span<const DensityJobBuffer> cachedRanges) const noexcept;
    float EvaluatePressure(float density) const;
    NeighborForceTerms EvaluateNeighborForceTerms(const FluidParticle& particle, bool hasViscosity) const;
    ParticleStatistics ScanParticleStatistics(const std::vector<FluidParticle>& particles) const;
    float ComputeStableTimeStep(const ParticleStatistics& statistics, float maximumDeltaTime) const;
    bool CanReuseVerletList(const std::vector<FluidParticle>& particles, const VerletStepState& verlet) const;
    ParticleStatistics AdvanceSubstep(std::vector<FluidParticle>& particles, float deltaTime,
        JobSystem* pJobSystem, VerletStepState* pVerlet = nullptr);

private:
    SPHSettings m_Settings{};
    SPHKernel m_Kernel{m_Settings.SmoothingRadius};
    FluidSpatialHashGrid m_SpatialHash;
    // 各行の近傍はGridの走査順を維持し、位置を更新する前のForce計算だけで使用します。
    std::array<DensityJobBuffer, MaximumWorkerJobs + 1u> m_DensityBuffers;
    std::vector<NeighborForceTerms> m_NeighborForceTerms;
    // 容量は再利用しますが、有効性はStepローカルなVerletStepStateだけで管理します。
    std::vector<math::Vec3> m_VerletReferencePositions;
    uint32_t m_LastSubstepCount = 0u;
    float m_LastMinimumSubstepDeltaTime = 0.0f;
    bool m_LastSubstepLimitReached = false;
};

} // namespace ph
} // namespace Raven
