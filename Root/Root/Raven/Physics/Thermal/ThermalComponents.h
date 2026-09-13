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

// Entity表面と一定温度の周囲流体との対流熱伝達です。
struct ThermalConvectionComponent
{
    float AmbientTemperature = 293.15f;
    float HeatTransferCoefficient = 10.0f; // h [W/(m^2*K)]
    float SurfaceArea = 1.0f;              // A [m^2]
    bool Enabled = true;
};

// ============================================================================
// ThermalRadiationComponent
// ============================================================================
// Entity表面と一定温度の放射Environmentとの熱放射を定義します。
// Stefan-Boltzmann則 Qdot = epsilon*sigma*A*(Tenv^4 - Tbody^4) を使用します。
// Emissivityは0～1の無次元値で、1に近いほど黒体に近い放射・吸収特性を持ちます。
// 現段階ではEnvironmentを無限Reservoirとして扱い、View Factorは1と仮定します。
struct ThermalRadiationComponent
{
    float EnvironmentTemperature = 293.15f; // [K]
    float Emissivity = 0.9f;                 // epsilon [-]
    float SurfaceArea = 1.0f;                // A [m^2]
    bool Enabled = true;
};

} // namespace Raven::ph
