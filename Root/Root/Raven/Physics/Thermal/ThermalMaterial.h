#pragma once

namespace Raven::ph
{
// ============================================================================
// ThermalMaterial
// ============================================================================
// 複数のThermalBodyで共有しやすい、基本的な材料熱物性を保持します。
// SI単位系を基準とし、SpecificHeatCapacityは J/(kg*K)、
// ThermalConductivityは W/(m*K) です。
//
// 相転移温度・潜熱・MeltFractionは現在ThermalBody側に置いています。
// これは相転移の進行度がBodyごとのRuntime状態であり、Material定数と混在させないためです。
// 将来Material Assetを導入する場合は、融点や潜熱の「材料定数」だけをMaterialへ移し、
// MeltFractionのような時間発展する状態はBody側に残す構造へ整理できます。
struct ThermalMaterial
{
    float SpecificHeatCapacity = 500.0f; // 固相側の比熱 c [J/(kg*K)]
    float ThermalConductivity = 50.0f;    // 熱伝導率 k [W/(m*K)]
};

}
