#pragma once

#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// ============================================================================
// TemperatureField
// ============================================================================
// world-space位置に対する環境温度 T [K] を返すScalarFieldです。
// ThermalBody::TemperatureがBody自身の集中熱容量状態を表すのに対し、TemperatureFieldは
// 空間側の境界条件・環境分布を表します。Field自身は熱量移動やBody状態更新を行いません。
class TemperatureField : public ScalarField
{
public:
    ~TemperatureField() override = default;
};

// 空間全体で一定の環境温度を返す最小のTemperatureField実装です。
// 既存ThermalEnvironmentContactのAmbientTemperatureと同じKelvin契約に合わせています。
class UniformTemperatureField final : public TemperatureField
{
public:
    UniformTemperatureField() = default;
    explicit UniformTemperatureField(float temperatureKelvin);

    float Evaluate(const math::Vec3& worldPosition) const override;

    void SetTemperatureKelvin(float temperatureKelvin);
    float GetTemperatureKelvin() const { return m_TemperatureKelvin; }

private:
    float m_TemperatureKelvin = 293.15f;
};

} // namespace Raven::ph
