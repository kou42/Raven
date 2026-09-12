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
// 第一段階ではDensity / Pressureのみを担当します。
// Pressure Force / Viscosity / Integrationは、この計算結果を土台に段階的に追加します。
class SPHSolver
{
public:
    explicit SPHSolver(const SPHSettings& settings = SPHSettings{});

    void SetSettings(const SPHSettings& settings);
    const SPHSettings& GetSettings() const { return m_Settings; }

    void ComputeDensity(std::vector<FluidParticle>& particles);
    void ComputePressure(std::vector<FluidParticle>& particles) const;
    void ComputeDensityAndPressure(std::vector<FluidParticle>& particles);

private:
    SPHSettings m_Settings{};
    FluidSpatialHashGrid m_SpatialHash;
};

} // namespace ph
} // namespace Raven
