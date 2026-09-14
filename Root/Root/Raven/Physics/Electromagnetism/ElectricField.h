#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Electromagnetism/CoulombForce.h"

namespace Raven::ph
{

// ============================================================================
// ElectricField
// ============================================================================
// world-space位置を入力として電場 E [N/C] を評価する抽象境界です。
// 荷電RigidBody側は電場の生成方法を知らず、Evaluate()の結果に q を掛けるだけにすることで、
// 一様電場・点電荷・将来のGrid Fieldを同じForce生成経路へ接続できます。
class ElectricField
{
public:
    virtual ~ElectricField() = default;

    virtual math::Vec3 Evaluate(const math::Vec3& worldPosition) const = 0;
};

// 空間全体で一定の電場を表します。基礎テストや平行板コンデンサ近似に利用できます。
class UniformElectricField final : public ElectricField
{
public:
    UniformElectricField() = default;
    explicit UniformElectricField(const math::Vec3& field) : m_Field(field) {}

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

    void SetField(const math::Vec3& field) { m_Field = field; }
    const math::Vec3& GetField() const { return m_Field; }

private:
    math::Vec3 m_Field{};
};

// 1つの点電荷が作る電場を表します。
// 特異点処理はCoulombForceSettingsと共有し、Force計算とField評価で距離下限の契約を一致させます。
class PointChargeElectricField final : public ElectricField
{
public:
    PointChargeElectricField() = default;
    PointChargeElectricField(
        const math::Vec3& sourcePosition,
        double sourceChargeCoulombs,
        const CoulombForceSettings& settings = CoulombForceSettings{});

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

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

// 電場 E [N/C] 中の電荷 q [C] に働く電気力 F = qE [N] を返します。
math::Vec3 ComputeElectricForce(double chargeCoulombs, const math::Vec3& electricField);

} // namespace Raven::ph
