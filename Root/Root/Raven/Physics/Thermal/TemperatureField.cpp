#include "Raven/Physics/Thermal/TemperatureField.h"

#include <algorithm>

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
    float outsideTemperatureKelvin)
    : m_Center(center)
{
    SetRadius(radius);
    SetInsideTemperatureKelvin(insideTemperatureKelvin);
    SetOutsideTemperatureKelvin(outsideTemperatureKelvin);
}

float SphericalTemperatureRegionField::Evaluate(const math::Vec3& worldPosition) const
{
    const math::Vec3 offset = worldPosition - m_Center;
    const float distanceSquared = math::Vec3::Dot(offset, offset);
    const float radiusSquared = m_Radius * m_Radius;

    // sqrtを避けた二乗距離比較により、Region判定だけのための不要な平方根計算を発生させません。
    // 境界上はInsideへ含め、Radius=0でもCenter一点を明確に局所領域として扱います。
    if (distanceSquared <= radiusSquared)
    {
        return m_InsideTemperatureKelvin;
    }

    return m_OutsideTemperatureKelvin;
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

void SphericalTemperatureRegionField::SetOutsideTemperatureKelvin(float temperatureKelvin)
{
    m_OutsideTemperatureKelvin = std::max(temperatureKelvin, 0.0f);
}

} // namespace Raven::ph
