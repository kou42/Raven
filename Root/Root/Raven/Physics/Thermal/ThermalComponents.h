#pragma once

#include "Raven/Physics/Thermal/TemperatureField.h"
#include "Raven/Physics/Thermal/ThermalBody.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{
// Entityが持つ熱状態そのものです。ThermalWorldはこのBodyを所有せず、
// Fixed Step開始時にThermalSystemがECSからpointerを解決してRuntime Registryへ登録します。
struct ThermalBodyComponent
{
    ThermalBody Body{};
    bool Enabled = true;
};

// Entity間に明示的な熱伝導リンクを設定します。
// ContactAreaとConductionDistanceから k_eff*A/d を計算し、Solverへ渡す時点では
// ThermalConductance G [W/K]へ正規化します。Rigid Contact由来の自動リンクより優先されます。
struct ThermalContactComponent
{
    EntityHandle TargetEntity{};
    float ContactArea = 1.0f;        // A [m^2]
    float ConductionDistance = 1.0f; // d [m]
    float ConductivityScale = 1.0f;  // 無次元補正係数
    bool Enabled = true;
};

// Rigid BodyのContact Manifoldから熱接触を自動生成するための近似パラメータです。
// 現在のPhysics Contactは実接触面積を直接持たないため、ContactPoint数×代表面積でAを近似します。
// ConductionDistanceも実際の表面粗さ・接触熱抵抗を解いている値ではなく、基礎モデル用の有効距離です。
struct ThermalRigidContactComponent
{
    float NominalContactAreaPerPoint = 1.0e-4f; // 1 ContactPointあたりの代表面積 [m^2]
    float ConductionDistance = 1.0e-2f;         // 有効伝導距離 [m]
    float ConductivityScale = 1.0f;             // 無次元補正係数
    bool Enabled = true;
};

// Entity表面と一定温度の周囲流体との対流熱伝達です。
// Newtonの冷却則 Qdot=h*A*(Tenv-Tbody) を使用し、Environmentは無限Reservoirとみなします。
// TemperatureFieldがThermalWorldへ登録されている場合、AmbientTemperatureはField未登録時のfallback値となり、
// RuntimeではEntityのworld-space位置で評価したField温度をTenvとして使用します。
struct ThermalConvectionComponent
{
    float AmbientTemperature = 293.15f;      // Tenv [K]
    float HeatTransferCoefficient = 10.0f;   // h [W/(m^2*K)]
    float SurfaceArea = 1.0f;                // A [m^2]
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
