#pragma once

#include "Raven/Physics/Thermal/ThermalBody.h"
#include "Raven/Scene/Entity.h"

namespace Raven::ph
{
// ============================================================================
// ThermalBodyComponent
// ============================================================================
// EntityをThermal Domainへ参加させるためのECS Componentです。
// Runtime pointerをSceneデータへ保存せず、熱状態そのものだけを保持します。
// ThermalWorldの非所有RegistryはFixed Step直前にECSから再構築されます。
struct ThermalBodyComponent
{
    ThermalBody Body{};
    bool Enabled = true;
};

// ============================================================================
// ThermalContactComponent
// ============================================================================
// このComponentを持つEntityをBody A、TargetEntityをBody Bとして熱接触を定義します。
// EntityHandleでPairを保持するため、ComponentStorageの再配置によってThermalBodyのアドレスが
// 変化してもSceneデータ自体にdangling pointerを残しません。
//
// ContactArea[m^2] / ConductionDistance[m]は現段階では明示設定です。
// Rigid Contact Manifoldとの自動Couplingを追加する際は、同じThermalContactへ変換して
// ThermalWorldのSolver部分を再利用します。
struct ThermalContactComponent
{
    EntityHandle TargetEntity{};
    float ContactArea = 1.0f;
    float ConductionDistance = 1.0f;
    float ConductivityScale = 1.0f;
    bool Enabled = true;
};

} // namespace Raven::ph
