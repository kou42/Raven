#include "Raven/Physics/Electromagnetism/MagneticField.h"

namespace Raven::ph
{

math::Vec3 ComputeMagneticForce(
    double chargeCoulombs,
    const math::Vec3& linearVelocity,
    const math::Vec3& magneticFluxDensity)
{
    // 磁気力は速度と磁場の双方に直交します。
    // doubleの電荷を最後にfloatへ変換し、RavenのForce accumulator(Vec3<float>)へ合わせます。
    const math::Vec3 velocityCrossField = math::Vec3::Cross(linearVelocity, magneticFluxDensity);
    return velocityCrossField * static_cast<float>(chargeCoulombs);
}

} // namespace Raven::ph
