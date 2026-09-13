#pragma once

#include <algorithm>

#include "Raven/Physics/Thermal/ThermalMaterial.h"

namespace Raven::ph
{
// ============================================================================
// ThermalBody
// ============================================================================
// 1つの集中熱容量を表します。Body内部の温度分布は解かず、全体を一様温度と仮定します。
// この近似により、伝導・対流・放射から受け取った熱量Q [J]だけで状態を更新できます。
//
// 相転移を有効にする場合はLatentHeatOfFusionを正値へ設定します。
// MeltFractionは0=固相、1=液相で、0～1の間では温度をPhaseChangeTemperatureへ固定し、
// 入出力された熱を温度変化ではなく潜熱として相の変化へ割り当てます。
struct ThermalBody
{
    float Temperature = 293.15f; // [K]
    float Mass = 1.0f;           // [kg]
    ThermalMaterial Material{};

    // 相転移の材料定数とRuntime状態です。MeltFractionだけが時間発展する状態です。
    float PhaseChangeTemperature = 273.15f;    // 融点 [K]
    float LatentHeatOfFusion = 0.0f;           // 融解潜熱 L [J/kg]
    float LiquidSpecificHeatCapacity = 500.0f; // 液相比熱 [J/(kg*K)]
    float MeltFraction = 0.0f;                 // 液相率 [0, 1]

    // Explicit Solverの時定数評価では現在相の顕熱容量C=m*cを使用します。
    // 相転移中の潜熱はApplyHeat側で直接処理し、無限大の見かけ比熱としては扱いません。
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

    // Bodyへ正味熱量Q [J]を与えます。
    // 加熱時は「固相顕熱 -> 融解潜熱 -> 液相顕熱」、冷却時は逆順に熱を配分します。
    // 相転移処理をWorld SolverではなくBodyへ閉じ込めることで、伝導・対流・放射側は
    // 相状態を意識せず、共通の「移動熱量Qを計算する」という責務だけを維持できます。
    void ApplyHeat(float heat)
    {
        const float solidHeatCapacity = Mass * Material.SpecificHeatCapacity;
        if (solidHeatCapacity <= 0.0f || Mass <= 0.0f)
        {
            return;
        }

        // 潜熱が無効なら従来の集中熱容量モデルそのものです。既存挙動との互換性もここで維持します。
        if (LatentHeatOfFusion <= 0.0f)
        {
            Temperature = std::max(0.0f, Temperature + heat / solidHeatCapacity);
            return;
        }

        const float liquidHeatCapacity = Mass * LiquidSpecificHeatCapacity;
        const float latentHeatCapacity = GetLatentHeatCapacity();
        float remainingHeat = heat;

        // 固相を加熱して融点へ到達させます。融点を越える分は次の潜熱処理へ残します。
        if (remainingHeat > 0.0f && Temperature < PhaseChangeTemperature)
        {
            const float heatToTransition = solidHeatCapacity * (PhaseChangeTemperature - Temperature);
            const float usedHeat = std::min(remainingHeat, heatToTransition);
            Temperature += usedHeat / solidHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 完全液相を冷却する場合は液相比熱を使って融点まで戻します。
        // 融点より下へ進む熱は凝固潜熱へ回すため、この段階では使い切りません。
        if (remainingHeat < 0.0f && Temperature > PhaseChangeTemperature && liquidHeatCapacity > 0.0f)
        {
            const float heatToTransition = liquidHeatCapacity * (PhaseChangeTemperature - Temperature);
            const float usedHeat = std::max(remainingHeat, heatToTransition);
            Temperature += usedHeat / liquidHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 融点では温度を変えず、入力熱を潜熱へ割り当てて液相率だけを増やします。
        if (remainingHeat > 0.0f && Temperature >= PhaseChangeTemperature && MeltFraction < 1.0f)
        {
            Temperature = PhaseChangeTemperature;
            const float heatToMelt = latentHeatCapacity * (1.0f - MeltFraction);
            const float usedHeat = std::min(remainingHeat, heatToMelt);
            MeltFraction += usedHeat / latentHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 冷却時は潜熱を放出しながら液相率を減らします。完全凝固するまでは融点を維持します。
        if (remainingHeat < 0.0f && Temperature <= PhaseChangeTemperature && MeltFraction > 0.0f)
        {
            Temperature = PhaseChangeTemperature;
            const float heatToFreeze = -latentHeatCapacity * MeltFraction;
            const float usedHeat = std::max(remainingHeat, heatToFreeze);
            MeltFraction += usedHeat / latentHeatCapacity;
            remainingHeat -= usedHeat;
        }

        // 浮動小数点誤差で相率が物理範囲を外れないよう、状態更新の境界で明示的に制限します。
        MeltFraction = std::clamp(MeltFraction, 0.0f, 1.0f);

        // 相転移完了後に残った熱だけを再び顕熱として扱います。
        if (remainingHeat > 0.0f && MeltFraction >= 1.0f && liquidHeatCapacity > 0.0f)
        {
            Temperature += remainingHeat / liquidHeatCapacity;
        }
        else if (remainingHeat < 0.0f && MeltFraction <= 0.0f)
        {
            Temperature += remainingHeat / solidHeatCapacity;
        }

        // Kelvinは負値を取らないため、数値安全性の最終境界として絶対零度でClampします。
        Temperature = std::max(Temperature, 0.0f);
    }
};

}
