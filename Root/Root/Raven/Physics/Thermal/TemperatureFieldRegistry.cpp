#include "Raven/Physics/Thermal/TemperatureFieldRegistry.h"

#include <algorithm>

namespace Raven::ph
{

bool TemperatureFieldRegistry::RegisterField(TemperatureField& field)
{
    if (ContainsField(field) == true)
    {
        return false;
    }
    m_Fields.push_back(&field);
    return true;
}

bool TemperatureFieldRegistry::UnregisterField(TemperatureField& field)
{
    const auto it = std::find(m_Fields.begin(), m_Fields.end(), &field);
    if (it == m_Fields.end())
    {
        return false;
    }
    m_Fields.erase(it);
    return true;
}

void TemperatureFieldRegistry::Clear()
{
    m_Fields.clear();
}

bool TemperatureFieldRegistry::ContainsField(const TemperatureField& field) const
{
    return std::find(m_Fields.begin(), m_Fields.end(), &field) != m_Fields.end();
}

float TemperatureFieldRegistry::Evaluate(const math::Vec3& worldPosition, float fallbackTemperatureKelvin) const
{
    if (m_Fields.empty() == true)
    {
        return std::max(fallbackTemperatureKelvin, 0.0f);
    }

    // 現段階ではFieldごとの優先度・Blend Weightを導入せず、登録Fieldを等価な環境寄与として平均します。
    // これによりUniform Fieldを複数登録しても温度を加算して非物理的に増幅せず、将来のBlend Policyも
    // Registry内部だけで差し替えられます。
    double temperatureSum = 0.0;
    for (const TemperatureField* field : m_Fields)
    {
        if (field != nullptr)
        {
            temperatureSum += static_cast<double>(field->Evaluate(worldPosition));
        }
    }
    return std::max(static_cast<float>(temperatureSum / static_cast<double>(m_Fields.size())), 0.0f);
}

} // namespace Raven::ph
