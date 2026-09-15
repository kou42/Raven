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

// 基準位置からの変位に比例して温度が変化する線形TemperatureFieldです。
// Gradientはworld-space各軸方向の温度変化率[K / world-unit]を表します。
// 例: Gradient={ 0, -6.5, 0 }なら、Yが1増えるごとに6.5K低下します。
class GradientTemperatureField final : public TemperatureField
{
public:
    GradientTemperatureField() = default;
    GradientTemperatureField(
        const math::Vec3& referencePosition,
        float referenceTemperatureKelvin,
        const math::Vec3& gradientKelvinPerUnit);

    float Evaluate(const math::Vec3& worldPosition) const override;

    void SetReferencePosition(const math::Vec3& referencePosition) { m_ReferencePosition = referencePosition; }
    const math::Vec3& GetReferencePosition() const { return m_ReferencePosition; }

    void SetReferenceTemperatureKelvin(float temperatureKelvin);
    float GetReferenceTemperatureKelvin() const { return m_ReferenceTemperatureKelvin; }

    void SetGradientKelvinPerUnit(const math::Vec3& gradientKelvinPerUnit) { m_GradientKelvinPerUnit = gradientKelvinPerUnit; }
    const math::Vec3& GetGradientKelvinPerUnit() const { return m_GradientKelvinPerUnit; }

private:
    math::Vec3 m_ReferencePosition{};
    float m_ReferenceTemperatureKelvin = 293.15f;
    math::Vec3 m_GradientKelvinPerUnit{};
};

// 球形のworld-space領域内外で異なる温度を返す局所TemperatureFieldです。
// 現段階では境界を明確に保つためHard Regionとし、Radius以内をInside、それ以外をOutsideとして評価します。
// Smooth Falloffが必要になった場合はRegion形状とBlend Policyを分離して拡張できるよう、Field自身は状態を持つだけにします。
class SphericalTemperatureRegionField final : public TemperatureField
{
public:
    SphericalTemperatureRegionField() = default;
    SphericalTemperatureRegionField(
        const math::Vec3& center,
        float radius,
        float insideTemperatureKelvin,
        float outsideTemperatureKelvin = 293.15f);

    float Evaluate(const math::Vec3& worldPosition) const override;

    void SetCenter(const math::Vec3& center) { m_Center = center; }
    const math::Vec3& GetCenter() const { return m_Center; }

    void SetRadius(float radius);
    float GetRadius() const { return m_Radius; }

    void SetInsideTemperatureKelvin(float temperatureKelvin);
    float GetInsideTemperatureKelvin() const { return m_InsideTemperatureKelvin; }

    void SetOutsideTemperatureKelvin(float temperatureKelvin);
    float GetOutsideTemperatureKelvin() const { return m_OutsideTemperatureKelvin; }

private:
    math::Vec3 m_Center{};
    float m_Radius = 1.0f;
    float m_InsideTemperatureKelvin = 293.15f;
    float m_OutsideTemperatureKelvin = 293.15f;
};

} // namespace Raven::ph
