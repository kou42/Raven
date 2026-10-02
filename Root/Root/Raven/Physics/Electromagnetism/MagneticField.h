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

// 点磁気双極子が作る磁束密度を評価します。
// DipoleMomentは磁気双極子モーメント [A*m^2]、Centerはworld-space位置です。
// 中心近傍の1/r^3特異点をSimulationへ流さないためMinimumDistanceで評価距離を下限Clampします。
class DipoleMagneticField final : public MagneticField
{
public:
    DipoleMagneticField() = default;

    DipoleMagneticField(
        const math::Vec3& center,
        const math::Vec3& dipoleMoment,
        float minimumDistance = 1.0e-4f)
        : m_Center(center)
        , m_DipoleMoment(dipoleMoment)
        , m_MinimumDistance(minimumDistance)
    {
    }

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

    void SetCenter(const math::Vec3& center) { m_Center = center; }
    const math::Vec3& GetCenter() const { return m_Center; }

    void SetDipoleMoment(const math::Vec3& dipoleMoment) { m_DipoleMoment = dipoleMoment; }
    const math::Vec3& GetDipoleMoment() const { return m_DipoleMoment; }

    void SetMinimumDistance(float minimumDistance) { m_MinimumDistance = minimumDistance; }
    float GetMinimumDistance() const { return m_MinimumDistance; }

private:
    math::Vec3 m_Center{};
    math::Vec3 m_DipoleMoment{};
    float m_MinimumDistance = 1.0e-4f;
};

// 磁気ローレンツ力 F = q(v x B) [N]をworld-spaceで返します。
// velocityは電荷を持つRigidBodyの重心LinearVelocity [m/s]として扱います。
math::Vec3 ComputeMagneticForce(
    double chargeCoulombs,
    const math::Vec3& velocity,
    const math::Vec3& magneticField);

} // namespace Raven::ph
