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

} // namespace Raven::ph
