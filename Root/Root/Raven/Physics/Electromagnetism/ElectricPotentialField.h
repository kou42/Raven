#pragma once

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// ============================================================================
// ElectricPotentialField
// ============================================================================
// world-space位置で電位 V [V] を評価するScalarFieldです。
// ElectricFieldがベクトル値Eを返すのに対し、こちらは各位置に1つのスカラー値を割り当てます。
class ElectricPotentialField : public ScalarField
{
public:
    ~ElectricPotentialField() override = default;
};

// 空間全体で同じ電位を返す定電位場です。
// 電位の絶対値には任意定数を加えられるため、主にScalarFieldの基礎実装や基準電位の表現に利用します。
class UniformElectricPotentialField final : public ElectricPotentialField
{
public:
    UniformElectricPotentialField() = default;
    explicit UniformElectricPotentialField(float potentialVolts)
        : m_PotentialVolts(potentialVolts)
    {
    }

    float Evaluate(const math::Vec3& worldPosition) const override;

    void SetPotentialVolts(float potentialVolts) { m_PotentialVolts = potentialVolts; }
    float GetPotentialVolts() const { return m_PotentialVolts; }

private:
    float m_PotentialVolts = 0.0f;
};

// 点電荷が作る電位 V = kq/r を表します。
// PointChargeElectricFieldと同じSource/Settingsを持たせ、中心近傍の数値安全契約も揃えます。
class PointChargeElectricPotentialField final : public ElectricPotentialField
{
public:
    PointChargeElectricPotentialField() = default;
    PointChargeElectricPotentialField(
        const math::Vec3& sourcePosition,
        double sourceChargeCoulombs,
        const CoulombForceSettings& settings = CoulombForceSettings{});

    float Evaluate(const math::Vec3& worldPosition) const override;

    void SetSourcePosition(const math::Vec3& position) { m_SourcePosition = position; }
    const math::Vec3& GetSourcePosition() const { return m_SourcePosition; }

    void SetSourceChargeCoulombs(double chargeCoulombs) { m_SourceChargeCoulombs = chargeCoulombs; }
    double GetSourceChargeCoulombs() const { return m_SourceChargeCoulombs; }

    void SetSettings(const CoulombForceSettings& settings) { m_Settings = settings; }
    const CoulombForceSettings& GetSettings() const { return m_Settings; }

private:
    math::Vec3 m_SourcePosition{};
    double m_SourceChargeCoulombs = 0.0;
    CoulombForceSettings m_Settings{};
};

} // namespace Raven::ph
