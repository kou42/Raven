#pragma once

#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// ============================================================================
// MagneticField
// ============================================================================
// world-space位置で磁束密度B [T]を評価するVectorFieldです。
// MagneticField自身はRigidBodyを変更せず、ElectromagneticSystemが電荷と速度からForceへ変換します。
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

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

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

// 磁気ローレンツ力 F = q(v x B) [N]をworld-spaceで返します。
// velocityは電荷を持つRigidBodyの重心LinearVelocity [m/s]として扱います。
math::Vec3 ComputeMagneticForce(
    double chargeCoulombs,
    const math::Vec3& velocity,
    const math::Vec3& magneticField);

} // namespace Raven::ph
