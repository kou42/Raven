#pragma once

#include <cstdint>
#include <functional>
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
// Density -> Pressure -> Force -> Integration -> External Constraint -> Boundary
// の順で1 Substepを進めます。
// Frame deltaTimeはStable Time Stepに基づいて必要な回数へ分割します。
//
// Scene / RigidBodyなど他Domainの型はSPHSolverへ持ち込まず、必要なCouplingは
// SubstepConstraintCallback経由で外側から差し込みます。
class SPHSolver
{
public:
    using SubstepConstraintCallback =
        std::function<void(std::vector<FluidParticle>& particles, float deltaTime)>;

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

    // 従来互換のStepです。外部Couplingを必要としない場合はこちらを使用します。
    void Step(std::vector<FluidParticle>& particles, float deltaTime);

    // 各SubstepでIntegration直後に外部Constraint/Couplingを解決します。
    // callbackはFluid固有のScene型を知らないため、Static Colliderだけでなく将来の
    // RigidBody / SoftBody / Thermal Couplingにも同じ境界を利用できます。
    void Step(
        std::vector<FluidParticle>& particles,
        float deltaTime,
        const SubstepConstraintCallback& constraintCallback);

    uint32_t GetLastSubstepCount() const { return m_LastSubstepCount; }
    float GetLastMinimumSubstepDeltaTime() const { return m_LastMinimumSubstepDeltaTime; }
    bool WasLastSubstepLimitReached() const { return m_LastSubstepLimitReached; }

private:
    void ComputeForcesUsingCurrentGrid(std::vector<FluidParticle>& particles) const;
    void AdvanceSubstep(
        std::vector<FluidParticle>& particles,
        float deltaTime,
        const SubstepConstraintCallback& constraintCallback);

private:
    SPHSettings m_Settings{};
    FluidSpatialHashGrid m_SpatialHash;
    uint32_t m_LastSubstepCount = 0u;
    float m_LastMinimumSubstepDeltaTime = 0.0f;
    bool m_LastSubstepLimitReached = false;
};

} // namespace ph
} // namespace Raven
