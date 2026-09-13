#pragma once

#include "Raven/Physics/Thermal/ThermalMaterial.h"

namespace Raven::ph
{
// ============================================================================
// ThermalBody
// ============================================================================
// 1つの集中熱容量(lumped thermal mass)を表します。
// Temperatureは絶対温度[K]、Massは[kg]です。
//
// 初期実装ではBody内部の温度分布を持たず、Body全体が一様温度であると仮定します。
// この単純化により、まず熱量保存とBody間熱伝導を明確に検証できます。
struct ThermalBody
{
    float Temperature = 293.15f;
    float Mass = 1.0f;
    ThermalMaterial Material{};

    float GetHeatCapacity() const
    {
        return Mass * Material.SpecificHeatCapacity;
    }
};

}
