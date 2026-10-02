#include "Raven/Physics/Electromagnetism/MagneticField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

math::Vec3 UniformMagneticField::Evaluate(const math::Vec3& worldPosition) const
{
    // 一様磁場は位置に依存しません。未使用であることを明示して警告を避けます。
    static_cast<void>(worldPosition);
    return m_MagneticFluxDensity;
}

math::Vec3 DipoleMagneticField::Evaluate(const math::Vec3& worldPosition) const
{
    constexpr float VacuumPermeabilityOverFourPi = 1.0e-7f;

    const math::Vec3 displacement = worldPosition - m_Center;
    const float distanceSq = displacement.LengthSq();

    // 双極子中心では方向を定義できないため、NaN/Infを避けてゼロ磁場を返します。
    if (distanceSq <= 0.0f)
    {
        return {};
    }

    const float minimumDistance = std::max(m_MinimumDistance, 0.0f);
    const float distance = std::sqrt(distanceSq);
    const float evaluationDistance = std::max(distance, minimumDistance);
    if (evaluationDistance <= 0.0f)
    {
        return {};
    }

    const math::Vec3 direction = displacement * (1.0f / distance);
    const float momentProjection = math::Vec3::Dot(m_DipoleMoment, direction);
    const float inverseDistanceCubed =
        1.0f / (evaluationDistance * evaluationDistance * evaluationDistance);

    // B = μ0/(4πr^3) * (3(m・rHat)rHat - m)。
    // 距離だけをClampし、方向は実際のworld-space位置から維持します。
    return (direction * (3.0f * momentProjection) - m_DipoleMoment)
        * (VacuumPermeabilityOverFourPi * inverseDistanceCubed);
}

math::Vec3 ComputeMagneticForce(
    double chargeCoulombs,
    const math::Vec3& velocity,
    const math::Vec3& magneticField)
{
    return math::Vec3::Cross(velocity, magneticField)
        * static_cast<float>(chargeCoulombs);
}

} // namespace Raven::ph
