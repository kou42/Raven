#pragma once

#include "Raven/Physics/Thermal/TemperatureField.h"
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

struct ThermalConvectionComponent
{
    float AmbientTemperature = 293.15f;
    float HeatTransferCoefficient = 10.0f;
    float SurfaceArea = 1.0f;
    bool Enabled = true;
};

struct ThermalRadiationComponent
{
    float EnvironmentTemperature = 293.15f;
    float Emissivity = 0.9f;
    float SurfaceArea = 1.0f;
    bool Enabled = true;
};

// Scene上に配置する球形Temperature Volumeです。
// FieldのCenterは毎Fixed Stepで同じEntityのTransformComponent::Positionから更新されるため、
// Entity移動に追従します。Radius/Falloff/Blend/PriorityなどのField設定はComponent側に保持します。
struct SphericalTemperatureVolumeComponent
{
    SphericalTemperatureRegionField Field{};
    bool Enabled = true;
};

// Scene上に配置するAxis-Aligned Box Temperature Volumeです。
// 現段階ではTransformのPositionだけをCenterへ反映し、Rotation/ScaleはField形状へ適用しません。
// 回転Box対応を暗黙に近似せず、将来のOriented Temperature Volume実装と責務を分けます。
struct BoxTemperatureVolumeComponent
{
    BoxTemperatureRegionField Field{};
    bool Enabled = true;
};

} // namespace Raven::ph
