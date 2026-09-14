#include "Raven/Physics/Electromagnetism/CoulombForce.h"

#include "Raven/Physics/Electromagnetism/ElectricField.h"

namespace Raven::ph
{

math::Vec3 ComputeCoulombForce(
    const math::Vec3& sourcePosition,
    double sourceChargeCoulombs,
    const math::Vec3& targetPosition,
    double targetChargeCoulombs,
    const CoulombForceSettings& settings)
{
    // Coulomb Forceを「sourceが作るElectric Fieldをtarget位置で評価し、F=qEへ変換する」
    // 一般形へ統一します。これによりUniform/Grid Fieldを追加しても、荷電Body側のForce生成契約を
    // 変更せずに同じComputeElectricForce()経路を再利用できます。
    const PointChargeElectricField field(sourcePosition, sourceChargeCoulombs, settings);
    return ComputeElectricForce(targetChargeCoulombs, field.Evaluate(targetPosition));
}

} // namespace Raven::ph
