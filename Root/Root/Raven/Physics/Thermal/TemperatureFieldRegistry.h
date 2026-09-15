#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Thermal/TemperatureField.h"

namespace Raven::ph
{

// ============================================================================
// TemperatureFieldRegistry
// ============================================================================
// Thermal Runtimeから参照するTemperatureFieldの非所有Registryです。
// Fieldの所有権は呼び出し側に残し、RegistryはFixed Step中の評価対象だけを管理します。
// 複数Fieldは各位置でのInfluenceをWeightとして加重平均し、局所Fieldの領域外寄与を除外します。
class TemperatureFieldRegistry
{
public:
    bool RegisterField(TemperatureField& field);
    bool UnregisterField(TemperatureField& field);
    void Clear();

    bool ContainsField(const TemperatureField& field) const;
    std::size_t GetRegisteredFieldCount() const { return m_Fields.size(); }
    const std::vector<TemperatureField*>& GetRegisteredFields() const { return m_Fields; }

    // worldPositionはConvection EntityのTransformComponent::Positionを想定します。
    // 有効なFieldがない位置ではfallbackTemperatureKelvinを返し、既存AmbientTemperature契約を維持します。
    float Evaluate(const math::Vec3& worldPosition, float fallbackTemperatureKelvin) const;

private:
    std::vector<TemperatureField*> m_Fields;
};

} // namespace Raven::ph
