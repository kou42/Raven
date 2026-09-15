#include "Raven/Physics/Thermal/TemperatureField.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
namespace
{
float ApplyTemperatureRegionFalloff(float linearInfluence, TemperatureRegionFalloff falloff)
{
    float influence = std::clamp(linearInfluence, 0.0f, 1.0f);
    if (falloff == TemperatureRegionFalloff::SmoothStep)
    {
        influence = influence * influence * (3.0f - 2.0f * influence);
    }
    return influence;
}
}

UniformTemperatureField::UniformTemperatureField(float temperatureKelvin) { SetTemperatureKelvin(temperatureKelvin); }
float UniformTemperatureField::Evaluate(const math::Vec3& worldPosition) const { (void)worldPosition; return m_TemperatureKelvin; }
void UniformTemperatureField::SetTemperatureKelvin(float temperatureKelvin) { m_TemperatureKelvin = std::max(temperatureKelvin, 0.0f); }

GradientTemperatureField::GradientTemperatureField(const math::Vec3& referencePosition, float referenceTemperatureKelvin,
    const math::Vec3& gradientKelvinPerUnit)
    : m_ReferencePosition(referencePosition), m_GradientKelvinPerUnit(gradientKelvinPerUnit)
{
    SetReferenceTemperatureKelvin(referenceTemperatureKelvin);
}
float GradientTemperatureField::Evaluate(const math::Vec3& worldPosition) const
{
    const math::Vec3 offset = worldPosition - m_ReferencePosition;
    return std::max(m_ReferenceTemperatureKelvin + math::Vec3::Dot(m_GradientKelvinPerUnit, offset), 0.0f);
}
void GradientTemperatureField::SetReferenceTemperatureKelvin(float temperatureKelvin)
{
    m_ReferenceTemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}

SphericalTemperatureRegionField::SphericalTemperatureRegionField(const math::Vec3& center, float radius,
    float insideTemperatureKelvin, float falloffDistance, TemperatureRegionFalloff falloff)
    : m_Center(center), m_Falloff(falloff)
{
    SetRadius(radius);
    SetInsideTemperatureKelvin(insideTemperatureKelvin);
    SetFalloffDistance(falloffDistance);
}
float SphericalTemperatureRegionField::Evaluate(const math::Vec3& worldPosition) const
{
    (void)worldPosition;
    return m_InsideTemperatureKelvin;
}
float SphericalTemperatureRegionField::EvaluateInfluence(const math::Vec3& worldPosition) const
{
    const math::Vec3 offset = worldPosition - m_Center;
    const float distanceSquared = math::Vec3::Dot(offset, offset);
    const float radiusSquared = m_Radius * m_Radius;
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
    return ApplyTemperatureRegionFalloff(1.0f - (distance - m_Radius) / m_FalloffDistance, m_Falloff);
}
void SphericalTemperatureRegionField::SetRadius(float radius) { m_Radius = std::max(radius, 0.0f); }
void SphericalTemperatureRegionField::SetInsideTemperatureKelvin(float temperatureKelvin) { m_InsideTemperatureKelvin = std::max(temperatureKelvin, 0.0f); }
void SphericalTemperatureRegionField::SetFalloffDistance(float falloffDistance) { m_FalloffDistance = std::max(falloffDistance, 0.0f); }

BoxTemperatureRegionField::BoxTemperatureRegionField(const math::Vec3& center, const math::Vec3& halfExtents,
    float insideTemperatureKelvin, float falloffDistance, TemperatureRegionFalloff falloff)
    : m_Center(center), m_Falloff(falloff)
{
    SetHalfExtents(halfExtents);
    SetInsideTemperatureKelvin(insideTemperatureKelvin);
    SetFalloffDistance(falloffDistance);
}

float BoxTemperatureRegionField::Evaluate(const math::Vec3& worldPosition) const
{
    (void)worldPosition;
    return m_InsideTemperatureKelvin;
}

float BoxTemperatureRegionField::EvaluateInfluence(const math::Vec3& worldPosition) const
{
    const math::Vec3 offset = worldPosition - m_Center;
    const float distanceX = std::max(std::abs(offset.x) - m_HalfExtents.x, 0.0f);
    const float distanceY = std::max(std::abs(offset.y) - m_HalfExtents.y, 0.0f);
    const float distanceZ = std::max(std::abs(offset.z) - m_HalfExtents.z, 0.0f);
    const float distanceSquared = distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ;

    // Box内部と表面では各軸の外側距離がすべて0になります。
    // Signed Distance全体を導入せず、局所Regionに必要な「Box外側から表面までの最短距離」だけを計算します。
    if (distanceSquared <= 0.0f)
    {
        return 1.0f;
    }
    if (m_Falloff == TemperatureRegionFalloff::Hard || m_FalloffDistance <= 0.0f)
    {
        return 0.0f;
    }
    const float falloffDistanceSquared = m_FalloffDistance * m_FalloffDistance;
    if (distanceSquared >= falloffDistanceSquared)
    {
        return 0.0f;
    }

    // sqrtはFalloff帯にいる場合だけ実行します。角の外側でもEuclidean距離を使うため、
    // 軸ごとの独立Falloffより等距離面が自然になり、Box corner周辺の温度遷移が歪みにくくなります。
    const float distance = std::sqrt(distanceSquared);
    return ApplyTemperatureRegionFalloff(1.0f - distance / m_FalloffDistance, m_Falloff);
}

void BoxTemperatureRegionField::SetHalfExtents(const math::Vec3& halfExtents)
{
    // 負Extentは形状として意味を持たないため各軸を0へClampし、Plane/Line/Point状の退化Boxも有効なRegionとして扱います。
    m_HalfExtents = math::Vec3{
        std::max(halfExtents.x, 0.0f),
        std::max(halfExtents.y, 0.0f),
        std::max(halfExtents.z, 0.0f)
    };
}
void BoxTemperatureRegionField::SetInsideTemperatureKelvin(float temperatureKelvin)
{
    m_InsideTemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}
void BoxTemperatureRegionField::SetFalloffDistance(float falloffDistance)
{
    m_FalloffDistance = std::max(falloffDistance, 0.0f);
}

} // namespace Raven::ph
