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
// このComponentを持つEntityをBody A、TargetEntityをBody Bとして明示的な熱接触を定義します。
// EntityHandleでPairを保持するため、ComponentStorageの再配置によってThermalBodyのアドレスが
// 変化してもSceneデータ自体にdangling pointerを残しません。
//
// ContactArea[m^2] / ConductionDistance[m]は現段階では明示設定です。
// Rigid Contact Manifold由来の自動接触とは独立しており、常時接続された熱リンク等に利用できます。
struct ThermalContactComponent
{
    EntityHandle TargetEntity{};
    float ContactArea = 1.0f;
    float ConductionDistance = 1.0f;
    float ConductivityScale = 1.0f;
    bool Enabled = true;
};

// ============================================================================
// ThermalRigidContactComponent
// ============================================================================
// Rigid BodyのContact ManifoldをThermalContactへ変換することを明示的に許可する設定です。
// 両EntityがこのComponentを持ち、Enabled=trueの場合だけ自動熱伝導を生成します。
// 既存SceneへThermalComponentを追加しただけで衝突挙動が変化しないようopt-inにしています。
//
// 現在のContact Manifoldは接触点を持ちますが真の接触面積は持たないため、PointCountに
// NominalContactAreaPerPoint[m^2]を掛けて有効接触面積を近似します。
// ConductionDistance[m]も集中熱容量モデル用の有効距離で、形状内部の温度勾配を直接解いてはいません。
struct ThermalRigidContactComponent
{
    float NominalContactAreaPerPoint = 1.0e-4f;
    float ConductionDistance = 1.0e-2f;
    float ConductivityScale = 1.0f;
    bool Enabled = true;
};

} // namespace Raven::ph
