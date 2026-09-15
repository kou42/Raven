#include "Raven/Physics/Thermal/TemperatureFieldRegistry.h"

#include <algorithm>
#include <limits>

namespace Raven::ph
{

bool TemperatureFieldRegistry::RegisterField(TemperatureField& field)
{
    if (ContainsField(field) == true || ContainsTransientField(field) == true)
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
    m_TransientFields.clear();
}

bool TemperatureFieldRegistry::RegisterTransientField(TemperatureField& field)
{
    if (ContainsField(field) == true || ContainsTransientField(field) == true)
    {
        return false;
    }
    m_TransientFields.push_back(&field);
    return true;
}

void TemperatureFieldRegistry::ClearTransientFields()
{
    m_TransientFields.clear();
}

bool TemperatureFieldRegistry::ContainsField(const TemperatureField& field) const
{
    return std::find(m_Fields.begin(), m_Fields.end(), &field) != m_Fields.end();
}

bool TemperatureFieldRegistry::ContainsTransientField(const TemperatureField& field) const
{
    return std::find(m_TransientFields.begin(), m_TransientFields.end(), &field) != m_TransientFields.end();
}

float TemperatureFieldRegistry::Evaluate(const math::Vec3& worldPosition, float fallbackTemperatureKelvin) const
{
    const float safeFallbackTemperatureKelvin = std::max(fallbackTemperatureKelvin, 0.0f);

    double weightedTemperatureSum = 0.0;
    double totalInfluence = 0.0;
    int highestOverridePriority = std::numeric_limits<int>::min();
    double overrideTemperatureSum = 0.0;
    double overrideInfluenceSum = 0.0;
    float overrideAlpha = 0.0f;

    // Persistent/Transientは寿命管理だけが異なり、温度合成規則は同一です。
    // 2つのvectorを同じ処理へ通すことで、ECS Volumeと外部Fieldの登録元によって結果が変わらないようにします。
    const auto accumulateFields = [&](const std::vector<TemperatureField*>& fields)
    {
        for (const TemperatureField* field : fields)
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

            if (field->GetBlendMode() == TemperatureFieldBlendMode::WeightedAverage)
            {
                weightedTemperatureSum += static_cast<double>(field->Evaluate(worldPosition)) * static_cast<double>(influence);
                totalInfluence += static_cast<double>(influence);
                continue;
            }

            const int priority = field->GetPriority();
            if (priority < highestOverridePriority)
            {
                continue;
            }
            if (priority > highestOverridePriority)
            {
                highestOverridePriority = priority;
                overrideTemperatureSum = 0.0;
                overrideInfluenceSum = 0.0;
                overrideAlpha = 0.0f;
            }

            overrideTemperatureSum += static_cast<double>(field->Evaluate(worldPosition)) * static_cast<double>(influence);
            overrideInfluenceSum += static_cast<double>(influence);
            // 同Priority Field数によってOverride強度が増幅しないよう、Group Alphaは最大Influenceを採用します。
            overrideAlpha = std::max(overrideAlpha, influence);
        }
    };

    accumulateFields(m_Fields);
    accumulateFields(m_TransientFields);

    float baseTemperatureKelvin = safeFallbackTemperatureKelvin;
    if (totalInfluence > 0.0)
    {
        baseTemperatureKelvin = std::max(static_cast<float>(weightedTemperatureSum / totalInfluence), 0.0f);
    }

    if (overrideInfluenceSum <= 0.0)
    {
        return baseTemperatureKelvin;
    }

    const float overrideTemperatureKelvin =
        std::max(static_cast<float>(overrideTemperatureSum / overrideInfluenceSum), 0.0f);
    return baseTemperatureKelvin + (overrideTemperatureKelvin - baseTemperatureKelvin) * overrideAlpha;
}

} // namespace Raven::ph
