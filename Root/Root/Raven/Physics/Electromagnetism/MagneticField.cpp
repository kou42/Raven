#include "Raven/Physics/Electromagnetism/MagneticField.h"

namespace Raven::ph
{

math::Vec3 UniformMagneticField::Evaluate(const math::Vec3& worldPosition) const
{
    // 一様磁場は位置に依存しません。未使用であることを明示して警告を避けます。
    static_cast<void>(worldPosition);
    return m_MagneticFluxDensity;
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
