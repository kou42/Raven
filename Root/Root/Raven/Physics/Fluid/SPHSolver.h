#pragma once

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
// Density -> Pressure -> Force -> Integration -> Boundary の順で1 Stepを進めます。
// 現段階では理解しやすいCPU実装を優先し、CFL/Substep/PBFなどの安定化は次段階で追加します。
class SPHSolver
{
public:
    explicit SPHSolver(const SPHSettings& settings = SPHSettings{});

    void SetSettings(const SPHSettings& settings);
    const SPHSettings& GetSettings() const { return m_Settings; }

    void ComputeDensity(std::vector<FluidParticle>& particles);
    void ComputePressure(std::vector<FluidParticle>& particles) const;
    void ComputeDensityAndPressure(std::vector<FluidParticle>& particles);

    // Pressure / Viscosity / GravityをParticle::Forceへ実際の力として蓄積します。
    void ComputeForces(std::vector<FluidParticle>& particles);

    // v += (F / m) * dt, x += v * dt のSemi-Implicit Eulerです。
    void Integrate(std::vector<FluidParticle>& particles, float deltaTime) const;

    // 軸平行Boxの内側へParticle中心を保持し、壁へ向かう速度成分だけを反射します。
    void ResolveBoundary(std::vector<FluidParticle>& particles) const;

    // 1回のSPH Simulation Stepです。
    void Step(std::vector<FluidParticle>& particles, float deltaTime);

private:
    // ComputeDensity()後は同じPositionに対するSpatial Hashが既に構築済みなので、
    // Step()では再Buildせずこの内部関数を使います。
    void ComputeForcesUsingCurrentGrid(std::vector<FluidParticle>& particles) const;

private:
    SPHSettings m_Settings{};
    FluidSpatialHashGrid m_SpatialHash;
};

} // namespace ph
} // namespace Raven
