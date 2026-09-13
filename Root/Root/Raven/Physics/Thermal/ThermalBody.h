#pragma once

#include <algorithm>
#include "Raven/Physics/Thermal/ThermalMaterial.h"

namespace Raven::ph
{
struct ThermalBody
{
    float Temperature = 293.15f;
    float Mass = 1.0f;
    ThermalMaterial Material{};

    // 0=固相、1=液相。相転移中は温度を融点へ固定し、熱量をこの状態へ蓄えます。
    float MeltFraction = 0.0f;

    float GetHeatCapacity() const
    {
        return Mass * Material.SpecificHeatCapacity;
    }

    float GetLatentHeatCapacity() const
    {
        return Mass * Material.LatentHeatOfFusion;
    }

    void ApplyHeat(float heat)
    {
        const float solidCapacity = Mass * Material.SpecificHeatCapacity;
        if (solidCapacity <= 0.0f)
        {
            return;
        }
        if (Material.LatentHeatOfFusion <= 0.0f || Mass <= 0.0f)
        {
            Temperature = std::max(0.0f, Temperature + heat / solidCapacity);
            return;
        }

        const float transitionTemperature = Material.PhaseChangeTemperature;
        const float latentCapacity = GetLatentHeatCapacity();
        float remainingHeat = heat;

        if (remainingHeat > 0.0f && Temperature < transitionTemperature)
        {
            const float heatToTransition = solidCapacity * (transitionTemperature - Temperature);
            const float usedHeat = std::min(remainingHeat, heatToTransition);
            Temperature += usedHeat / solidCapacity;
            remainingHeat -= usedHeat;
        }
        if (remainingHeat < 0.0f && Temperature > transitionTemperature)
        {
            const float liquidCapacity = Mass * Material.LiquidSpecificHeatCapacity;
            if (liquidCapacity > 0.0f)
            {
                const float heatToTransition = liquidCapacity * (transitionTemperature - Temperature);
                const float usedHeat = std::max(remainingHeat, heatToTransition);
                Temperature += usedHeat / liquidCapacity;
                remainingHeat -= usedHeat;
            }
        }

        if (remainingHeat > 0.0f && Temperature >= transitionTemperature && MeltFraction < 1.0f)
        {
            Temperature = transitionTemperature;
            const float heatToMelt = latentCapacity * (1.0f - MeltFraction);
            const float usedHeat = std::min(remainingHeat, heatToMelt);
            MeltFraction += usedHeat / latentCapacity;
            remainingHeat -= usedHeat;
        }
        if (remainingHeat < 0.0f && Temperature <= transitionTemperature && MeltFraction > 0.0f)
        {
            Temperature = transitionTemperature;
            const float heatToFreeze = -latentCapacity * MeltFraction;
            const float usedHeat = std::max(remainingHeat, heatToFreeze);
            MeltFraction += usedHeat / latentCapacity;
            remainingHeat -= usedHeat;
        }

        MeltFraction = std::clamp(MeltFraction, 0.0f, 1.0f);
        if (remainingHeat > 0.0f && MeltFraction >= 1.0f)
        {
            const float liquidCapacity = Mass * Material.LiquidSpecificHeatCapacity;
            if (liquidCapacity > 0.0f)
            {
                Temperature += remainingHeat / liquidCapacity;
            }
        }
        else if (remainingHeat < 0.0f && MeltFraction <= 0.0f)
        {
            Temperature += remainingHeat / solidCapacity;
        }
        Temperature = std::max(Temperature, 0.0f);
    }
};

}
