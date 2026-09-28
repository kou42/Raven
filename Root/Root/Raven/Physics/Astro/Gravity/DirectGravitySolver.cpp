#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven::ph
{

void DirectGravitySolver::ComputeForces(
    const std::vector<AstroBodyState>& bodies,
    const GravitySolverSettings& settings,
    std::vector<math::Vec3>& outForces) const
{
    outForces.assign(bodies.size(), math::Vec3{});

    const double gravitationalConstant = settings.GravitationalConstant;
    const double minimumDistance = std::max(settings.MinimumDistance, 0.0);
    if (std::isfinite(gravitationalConstant) == false || gravitationalConstant <= 0.0)
    {
        return;
    }

    for (std::size_t i = 0u; i < bodies.size(); ++i)
    {
        for (std::size_t j = i + 1u; j < bodies.size(); ++j)
        {
            const AstroBodyState& a = bodies[i];
            const AstroBodyState& b = bodies[j];

            if (std::isfinite(a.Mass) == false
                || std::isfinite(b.Mass) == false
                || a.Mass <= 0.0
                || b.Mass <= 0.0)
            {
                continue;
            }

            const double dx = static_cast<double>(b.Position.x) - static_cast<double>(a.Position.x);
            const double dy = static_cast<double>(b.Position.y) - static_cast<double>(a.Position.y);
            const double dz = static_cast<double>(b.Position.z) - static_cast<double>(a.Position.z);
            const double distanceSquared = dx * dx + dy * dy + dz * dz;
            if (std::isfinite(distanceSquared) == false || distanceSquared <= 0.0)
            {
                // 同一点では方向を定義できません。MinimumDistanceだけで大きさをClampしても
                // 任意方向のForceを生成してしまうため、このペアは有限なゼロForceとします。
                continue;
            }

            const double distance = std::sqrt(distanceSquared);
            const double effectiveDistance = std::max(distance, minimumDistance);
            if (effectiveDistance <= std::numeric_limits<double>::epsilon())
            {
                continue;
            }

            const double forceMagnitude =
                gravitationalConstant * a.Mass * b.Mass / (effectiveDistance * effectiveDistance);
            if (std::isfinite(forceMagnitude) == false)
            {
                continue;
            }

            const double inverseDistance = 1.0 / distance;
            const math::Vec3 forceOnA{
                static_cast<float>(dx * inverseDistance * forceMagnitude),
                static_cast<float>(dy * inverseDistance * forceMagnitude),
                static_cast<float>(dz * inverseDistance * forceMagnitude)
            };

            if (std::isfinite(forceOnA.x) == false
                || std::isfinite(forceOnA.y) == false
                || std::isfinite(forceOnA.z) == false)
            {
                continue;
            }

            // Generate / Receiveを独立させることで、Static sourceやprobe bodyを表現できます。
            // 両者が通常の天体なら同じ計算結果を±で加え、作用反作用の対称性を維持します。
            if (a.ReceiveGravity == true && b.GenerateGravity == true)
            {
                outForces[i] += forceOnA;
            }
            if (b.ReceiveGravity == true && a.GenerateGravity == true)
            {
                outForces[j] -= forceOnA;
            }
        }
    }
}

} // namespace Raven::ph
