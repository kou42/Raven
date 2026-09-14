#include "Raven/Physics/Electromagnetism/CoulombForce.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

math::Vec3 ComputeCoulombForce(
    const math::Vec3& sourcePosition,
    double sourceChargeCoulombs,
    const math::Vec3& targetPosition,
    double targetChargeCoulombs,
    const CoulombForceSettings& settings)
{
    const math::Vec3 displacement = targetPosition - sourcePosition;
    const double distanceSquared = static_cast<double>(displacement.LengthSq());

    const double minimumDistance = static_cast<double>(std::max(settings.MinimumDistance, 0.0f));
    const double minimumDistanceSquared = minimumDistance * minimumDistance;
    const double safeDistanceSquared = std::max(distanceSquared, minimumDistanceSquared);

    // 完全同位置では方向が定義できません。特異点回避のためゼロForceを返し、
    // 今後Softeningや有限サイズ電荷モデルを導入する際にこの契約を置き換えられるようにします。
    if (distanceSquared <= 1.0e-24)
    {
        return math::Vec3{};
    }

    const double distance = std::sqrt(distanceSquared);
    const math::Vec3 direction = displacement / static_cast<float>(distance);

    // F_target = k * q_source * q_target / r^2 * rHat
    // q_source*q_target > 0 ならsourceからtargetへ向かうため斥力、
    // q_source*q_target < 0 なら符号反転してsource側へ向かうため引力になります。
    const double magnitude = settings.CoulombConstant
        * sourceChargeCoulombs
        * targetChargeCoulombs
        / safeDistanceSquared;

    return direction * static_cast<float>(magnitude);
}

} // namespace Raven::ph
