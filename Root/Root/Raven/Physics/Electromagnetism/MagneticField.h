#pragma once

#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// ============================================================================
// MagneticField
// ============================================================================
// world-spaceの磁束密度 B [T] を返すVectorFieldです。
// MagneticField自体は荷電Bodyの速度や電荷を知らず、空間に定義された場だけを表します。
class MagneticField : public VectorField
{
public:
    ~MagneticField() override = default;
};

// 空間上のどの位置でも同じ磁束密度を返す一様磁場です。
class UniformMagneticField final : public MagneticField
{
public:
    UniformMagneticField() = default;
    explicit UniformMagneticField(const math::Vec3& magneticFluxDensity)
        : m_MagneticFluxDensity(magneticFluxDensity)
    {
    }

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override
    {
        static_cast<void>(worldPosition);
        return m_MagneticFluxDensity;
    }

    void SetMagneticFluxDensity(const math::Vec3& magneticFluxDensity)
    {
        m_MagneticFluxDensity = magneticFluxDensity;
    }

    const math::Vec3& GetMagneticFluxDensity() const
    {
        return m_MagneticFluxDensity;
    }

private:
    math::Vec3 m_MagneticFluxDensity{};
};

// 磁場によるLorentz Force F = q(v x B) を計算します。
// ElectricFieldのF=qEと同様、FieldとBody固有情報（電荷・速度）を分離したままForceへ変換します。
math::Vec3 ComputeMagneticForce(
    double chargeCoulombs,
    const math::Vec3& linearVelocity,
    const math::Vec3& magneticFluxDensity);

} // namespace Raven::ph
