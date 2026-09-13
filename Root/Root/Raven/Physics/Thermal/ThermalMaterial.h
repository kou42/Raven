#pragma once

namespace Raven::ph
{
// ============================================================================
// ThermalMaterial
// ============================================================================
// 熱シミュレーションで使用する材料定数です。
// SI単位系を基準とし、SpecificHeatCapacityは J/(kg*K)、
// ThermalConductivityは W/(m*K) で保持します。
//
// 現段階では熱伝導の最小基盤に必要な値だけを持たせます。
// 密度、放射率、相転移温度などは、それらを実際に扱う段階で追加します。
struct ThermalMaterial
{
    float SpecificHeatCapacity = 500.0f;
    float ThermalConductivity = 50.0f;
};

}
