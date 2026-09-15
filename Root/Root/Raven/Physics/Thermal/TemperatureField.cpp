#include "Raven/Physics/Thermal/TemperatureField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{

UniformTemperatureField::UniformTemperatureField(float temperatureKelvin)
{
    SetTemperatureKelvin(temperatureKelvin);
}

float UniformTemperatureField::Evaluate(const math::Vec3& worldPosition) const
{
    (void)worldPosition;
    return m_TemperatureKelvin;
}

void UniformTemperatureField::SetTemperatureKelvin(float temperatureKelvin)
{
    // ThermalBodyと既存Environment ContactがKelvinを使用しているため、Field側も同じ単位契約に統一します。
    // 負の絶対温度をRuntimeへ流さないよう、Fieldの状態更新境界で絶対零度へClampします。
    m_TemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}

GradientTemperatureField::GradientTemperatureField(
    const math::Vec3& referencePosition,
    float referenceTemperatureKelvin,
    const math::Vec3& gradientKelvinPerUnit)
    : m_ReferencePosition(referencePosition)
    , m_GradientKelvinPerUnit(gradientKelvinPerUnit)
{
    SetReferenceTemperatureKelvin(referenceTemperatureKelvin);
}

float GradientTemperatureField::Evaluate(const math::Vec3& worldPosition) const
{
    // 線形Scalar Field T(x)=T0+grad(T)・(x-x0) として評価します。
    // GradientをVec3で保持することで、特定軸だけでなく任意方向の温度勾配を同じ式で扱えます。
    const math::Vec3 offset = worldPosition - m_ReferencePosition;
    const float temperatureKelvin =
        m_ReferenceTemperatureKelvin + math::Vec3::Dot(m_GradientKelvinPerUnit, offset);

    // 大きな負勾配や遠距離評価でもKelvin契約を破らないよう、最終評価値も絶対零度でClampします。
    return std::max(temperatureKelvin, 0.0f);
}

void GradientTemperatureField::SetReferenceTemperatureKelvin(float temperatureKelvin)
{
    m_ReferenceTemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}

SphericalTemperatureRegionField::SphericalTemperatureRegionField(
    const math::Vec3& center,
    float radius,
    float insideTemperatureKelvin,
    float falloffDistance,
    TemperatureRegionFalloff falloff)
    : m_Center(center)
    , m_Falloff(falloff)
{
    SetRadius(radius);
    SetInsideTemperatureKelvin(insideTemperatureKelvin);
    SetFalloffDistance(falloffDistance);
}

float SphericalTemperatureRegionField::Evaluate(const math::Vec3& worldPosition) const
{
    (void)worldPosition;
    // Regionの外側温度をField自身に持たせると、領域外でもRegistry平均へ参加してしまいます。
    // 温度値と空間的な有効度を分離し、合成はRegistryのInfluence Weightへ一元化します。
    return m_InsideTemperatureKelvin;
}

float SphericalTemperatureRegionField::EvaluateInfluence(const math::Vec3& worldPosition) const
{
    const math::Vec3 offset = worldPosition - m_Center;
    const float distanceSquared = math::Vec3::Dot(offset, offset);
    const float radiusSquared = m_Radius * m_Radius;

    // Core内部は平方根なしで判定できるため、最も頻繁な完全Influenceケースではsqrtを避けます。
    if (distanceSquared <= radiusSquared)
    {
        return 1.0f;
    }

    if (m_Falloff == TemperatureRegionFalloff::Hard || m_FalloffDistance <= 0.0f)
    {
        return 0.0f;
    }

    const float outerRadius = m_Radius + m_FalloffDistance;
    if (distanceSquared >= outerRadius * outerRadius)
    {
        return 0.0f;
    }

    const float distance = std::sqrt(distanceSquared);
    float influence = 1.0f - (distance - m_Radius) / m_FalloffDistance;
    influence = std::clamp(influence, 0.0f, 1.0f);

    if (m_Falloff == TemperatureRegionFalloff::SmoothStep)
    {
        // smoothstep(0,1,x)により境界両端の一次微分を0にし、移動Bodyが境界を横切る際の温度変化を滑らかにします。
        influence = influence * influence * (3.0f - 2.0f * influence);
    }

    return influence;
}

void SphericalTemperatureRegionField::SetRadius(float radius)
{
    // 負半径は形状として意味を持たないため、設定境界で0へClampします。
    m_Radius = std::max(radius, 0.0f);
}

void SphericalTemperatureRegionField::SetInsideTemperatureKelvin(float temperatureKelvin)
{
    m_InsideTemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}

void SphericalTemperatureRegionField::SetFalloffDistance(float falloffDistance)
{
    m_FalloffDistance = std::max(falloffDistance, 0.0f);
}

} // namespace Raven::ph
