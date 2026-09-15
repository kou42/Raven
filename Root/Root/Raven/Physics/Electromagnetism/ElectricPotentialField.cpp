#include "Raven/Physics/Electromagnetism/ElectricPotentialField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

float UniformElectricPotentialField::Evaluate(const math::Vec3& worldPosition) const
{
    static_cast<void>(worldPosition);
    return m_PotentialVolts;
}

PointChargeElectricPotentialField::PointChargeElectricPotentialField(
    const math::Vec3& sourcePosition,
    double sourceChargeCoulombs,
    const CoulombForceSettings& settings)
    : m_SourcePosition(sourcePosition),
      m_SourceChargeCoulombs(sourceChargeCoulombs),
      m_Settings(settings)
{
}

float PointChargeElectricPotentialField::Evaluate(const math::Vec3& worldPosition) const
{
    const math::Vec3 displacement = worldPosition - m_SourcePosition;
    const double distanceSquared = static_cast<double>(displacement.LengthSq());

    // 理想点電荷の電位は中心で発散します。ElectricFieldと同様にMinimumDistanceを使い、
    // Simulationや可視化へInfを流さず有限値を返す数値安全な契約にします。
    const double minimumDistance = static_cast<double>(std::max(m_Settings.MinimumDistance, 0.0f));
    const double minimumDistanceSquared = minimumDistance * minimumDistance;
    const double safeDistanceSquared = std::max(distanceSquared, minimumDistanceSquared);
    if (safeDistanceSquared <= 1.0e-24)
    {
        // MinimumDistanceも0で中心を評価した場合は有限値を定義できないため0を返します。
        return 0.0f;
    }

    const double safeDistance = std::sqrt(safeDistanceSquared);
    const double potential = m_Settings.CoulombConstant
        * m_SourceChargeCoulombs
        / safeDistance;
    return static_cast<float>(potential);
}

} // namespace Raven::ph
