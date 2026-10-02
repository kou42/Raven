#pragma once

#include "Raven/Physics/Astro/Gravity/GravitySolver.h"

namespace Raven::ph
{

class BarnesHutGravitySolver final : public GravitySolver
{
public:
    void SetTheta(double theta) { m_Theta = theta; }
    double GetTheta() const { return m_Theta; }

    void ComputeForces(
        const std::vector<AstroBodyState>& bodies,
        const GravitySolverSettings& settings,
        std::vector<AstroVector3>& outForces,
        AstroStatistics* statistics = nullptr) const override;

private:
    double m_Theta = 0.5;
};

} // namespace Raven::ph
