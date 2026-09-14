#include "Raven/Physics/Electromagnetism/ElectricField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

math::Vec3 UniformElectricField::Evaluate(const math::Vec3& worldPosition) const
{
    // 一様電場は位置に依存しません。未使用であることを明示して警告を避けます。
    static_cast<void>(worldPosition);
    return m_Field;
}

PointChargeElectricField::PointChargeElectricField(
    const math::Vec3& sourcePosition,
    double sourceChargeCoulombs,
    const CoulombForceSettings& settings)
    : m_SourcePosition(sourcePosition),
      m_SourceChargeCoulombs(sourceChargeCoulombs),
      m_Settings(settings)
{
}

math::Vec3 PointChargeElectricField::Evaluate(const math::Vec3& worldPosition) const
{
    const math::Vec3 displacement = worldPosition - m_SourcePosition;
    const double distanceSquared = static_cast<double>(displacement.LengthSq());

    // 点電荷と評価点が完全同位置では方向が定義できません。
    // Coulomb Forceと同じ契約でゼロを返し、NaNや任意方向の巨大な電場を生成しないようにします。
    if (distanceSquared <= 1.0e-24)
    {
        return math::Vec3{};
    }

    const double minimumDistance = static_cast<double>(std::max(m_Settings.MinimumDistance, 0.0f));
    const double minimumDistanceSquared = minimumDistance * minimumDistance;
    const double safeDistanceSquared = std::max(distanceSquared, minimumDistanceSquared);
    const double distance = std::sqrt(distanceSquared);
    const math::Vec3 direction = displacement / static_cast<float>(distance);

    // E = k * q_source / r^2 * rHat
    // sourceが正なら外向き、負なら符号が反転してsourceへ向かいます。
    const double magnitude = m_Settings.CoulombConstant
        * m_SourceChargeCoulombs
        / safeDistanceSquared;
    return direction * static_cast<float>(magnitude);
}

math::Vec3 ComputeElectricForce(double chargeCoulombs, const math::Vec3& electricField)
{
    return electricField * static_cast<float>(chargeCoulombs);
}

} // namespace Raven::ph
