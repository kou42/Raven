#include "Raven/Physics/Astro/OrbitalDiagnostics.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

OrbitalDiagnostics OrbitalDiagnosticsCalculator::Compute(
    const std::vector<AstroBodyState>& bodies,
    const GravitySolverSettings& settings)
{
    OrbitalDiagnostics result{};

    for (const AstroBodyState& body : bodies)
    {
        if (std::isfinite(body.Mass) == false || body.Mass <= 0.0)
        {
            continue;
        }

        result.KineticEnergy += 0.5 * body.Mass * body.Velocity.LengthSq();
        result.TotalLinearMomentum += body.Velocity * body.Mass;
        result.TotalAngularMomentum +=
            AstroVector3::Cross(body.Position, body.Velocity * body.Mass);
    }

    const double gravitationalConstant = settings.GravitationalConstant;
    const double minimumDistance = std::max(settings.MinimumDistance, 0.0);
    if (std::isfinite(gravitationalConstant) == true && gravitationalConstant > 0.0)
    {
        for (std::size_t i = 0u; i < bodies.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < bodies.size(); ++j)
            {
                const AstroBodyState& a = bodies[i];
                const AstroBodyState& b = bodies[j];
                if (a.Mass <= 0.0 || b.Mass <= 0.0
                    || std::isfinite(a.Mass) == false || std::isfinite(b.Mass) == false)
                {
                    continue;
                }

                const double distance = (b.Position - a.Position).Length();
                if (std::isfinite(distance) == false || distance <= 0.0)
                {
                    continue;
                }

                const double effectiveDistance = std::max(distance, minimumDistance);
                if (effectiveDistance <= 0.0)
                {
                    continue;
                }

                result.PotentialEnergy -=
                    gravitationalConstant * a.Mass * b.Mass / effectiveDistance;
            }
        }
    }

    result.TotalEnergy = result.KineticEnergy + result.PotentialEnergy;
    return result;
}

} // namespace Raven::ph
