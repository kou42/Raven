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
// Persistent Fieldは外部所有者が明示的に登録解除するまで維持し、Transient Fieldは
// ECS同期ごとに再構築します。両者を分離することでScene由来Fieldだけを安全に更新できます。
class TemperatureFieldRegistry
{
public:
    bool RegisterField(TemperatureField& field);
    bool UnregisterField(TemperatureField& field);
    void Clear();

    // ECS ComponentなどFixed Stepごとにpointerを解決し直すField用です。
    // ComponentStorageの再配置を跨いでpointerを保持しないため、ThermalSystemが同期開始時にClearTransientFields()します。
    bool RegisterTransientField(TemperatureField& field);
    void ClearTransientFields();

    bool ContainsField(const TemperatureField& field) const;
    bool ContainsTransientField(const TemperatureField& field) const;
    std::size_t GetRegisteredFieldCount() const { return m_Fields.size() + m_TransientFields.size(); }
    std::size_t GetPersistentFieldCount() const { return m_Fields.size(); }
    std::size_t GetTransientFieldCount() const { return m_TransientFields.size(); }
    const std::vector<TemperatureField*>& GetRegisteredFields() const { return m_Fields; }
    const std::vector<TemperatureField*>& GetTransientFields() const { return m_TransientFields; }

    // worldPositionはConvection EntityのTransformComponent::Positionを想定します。
    // 有効なFieldがない位置ではfallbackTemperatureKelvinを返し、既存AmbientTemperature契約を維持します。
    float Evaluate(const math::Vec3& worldPosition, float fallbackTemperatureKelvin) const;

private:
    std::vector<TemperatureField*> m_Fields;
    std::vector<TemperatureField*> m_TransientFields;
};

} // namespace Raven::ph
