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

    // TemperatureとInfluenceを分離して加重平均することで、Global Field同士は従来どおり等価平均され、
    // Region Fieldは領域外でWeight=0となって平均対象から除外されます。Falloff中は0..1のWeightとなるため、
    // Hardな有効/無効切替だけでなく局所温度への滑らかな遷移も同じ合成式で扱えます。
    double weightedTemperatureSum = 0.0;
    double totalInfluence = 0.0;
    for (const TemperatureField* field : m_Fields)
    {
        if (field == nullptr)
        {
            continue;
        }

        const float influence = std::clamp(field->EvaluateInfluence(worldPosition), 0.0f, 1.0f);
        if (influence <= 0.0f)
        {
            continue;
        }

        weightedTemperatureSum +=
            static_cast<double>(field->Evaluate(worldPosition)) * static_cast<double>(influence);
        totalInfluence += static_cast<double>(influence);
    }

    if (totalInfluence <= 0.0)
    {
        return std::max(fallbackTemperatureKelvin, 0.0f);
    }

    return std::max(static_cast<float>(weightedTemperatureSum / totalInfluence), 0.0f);
}

} // namespace Raven::ph
