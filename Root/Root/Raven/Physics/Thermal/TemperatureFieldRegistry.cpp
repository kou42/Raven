#include "Raven/Physics/Thermal/TemperatureFieldRegistry.h"

#include <algorithm>
#include <limits>

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
    const float safeFallbackTemperatureKelvin = std::max(fallbackTemperatureKelvin, 0.0f);
    if (m_Fields.empty() == true)
    {
        return safeFallbackTemperatureKelvin;
    }

    // WeightedAverage Fieldは従来互換の基礎環境温度を構成します。
    // Override Fieldは別Passで評価し、最も高いPriority Groupだけをこの基礎温度へ重ねます。
    // これにより局所的な炉・冷却室などがCore内部では環境温度を完全に置換しつつ、
    // Falloff領域ではEvaluateInfluence()をAlphaとして自然に基礎環境へ戻れます。
    double weightedTemperatureSum = 0.0;
    double totalInfluence = 0.0;
    for (const TemperatureField* field : m_Fields)
    {
        if (field == nullptr || field->GetBlendMode() != TemperatureFieldBlendMode::WeightedAverage)
        {
            continue;
        }

        const float influence = std::clamp(field->EvaluateInfluence(worldPosition), 0.0f, 1.0f);
        if (influence <= 0.0f)
        {
            continue;
        }

        weightedTemperatureSum += static_cast<double>(field->Evaluate(worldPosition)) * static_cast<double>(influence);
        totalInfluence += static_cast<double>(influence);
    }

    float baseTemperatureKelvin = safeFallbackTemperatureKelvin;
    if (totalInfluence > 0.0)
    {
        baseTemperatureKelvin = std::max(static_cast<float>(weightedTemperatureSum / totalInfluence), 0.0f);
    }

    int highestOverridePriority = std::numeric_limits<int>::min();
    double overrideTemperatureSum = 0.0;
    double overrideInfluenceSum = 0.0;
    float overrideAlpha = 0.0f;

    for (const TemperatureField* field : m_Fields)
    {
        if (field == nullptr || field->GetBlendMode() != TemperatureFieldBlendMode::Override)
        {
            continue;
        }

        const float influence = std::clamp(field->EvaluateInfluence(worldPosition), 0.0f, 1.0f);
        if (influence <= 0.0f)
        {
            continue;
        }

        const int priority = field->GetPriority();
        if (priority < highestOverridePriority)
        {
            continue;
        }

        if (priority > highestOverridePriority)
        {
            // より高いPriorityを発見した時点で低Priority Groupの集計を破棄します。
            // 同Priority内だけをまとめることで、Field登録順によって結果が変化することを防ぎます。
            highestOverridePriority = priority;
            overrideTemperatureSum = 0.0;
            overrideInfluenceSum = 0.0;
            overrideAlpha = 0.0f;
        }

        overrideTemperatureSum += static_cast<double>(field->Evaluate(worldPosition)) * static_cast<double>(influence);
        overrideInfluenceSum += static_cast<double>(influence);
        // 同Priority Fieldの数が増えただけでOverride強度が増幅しないよう、Group Alphaは最大Influenceを採用します。
        overrideAlpha = std::max(overrideAlpha, influence);
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
