#pragma once

#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

enum class TemperatureFieldBlendMode
{
    WeightedAverage,
    Override
};

class TemperatureField : public ScalarField
{
public:
    ~TemperatureField() override = default;

    // Registryで複数Fieldを合成するときの位置依存Weightを返します。
    // Global Fieldは既定で1、局所Fieldは領域外で0を返すことで平均対象から自然に除外できます。
    virtual float EvaluateInfluence(const math::Vec3& worldPosition) const
    {
        (void)worldPosition;
        return 1.0f;
    }

    void SetBlendMode(TemperatureFieldBlendMode blendMode) { m_BlendMode = blendMode; }
    TemperatureFieldBlendMode GetBlendMode() const { return m_BlendMode; }

    // PriorityはOverride Field同士が重なった場合だけ使用します。
    // 数値が大きいFieldを優先し、同PriorityはInfluence加重平均して登録順依存を避けます。
    void SetPriority(int priority) { m_Priority = priority; }
    int GetPriority() const { return m_Priority; }

private:
    TemperatureFieldBlendMode m_BlendMode = TemperatureFieldBlendMode::WeightedAverage;
    int m_Priority = 0;
};

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
class GradientTemperatureField final : public TemperatureField
{
public:
    GradientTemperatureField() = default;
    GradientTemperatureField(const math::Vec3& referencePosition, float referenceTemperatureKelvin, const math::Vec3& gradientKelvinPerUnit);
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

enum class TemperatureRegionFalloff
{
    Hard,
    Linear,
    SmoothStep
};

// 球形のworld-space領域に局所温度を与えるTemperatureFieldです。
class SphericalTemperatureRegionField final : public TemperatureField
{
public:
    SphericalTemperatureRegionField() = default;
    SphericalTemperatureRegionField(const math::Vec3& center, float radius, float insideTemperatureKelvin,
        float falloffDistance = 0.0f, TemperatureRegionFalloff falloff = TemperatureRegionFalloff::Hard);
    float Evaluate(const math::Vec3& worldPosition) const override;
    float EvaluateInfluence(const math::Vec3& worldPosition) const override;
    void SetCenter(const math::Vec3& center) { m_Center = center; }
    const math::Vec3& GetCenter() const { return m_Center; }
    void SetRadius(float radius);
    float GetRadius() const { return m_Radius; }
    void SetInsideTemperatureKelvin(float temperatureKelvin);
    float GetInsideTemperatureKelvin() const { return m_InsideTemperatureKelvin; }
    void SetFalloffDistance(float falloffDistance);
    float GetFalloffDistance() const { return m_FalloffDistance; }
    void SetFalloff(TemperatureRegionFalloff falloff) { m_Falloff = falloff; }
    TemperatureRegionFalloff GetFalloff() const { return m_Falloff; }
private:
    math::Vec3 m_Center{};
    float m_Radius = 1.0f;
    float m_InsideTemperatureKelvin = 293.15f;
    float m_FalloffDistance = 0.0f;
    TemperatureRegionFalloff m_Falloff = TemperatureRegionFalloff::Hard;
};

// Axis-Aligned Box領域に局所温度を与えるTemperatureFieldです。
// HalfExtentsでCore Boxを表し、FalloffはBox表面からの最短Euclidean距離で評価します。
// Collision AABBへ依存させずThermal Field単体で完結させることで、Field層とBroad Phase層の結合を避けます。
class BoxTemperatureRegionField final : public TemperatureField
{
public:
    BoxTemperatureRegionField() = default;
    BoxTemperatureRegionField(const math::Vec3& center, const math::Vec3& halfExtents, float insideTemperatureKelvin,
        float falloffDistance = 0.0f, TemperatureRegionFalloff falloff = TemperatureRegionFalloff::Hard);

    float Evaluate(const math::Vec3& worldPosition) const override;
    float EvaluateInfluence(const math::Vec3& worldPosition) const override;

    void SetCenter(const math::Vec3& center) { m_Center = center; }
    const math::Vec3& GetCenter() const { return m_Center; }
    void SetHalfExtents(const math::Vec3& halfExtents);
    const math::Vec3& GetHalfExtents() const { return m_HalfExtents; }
    void SetInsideTemperatureKelvin(float temperatureKelvin);
    float GetInsideTemperatureKelvin() const { return m_InsideTemperatureKelvin; }
    void SetFalloffDistance(float falloffDistance);
    float GetFalloffDistance() const { return m_FalloffDistance; }
    void SetFalloff(TemperatureRegionFalloff falloff) { m_Falloff = falloff; }
    TemperatureRegionFalloff GetFalloff() const { return m_Falloff; }

private:
    math::Vec3 m_Center{};
    math::Vec3 m_HalfExtents{ 0.5f, 0.5f, 0.5f };
    float m_InsideTemperatureKelvin = 293.15f;
    float m_FalloffDistance = 0.0f;
    TemperatureRegionFalloff m_Falloff = TemperatureRegionFalloff::Hard;
};

} // namespace Raven::ph
