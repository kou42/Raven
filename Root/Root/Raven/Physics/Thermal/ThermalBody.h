#pragma once

#include <algorithm>

#include "Raven/Physics/Thermal/ThermalMaterial.h"

namespace Raven::ph
{
// ============================================================================
// ThermalBody
// ============================================================================
// 1つの集中熱容量を表します。
// 相転移を有効にする場合はLatentHeatOfFusionを正値へ設定します。
// MeltFractionは0=固相、1=液相で、相転移中は温度をPhaseChangeTemperatureへ固定します。
struct ThermalBody
{
    float Temperature = 293.15f;
    float Mass = 1.0f;
    ThermalMaterial Material{};

    float PhaseChangeTemperature = 273.15f;    // [K]
    float LatentHeatOfFusion = 0.0f;           // [J/kg]
    float LiquidSpecificHeatCapacity = 500.0f; // [J/(kg*K)]
    float MeltFraction = 0.0f;                 // [0, 1]

    float GetHeatCapacity() const
    {
        if (MeltFraction >= 1.0f && LiquidSpecificHeatCapacity > 0.0f)
        {
            return Mass * LiquidSpecificHeatCapacity;
        }
        return Mass * Material.SpecificHeatCapacity;
    }

    float GetLatentHeatCapacity() const
    {
        return Mass * LatentHeatOfFusion;
    }

    void ApplyHeat(float heat)
    {
        const float solidHeatCapacity = Mass * Material.SpecificHeatCapacity;
        if (solidHeatCapacity <= 0.0f || Mass <= 0.0f)
        {
            return;
        }

        if (LatentHeatOfFusion <= 0.0f)
        {
            Temperature = std::max(0.0f, Temperature + heat / solidHeatCapacity);
            return;
        }

        const float liquidHeatCapacity = Mass * LiquidSpecificHeatCapacity;
        const float latentHeatCapacity = GetLatentHeatCapacity();
        float remainingHeat = heat;

        // 固相を加熱して融点へ到達させます。
        if (remainingHeat > 0.0f && Temperature < PhaseChangeTemperature)
        {
            const float heatToTransition = solidHeatCapacity * (PhaseChangeTemperature - Temperature);
            const float usedHeat = std::min(remainingHeat, heatToTransition);
            Temperature += usedHeat / solidHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 液相を冷却して融点へ到達させます。
        if (remainingHeat < 0.0f && Temperature > PhaseChangeTemperature && liquidHeatCapacity > 0.0f)
        {
            const float heatToTransition = liquidHeatCapacity * (PhaseChangeTemperature - Temperature);
            const float usedHeat = std::max(remainingHeat, heatToTransition);
            Temperature += usedHeat / liquidHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 融点では温度を変えず、熱を潜熱としてMeltFractionへ蓄えます。
        if (remainingHeat > 0.0f && Temperature >= PhaseChangeTemperature && MeltFraction < 1.0f)
        {
            Temperature = PhaseChangeTemperature;
            const float heatToMelt = latentHeatCapacity * (1.0f - MeltFraction);
            const float usedHeat = std::min(remainingHeat, heatToMelt);
            MeltFraction += usedHeat / latentHeatCapacity;
            remainingHeat -= usedHeat;
        }

        if (remainingHeat < 0.0f && Temperature <= PhaseChangeTemperature && MeltFraction > 0.0f)
        {
            Temperature = PhaseChangeTemperature;
            const float heatToFreeze = -latentHeatCapacity * MeltFraction;
            const float usedHeat = std::max(remainingHeat, heatToFreeze);
            MeltFraction += usedHeat / latentHeatCapacity;
            remainingHeat -= usedHeat;
        }

        MeltFraction = std::clamp(MeltFraction, 0.0f, 1.0f);

        // 相転移完了後に残った熱だけを顕熱として扱います。
        if (remainingHeat > 0.0f && MeltFraction >= 1.0f && liquidHeatCapacity > 0.0f)
        {
            Temperature += remainingHeat / liquidHeatCapacity;
        }
        else if (remainingHeat < 0.0f && MeltFraction <= 0.0f)
        {
            Temperature += remainingHeat / solidHeatCapacity;
        }

        Temperature = std::max(Temperature, 0.0f);
    }
};

}
