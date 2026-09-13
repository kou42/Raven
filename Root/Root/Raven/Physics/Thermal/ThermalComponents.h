#pragma once

#include "Raven/Physics/Thermal/ThermalBody.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{
struct ThermalBodyComponent
{
    ThermalBody Body{};
    bool Enabled = true;
};

struct ThermalContactComponent
{
    EntityHandle TargetEntity{};
    float ContactArea = 1.0f;
    float ConductionDistance = 1.0f;
    float ConductivityScale = 1.0f;
    bool Enabled = true;
};

struct ThermalRigidContactComponent
{
    float NominalContactAreaPerPoint = 1.0e-4f;
    float ConductionDistance = 1.0e-2f;
    float ConductivityScale = 1.0f;
    bool Enabled = true;
};

// ============================================================================
// ThermalConvectionComponent
// ============================================================================
// Entity表面と一定温度の周囲環境との対流熱伝達を定義します。
// Newtonの冷却則 Qdot = h*A*(Tambient - Tbody) を使用します。
// AmbientTemperature[K]は無限熱容量Reservoirとして扱うため、Bodyから熱を受けても変化しません。
// HeatTransferCoefficientは流体・流速・形状をまとめた境界係数であり、材料の熱伝導率とは別物です。
struct ThermalConvectionComponent
{
    float AmbientTemperature = 293.15f;
    float HeatTransferCoefficient = 10.0f; // h [W/(m^2*K)]
    float SurfaceArea = 1.0f;              // A [m^2]
    bool Enabled = true;
};

} // namespace Raven::ph
