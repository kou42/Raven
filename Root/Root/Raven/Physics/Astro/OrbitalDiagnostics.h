#pragma once

#include <vector>

#include "Raven/Physics/Astro/Gravity/GravitySolver.h"

namespace Raven::ph
{

struct OrbitalDiagnostics
{
    double KineticEnergy = 0.0;
    double PotentialEnergy = 0.0;
    double TotalEnergy = 0.0;
    AstroVector3 TotalLinearMomentum{};
    AstroVector3 TotalAngularMomentum{};
};

class OrbitalDiagnosticsCalculator
{
public:
    static OrbitalDiagnostics Compute(
        const std::vector<AstroBodyState>& bodies,
        const GravitySolverSettings& settings);
};

} // namespace Raven::ph
